/* All UI colours in one place. Change a value here and rebuild; nothing else needs to know.
 * Format: GFX_RGB(0xRR, 0xGG, 0xBB) - the hex comment is the same colour written #RRGGBB.
 *
 * Two themes: DARK (default, black background to match the black case) and LIGHT. */
#pragma once
#include "gfx.h"

/* ============================ DARK ============================ */
#define COL_D_BG            GFX_RGB(0x00, 0x00, 0x00)   /* #000000 background */
#define COL_D_FACE          GFX_RGB(0xF4, 0xF4, 0xF4)   /* #F4F4F4 face */
#define COL_D_TEXT_DIM      GFX_RGB(0x7C, 0x7C, 0x80)   /* #7C7C80 corner text, hints, latest line (before fade) */
#define COL_D_LINE_TEXT     GFX_RGB(0xB8, 0xB8, 0xB4)   /* #B8B8B4 latest sentence under the face (full strength) */

#define COL_D_BAR           GFX_RGB(0x16, 0x18, 0x1A)   /* #16181A top / bottom bar, panel: a little lighter than the background */
#define COL_D_SEP           GFX_RGB(0x26, 0x29, 0x2C)   /* #26292C very faint separator lines */
#define COL_D_ICON          GFX_RGB(0xC9, 0xCB, 0xC8)   /* #C9CBC8 icons in the bars */
#define COL_D_ICON_PRESSED  GFX_RGB(0xF2, 0xC4, 0x8D)   /* #F2C48D icon while pressed */

#define COL_D_KE_BUBBLE     GFX_RGB(0x2F, 0x4A, 0x3A)   /* #2F4A3A my bubble: ink green */
#define COL_D_KE_TEXT       GFX_RGB(0xF1, 0xEB, 0xDD)   /* #F1EBDD my text: light cream */
#define COL_D_HER_BUBBLE    GFX_RGB(0xEF, 0xE3, 0xCF)   /* #EFE3CF her bubble: milk cream */
#define COL_D_HER_TEXT      GFX_RGB(0x3B, 0x2A, 0x1E)   /* #3B2A1E her text: dark brown */
#define COL_D_AVATAR_BG     GFX_RGB(0x22, 0x36, 0x2A)   /* #22362A little face pill in front of my messages */
#define COL_D_AVATAR_TEXT   GFX_RGB(0xE4, 0xDF, 0xD2)   /* #E4DFD2 */

#define COL_D_CAP           GFX_RGB(0x2B, 0x2F, 0x33)   /* #2B2F33 quick-phrase capsule */
#define COL_D_CAP_TEXT      GFX_RGB(0xEC, 0xE8, 0xDE)   /* #ECE8DE */
#define COL_D_CELL          GFX_RGB(0x22, 0x25, 0x28)   /* #222528 emoji grid cell */
#define COL_D_CELL_TEXT     GFX_RGB(0xEC, 0xE8, 0xDE)   /* #ECE8DE */
#define COL_D_PRESSED       GFX_RGB(0x4A, 0x52, 0x58)   /* #4A5258 capsule / cell while pressed */

#define COL_D_ONLINE        GFX_RGB(0x4C, 0xC3, 0x8A)   /* #4CC38A bridge reachable */
#define COL_D_OFFLINE       GFX_RGB(0x6B, 0x6F, 0x73)   /* #6B6F73 bridge not reachable */
#define COL_D_ACCENT        GFX_RGB(0xF0, 0x8A, 0x9A)   /* #F08A9A toasts */
#define COL_D_BLUSH         GFX_RGB(0xFF, 0x80, 0xA0)   /* #FF80A0 the // of a blush face */
#define COL_D_TOAST_TEXT    GFX_RGB(0x20, 0x10, 0x14)   /* #201014 */

/* ============================ LIGHT ============================ */
#define COL_L_BG            GFX_RGB(0xF6, 0xF2, 0xEA)   /* #F6F2EA */
#define COL_L_FACE          GFX_RGB(0x2B, 0x2B, 0x33)   /* #2B2B33 */
#define COL_L_TEXT_DIM      GFX_RGB(0x8A, 0x86, 0x80)   /* #8A8680 */
#define COL_L_LINE_TEXT     GFX_RGB(0x5A, 0x57, 0x52)   /* #5A5752 */

#define COL_L_BAR           GFX_RGB(0xEA, 0xE5, 0xDA)   /* #EAE5DA */
#define COL_L_SEP           GFX_RGB(0xD9, 0xD3, 0xC7)   /* #D9D3C7 */
#define COL_L_ICON          GFX_RGB(0x55, 0x57, 0x55)   /* #555755 */
#define COL_L_ICON_PRESSED  GFX_RGB(0xC7, 0x7A, 0x2E)   /* #C77A2E */

#define COL_L_KE_BUBBLE     GFX_RGB(0xC6, 0xE0, 0xCE)   /* #C6E0CE */
#define COL_L_KE_TEXT       GFX_RGB(0x1E, 0x2E, 0x24)   /* #1E2E24 */
#define COL_L_HER_BUBBLE    GFX_RGB(0xE6, 0xD3, 0xB1)   /* #E6D3B1 */
#define COL_L_HER_TEXT      GFX_RGB(0x3B, 0x2A, 0x1E)   /* #3B2A1E */
#define COL_L_AVATAR_BG     GFX_RGB(0xDD, 0xEC, 0xE0)   /* #DDECE0 */
#define COL_L_AVATAR_TEXT   GFX_RGB(0x2F, 0x4A, 0x3A)   /* #2F4A3A */

#define COL_L_CAP           GFX_RGB(0xFF, 0xFF, 0xFF)   /* #FFFFFF */
#define COL_L_CAP_TEXT      GFX_RGB(0x33, 0x33, 0x3A)   /* #33333A */
#define COL_L_CELL          GFX_RGB(0xFB, 0xF8, 0xF2)   /* #FBF8F2 */
#define COL_L_CELL_TEXT     GFX_RGB(0x33, 0x33, 0x3A)   /* #33333A */
#define COL_L_PRESSED       GFX_RGB(0xD0, 0xCA, 0xC0)   /* #D0CAC0 */

#define COL_L_ONLINE        GFX_RGB(0x2E, 0x9E, 0x63)   /* #2E9E63 */
#define COL_L_OFFLINE       GFX_RGB(0xB0, 0xB3, 0xB6)   /* #B0B3B6 */
#define COL_L_ACCENT        GFX_RGB(0xF0, 0x8A, 0x9A)   /* #F08A9A */
#define COL_L_BLUSH         GFX_RGB(0xF0, 0x70, 0x90)   /* #F07090 */
#define COL_L_TOAST_TEXT    GFX_RGB(0x20, 0x10, 0x14)   /* #201014 */
