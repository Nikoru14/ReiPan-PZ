/* ==========================================================================
 *  reipan/rendering/reipan_graph3d.h
 *
 *  Host side of the 3D engine: where the SGD walkers (gra3dSGD.c) hand the
 *  units they would send to VU1 to the PC renderer. The PS2 runs the meshes
 *  through VU1 microcode from the DMA packets; the PC has no VU1, so the
 *  walkers call these at the same points MioPan's gra3dSGD.c calls
 *  MioPan_Graph3d* (miopan_graph3d.h), with the same arguments.
 *
 *  All pointers are host pointers into emulated EE memory; the SGD data they
 *  lead to keeps EE addresses (sgd_types.h, SgdPtr).
 * ======================================================================== */
#ifndef REIPAN_GRAPH3D_H
#define REIPAN_GRAPH3D_H

struct GRA3DCAMERA;
struct SGDFILEHEADER;
struct SGDPROCUNITHEADER;

// gra3dApplyCamera's own matrices to the renderer: view is the world->view
// matrix it just built, the projection is camera->matViewClipObject.
void ReiPan_Graph3dApplyCamera(const GRA3DCAMERA *camera, const float (*view)[4]);

// A preset (pre-lit) mesh: vertices from the VUVN unit vuvn, strips and
// colours from mesh, placed by local_world (row-major 4x4).
void ReiPan_Graph3dDrawPresetMesh(SGDFILEHEADER *owner, SGDPROCUNITHEADER *vuvn,
                                  SGDPROCUNITHEADER *mesh, const float *local_world);
// The same mesh, for the over-the-shoulder camera's collision: while the
// scenery is being collected (reipan_ots_geometry.h), its triangles are
// decoded once and this draw of them recorded with local_world.
void ReiPan_Graph3dOtsRecordPresetMesh(SGDFILEHEADER *owner, SGDPROCUNITHEADER *vuvn,
                                       SGDPROCUNITHEADER *mesh, const float *local_world);
// A realtime (VU1-lit) mesh.
void ReiPan_Graph3dDrawRuntimeMesh(SGDFILEHEADER *owner, SGDPROCUNITHEADER *vuvn,
                                   SGDPROCUNITHEADER *mesh, const float *local_world);
// A mesh of the last block (skinned): post_vuvn, when not null, holds the
// vertex / normal pairs SetVUVNDataPost blended (two quadwords per vertex).
void ReiPan_Graph3dDrawRuntimeMeshPost(SGDFILEHEADER *owner, SGDPROCUNITHEADER *vuvn,
                                       SGDPROCUNITHEADER *mesh, const float *local_world,
                                       const float (*post_vuvn)[2][4]);
// A GS image unit (its data is a GIF image transfer).
void ReiPan_Graph3dUploadGsImage(SGDPROCUNITHEADER *image_unit);
// gra3dChangeST is about to move owner's ST coordinates: the resident mesh
// goes, the resolved textures (named by TEX0, which it does not touch) stay.
void ReiPan_Graph3dNotifyUVChange(SGDFILEHEADER *owner);
// sgdRemap / sgdRemapInverse are about to rewrite owner's addresses: the model
// at this address may be a new one, so drop everything held for it (the
// post-skin cache, its resident mesh and resolved textures).
void ReiPan_Graph3dInvalidateSkinCache(SGDFILEHEADER *owner);

// The TRI2 half of resolved textures, for gra3dLoadTRI2FileToVRAM.
// TextureUploadBound is asked before a TRI2 header chain is mirrored into GS
// memory: non-zero means it belongs to the preset model being walked and that
// model's textures are already resolved for the colour mode, so the host
// mirror is skipped. NoteTextureUpload is told after one that went through,
// which lets the model's first draw resolve its textures from GS memory.
int  ReiPan_Graph3dTextureUploadBound(const void *tri2_head);
void ReiPan_Graph3dNoteTextureUpload(const void *tri2_head);

// The TIM2 half, for the character / item model code (FF2 mdlwork.c), which
// sends a pack's TIM2 pictures before every draw. Bracket each send with
// Begin / End: source is the pack (a character's MPK or an item's pk2), kind
// says which, monotone is non-zero for the monotone CLUT set. Begin returns
// non-zero when the pack's textures are already resolved for the mode; the GS
// uploads inside the bracket are then dropped. NotifyTex0Rebase retires a
// character pack's resolved textures when its TEX0s are re-based.
enum
{
    REIPAN_TIM2_SOURCE_CHARACTER = 0,
    REIPAN_TIM2_SOURCE_ITEM      = 1,
};
int  ReiPan_Graph3dBeginTim2Upload(void *source, int kind, int monotone);
void ReiPan_Graph3dEndTim2Upload(void);
void ReiPan_Graph3dNotifyTex0Rebase(void *source);

// A 3D effect quad (MioPan RendererPacket3DUV): count (3 or 4) world corners in
// GS strip order, st their texture coordinates (s, t pairs), colour 0x80 = 1.0,
// tex0 the GS TEX0, z_offset the packet's GS-Z bias. The effect's DIRECT packet
// is built as on the PS2 and never runs on the host; this is what draws it.
void ReiPan_Graph3dDrawEffectQuad(const float (*world)[4], int count, const float *st,
                                  int r, int g, int b, int a, unsigned long long tex0,
                                  int z_offset);

// Untextured gouraud triangles (MioPan RendererGouraudTriangles3D): count
// world points (a multiple of 3, at most 144) with rgba per point, colour
// 0xff = 1.0 and alpha 0x80 = 1.0 (TME off). For the effect ribbons
// (effect_ene.c DrawNewPerticleSub), whose GS strips the host never runs.
void ReiPan_Graph3dDrawGouraudTriangles(const float (*world)[4], const unsigned char *rgba,
                                        int count);

// ReiPan_RendererBeginAnchor for a world point (w ignored), projected the way
// the effect quads above project theirs: the screen sprites that follow, up to
// ReiPan_RendererEndAnchor, move with it under frame smoothing's in-between
// cameras. For an effect that lays a sprite out in 2D around a projected
// position (SubHalo's halo) rather than drawing it in 3D. gain as there.
void ReiPan_Graph3dBeginWorldAnchor(const float *world, float gain = 1.0f);
// The same for the scissor box (ReiPan_RendererBeginScissorAnchor): the draws
// queued until ReiPan_RendererEndScissorAnchor keep their GS SCISSOR box on
// this world point under in-between cameras. Called while the room's camera is
// live (gra3dMirror.c's box around the mirror, at its centre).
void ReiPan_Graph3dBeginScissorAnchor(const float *world);

#endif /* REIPAN_GRAPH3D_H */
