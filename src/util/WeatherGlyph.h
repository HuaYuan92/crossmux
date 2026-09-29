#pragma once

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

class GfxRenderer;

// Compact monochrome weather icons drawn from GfxRenderer primitives. Shared by
// the standalone Weather app and the standby weather face so the two surfaces
// stay visually identical without duplicating the drawing code. Waveshare-only:
// the whole module compiles away on the shared X3/X4 image.
namespace WeatherGlyph {

// Draw the glyph for a WMO weather interpretation code, centered at (cx, cy).
// `s` is the characteristic radius/size; larger values scale the whole icon.
void draw(GfxRenderer& r, int code, int cx, int cy, int s);

}  // namespace WeatherGlyph

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
