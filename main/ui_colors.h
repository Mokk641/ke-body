/* All UI colours in one place. Change a value here and rebuild; nothing else needs to know.
 * Format: GFX_RGB(0xRR, 0xGG, 0xBB) - the comment is the same colour as #RRGGBB.
 * GFX_RGB packs to RGB565 (5/6/5 bits); the panel is driven exactly like Waveshare's own demo
 * (BGR element order + colour inversion, byte-swapped pixels), so these are true sRGB values.
 * If the real screen looks different from the phone, use the serial commands `colortest`
 * and `color` (README section on colour calibration) instead of editing these.
 *
 * Phase 6: iOS dark-mode iMessage look. Two themes: DARK (default) and LIGHT. */
#pragma once
#include "gfx.h"

/* ============================ DARK ============================ */
#define COL_D_BG            GFX_RGB(0x00, 0x00, 0x00)   /* #000000 background */
#define COL_D_FACE          GFX_GREY(0xFF)   /* #FFFFFF the big face */
#define COL_D_TEXT_DIM      GFX_GREY(0x64)   /* #636366 corner text, "swipe up" hint */
#define COL_D_LINE_TEXT     GFX_GREY(0x9C)   /* #9A9AA0 latest sentence under the face (full strength) */

#define COL_D_BAR           GFX_GREY(0x1D)   /* #1C1C1E top bar, bottom capsule, panel */
#define COL_D_SEP           GFX_GREY(0x2D)   /* #2C2C2E very faint separator */
#define COL_D_ICON          GFX_GREY(0xA3)   /* #A1A1A6 thin light-grey icons */
#define COL_D_ICON_PRESSED  GFX_GREY(0xFF)   /* #FFFFFF icon while pressed */

#define COL_D_KE_BUBBLE     GFX_GREY(0x3B)   /* #3A3A3C my bubble (left): dark grey, lighter than the black page */
#define COL_D_KE_TEXT       GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_D_HER_BUBBLE    GFX_RGB(0xF2, 0x70, 0x8F)   /* #F2708F her bubble (right): pink, a little toward orange (#F0609E looked magenta on the real screen) */
#define COL_D_HER_TEXT      GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_D_AVATAR_BG     GFX_GREY(0x2D)   /* #2C2C2E little circle with my face in the top bar */
#define COL_D_AVATAR_TEXT   GFX_GREY(0xFF)   /* #FFFFFF */

#define COL_D_CAP           GFX_GREY(0x2D)   /* #2C2C2E quick-phrase capsule */
#define COL_D_CAP_TEXT      GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_D_CELL          GFX_GREY(0x2D)   /* #2C2C2E emoji cell */
#define COL_D_CELL_TEXT     GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_D_PRESSED       GFX_GREY(0x49)   /* #48484A capsule / cell while pressed */
#define COL_D_DISABLED_TEXT GFX_GREY(0x64)   /* #636366 button text while a send is in progress */

#define COL_D_ONLINE        GFX_RGB(0x30, 0xD1, 0x58)   /* #30D158 bridge reachable (dot beside the name) */
#define COL_D_OFFLINE       GFX_GREY(0x64)   /* #636366 (not drawn: offline shows no dot) */
#define COL_D_ACCENT        GFX_RGB(0xF0, 0x8A, 0x9A)   /* #F08A9A toasts */
#define COL_D_BLUSH         GFX_RGB(0xFF, 0x80, 0xA0)   /* #FF80A0 the // of a blush face */
#define COL_D_TOAST_TEXT    GFX_RGB(0x20, 0x10, 0x14)   /* #201014 */

/* ============================ LIGHT ============================ */
#define COL_L_BG            GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_L_FACE          GFX_RGB(0x00, 0x00, 0x00)   /* #000000 white theme: the big face is black */
#define COL_L_TEXT_DIM      GFX_GREY(0xAF)   /* #AEAEB2 */
#define COL_L_LINE_TEXT     GFX_GREY(0x6D)   /* #6C6C70 */

#define COL_L_BAR           GFX_GREY(0xF4)   /* #F2F2F7 */
#define COL_L_SEP           GFX_GREY(0xD2)   /* #D1D1D6 */
#define COL_L_ICON          GFX_GREY(0x5C)   /* #5C5C5C dark-grey icons */
#define COL_L_ICON_PRESSED  GFX_RGB(0x00, 0x00, 0x00)   /* #000000 */

#define COL_L_KE_BUBBLE     GFX_GREY(0x1C)   /* #1C1C1E my bubble (left): near-black */
#define COL_L_KE_TEXT       GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_L_HER_BUBBLE    GFX_RGB(0xF2, 0x70, 0x8F)   /* #F2708F her bubble (right): pink, a little toward orange (#F0609E looked magenta on the real screen) */
#define COL_L_HER_TEXT      GFX_GREY(0xFF)   /* #FFFFFF */
#define COL_L_AVATAR_BG     GFX_GREY(0xE7)   /* #E5E5EA */
#define COL_L_AVATAR_TEXT   GFX_GREY(0x1D)   /* #1C1C1E */

#define COL_L_CAP           GFX_GREY(0xE7)   /* #E5E5EA */
#define COL_L_CAP_TEXT      GFX_RGB(0x00, 0x00, 0x00)   /* #000000 */
#define COL_L_CELL          GFX_GREY(0xE7)   /* #E5E5EA */
#define COL_L_CELL_TEXT     GFX_RGB(0x00, 0x00, 0x00)   /* #000000 */
#define COL_L_PRESSED       GFX_GREY(0xC9)   /* #C7C7CC */
#define COL_L_DISABLED_TEXT GFX_GREY(0xAF)   /* #AEAEB2 */

#define COL_L_ONLINE        GFX_RGB(0x34, 0xC7, 0x59)   /* #34C759 */
#define COL_L_OFFLINE       GFX_GREY(0xC9)   /* #C7C7CC */
#define COL_L_ACCENT        GFX_RGB(0xF0, 0x8A, 0x9A)   /* #F08A9A */
#define COL_L_BLUSH         GFX_RGB(0xF0, 0x70, 0x90)   /* #F07090 */
#define COL_L_TOAST_TEXT    GFX_RGB(0x20, 0x10, 0x14)   /* #201014 */
