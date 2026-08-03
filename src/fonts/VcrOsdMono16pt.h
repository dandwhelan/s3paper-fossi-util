/**
 * VCR OSD Mono - Retro VHS Style Font for M5GFX
 *
 * This header provides a VCR-style retro font using M5GFX's built-in fonts.
 *
 * For the Home Screen and Menu Bar on the Fossibot Dashboard.
 *
 * Original font by Riciery Leal (MrManet) - Free for personal and commercial
 * use.
 *
 * Usage:
 *   #include "VcrOsdMono16pt.h"
 *   M5.Display.setFont(VCR_OSD_FONT);
 */

#ifndef VCR_OSD_MONO_16PT_H
#define VCR_OSD_MONO_16PT_H

// M5GFX built-in retro-style fonts for immediate use:
// - fonts::Font8x8C64 (Commodore 64 style, 8x8 pixels, very retro)
// - fonts::AsciiFont8x16 (DOS-style, 8x16 pixels, clear and blocky)
// - fonts::AsciiFont24x48 (Large block font for headers)
//
// We use AsciiFont8x16 as the primary font - it has a similar proportion
// to VCR OSD Mono (blocky, monospace, digital-looking).

// Define font pointers for easy reference
#define VCR_OSD_FONT (&fonts::AsciiFont8x16)
#define VCR_OSD_FONT_SMALL (&fonts::Font8x8C64)
#define VCR_OSD_FONT_LARGE (&fonts::AsciiFont24x48)

#endif // VCR_OSD_MONO_16PT_H
