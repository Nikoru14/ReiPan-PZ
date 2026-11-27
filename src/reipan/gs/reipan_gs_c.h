#ifndef REIPAN_GS_C_H
#define REIPAN_GS_C_H

#include "sce_gs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ReiPan_GsClear(void);
/* Image transfers written in parallel bands, and on the calling thread, since
 * the last call (reipan_gs_upload.inc). */
void ReiPan_GsUploadSplitStats(unsigned *split, unsigned *inline_count);
void ReiPan_GsUpload(sceGsLoadImage *image_load, unsigned char *image);
/* While non-zero, ReiPan_GsUpload() drops what it is handed -- nothing is
 * written, invalidated or counted.  Set only around a model's own texture
 * sends once the host has resolved that model's textures (see
 * ReiPan_Graph3dBeginTim2Upload()); every other upload is untouched. */
void ReiPan_GsSetUploadSuppressed(int suppressed);
/* While set, called with every transfer ReiPan_GsUpload() goes on to perform
 * -- the host texture binding uses it to learn exactly what a character's or
 * item's send writes.  NULL clears it. */
void ReiPan_GsSetUploadObserver(void (*observer)(const sceGsLoadImage *image,
                                                 void *user),
                                void *user);
void ReiPan_GsStore(sceGsStoreImage *sp, unsigned char *out);
unsigned char *ReiPan_GsDownloadTexture(sceGsTex0 *tex0, uint64_t *hash);
/* The same decode with the texel alpha left as the GS stores it (0x80 is
 * opaque, up to 0xff), for draws that run the GS alpha test on it -- see
 * GetTextureRawAlpha() in the renderer.  TCC=RGB reads as 0x80. */
unsigned char *ReiPan_GsDownloadTextureRawAlpha(sceGsTex0 *tex0);
uint64_t ReiPan_GetTextureHash(sceGsTex0 *tex0);

void ReiPan_GsResetFrameMetrics(void);
int ReiPan_GsGetUploadCount(void);
uint64_t ReiPan_GsGetUploadCountLive(void);
uint64_t ReiPan_GsGetUploadBytesLive(void);
int ReiPan_GsGetUploadBytes(void);
float ReiPan_GsGetUploadMs(void);
int ReiPan_GsGetDownloadCount(void);
int ReiPan_GsGetDownloadBytes(void);
float ReiPan_GsGetDownloadMs(void);

int ReiPan_GsHasPendingUploads(void);
void ReiPan_GsConsumePendingUploads(
    void (*cb)(int addr, int size, void *ud), void *ud);
void ReiPan_GetTextureGsRegion(sceGsTex0 *tex0, int *out_addr, int *out_size);

#ifdef __cplusplus
}
#endif

#endif
