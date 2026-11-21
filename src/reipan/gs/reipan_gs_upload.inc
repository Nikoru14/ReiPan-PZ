/* ==========================================================================
 *  reipan_gs_upload.inc  (included by reipan_gs.cpp)
 *
 *  Writing a host-to-GS image transfer into the emulated VRAM, fast.
 *
 *  The game streams its textures to the GS every frame -- 170 to 250 transfers
 *  and 2 to 3.5 MB a frame in a room -- and each texel lands at its swizzled
 *  address.  Computed texel by texel through GetPixelAddress*() and written a
 *  byte at a time, that was most of the game thread's frame on the PS5.
 *
 *    - Each format's swizzle within one page is a table, built once from the
 *      address functions themselves; an address is then the page's base, the
 *      start block's offset and one lookup.  (The block offset and the page
 *      base only ever add to the table's value.)
 *    - A texel is stored whole where the address mask keeps it inside VRAM.
 *    - A large transfer is split into bands of 16 rows, on 16-row boundaries,
 *      written by a few worker threads at once.  No two texels of different
 *      bands share a byte: a 4-bit texel's byte partner is always in its own
 *      16-row band (the swizzle's blocks are at most 16 rows high).
 *
 *  What lands in VRAM is exactly what the texel-by-texel loops wrote.
 *
 *  Needs, before inclusion: GetPixelAddressPSMCT32/16/PSMT8/PSMT4, the PSM
 *  enumerators, GsSpawnWorker(int (*)(void *), void *) and GsUploadWorkers().
 * ======================================================================== */

#include <atomic>
#include <condition_variable>
#include <mutex>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define REIPAN_GS_PAUSE() _mm_pause()
#else
#define REIPAN_GS_PAUSE() ((void)0)
#endif

namespace
{

struct GsSwizzleTables
{
    int ct32[32][64];   /* in 32-bit words */
    int ct16[64][64];
    int t8[64][128];
    int t4[128][128];   /* in nibbles */

    GsSwizzleTables()
    {
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 64; x++)
                ct32[y][x] = GetPixelAddressPSMCT32(0, 1, x, y) >> 2;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                ct16[y][x] = GetPixelAddressPSMCT16(0, 1, x, y);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 128; x++)
                t8[y][x] = GetPixelAddressPSMT8(0, 2, x, y);
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
                t4[y][x] = GetPixelAddressPSMT4(0, 2, x, y);
    }
};

const GsSwizzleTables &GsSwizzle()
{
    static const GsSwizzleTables tables;
    return tables;
}

enum GsUploadKind
{
    kGsUpload32,
    kGsUpload24,
    kGsUpload16,
    kGsUpload8,
    kGsUpload8H,
    kGsUpload4,
    kGsUpload4HL,
    kGsUpload4HH,
};

struct GsUploadJob
{
    GsUploadKind kind;
    int dbp, dbw, dsax, dsay, rrw, rrh;
    const uint8_t *in;
    unsigned char *mem;
    size_t size;
};

inline int GsCt32RowBase(int dbp, int dbw, int y)
{
    return (((dbp >> 5) + (y >> 5) * dbw) << 11) + ((dbp & 0x1f) << 6);
}

/* Rows y0..y1 (absolute, inside the transfer) of the job. */
void GsUploadRows(const GsUploadJob &j, int y0, int y1)
{
    const GsSwizzleTables &t = GsSwizzle();
    unsigned char *const mem = j.mem;
    const size_t size = j.size;
    const int x0 = j.dsax;
    const int x1 = j.dsax + j.rrw;
    const size_t row_texels = (size_t)j.rrw;
    const size_t first = (size_t)(y0 - j.dsay) * row_texels;

    switch (j.kind)
    {
    case kGsUpload32:
    case kGsUpload24:
    {
        const int bpp = j.kind == kGsUpload32 ? 4 : 3;
        const uint8_t *in = j.in + first * (size_t)bpp;
        for (int y = y0; y < y1; ++y)
        {
            const int base = GsCt32RowBase(j.dbp, j.dbw, y);
            const int *row = t.ct32[y & 31];
            for (int x = x0; x < x1; ++x)
            {
                const int a = ((base + ((x >> 6) << 11) + row[x & 63]) << 2) & 0x003ffffc;
                std::memcpy(mem + a, in, (size_t)bpp);
                in += bpp;
            }
        }
        break;
    }
    case kGsUpload16:
    {
        const uint8_t *in = j.in + first * 2;
        for (int y = y0; y < y1; ++y)
        {
            const int base = (((j.dbp >> 5) + (y >> 6) * j.dbw) << 13) + ((j.dbp & 0x1f) << 8);
            const int *row = t.ct16[y & 63];
            for (int x = x0; x < x1; ++x)
            {
                const int a = (base + ((x >> 6) << 13) + row[x & 63]) & 0x003ffffe;
                std::memcpy(mem + a, in, 2);
                in += 2;
            }
        }
        break;
    }
    case kGsUpload8H:
    {
        const uint8_t *in = j.in + first;
        for (int y = y0; y < y1; ++y)
        {
            const int base = GsCt32RowBase(j.dbp, j.dbw, y);
            const int *row = t.ct32[y & 31];
            for (int x = x0; x < x1; ++x)
            {
                mem[(((base + ((x >> 6) << 11) + row[x & 63]) << 2) & 0x003ffffc) + 3] = *in++;
            }
        }
        break;
    }
    case kGsUpload8:
    {
        const uint8_t *in = j.in + first;
        for (int y = y0; y < y1; ++y)
        {
            const int base = (((j.dbp >> 5) + (y >> 6) * (j.dbw >> 1)) << 13) +
                             ((j.dbp & 0x1f) << 8);
            const int *row = t.t8[y & 63];
            for (int x = x0; x < x1; ++x)
            {
                const unsigned a = (unsigned)(base + ((x >> 7) << 13) + row[x & 127]);
                if (a < size)
                {
                    mem[a] = *in;
                }
                ++in;
            }
        }
        break;
    }
    case kGsUpload4HL:
    case kGsUpload4HH:
    {
        const int shift = j.kind == kGsUpload4HL ? 0 : 4;
        const unsigned char keep = j.kind == kGsUpload4HL ? 0xf0 : 0x0f;
        size_t s = first;
        for (int y = y0; y < y1; ++y)
        {
            const int base = GsCt32RowBase(j.dbp, j.dbw, y);
            const int *row = t.ct32[y & 31];
            for (int x = x0; x < x1; ++x, ++s)
            {
                const int n = (j.in[s >> 1] >> ((s & 1) << 2)) & 0x0f;
                unsigned char &d =
                    mem[(((base + ((x >> 6) << 11) + row[x & 63]) << 2) & 0x003ffffc) + 3];
                d = (unsigned char)((d & keep) | (n << shift));
            }
        }
        break;
    }
    case kGsUpload4:
    {
        size_t s = first;
        for (int y = y0; y < y1; ++y)
        {
            const int base = (((j.dbp >> 5) + (y >> 7) * (j.dbw >> 1)) << 14) +
                             ((j.dbp & 0x1f) << 9);
            const int *row = t.t4[y & 127];
            for (int x = x0; x < x1; ++x, ++s)
            {
                const int n = (j.in[s >> 1] >> ((s & 1) << 2)) & 0x0f;
                const int a = base + ((x >> 7) << 14) + row[x & 127];
                const unsigned d = (unsigned)(a >> 1);
                if (a >= 0 && d < size)
                {
                    const int sh = (a & 1) << 2;
                    mem[d] = (unsigned char)((n << sh) | (mem[d] & (0xf0 >> sh)));
                }
            }
        }
        break;
    }
    }
}

/* ---- the workers ------------------------------------------------------- */

constexpr int kGsBandRows = 16;
/* Below this many texels a transfer is written on the calling thread: the
 * hand-off would cost more than it saves. */
constexpr int kGsParallelTexels = 8192;
/* How long an idle worker spins for the next transfer before it sleeps: the
 * game's transfers come in bursts. */
constexpr int kGsSpinPauses = 20000;

struct GsUploadPool
{
    std::atomic<unsigned> generation{0};
    std::atomic<int> active{0};
    std::atomic<int> busy{0};
    std::atomic<int> next_band{0};
    std::atomic<int> bands_done{0};
    std::atomic<int> sleepers{0};
    std::mutex lock;
    std::condition_variable wake;
    GsUploadJob job{};
    int band_count = 0;
    int band_y0 = 0;
    int workers = -1;
};

/* Never destroyed: its workers sleep on its condition variable for as long as
 * the process lives, and destroying one with waiters blocks the exit. */
/* Transfers written in bands by the workers, and on the calling thread: for
 * the PS5's performance log. */
std::atomic<unsigned> g_gs_uploads_split{0};
std::atomic<unsigned> g_gs_uploads_inline{0};

GsUploadPool &GsPool()
{
    static GsUploadPool *const pool = new GsUploadPool;
    return *pool;
}

void GsDoBands(GsUploadPool &p)
{
    for (;;)
    {
        const int i = p.next_band.fetch_add(1);
        if (i >= p.band_count)
        {
            return;
        }
        const int y_end = p.job.dsay + p.job.rrh;
        const int y0 = std::max(p.job.dsay, p.band_y0 + i * kGsBandRows);
        const int y1 = std::min(y_end, p.band_y0 + (i + 1) * kGsBandRows);
        if (y0 < y1)
        {
            GsUploadRows(p.job, y0, y1);
        }
        p.bands_done.fetch_add(1);
    }
}

int GsUploadWorker(void *)
{
    GsUploadPool &p = GsPool();
    unsigned seen = p.generation.load();
    for (;;)
    {
        unsigned now;
        int spins = 0;
        while ((now = p.generation.load()) == seen)
        {
            if (++spins < kGsSpinPauses)
            {
                REIPAN_GS_PAUSE();
                continue;
            }
            std::unique_lock<std::mutex> guard(p.lock);
            p.sleepers.fetch_add(1);
            p.wake.wait(guard, [&] { return p.generation.load() != seen; });
            p.sleepers.fetch_sub(1);
            spins = 0;
        }
        seen = now;
        p.busy.fetch_add(1);
        if (p.generation.load() == now && p.active.load() != 0)
        {
            GsDoBands(p);
        }
        p.busy.fetch_sub(1);
    }
    return 0;
}

/*
 * Whether no two texels of the transfer can land on the same VRAM byte (other
 * than a 4-bit texel's partner, always in its band): only then may its bands
 * be written in any order.  A buffer that starts mid-page spills blocks into
 * the next page, a transfer wider than its buffer runs into the next row of
 * pages, and one that runs off the top of VRAM wraps -- each can map two
 * texels of different rows to one address, and then the later row has to win,
 * as it does written in order.
 */
bool GsBandsIndependent(const GsUploadJob &j)
{
    if ((j.dbp & 0x1f) != 0 || j.dsax < 0 || j.dsay < 0)
    {
        return false;
    }
    const int x_last = j.dsax + j.rrw - 1;
    const int y_last = j.dsay + j.rrh - 1;
    int pages_wide;
    int page_w;
    int page_h;
    switch (j.kind)
    {
    case kGsUpload16:
        pages_wide = j.dbw;
        page_w = 64;
        page_h = 64;
        break;
    case kGsUpload8:
        pages_wide = j.dbw >> 1;
        page_w = 128;
        page_h = 64;
        break;
    case kGsUpload4:
        pages_wide = j.dbw >> 1;
        page_w = 128;
        page_h = 128;
        break;
    default: /* the 32-bit layout: 32, 24, 8H, 4HL, 4HH */
        pages_wide = j.dbw;
        page_w = 64;
        page_h = 32;
        break;
    }
    if (pages_wide <= 0 || x_last >= pages_wide * page_w)
    {
        return false;
    }
    /* Every format's page is 8 KB: 512 of them in VRAM. */
    const long long last_page =
        (long long)(j.dbp >> 5) + (long long)(y_last / page_h) * pages_wide + x_last / page_w;
    return last_page < (long long)(j.size / 8192);
}

void GsRunUpload(const GsUploadJob &job)
{
    if (job.rrw <= 0 || job.rrh <= 0 || job.in == nullptr || job.mem == nullptr)
    {
        return;
    }
    GsUploadPool &p = GsPool();
    if (p.workers < 0)
    {
        p.workers = 0;
        const int wanted = GsUploadWorkers();
        for (int i = 0; i < wanted; i++)
        {
            if (GsSpawnWorker(GsUploadWorker, nullptr))
            {
                p.workers++;
            }
        }
    }

    const int band_y0 = job.dsay & ~(kGsBandRows - 1);
    const int bands = (job.dsay + job.rrh - band_y0 + kGsBandRows - 1) / kGsBandRows;
    if (p.workers == 0 || bands < 2 || job.rrw * job.rrh < kGsParallelTexels ||
        !GsBandsIndependent(job))
    {
        g_gs_uploads_inline.fetch_add(1, std::memory_order_relaxed);
        GsUploadRows(job, job.dsay, job.dsay + job.rrh);
        return;
    }
    g_gs_uploads_split.fetch_add(1, std::memory_order_relaxed);

    /* Every field before `active`: a worker that wakes early starts on them. */
    p.job = job;
    p.band_y0 = band_y0;
    p.band_count = bands;
    p.next_band.store(0);
    p.bands_done.store(0);
    p.active.store(1);
    p.generation.fetch_add(1);
    if (p.sleepers.load() > 0)
    {
        std::lock_guard<std::mutex> guard(p.lock);
        p.wake.notify_all();
    }

    GsDoBands(p);
    while (p.bands_done.load() < bands)
    {
        REIPAN_GS_PAUSE();
    }
    p.active.store(0);
    while (p.busy.load() != 0)
    {
        REIPAN_GS_PAUSE();
    }
}

} // namespace
