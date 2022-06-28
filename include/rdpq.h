/**
 * @file rdpq.h
 * @brief RDP Command queue
 * @ingroup rsp
 * 
 * The RDP command queue is a library that allows to enqueue RDP commands for
 * asynchronous execution. It is the most low-level RDP library provided by
 * libdragon, and it exposes all the hardware primitives.
 * 
 * Normally, RDP commands are generated by both the CPU and the RSP. The normal
 * split is that CPU is in charge of render mode changes (eg: loading textures,
 * defining the alpha blending behavior, etc.), while the RSP executes a full
 * T&L pipeline which terminates with the generation of RDP triangle primitives.
 * 
 * This library is a rspq overlay, so it works through the RSP. All RDP commands
 * are in fact enqueued in the main RSP command queue, and they are sent to
 * the RDP by the RSP. There are two main reasons for this design (rather than
 * letting the CPU directly send the commands to the RDP):
 * 
 *  * Given that CPU and RSP usually work in parallel (with as few as possible
 *    syncing points), it is necessary to make sure that the CPU is able to
 *    schedule RDP commands that will be executed in the right order with
 *    respect to commands generated by RSP. This is easy to do if CPU-generated
 *    RDP commands always go through RSP in main command queue.
 *    
 *  * Most of the commands are sent unchanged to the RDP (we call them "passhtrough").
 *    Some commands, instead, are manipulated by the RSP and changed before
 *    they hit the RDP (we call these "fixups"). This is done to achieve a saner
 *    semantic for the programmer, hiding a few dark corners of the RDP hardware.
 * 
 * The documentation of the public API of this library describes the final
 * behavior of each rdpq command, without explicitly mentioning whether it is
 * obtained via fixups or not. For more information on these, see the
 * documentation of rdpq.c, which gives an overview of many implementation details.
 * 
 */

#ifndef __LIBDRAGON_RDPQ_H
#define __LIBDRAGON_RDPQ_H

#include <stdint.h>
#include <stdbool.h>
#include "graphics.h"
#include "n64sys.h"
#include "rdp_commands.h"
#include "surface.h"
#include "debug.h"

enum {
    RDPQ_CMD_NOOP                       = 0x00,
    RDPQ_CMD_SET_LOOKUP_ADDRESS         = 0x01,
    RDPQ_CMD_PUSH_RENDER_MODE           = 0x02,
    RDPQ_CMD_POP_RENDER_MODE            = 0x03,
    RDPQ_CMD_POP_RENDER_MODE_FIX        = 0x04,
    RDPQ_CMD_SET_COMBINE_MODE_2PASS     = 0x05,
    RDPQ_CMD_SET_COMBINE_MODE_2PASS_FIX = 0x06,
    RDPQ_CMD_TRI                        = 0x08,
    RDPQ_CMD_TRI_ZBUF                   = 0x09,
    RDPQ_CMD_TRI_TEX                    = 0x0A,
    RDPQ_CMD_TRI_TEX_ZBUF               = 0x0B,
    RDPQ_CMD_TRI_SHADE                  = 0x0C,
    RDPQ_CMD_TRI_SHADE_ZBUF             = 0x0D,
    RDPQ_CMD_TRI_SHADE_TEX              = 0x0E,
    RDPQ_CMD_TRI_SHADE_TEX_ZBUF         = 0x0F,

    RDPQ_CMD_TEXTURE_RECTANGLE_EX       = 0x10,
    RDPQ_CMD_TEXTURE_RECTANGLE_EX_FIX   = 0x11,
    RDPQ_CMD_SET_SCISSOR_EX             = 0x12,
    RDPQ_CMD_SET_SCISSOR_EX_FIX         = 0x13,
    RDPQ_CMD_MODIFY_OTHER_MODES         = 0x14,
    RDPQ_CMD_MODIFY_OTHER_MODES_FIX     = 0x15,
    RDPQ_CMD_SET_FILL_COLOR_32          = 0x16,
    RDPQ_CMD_SET_FILL_COLOR_32_FIX      = 0x17,
    RDPQ_CMD_SET_BLENDING_MODE          = 0x18,
    RDPQ_CMD_SET_BLENDING_MODE_FIX      = 0x19,
    RDPQ_CMD_SET_COMBINE_MODE_1PASS     = 0x1B,
    RDPQ_CMD_SET_COMBINE_MODE_1PASS_FIX = 0x1C,
    RDPQ_CMD_SET_TEXTURE_IMAGE_FIX      = 0x1D,
    RDPQ_CMD_SET_Z_IMAGE_FIX            = 0x1E,
    RDPQ_CMD_SET_COLOR_IMAGE_FIX        = 0x1F,

    RDPQ_CMD_SET_OTHER_MODES_FIX        = 0x20,
    RDPQ_CMD_SYNC_FULL_FIX              = 0x21,
    RDPQ_CMD_TEXTURE_RECTANGLE          = 0x24,
    RDPQ_CMD_TEXTURE_RECTANGLE_FLIP     = 0x25,
    RDPQ_CMD_SYNC_LOAD                  = 0x26,
    RDPQ_CMD_SYNC_PIPE                  = 0x27,
    RDPQ_CMD_SYNC_TILE                  = 0x28,
    RDPQ_CMD_SYNC_FULL                  = 0x29,
    RDPQ_CMD_SET_KEY_GB                 = 0x2A,
    RDPQ_CMD_SET_KEY_R                  = 0x2B,
    RDPQ_CMD_SET_CONVERT                = 0x2C,
    RDPQ_CMD_SET_SCISSOR                = 0x2D,
    RDPQ_CMD_SET_PRIM_DEPTH             = 0x2E,
    RDPQ_CMD_SET_OTHER_MODES            = 0x2F,

    RDPQ_CMD_LOAD_TLUT                  = 0x30,
    RDPQ_CMD_SET_TILE_SIZE              = 0x32,
    RDPQ_CMD_LOAD_BLOCK                 = 0x33,
    RDPQ_CMD_LOAD_TILE                  = 0x34,
    RDPQ_CMD_SET_TILE                   = 0x35,
    RDPQ_CMD_FILL_RECTANGLE             = 0x36,
    RDPQ_CMD_SET_FILL_COLOR             = 0x37,
    RDPQ_CMD_SET_FOG_COLOR              = 0x38,
    RDPQ_CMD_SET_BLEND_COLOR            = 0x39,
    RDPQ_CMD_SET_PRIM_COLOR             = 0x3A,
    RDPQ_CMD_SET_ENV_COLOR              = 0x3B,
    RDPQ_CMD_SET_COMBINE_MODE_RAW       = 0x3C,
    RDPQ_CMD_SET_TEXTURE_IMAGE          = 0x3D,
    RDPQ_CMD_SET_Z_IMAGE                = 0x3E,
    RDPQ_CMD_SET_COLOR_IMAGE            = 0x3F,
};

#define RDPQ_CFG_AUTOSYNCPIPE   (1 << 0)
#define RDPQ_CFG_AUTOSYNCLOAD   (1 << 1)
#define RDPQ_CFG_AUTOSYNCTILE   (1 << 2)

#define AUTOSYNC_TILE(n)  (1    << (0+(n)))
#define AUTOSYNC_TILES    (0xFF << 0)
#define AUTOSYNC_TMEM(n)  (1    << (8+(n)))
#define AUTOSYNC_TMEMS    (0xFF << 8)
#define AUTOSYNC_PIPE     (1    << 16)

/** @brief Used internally for bit-packing RDP commands. */
#define _carg(value, mask, shift) (((uint32_t)((value) & mask)) << shift)

#ifdef __cplusplus
extern "C" {
#endif

void rdpq_init();

void rdpq_close();

/**
 * @brief Add a fence to synchronize RSP with RDP commands.
 * 
 * This function schedules a fence in the RSP queue that makes RSP waits until
 * all previously enqueued RDP commands have finished executing. This is useful
 * in the rare cases in which you need to post-process the output of RDP with RSP
 * commands.
 * 
 * Notice that the RSP will spin-lock waiting for RDP to become idle, so, if
 * possible, call rdpq_fence as late as possible, to allow for parallel RDP/RSP
 * execution for the longest possible time.
 * 
 * Notice that this does not block the CPU in any way; the CPU will just
 * schedule the fence command in the RSP queue and continue execution. If you
 * need to block the CPU until the RDP is done, check #rspq_wait or #rdpq_sync_full
 * instead.
 * 
 * @see #rdpq_sync_full
 * @see #rspq_wait
 */
void rdpq_fence(void);

void rdpq_set_config(uint32_t cfg);
uint32_t rdpq_change_config(uint32_t on, uint32_t off);

void rdpq_triangle(uint8_t tile, uint8_t level, int32_t pos_offset, int32_t shade_offset, int32_t tex_offset, int32_t z_offset, const float *v1, const float *v2, const float *v3);

/**
 * @brief Low level function to draw a textured rectangle
 */
inline void rdpq_texture_rectangle_fx(uint8_t tile, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int16_t s, int16_t t, int16_t dsdx, int16_t dtdy)
{
    extern void __rdpq_texture_rectangle(uint32_t, uint32_t, uint32_t, uint32_t);

    __rdpq_texture_rectangle(
        _carg(x1, 0xFFF, 12) | _carg(y1, 0xFFF, 0),
        _carg(tile, 0x7, 24) | _carg(x0, 0xFFF, 12) | _carg(y0, 0xFFF, 0),
        _carg(s, 0xFFFF, 16) | _carg(t, 0xFFFF, 0),
        _carg(dsdx, 0xFFFF, 16) | _carg(dtdy, 0xFFFF, 0));
}

#define rdpq_texture_rectangle(tile, x0, y0, x1, y1, s, t, dsdx, dtdy) ({ \
    rdpq_texture_rectangle_fx((tile), (x0)*4, (y0)*4, (x1)*4, (y1)*4, (s)*32, (t)*32, (dsdx)*1024, (dtdy)*1024); \
})

/**
 * @brief Low level function to draw a textured rectangle (s and t coordinates flipped)
 */
inline void rdpq_texture_rectangle_flip_fx(uint8_t tile, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int16_t s, int16_t t, int16_t dsdx, int16_t dtdy)
{
    extern void __rdpq_write16_syncuse(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

    // Note that this command is broken in copy mode, so it doesn't
    // require any fixup. The RSP will trigger an assert if this
    // is called in such a mode.
    __rdpq_write16_syncuse(RDPQ_CMD_TEXTURE_RECTANGLE_FLIP,
        _carg(x1, 0xFFF, 12) | _carg(y1, 0xFFF, 0),
        _carg(tile, 0x7, 24) | _carg(x0, 0xFFF, 12) | _carg(y0, 0xFFF, 0),
        _carg(s, 0xFFFF, 16) | _carg(t, 0xFFFF, 0),
        _carg(dsdx, 0xFFFF, 16) | _carg(dtdy, 0xFFFF, 0),
        AUTOSYNC_PIPE | AUTOSYNC_TILE(tile) | AUTOSYNC_TMEM(0));
}

#define rdpq_texture_rectangle_flip(tile, x0, y0, x1, y1, s, t, dsdx, dtdy) ({ \
    rdpq_texture_rectangle_flip_fx((tile), (x0)*4, (y0)*4, (x1)*4, (y1)*4, (s)*32, (t)*32, (dsdx)*1024, (dtdy)*1024); \
})

/**
 * @brief Low level function to set the green and blue components of the chroma key
 */
inline void rdpq_set_chromakey_parms(color_t color, 
    int edge_r, int edge_g, int edge_b,
    int width_r, int width_g, int width_b)
{
    float fsr = 1.0f / edge_r;
    float fsg = 1.0f / edge_g;
    float fsb = 1.0f / edge_b;
    uint8_t sr = fsr * 255.0f;
    uint8_t sg = fsg * 255.0f;
    uint8_t sb = fsb * 255.0f;
    float fwr = width_r * fsr;
    float fwg = width_g * fsg;
    float fwb = width_b * fsb;
    uint16_t wr = fwr * 255.0f;
    uint16_t wg = fwg * 255.0f;
    uint16_t wb = fwb * 255.0f;

    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_KEY_R,
        0, 
        _carg(wr, 0xFFF, 16) | _carg(color.r, 0xFF, 8) | _carg(sr, 0xFF, 0),
        AUTOSYNC_PIPE);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_KEY_GB,
        _carg(wg, 0xFFF, 12) | _carg(wb, 0xFFF, 0),
        _carg(color.g, 0xFF, 24) | _carg(sg, 0xFF, 16) | _carg(color.b, 0xFF, 8) | _carg(sb, 0xFF, 0),
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level functions to set the matrix coefficients for texture format conversion
 */
inline void rdpq_set_yuv_parms(uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4, uint16_t k5)
{
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_CONVERT,
        _carg(k0, 0x1FF, 13) | _carg(k1, 0x1FF, 4) | (((uint32_t)(k2 & 0x1FF)) >> 5),
        _carg(k2, 0x1F, 27) | _carg(k3, 0x1FF, 18) | _carg(k4, 0x1FF, 9) | _carg(k5, 0x1FF, 0),
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level function to set the scissoring region
 */
#define rdpq_set_scissor(x0, y0, x1, y1) ({ \
    extern void __rdpq_set_scissor(uint32_t, uint32_t); \
    uint32_t x0fx = (x0)*4; \
    uint32_t y0fx = (y0)*4; \
    uint32_t x1fx = (x1)*4; \
    uint32_t y1fx = (y1)*4; \
    assertf(x0fx <= x1fx, "x0 must not be greater than x1!"); \
    assertf(y0fx <= y1fx, "y0 must not be greater than y1!"); \
    assertf(x1fx > 0, "x1 must not be zero!"); \
    assertf(y1fx > 0, "y1 must not be zero!"); \
    __rdpq_set_scissor( \
        _carg(x0fx, 0xFFF, 12) | _carg(y0fx, 0xFFF, 0), \
        _carg(x1fx, 0xFFF, 12) | _carg(y1fx, 0xFFF, 0)); \
})

/**
 * @brief Low level function to set the primitive depth
 */
inline void rdpq_set_prim_depth(uint16_t primitive_z, int16_t primitive_delta_z)
{
    // NOTE: this does not require a pipe sync
    extern void __rdpq_write8(uint32_t, uint32_t, uint32_t);
    __rdpq_write8(RDPQ_CMD_SET_PRIM_DEPTH, 0, _carg(primitive_z, 0xFFFF, 16) | _carg(primitive_delta_z, 0xFFFF, 0));
}

/**
 * @brief Low level function to load a texture palette into TMEM
 */
inline void rdpq_load_tlut(uint8_t tile, uint8_t lowidx, uint8_t highidx)
{
    extern void __rdpq_write8_syncchangeuse(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchangeuse(RDPQ_CMD_LOAD_TLUT, 
        _carg(lowidx, 0xFF, 14), 
        _carg(tile, 0x7, 24) | _carg(highidx, 0xFF, 14),
        AUTOSYNC_TMEM(0),
        AUTOSYNC_TILE(tile));
}

/**
 * @brief Low level function to set the size of a tile descriptor
 */
inline void rdpq_set_tile_size_fx(uint8_t tile, uint16_t s0, uint16_t t0, uint16_t s1, uint16_t t1)
{
    extern void __rdpq_write8_syncchange(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_TILE_SIZE,
        _carg(s0, 0xFFF, 12) | _carg(t0, 0xFFF, 0),
        _carg(tile, 0x7, 24) | _carg(s1-4, 0xFFF, 12) | _carg(t1-4, 0xFFF, 0),
        AUTOSYNC_TILE(tile));
}

#define rdpq_set_tile_size(tile, s0, t0, s1, t1) ({ \
    rdpq_set_tile_size_fx((tile), (s0)*4, (t0)*4, (s1)*4, (t1)*4); \
})

/**
 * @brief Low level function to load a texture image into TMEM in a single memory transfer
 */
inline void rdpq_load_block_fx(uint8_t tile, uint16_t s0, uint16_t t0, uint16_t s1, uint16_t dxt)
{
    extern void __rdpq_write8_syncchangeuse(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchangeuse(RDPQ_CMD_LOAD_BLOCK,
        _carg(s0, 0xFFC, 12) | _carg(t0, 0xFFC, 0),
        _carg(tile, 0x7, 24) | _carg(s1-4, 0xFFC, 12) | _carg(dxt, 0xFFF, 0),
        AUTOSYNC_TMEM(0),
        AUTOSYNC_TILE(tile));
}

// TODO: perform ceiling function on dxt
#define rdpq_load_block(tile, s0, t0, s1, dxt) ({ \
    rdpq_load_block_fx((tile), (s0)*4, (t0)*4, (s1)*4, (dxt)*2048); \
})

/**
 * @brief Low level function to load a texture image into TMEM
 */
inline void rdpq_load_tile_fx(uint8_t tile, uint16_t s0, uint16_t t0, uint16_t s1, uint16_t t1)
{
    extern void __rdpq_write8_syncchangeuse(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchangeuse(RDPQ_CMD_LOAD_TILE,
        _carg(s0, 0xFFF, 12) | _carg(t0, 0xFFF, 0),
        _carg(tile, 0x7, 24) | _carg(s1-4, 0xFFF, 12) | _carg(t1-4, 0xFFF, 0),
        AUTOSYNC_TMEM(0),
        AUTOSYNC_TILE(tile));
}

#define rdpq_load_tile(tile, s0, t0, s1, t1) ({ \
    rdpq_load_tile_fx((tile), (s0)*4, (t0)*4, (s1)*4, (t1)*4); \
})

/**
 * @brief Enqueue a RDP SET_TILE command (full version)
 */
inline void rdpq_set_tile_full(uint8_t tile, tex_format_t format, 
    uint16_t tmem_addr, uint16_t tmem_pitch, uint8_t palette,
    uint8_t ct, uint8_t mt, uint8_t mask_t, uint8_t shift_t,
    uint8_t cs, uint8_t ms, uint8_t mask_s, uint8_t shift_s)
{
    assertf((tmem_addr % 8) == 0, "invalid tmem_addr %d: must be multiple of 8", tmem_addr);
    assertf((tmem_pitch % 8) == 0, "invalid tmem_pitch %d: must be multiple of 8", tmem_pitch);
    extern void __rdpq_write8_syncchange(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_TILE,
        _carg(format, 0x1F, 19) | _carg(tmem_pitch/8, 0x1FF, 9) | _carg(tmem_addr/8, 0x1FF, 0),
        _carg(tile, 0x7, 24) | _carg(palette, 0xF, 20) | 
        _carg(ct, 0x1, 19) | _carg(mt, 0x1, 18) | _carg(mask_t, 0xF, 14) | _carg(shift_t, 0xF, 10) | 
        _carg(cs, 0x1, 9) | _carg(ms, 0x1, 8) | _carg(mask_s, 0xF, 4) | _carg(shift_s, 0xF, 0),
        AUTOSYNC_TILE(tile));
}

/**
 * @brief Enqueue a RDP SET_TILE command (basic version)
 * 
 * This RDP command allows to configure one of the internal tile descriptors
 * of the RDP. A tile descriptor is used to describe property of a texture
 * either being loaded into TMEM, or drawn from TMEM into the target buffer.
 * 
 * @param[in]  tile        Tile descriptor index (0-7)
 * @param[in]  format      Texture format
 * @param[in]  tmem_addr   Address in tmem where the texture is (or will be loaded)
 * @param[in]  tmem_pitch  Pitch of the texture in tmem in bytes (must be multiple of 8)
 * @param[in]  palette     Optional palette associated to the tile. For textures in
 *                         #FMT_CI4 format, specify the palette index (0-15),
 *                         otherwise use 0.
 */
inline void rdpq_set_tile(uint8_t tile, tex_format_t format, 
    uint16_t tmem_addr, uint16_t tmem_pitch, uint8_t palette)
{
    assertf((tmem_addr % 8) == 0, "invalid tmem_addr %d: must be multiple of 8", tmem_addr);
    assertf((tmem_pitch % 8) == 0, "invalid tmem_pitch %d: must be multiple of 8", tmem_pitch);
    extern void __rdpq_write8_syncchange(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_TILE,
        _carg(format, 0x1F, 19) | _carg(tmem_pitch/8, 0x1FF, 9) | _carg(tmem_addr/8, 0x1FF, 0),
        _carg(tile, 0x7, 24) | _carg(palette, 0xF, 20),
        AUTOSYNC_TILE(tile));
}

/**
 * @brief Enqueue a FILL_RECTANGLE RDP command using fixed point coordinates.
 * 
 * This function is similar to #rdpq_fill_rectangle, but coordinates must be
 * specified using fixed point numbers (0.10.2).
 *
 * @param[x0]   x0      Top-left X coordinate of the rectangle
 * @param[y0]   y0      Top-left Y coordinate of the rectangle
 * @param[x1]   x1      Bottom-right *exclusive* X coordinate of the rectangle
 * @param[y1]   y1      Bottom-right *exclusive* Y coordinate of the rectangle
 * 
 * @see rdpq_fill_rectangle
 */
inline void rdpq_fill_rectangle_fx(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    extern void __rdpq_write8_syncuse(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_write8_syncuse(RDPQ_CMD_FILL_RECTANGLE,
        _carg(x1, 0xFFF, 12) | _carg(y1, 0xFFF, 0),
        _carg(x0, 0xFFF, 12) | _carg(y0, 0xFFF, 0),
        AUTOSYNC_PIPE);
}

/**
 * @brief Enqueue a FILL_RECTANGLE RDP command.
 * 
 * This command is used to render a rectangle filled with a solid color.
 * The color must have been configured via #rdpq_set_fill_color, and the
 * render mode should be set to #SOM_CYCLE_FILL via #rdpq_set_other_modes.
 * 
 * The rectangle must be defined using exclusive bottom-right bounds, so for
 * instance `rdpq_fill_rectangle(10,10,30,30)` will draw a square of exactly
 * 20x20 pixels.
 * 
 * Fractional values can be used, and will create a semi-transparent edge. For
 * instance, `rdp_fill_rectangle(9.75,9.75,30.25,30.25)` will create a 22x22 pixel
 * square, with the most external pixel rows and columns having a alpha of 25%.
 * This obviously makes more sense in RGBA32 mode where there is enough alpha
 * bitdepth to appreciate the result. Make sure to configure the blender via
 * #rdpq_set_other_modes to decide the blending formula.
 * 
 * Notice that coordinates are unsigned numbers, so negative numbers are not
 * supported. Coordinates bigger than the target buffer will be automatically
 * clipped.
 * 
 * @param[x0]   x0      Top-left X coordinate of the rectangle (integer or float)
 * @param[y0]   y0      Top-left Y coordinate of the rectangle (integer or float)
 * @param[x1]   x1      Bottom-right *exclusive* X coordinate of the rectangle (integer or float)
 * @param[y1]   y1      Bottom-right *exclusive* Y coordinate of the rectangle (integer or float)
 * 
 * @see rdpq_fill_rectangle_fx
 * @see rdpq_set_fill_color
 * @see rdpq_set_fill_color_stripes
 * @see rdpq_set_other_modes
 * 
 */
#define rdpq_fill_rectangle(x0, y0, x1, y1) ({ \
    rdpq_fill_rectangle_fx((x0)*4, (y0)*4, (x1)*4, (y1)*4); \
})

/**
 * @brief Enqueue a SET_FILL_COLOR RDP command.
 * 
 * This command is used to configure the color used by #rdpq_fill_rectangle.
 * 
 * @param[in]    color   The color to use to fill
 */
inline void rdpq_set_fill_color(color_t color) {
    extern void __rdpq_set_fill_color(uint32_t);
    __rdpq_set_fill_color((color.r << 24) | (color.g << 16) | (color.b << 8) | (color.a << 0));
}

/**
 * @brief Enqueue a SET_FILL_COLOR RDP command to draw a striped pattern.
 * 
 * This command is similar to #rdpq_set_fill_color, but allows to configure
 * two colors, and creates a fill pattern that alternates horizontally between
 * them every 2 pixels (creating vertical stripes).
 * 
 * This command relies on a low-level hack of how RDP works in filling primitives,
 * so there is no configuration knob: it only works with RGBA 16-bit target
 * buffers, it only allows two colors, and the vertical stripes are exactly
 * 2 pixel width.
 * 
 * @param[in]   color1      Color of the first vertical stripe
 * @param[in]   color2      Color of the second vertical stripe
 * 
 * @see #rdpq_set_fill_color
 *
 */
inline void rdpq_set_fill_color_stripes(color_t color1, color_t color2) {
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    uint32_t c1 = (((int)color1.r >> 3) << 11) | (((int)color1.g >> 3) << 6) | (((int)color1.b >> 3) << 1) | (color1.a >> 7);
    uint32_t c2 = (((int)color2.r >> 3) << 11) | (((int)color2.g >> 3) << 6) | (((int)color2.b >> 3) << 1) | (color2.a >> 7);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_FILL_COLOR, 0, (c1 << 16) | c2,
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level function to set the fog color
 */
inline void rdpq_set_fog_color(color_t color)
{
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_FOG_COLOR, 0, color_to_packed32(color),
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level function to set the blend color
 */
inline void rdpq_set_blend_color(color_t color)
{
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_BLEND_COLOR, 0, color_to_packed32(color),
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level function to set the primitive color
 */
inline void rdpq_set_prim_color(color_t color)
{
    // NOTE: this does not require a pipe sync
    extern void __rdpq_write8(uint32_t cmd_id, uint32_t arg0, uint32_t arg1);
    __rdpq_write8(RDPQ_CMD_SET_PRIM_COLOR, 0, color_to_packed32(color));
}

/**
 * @brief Low level function to set the environment color
 */
inline void rdpq_set_env_color(color_t color)
{
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_ENV_COLOR, 0, color_to_packed32(color),
        AUTOSYNC_PIPE);
}

/**
 * @brief Low level function to set RDRAM pointer to a texture image
 */
inline void rdpq_set_texture_image_lookup(uint8_t index, uint32_t offset, tex_format_t format, uint16_t width)
{
    assertf(index <= 15, "Lookup address index out of range [0,15]: %d", index);
    extern void __rdpq_set_fixup_image(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_set_fixup_image(RDPQ_CMD_SET_TEXTURE_IMAGE, RDPQ_CMD_SET_TEXTURE_IMAGE_FIX,
        _carg(format, 0x1F, 19) | _carg(width-1, 0x3FF, 0),
        _carg(index, 0xF, 28) | (offset & 0xFFFFFF));
}

inline void rdpq_set_texture_image(const void* dram_ptr, tex_format_t format, uint16_t width)
{
    rdpq_set_texture_image_lookup(0, PhysicalAddr(dram_ptr), format, width);
}

/**
 * @brief Low level function to set RDRAM pointer to the depth buffer
 */
inline void rdpq_set_z_image_lookup(uint8_t index, uint32_t offset)
{
    assertf(index <= 15, "Lookup address index out of range [0,15]: %d", index);
    extern void __rdpq_set_fixup_image(uint32_t, uint32_t, uint32_t, uint32_t);
    __rdpq_set_fixup_image(RDPQ_CMD_SET_Z_IMAGE, RDPQ_CMD_SET_Z_IMAGE_FIX,
        0, 
        _carg(index, 0xF, 28) | (offset & 0xFFFFFF));
}

inline void rdpq_set_z_image(void* dram_ptr)
{
    assertf(((uint32_t)dram_ptr & 7) == 0, "buffer pointer is not aligned to 8 bytes, so it cannot use as RDP depth image");
    rdpq_set_z_image_lookup(0, PhysicalAddr(dram_ptr));
}

/**
 * @brief Low level function to set RDRAM pointer to the color buffer
 */
inline void rdpq_set_color_image_lookup_no_scissor(uint8_t index, uint32_t offset, tex_format_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    assertf(format == FMT_RGBA32 || format == FMT_RGBA16 || format == FMT_CI8, "Image format is not supported!\nIt must be FMT_RGBA32, FMT_RGBA16 or FMT_CI8");

    uint32_t bitdepth = TEX_FORMAT_BYTES_PER_PIXEL(format);
    assertf(stride % bitdepth == 0, "Stride must be a multiple of the bitdepth!");
    assertf(index <= 15, "Lookup address index out of range [0,15]: %d", index);

    extern void __rdpq_set_color_image(uint32_t, uint32_t);
    __rdpq_set_color_image(
        _carg(format, 0x1F, 19) | _carg((stride/bitdepth)-1, 0x3FF, 0),
        _carg(index, 0xF, 28) | (offset & 0xFFFFFF));
}

inline void rdpq_set_color_image_lookup(uint8_t index, uint32_t offset, tex_format_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    rdpq_set_color_image_lookup_no_scissor(index, offset, format, width, height, stride);
    rdpq_set_scissor(0, 0, width, height);
}

/**
 * @brief Enqueue a SET_COLOR_IMAGE RDP command.
 * 
 * This command is used to specify the target buffer that the RDP will draw to.
 *
 * Calling this function also automatically configures scissoring (via
 * #rdpq_set_scissor), so that all draw commands are clipped within the buffer,
 * to avoid overwriting memory around it.
 *
 * @param      dram_ptr  Pointer to the buffer in RAM
 * @param[in]  format    Format of the buffer. Supported formats are:
 *                       #FMT_RGBA32, #FMT_RGBA16, #FMT_I8.
 * @param[in]  width     Width of the buffer in pixels
 * @param[in]  height    Height of the buffer in pixels
 * @param[in]  stride    Stride of the buffer in bytes (distance between one
 *                       row and the next one)
 *                       
 * @see #rdpq_set_color_image_surface
 */
inline void rdpq_set_color_image_no_scissor(void* dram_ptr, tex_format_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    assertf(((uint32_t)dram_ptr & 63) == 0, "buffer pointer is not aligned to 64 bytes, so it cannot use as RDP color image.\nAllocate it with memalign(64, len) or malloc_uncached_align(64, len)");
    rdpq_set_color_image_lookup_no_scissor(0, PhysicalAddr(dram_ptr), format, width, height, stride);
}

inline void rdpq_set_color_image(void* dram_ptr, tex_format_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    assertf(((uint32_t)dram_ptr & 7) == 0, "buffer pointer is not aligned to 8 bytes, so it cannot use as RDP color image");
    rdpq_set_color_image_lookup(0, PhysicalAddr(dram_ptr), format, width, height, stride);
}

/**
 * @brief Enqueue a SET_COLOR_IMAGE RDP command, using a #surface_t
 * 
 * This command is similar to #rdpq_set_color_image, but the target buffer is
 * specified using a #surface_t.
 * 
 * @param[in]  surface  Target buffer to draw to
 * 
 * @see #rdpq_set_color_image
 */
inline void rdpq_set_color_image_surface_no_scissor(surface_t *surface)
{
    rdpq_set_color_image_no_scissor(surface->buffer, surface_get_format(surface), surface->width, surface->height, surface->stride);
}

inline void rdpq_set_color_image_surface(surface_t *surface)
{
    rdpq_set_color_image(surface->buffer, surface_get_format(surface), surface->width, surface->height, surface->stride);
}

inline void rdpq_set_lookup_address(uint8_t index, void* rdram_addr)
{
    assertf(index > 0 && index <= 15, "Lookup address index out of range [1,15]: %d", index);
    extern void __rdpq_dynamic_write8(uint32_t, uint32_t, uint32_t);
    __rdpq_dynamic_write8(RDPQ_CMD_SET_LOOKUP_ADDRESS, index << 2, PhysicalAddr(rdram_addr));
}

/**
 * @brief Schedule a RDP SYNC_PIPE command.
 * 
 * This command must be sent before changing the RDP pipeline configuration (eg: color
 * combiner, blender, colors, etc.) if the RDP is currently drawing.
 * 
 * Normally, you do not need to call this function because rdpq automatically
 * emits sync commands whenever necessary. You must call this function only
 * if you have disabled autosync for SYNC_PIPE (see #RDPQ_CFG_AUTOSYNCPIPE).
 * 
 * @note No software emulator currently requires this command, so manually
 *       sending SYNC_PIPE should be developed on real hardware.
 */
void rdpq_sync_pipe(void);

/**
 * @brief Schedule a RDP SYNC_TILE command.
 * 
 * This command must be sent before changing a RDP tile configuration if the
 * RDP is currently drawing using that same tile.
 * 
 * Normally, you do not need to call this function because rdpq automatically
 * emits sync commands whenever necessary. You must call this function only
 * if you have disabled autosync for SYNC_TILE (see #RDPQ_CFG_AUTOSYNCTILE).
 * 
 * @note No software emulator currently requires this command, so manually
 *       sending SYNC_TILE should be developed on real hardware.
 */
void rdpq_sync_tile(void);

/**
 * @brief Schedule a RDP SYNC_FULL command and register a callback when it is done.
 * 
 * This function schedules a RDP SYNC_FULL command into the RSP queue. This
 * command basically forces the RDP to finish drawing everything that has been
 * sent to it before it, and then generate an interrupt when it is done.
 * 
 * This is normally useful at the end of the frame. For instance, it is used
 * internally by #rdp_detach_display to make sure RDP is finished drawing on
 * the target display before detaching it.
 * 
 * The function can be passed an optional callback that will be called
 * when the RDP interrupt triggers. This can be useful to perform some operations
 * asynchronously.
 * 
 * @param      callback  A callback to invoke under interrupt when the RDP
 *                       is finished drawing, or NULL if no callback is necessary.
 * @param      arg       Opaque argument that will be passed to the callback.
 * 
 * @see #rspq_wait
 * @see #rdpq_fence
 * 
 */
void rdpq_sync_full(void (*callback)(void*), void* arg);

/**
 * @brief Low level function to synchronize RDP texture load operations
 */
void rdpq_sync_load(void);


/**
 * @brief Low-level function to set the rendering mode register.
 * 
 * This function enqueues a low-level SET_OTHER_MODES RDP command that changes
 * the RDP current mode, setting it to a new value
 * 
 * This function is very low level and requires very good knowledge of internal
 * RDP state management. Moreover, it completely overwrites any existing
 * configuration for all bits, so it must be used with caution within a block.
 * 
 * @note If possible, prefer using the rdpq_rm function family that exposes a
 * higher level API for changing the current render mode.
 *
 * @param      mode     The new render mode. See the RDP_RM
 * 
 */
inline void rdpq_set_other_modes_raw(uint64_t mode)
{
    extern void __rdpq_set_other_modes(uint32_t, uint32_t);
    __rdpq_set_other_modes(
        (mode >> 32) & 0x00FFFFFF,
        mode & 0xFFFFFFFF);
}

/**
 * @brief Low-level function to partly change the rendering mode register.
 * 
 * This function allows to partially change the RDP render mode register, 
 * enqueuing a command that will modify only the requested bits. This function
 * is to be preferred to #rdpq_set_other_modes_raw as it preservers existing
 * render mode for all the other bits, so it allows for easier composition.
 * 
 */
inline void rdpq_change_other_modes_raw(uint64_t mask, uint64_t val)
{
    extern void __rdpq_modify_other_modes(uint32_t, uint32_t, uint32_t);

    if (mask >> 32)
        __rdpq_modify_other_modes(0, ~(mask >> 32), val >> 32);
    if ((uint32_t)mask)
        __rdpq_modify_other_modes(4, ~(uint32_t)mask, (uint32_t)val);
}

uint64_t rdpq_get_other_modes_raw(void);

inline void rdpq_set_combiner_raw(uint64_t comb) {
    extern void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync);
    __rdpq_write8_syncchange(RDPQ_CMD_SET_COMBINE_MODE_RAW,
        (comb >> 32) & 0x00FFFFFF,
        comb & 0xFFFFFFFF,
        AUTOSYNC_PIPE);   
}

/**
 * @brief Push the current render mode into the stack
 * 
 * This function allows to push the current render mode into an internal stack.
 * It allows to temporarily modify the render mode, and later recover its value.
 */

void rdpq_mode_push(void);

/**
 * @brief Pop the current render mode from the stack
 * 
 * This function allows to pop a previously pushed render mode from the stack,
 * setting it as current again.
 */

void rdpq_mode_pop(void);

typedef uint64_t rdpq_combiner_t;
typedef uint32_t rdpq_blender_t;

typedef enum rdpq_sampler_s {
    SAMPLER_POINT = 0,
    SAMPLER_BILINEAR,
    SAMPLER_MEDIAN
} rdpq_sampler_t;

typedef enum rdpq_dither_s {
    DITHER_SQUARE = 0,
    DITHER_BAYER,
    DITHER_NOISE,
    DITHER_NONE
} rdpq_dither_t;

/**
 * @brief Reset render mode to FILL type.
 * 
 * This function sets the render mode type to FILL, which is used to quickly
 * fill portions of the screens with a solid color. The specified color is
 * configured via #rdpq_set_fill_color, and can be changed later.
 * 
 * Notice that in FILL mode most of the RDP features are disabled, so all other
 * render modes settings (rdpq_mode_* functions) do not work.
 *
 * @param[in]  color  The fill color to use
 */
inline void rdpq_set_mode_fill(color_t color) {
    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    rdpq_set_fill_color(color);
}

/**
 * @brief Reset render mode to COPY type.
 * 
 * This function sets the render mode type to COPY, which is used to quickly
 * blit bitmaps. In COPY mode, only texture rectangles (aka "sprites") can be
 * drawn and no advanced render mode features are working (rdpq_mode_* functions).
 * 
 * The only available feature is transparency: pixels with alpha set to 0 can
 * optionally be discarded during blit, so that the target buffer contents is
 * not overwritten for those pixels. This is implemented using alpha compare.
 * 
 * @param[in]  transparency   If true, pixels with alpha set to 0 are not drawn
 */
inline void rdpq_set_mode_copy(bool transparency) {
    if (transparency) rdpq_set_blend_color(RGBA32(0,0,0,1));
    rdpq_set_other_modes_raw(SOM_CYCLE_COPY | (transparency ? SOM_ALPHA_COMPARE : 0));
}

inline void rdpq_set_mode_standard(void) {
    // FIXME: accept structure?
    // FIXME: reset combiner?
    rdpq_set_other_modes_raw(SOM_CYCLE_1 | SOM_TC_FILTER | SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE);
}

inline void rdpq_mode_combiner(rdpq_combiner_t comb) {
    extern void __rdpq_fixup_write8(uint32_t cmd_id_dyn, uint32_t cmd_id_fix, int skip_size, uint32_t arg0, uint32_t arg1);

    // FIXME: autosync pipe
    if (comb & RDPQ_COMBINER_2PASS)
        __rdpq_fixup_write8(RDPQ_CMD_SET_COMBINE_MODE_2PASS, RDPQ_CMD_SET_COMBINE_MODE_2PASS_FIX, 4,
            (comb >> 32) & 0x00FFFFFF,
            comb & 0xFFFFFFFF);
    else
        __rdpq_fixup_write8(RDPQ_CMD_SET_COMBINE_MODE_1PASS, RDPQ_CMD_SET_COMBINE_MODE_1PASS_FIX, 4,
            (comb >> 32) & 0x00FFFFFF,
            comb & 0xFFFFFFFF);
}

inline void rdpq_mode_blender(rdpq_blender_t blend) {
    extern void __rdpq_fixup_write8(uint32_t cmd_id_dyn, uint32_t cmd_id_fix, int skip_size, uint32_t arg0, uint32_t arg1);

    // NOTE: basically everything this function does will be constant-propagated
    // when the function is called with a compile-time constant argument, which
    // should be the vast majority of times.

    // RDPQ_CMD_SET_BLENDING_MODE accepts two blender configurations: the one
    // to use in 1cycle mode, and the one to use in 2cycle mode. MAKE_SBM_ARG
    // encodes the two configurations into a 64-bit word to be used with the command.
    #define MAKE_SBM_ARG(blend_1cyc, blend_2cyc) \
        ((((uint64_t)(blend_1cyc) >> 6) & 0x3FFFFFF) | \
         (((uint64_t)(blend_2cyc) >> 6) & 0x3FFFFFF) << 26)

    rdpq_blender_t blend_1cyc, blend_2cyc;
    if (blend & RDPQ_BLENDER_2PASS) {
        // A 2-pass blender will force 2cycle mode, so we don't care about the
        // configuration for 1cycle mode. Let's just use 0 for it, it will not
        // be used anyway.
        blend_1cyc = 0;
        blend_2cyc = blend;
    } else {
        // A single pass blender can be used as-is in 1cycle mode (the macros
        // in rdp_commands have internally configured the same settings in both
        // passes, as this is what RDP expects).
        // For 2-cycle mode, instead, it needs to be changed: the configuration
        // is valid for the second pass, but the first pass needs to changed
        // with a passthrough (PIXEL * 0 + PIXEL * 1). Notice that we can't do
        // the passthrough in the second pass because of the way the 2pass
        // blender formula works.
        const rdpq_blender_t passthrough = RDPQ_BLENDER1((PIXEL_RGB, ZERO, PIXEL_RGB, ONE));
        blend_1cyc = blend;
        blend_2cyc = (passthrough & SOM_BLEND0_MASK) | 
                     (blend       & SOM_BLEND1_MASK);
    }

    // FIXME: autosync pipe
    uint64_t cfg = MAKE_SBM_ARG(blend_1cyc, blend_2cyc);
    __rdpq_fixup_write8(RDPQ_CMD_SET_BLENDING_MODE, RDPQ_CMD_SET_BLENDING_MODE_FIX, 4,
        (cfg >> 32) & 0x00FFFFFF,
        cfg & 0xFFFFFFFF);
}

inline void rdpq_mode_blender_off(void) {
    extern void __rdpq_fixup_write8(uint32_t cmd_id_dyn, uint32_t cmd_id_fix, int skip_size, uint32_t arg0, uint32_t arg1);
    __rdpq_fixup_write8(RDPQ_CMD_SET_BLENDING_MODE, RDPQ_CMD_SET_BLENDING_MODE_FIX, 4, 0, 0);
}

inline void rdpq_mode_dithering(rdpq_dither_t rgb, rdpq_dither_t alpha) {
    rdpq_change_other_modes_raw(
        SOM_RGBDITHER_MASK | SOM_ALPHADITHER_MASK,
        ((uint64_t)rgb << SOM_RGBDITHER_SHIFT) | ((uint64_t)alpha << SOM_ALPHADITHER_SHIFT));
}

inline void rdpq_mode_alphacompare(bool enable, int threshold) {
    if (enable && threshold > 0) rdpq_set_blend_color(RGBA32(0,0,0,threshold));
    rdpq_change_other_modes_raw(
        SOM_ALPHACOMPARE_MASK, enable ? SOM_ALPHA_COMPARE : 0
    );
}

inline void rdpq_mode_zoverride(bool enable, uint16_t z, int16_t deltaz) {
    if (enable) rdpq_set_prim_depth(z, deltaz);
    rdpq_change_other_modes_raw(
        SOM_Z_SOURCE_PRIM, enable ? SOM_Z_SOURCE_PRIM : 0
    );
}

inline void rdpq_mode_sampler(rdpq_sampler_t s) {
    uint64_t samp;
    switch (s) {
        case SAMPLER_POINT:    samp = SOM_SAMPLE_1X1; break;
        case SAMPLER_MEDIAN:   samp = SOM_SAMPLE_2X2 | SOM_SAMPLE_MIDTEXEL; break;
        case SAMPLER_BILINEAR: samp = SOM_SAMPLE_2X2; break;
    }
    rdpq_change_other_modes_raw(SOM_SAMPLE_MASK, samp);    
}

#ifdef __cplusplus
}
#endif

#endif
