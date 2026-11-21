#ifndef REIPAN_OTS_GEOMETRY_H
#define REIPAN_OTS_GEOMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rooms' own triangles, for the over-the-shoulder camera's collision.
 *
 * The game has no collision for a camera that hangs above the player's
 * shoulder: its map hit data is about where the player may walk.  The shapes
 * that are drawn are what the camera must not pass through, so the scenery
 * draw hands every room and placed-model mesh it draws in the main scene to
 * this module.  Each mesh's triangles are kept once, in the mesh's local
 * space, and each frame's draws of them with their world matrices; the camera
 * then casts segments against them.
 *
 * Only the room and placed-model draws are collected (see MapDrawRooms and
 * MapPutDraw): not the mirror pass, not characters, ghosts or the camera in
 * the player's hands.  What is drawn last frame is what the camera collides
 * with this frame.
 */

/* Non-zero while the scenery being drawn should be recorded. */
int ReiPan_OtsGeomCollecting(void);

/* The camera turns collection on while it is in use. */
void ReiPan_OtsGeomSetEnabled(int on);

/* MapDrawRooms / MapPutDraw open and close a scenery scope around the draws
 * that count; nested scopes (a character drawn among the placed models) turn
 * it off for their duration. */
void ReiPan_OtsGeomBeginScenery(void);
void ReiPan_OtsGeomEndScenery(void);
void ReiPan_OtsGeomSuspend(void);
void ReiPan_OtsGeomResume(void);

/* Whether the mesh `key` of the data block `top` is already decoded. */
int ReiPan_OtsGeomHasMesh(const void *key, const void *top);

/* Decodes a mesh: `positions` holds `vertex_count` xyz triples in its local
 * space, `indices` `index_count` vertex numbers, three a triangle. */
void ReiPan_OtsGeomAddMesh(const void *key, const void *top,
                           const float *positions, int vertex_count,
                           const unsigned int *indices, int index_count);

/* The data block `top` is about to hold something else (a model being
 * loaded or remapped there): forget its meshes and their draws. */
void ReiPan_OtsGeomForgetOwner(const void *top);

/* Records this frame's draw of a decoded mesh with its local-to-world matrix
 * (PS2 layout: rows 0-2 are the axes, row 3 the translation). */
void ReiPan_OtsGeomRecordDraw(const void *key, const void *top,
                              const float model[4][4]);

/* The draws recorded while the last frame was drawn become the ones
 * ReiPan_OtsGeomCast tests; call once per frame before casting. */
void ReiPan_OtsGeomNextFrame(void);

/* Pieces known for casting this frame. */
int ReiPan_OtsGeomPieceCount(void);

typedef struct ReiPan_OtsGeomHit
{
    float distance;       /* along the segment, or the length when clear */
    int   hit;            /* non-zero when something was hit */
    int   pieces_tested;  /* pieces whose bounds the segment crossed */
    int   triangles_tested;
    float hit_size[3];    /* world bounds of what was hit, for the readout */
} ReiPan_OtsGeomHit;

/* Casts a sphere of `radius` (five parallel rays) from `from` along the unit
 * vector `dir` for `length` against the recorded scenery.  Pieces thinner than
 * `thin` across both horizontal directions (ropes, poles, candles) are seen
 * past. */
void ReiPan_OtsGeomCast(const float from[3], const float dir[3], float length,
                        float radius, float thin, ReiPan_OtsGeomHit *out);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_OTS_GEOMETRY_H */
