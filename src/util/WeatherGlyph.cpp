#include "util/WeatherGlyph.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <GfxRenderer.h>

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f;

// A filled disc, since GfxRenderer has no circle primitive. Cheap and adequate
// for the small weather glyphs; only ever called from the render path.
void fillDisc(GfxRenderer& r, const int cx, const int cy, const int rad) {
  for (int dy = -rad; dy <= rad; ++dy) {
    const int dx = static_cast<int>(std::sqrt(static_cast<float>(rad * rad - dy * dy)));
    r.fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, true);
  }
}

void drawSun(GfxRenderer& r, const int cx, const int cy, const int rad) {
  fillDisc(r, cx, cy, rad);
  for (int a = 0; a < 8; ++a) {
    const float ang = a * kPi / 4.0f;
    const int ix = cx + static_cast<int>(std::lround((rad + 2) * std::cos(ang)));
    const int iy = cy + static_cast<int>(std::lround((rad + 2) * std::sin(ang)));
    const int ox = cx + static_cast<int>(std::lround((rad + 6) * std::cos(ang)));
    const int oy = cy + static_cast<int>(std::lround((rad + 6) * std::sin(ang)));
    r.drawLine(ix, iy, ox, oy, true);
  }
}

// A filled cloud anchored so its base sits at (cx, cy + s).
void drawCloud(GfxRenderer& r, const int cx, const int cy, const int s) {
  fillDisc(r, cx - s, cy, s);
  fillDisc(r, cx + s, cy, s);
  fillDisc(r, cx, cy - s / 2 - 1, s);
  r.fillRect(cx - 2 * s, cy, 4 * s, s + 1, true);
}

void drawRain(GfxRenderer& r, const int cx, const int cy, const int s) {
  drawCloud(r, cx, cy, s);
  for (int i = -1; i <= 1; ++i) {
    const int x = cx + i * (s + 2);
    r.drawLine(x, cy + s + 3, x - 2, cy + s + 9, true);
  }
}

void drawSnowGlyph(GfxRenderer& r, const int cx, const int cy, const int s) {
  drawCloud(r, cx, cy, s);
  for (int i = -1; i <= 1; ++i) {
    fillDisc(r, cx + i * (s + 2), cy + s + 6, 1);
  }
}

void drawThunder(GfxRenderer& r, const int cx, const int cy, const int s) {
  drawCloud(r, cx, cy, s);
  const int bx = cx;
  const int by = cy + s + 2;
  r.drawLine(bx, by, bx - 3, by + 5, true);
  r.drawLine(bx - 3, by + 5, bx + 2, by + 5, true);
  r.drawLine(bx + 2, by + 5, bx - 2, by + 11, true);
}

void drawFog(GfxRenderer& r, const int cx, const int cy, const int s) {
  for (int i = 0; i < 4; ++i) {
    const int y = cy - s + i * 5;
    r.drawLine(cx - s - 4, y, cx + s + 4, y, true);
  }
}

}  // namespace

namespace WeatherGlyph {

// Draw a compact monochrome glyph for a WMO code, centered at (cx, cy).
void draw(GfxRenderer& r, const int code, const int cx, const int cy, const int s) {
  switch (code) {
    case 0:
      drawSun(r, cx, cy, s);
      break;
    case 1:
    case 2:
      drawSun(r, cx - s, cy - s, s - 1);
      drawCloud(r, cx + 2, cy + s / 2, s);
      break;
    case 3:
      drawCloud(r, cx, cy, s);
      break;
    case 45:
    case 48:
      drawFog(r, cx, cy, s);
      break;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
      drawRain(r, cx, cy, s);
      break;
    case 65:
      drawRain(r, cx, cy, s);
      break;
    case 71:
    case 73:
    case 77:
    case 75:
    case 85:
    case 86:
      drawSnowGlyph(r, cx, cy, s);
      break;
    case 95:
    case 96:
    case 99:
      drawThunder(r, cx, cy, s);
      break;
    default:
      r.drawRect(cx - s, cy - s, 2 * s, 2 * s, true);
      break;
  }
}

}  // namespace WeatherGlyph

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
