/* ==========================================================================
 *  system/playpss/audiodec.c
 *
 *  The audio half of the PSS movie player. playpss.c demuxes the ADPCM
 *  elementary stream out of the .pss and hands it here; this keeps it moving
 *  to the SPU2 and nothing else.
 *
 *  Two rings, one pump. The demuxer writes into the EE ring through
 *  audioDecBeginPut() / audioDecEndPut(); the SPU2's auto-DMA reads
 *  continuously out of an IOP-side ring it was pointed at once, at start; and
 *  audioDecSendToIOP() copies whole 1024-byte blocks from the first into the
 *  second, into the gap between where the SPU2 is reading and where the last
 *  copy ended. No interrupt, no callback: the pump polls the auto-DMA's play
 *  address once a frame (sceSdBlockTransStatus).
 *
 *  Nothing under 1024 bytes is sent: every size is rounded down to a whole
 *  block, both ends refuse a copy if either side has less than one block, and
 *  the play/write gap is measured a block short -- the DMA is never
 *  overtaken, at the cost of running up to a block behind.
 *
 *  The header is read through the same producer API as the samples: while
 *  `state` is 0 the free span audioDecBeginPut() reports is the 40-byte
 *  SShd/SSbd pair inside AudioDec itself, and audioDecEndPut() moves to state
 *  1 when the 40th byte lands.
 *
 *  FF3 0x272d90..0x273568. FF2's audiodec.c in the same order with the same
 *  bodies, ported from MioPan and checked against every FF3 decompilation.
 *  FF3 differences: audioDecEndPut no longer prints the stream's parameters,
 *  and audioDecDelete (an 8-byte "return 1" in FF2) is gone.
 *
 *  PORT: the IOP side is the platform's (sdk/iop_libsd.cpp plays the
 *  auto-DMA for real, sdk/sif.cpp copies SIF DMA into IOP memory); IOP
 *  addresses stay ints as on the PS2, see sceSdRemote() in sdk/libsdr.cpp.
 * ======================================================================== */
#include "system/playpss/audiodec.h"

#include <stdint.h>
#include <string.h>                     // memset

#include "sdk/eekernel.h"               // FlushCache
#include "sdk/libsd.h"                  // SD_BLOCK_TRANS_*, SD_P_BVOL*
#include "sdk/libsdr.h"                 // sceSdRemote, SDR_*
#include "sdk/sifman.h"                 // sceSifSetDma, sceSifDmaStat

// The 40 bytes of "SShd" + "SSbd" that open the stream.
#define AUDIODEC_HDR_SIZE       ((int)(sizeof(SpuStreamHeader) + sizeof(SpuStreamBody)))

// Nothing smaller than this is ever shipped to the IOP -- see the banner.
#define AUDIODEC_BLOCK          1024

// Where the sector of silence is parked in SPU2 memory. The auto-DMA is aimed
// at it whenever the stream is not running, so a pause is silent rather than
// a held buffer repeating.
#define AUDIODEC_ZERO_SPU_ADRS  0x4000

static_assert(AUDIODEC_HDR_SIZE == 0x28, "the SShd/SSbd header is 0x28 bytes");

// The SPU2 core the stream plays on (audioDecCreate's last argument).
static int AUTODMA_CH;                  // FF3 0x373b08 (gp+0x618)
// The scene's volume as a percentage; changeInputVolume() scales by it.
static int audio_vol_percent;           // FF3 0x373b0c (gp+0x61c)

static void iopGetArea(int *pd0, int *d0, int *pd1, int *d1, AudioDec *ad, int pos);
static int  sendToIOP2area(int pd0, int d0, int pd1, int d1,
                           u_char *ps0, int s0, u_char *ps1, int s1);
static int  sendToIOP(int dst, u_char *src, int size);
static void changeInputVolume(int val);

// Record the rings the caller allocated (EE one out of the movie heap, the
// two IOP ones through the IOP allocator), blank the sector of silence and
// upload it to the SPU2 so everything before the first real block is quiet.
// FF3 0x00272d90
int audioDecCreate(AudioDec *ad, u_char *buff, int buffSize, int iopBuff, int iopBuffSize,
                   u_char *zeroBuff, int iopZeroBuff, int zeroBuffSize,
                   int audio_vol, int auto_dma_core)
{
    AUTODMA_CH = auto_dma_core;

    ad->state          = 0;
    ad->hdrCount       = 0;
    ad->data           = buff;
    ad->put            = 0;
    ad->count          = 0;
    ad->size           = buffSize;
    ad->totalBytes     = 0;
    ad->totalBytesSent = 0;

    ad->iopBuff     = iopBuff;
    ad->iopLastPos  = 0;
    ad->iopPausePos = 0;
    ad->iopBuffSize = iopBuffSize;

    ad->iopZero     = iopZeroBuff;
    ad->iopZeroSize = zeroBuffSize;

    memset(zeroBuff, 0, zeroBuffSize);
    sendToIOP(ad->iopZero, zeroBuff, zeroBuffSize);

    sceSdRemote(1, SDR_VOICE_TRANS, AUTODMA_CH, 0,
                ad->iopZero, AUDIODEC_ZERO_SPU_ADRS, ad->iopZeroSize);

    audio_vol_percent = audio_vol;

    return 1;
}

// Mute, stop the auto-DMA, and point it at the sector of silence. Stopping
// it answers the address the DMA had reached; minus the ring base that is
// where audioDecResume() restarts. The top byte of the answer is status.
// FF3 0x00272e60
void audioDecPause(AudioDec *ad)
{
    int ret;

    ad->state = 3;

    changeInputVolume(0);

    ret = sceSdRemote(1, SDR_BLOCK_TRANS, AUTODMA_CH, SD_BLOCK_TRANS_STAT, 0, 0);
    // PC: the 24-bit mask is taken of the offset, not of the address: the same
    // on the PS2, where every IOP address is under 2 MB, and right wherever the
    // host put the ring (iop_host.cpp's low pool cannot always be had -- never
    // on the PS5).
    ad->iopPausePos = (ret - ad->iopBuff) & 0xffffff;

    sceSdRemote(1, SDR_VOICE_TRANS, AUTODMA_CH, 0,
                ad->iopZero, AUDIODEC_ZERO_SPU_ADRS, ad->iopZeroSize);
}

// Unmute and set the auto-DMA loop running again from where the pause left
// it, over the ring rounded down to whole blocks.
// FF3 0x00272ee0
void audioDecResume(AudioDec *ad)
{
    changeInputVolume(0x7fff);

    sceSdRemote(1, SDR_BLOCK_TRANS, AUTODMA_CH, SD_BLOCK_TRANS_CONT,
                ad->iopBuff,
                ad->iopBuffSize / AUDIODEC_BLOCK * AUDIODEC_BLOCK,
                ad->iopBuff + ad->iopPausePos);

    ad->state = 2;
}

// A start is a resume from iopPausePos 0, where audioDecCreate() left it.
// FF3 0x00272f48
void audioDecStart(AudioDec *ad)
{
    audioDecResume(ad);
}

// Stop and empty both rings, back to the state audioDecCreate() left, so the
// next stream is read from its header again. Buffers and sizes survive.
// FF3 0x00272f60
void audioDecReset(AudioDec *ad)
{
    audioDecPause(ad);

    ad->iopPausePos    = 0;
    ad->state          = 0;
    ad->hdrCount       = 0;
    ad->put            = 0;
    ad->count          = 0;
    ad->totalBytes     = 0;
    ad->totalBytesSent = 0;
    ad->iopLastPos     = 0;
}

// Where the demuxer may write, as up to two spans. While the header is short
// (state 0) span 0 is the rest of the SShd/SSbd pair inside AudioDec and span
// 1 the whole ring, so one pass lands the header and the samples after it.
// Otherwise span 0 runs from the write cursor to the end of the ring or the
// read cursor, span 1 is the wrapped remainder; the free length is
// size - count, so a full ring reports a zero-length span.
// FF3 0x00272fa8
void audioDecBeginPut(AudioDec *ad, u_char **ptr0, int *len0, u_char **ptr1, int *len1)
{
    int len;

    if (ad->state == 0)
    {
        *ptr0 = (u_char *)&ad->sshd + ad->hdrCount;
        *len0 = AUDIODEC_HDR_SIZE - ad->hdrCount;
        *ptr1 = ad->data;
        *len1 = ad->size;
        return;
    }

    len = ad->size - ad->count;

    if (len <= ad->size - ad->put)
    {
        *ptr0 = ad->data + ad->put;
        *len0 = len;
        *ptr1 = nullptr;
        *len1 = 0;
    }
    else
    {
        *ptr0 = ad->data + ad->put;
        *len0 = ad->size - ad->put;
        *ptr1 = ad->data;
        *len1 = len - (ad->size - ad->put);
    }
}

// Commit `size` bytes the demuxer just wrote. The part that belongs to the
// header comes off the front first; the 40th header byte moves the state on.
// FF3 0x00273048
void audioDecEndPut(AudioDec *ad, int size)
{
    if (ad->state == 0)
    {
        int hdr_add = AUDIODEC_HDR_SIZE - ad->hdrCount;

        if (size < hdr_add)
            hdr_add = size;

        ad->hdrCount += hdr_add;
        size         -= hdr_add;

        if (ad->hdrCount >= AUDIODEC_HDR_SIZE)
            ad->state = 1;
    }

    ad->put         = (ad->put + size) % ad->size;
    ad->count      += size;
    ad->totalBytes += size;
}

// "The IOP ring has been filled once": playpss.c waits on this before
// audioDecStart(), so the auto-DMA never begins on a short buffer.
// FF3 0x002730c0
int audioDecIsPreset(AudioDec *ad)
{
    return ad->totalBytesSent >= ad->iopBuffSize;
}

// One frame of the pump: the writable span of the IOP ring, the readable span
// of the EE ring, copy the smaller in whole blocks, advance both cursors.
// State 1 (nothing playing yet) fills the ring front to back; state 2 writes
// what iopGetArea() says is safely behind the SPU2's read position; states 0
// and 3 (header not read, paused) send nothing.
// FF3 0x002730d8
int audioDecSendToIOP(AudioDec *ad)
{
    int     pd0 = 0;
    int     d0  = 0;
    int     pd1 = 0;
    int     d1  = 0;
    u_char *ps0;
    int     s0;
    int     s1;
    int     get;
    int     len;
    int     count_sent = 0;

    // PC: the ROM leaves pd0..d1 unset for a state outside 0..3, which the
    // machine never reaches; zeroed here so such a state sends nothing.
    switch (ad->state)
    {
    case 0:
        return 0;

    case 1:
        pd0 = ad->iopBuff + ad->totalBytesSent % ad->iopBuffSize;
        d0  = ad->iopBuffSize - ad->totalBytesSent;
        pd1 = 0;
        d1  = 0;
        break;

    case 2:
    {
        // PC: the offset masked, not the address (see audioDecPause).
        int pos = (sceSdRemote(1, SDR_BLOCK_TRANS_STATUS, AUTODMA_CH) - ad->iopBuff) & 0xffffff;

        iopGetArea(&pd0, &d0, &pd1, &d1, ad, pos);
        break;
    }

    case 3:
        return 0;
    }

    get = (ad->put - ad->count + ad->size) % ad->size;
    ps0 = ad->data + get;

    len = ad->count / AUDIODEC_BLOCK * AUDIODEC_BLOCK;

    s0 = (ad->size - get < len) ? ad->size - get : len;
    s1 = len - s0;

    if (AUDIODEC_BLOCK <= d0 + d1 && AUDIODEC_BLOCK <= s0 + s1)
        count_sent = sendToIOP2area(pd0, d0, pd1, d1, ps0, s0, ad->data, s1);

    ad->count          -= count_sent;
    ad->totalBytesSent += count_sent;
    ad->iopLastPos      = (ad->iopLastPos + count_sent) % ad->iopBuffSize;

    return count_sent;
}

// Which part of the IOP ring may be written while the auto-DMA reads at
// `pos`: from the last write to one block short of the play position, whole
// blocks, split in two if it wraps. `pos - iopLastPos` is tested unsigned, so
// a play position that wrapped past the write cursor counts as room.
// FF3 0x00273280
static void iopGetArea(int *pd0, int *d0, int *pd1, int *d1, AudioDec *ad, int pos)
{
    int len = (pos + ad->iopBuffSize - ad->iopLastPos - AUDIODEC_BLOCK) % ad->iopBuffSize;

    if ((u_int)(pos - ad->iopLastPos) < AUDIODEC_BLOCK)
    {
        *pd0 = ad->iopBuff;
        *d0  = 0;
        *pd1 = ad->iopBuff;
        *d1  = 0;
        return;
    }

    len = len / AUDIODEC_BLOCK * AUDIODEC_BLOCK;

    if (len <= ad->iopBuffSize - ad->iopLastPos)
    {
        *pd0 = ad->iopBuff + ad->iopLastPos;
        *d0  = len;
        *pd1 = 0;
        *d1  = 0;
    }
    else
    {
        *pd0 = ad->iopBuff + ad->iopLastPos;
        *d0  = ad->iopBuffSize - ad->iopLastPos;
        *pd1 = ad->iopBuff;
        *d1  = len - (ad->iopBuffSize - ad->iopLastPos);
    }
}

// Copy up to two source spans into up to two destination spans; returns the
// bytes sent. The rings wrap at different places, so it is one, two or three
// transfers depending on where the seams fall. A source longer than the
// destination is trimmed from the tail: the second span first.
// FF3 0x00273340
static int sendToIOP2area(int pd0, int d0, int pd1, int d1,
                          u_char *ps0, int s0, u_char *ps1, int s1)
{
    if (d0 + d1 < s0 + s1)
    {
        int diff = s0 + s1 - (d0 + d1);

        if (s1 <= diff)
        {
            s0 -= diff - s1;
            s1  = 0;
        }
        else
        {
            s1 -= diff;
        }
    }

    if (d0 <= s0)
    {
        // The destination seam falls inside the first source span.
        sendToIOP(pd0, ps0, d0);
        sendToIOP(pd1, ps0 + d0, s0 - d0);
        sendToIOP(pd1 + s0 - d0, ps1, s1);
    }
    else if (d0 - s0 <= s1)
    {
        // ... inside the second one.
        sendToIOP(pd0, ps0, s0);
        sendToIOP(pd0 + s0, ps1, d0 - s0);
        sendToIOP(pd1, ps1 + d0 - s0, s1 - (d0 - s0));
    }
    else
    {
        // ... past the end of the source: one contiguous destination.
        sendToIOP(pd0, ps0, s0);
        sendToIOP(pd0 + s0, ps1, s1);
    }

    return s0 + s1;
}

// One EE -> IOP transfer, run to completion (the ROM spins on
// sceSifDmaStat; FlushCache makes the just-written EE buffer visible).
// FF3 0x00273490
static int sendToIOP(int dst, u_char *src, int size)
{
    sceSifDmaData transData;
    u_int         did;

    if (size <= 0)
        return 0;

    // PC: the ROM stores (u_int)src; the host keeps the pointer, and `addr`
    // carries the IOP address (see sceSifSetDma in sdk/sif.cpp).
    transData.data = src;
    transData.addr = (void *)(uintptr_t)(u_int)dst;
    transData.size = size;
    transData.mode = 0;

    FlushCache(0);

    did = sceSifSetDma(&transData, 1);

    while (sceSifDmaStat(did) >= 0)
    {
    }

    return size;
}

// The auto-DMA input volume (BVOLL/BVOLR: the core's external-input level,
// so the movie is attenuated without touching game audio), scaled by the
// scene's percentage. Pause and resume drive it to 0 and 0x7fff.
// FF3 0x00273500
static void changeInputVolume(int val)
{
    val = val * audio_vol_percent / 100;

    sceSdRemote(1, SDR_SET_PARAM, AUTODMA_CH | SD_P_BVOLL, val);
    sceSdRemote(1, SDR_SET_PARAM, AUTODMA_CH | SD_P_BVOLR, val);
}
