#pragma once

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <cstdint>
#include <string>
#include <vector>

// File-browser image previews (Waveshare 3.97 only). PNG/JPEG sources are
// decoded once into a 1-bit BMP under /.crosspoint/thumbs/ (keyed by a hash of
// the full path); BMP sources decode straight to memory. get() hands back BW1
// bits (row-major, MSB-first, set-bit = ink) suitable for fui::BitmapRef with
// progmem=false. Loaded entries live in a small static map: callers MUST call
// clearVisibleCache() before filling a different visible window so pointers
// handed out earlier cannot dangle, and new loads are refused once the map is
// full (the row keeps its generic icon).
namespace ImageThumbLoader {

struct ThumbBits {
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint8_t> bits;
};

// Cheap extension triage: .png/.jpg/.jpeg/.bmp (case-insensitive).
bool isPreviewable(const std::string& name);

// Full SD path -> cached bits, or nullptr when nothing previewable is
// available (missing file, failed decode, cache full). Generates the cached
// BMP on first call; a failed generation leaves an empty marker so the row is
// not retried every render.
const ThumbBits* get(const std::string& fullPath);

// Drop every in-memory entry. Call when the visible window changes to a new
// page/directory, BEFORE re-querying get() for the new window.
void clearVisibleCache();

// Rename/move bookkeeping: forget the old key and delete its cached BMP so a
// recycled name re-generates instead of showing a stale preview.
void onPathChanged(const std::string& oldFullPath);

}  // namespace ImageThumbLoader

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
