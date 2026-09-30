#include "util/ImageThumbLoader.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <vector>

namespace ImageThumbLoader {
namespace {

constexpr const char* kThumbDir = "/.crosspoint/thumbs";
// Longest cached edge. Doubles as the on-screen display budget: the grid cell
// icon slot is 72 px and list rows cap icons at 24 px, both sampled with
// Contain from this buffer.
constexpr int kMaxDim = 96;
// Bound on concurrently held previews. A page of grid cells is 12 entries;
// refusing beyond the cap only degrades to the generic icon, and it keeps the
// map (and every ThumbBits pointer handed out) stable for the whole frame.
constexpr size_t kCacheCap = 32;

std::string cachePathFor(const std::string& fullPath) {
  // FNV-1a over the lowercased path: SD lookups are case-insensitive on most
  // cameras but case-preserving, and the preview must survive a pure rename
  // of letter case.
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : fullPath) {
    hash = (hash ^ static_cast<uint64_t>(std::tolower(c))) * 1099511628211ull;
  }
  char hex[17];
  snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
  return std::string(kThumbDir) + "/" + hex + ".bmp";
}

// -1 absent, 0 present-but-empty (the terminal marker for a failed
// generation), 1 present with content.
int inspectFile(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("THUMB", path.c_str(), file)) return -1;
  return file.fileSize() > 0 ? 1 : 0;
}

// Decode any Bitmap-supported BMP to BW1 bits (set-bit = ink), nearest-
// neighbour downscaled to kMaxDim and flipped to top-down as needed. Rows
// stream through a two-row scratch window, so a large source BMP costs only
// its own row bytes in RAM.
bool decodeToBits(const std::string& path, ThumbBits& out) {
  HalFile file;
  if (!Storage.openFileForRead("THUMB", path.c_str(), file)) return false;
  if (file.fileSize() == 0) return false;

  Bitmap bmp(file);
  if (bmp.parseHeaders() != BmpReaderError::Ok) return false;
  const int width = bmp.getWidth();
  const int height = bmp.getHeight();
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;

  const int outWidth = std::min(width, kMaxDim);
  const int outHeight = std::min(height, kMaxDim);
  const int stride = (outWidth + 7) / 8;
  out.bits.assign(static_cast<size_t>(stride) * outHeight, 0);
  out.width = static_cast<uint16_t>(outWidth);
  out.height = static_cast<uint16_t>(outHeight);

  // readNextRow contract (same as GfxRenderer::drawBitmap): 2-bit-packed ink
  // row of ceil(width/4) bytes plus a raw row scratch of getRowBytes().
  const size_t packedSize = static_cast<size_t>((width + 3) / 4);
  std::vector<uint8_t> row(packedSize + static_cast<size_t>(bmp.getRowBytes()));
  uint8_t* const packed = row.data();
  uint8_t* const raw = packed + packedSize;

  const bool topDown = bmp.isTopDown();
  for (int sourceRow = 0; sourceRow < height; ++sourceRow) {
    if (bmp.readNextRow(packed, raw) != BmpReaderError::Ok) return false;
    const int displayRow = topDown ? sourceRow : height - 1 - sourceRow;
    const int outY = static_cast<int>(static_cast<int64_t>(displayRow) * outHeight / height);
    uint8_t* const bits = out.bits.data() + static_cast<size_t>(outY) * stride;
    for (int x = 0; x < outWidth; ++x) {
      const int srcX = static_cast<int>(static_cast<int64_t>(x) * width / outWidth);
      const uint8_t level = (packed[srcX >> 2] >> (6 - 2 * (srcX & 3))) & 3;
      if (level < 2) bits[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7));
    }
  }
  return true;
}

// Render the PNG/JPEG source into a 1-bit cached BMP. On failure an empty
// file is left behind as the terminal marker (same contract as the book
// cover cache), so later frames stop retrying.
bool generateCacheFile(const std::string& srcPath, const std::string& dstPath) {
  if (!Storage.exists(kThumbDir)) Storage.mkdir(kThumbDir);

  HalFile src;
  if (!Storage.openFileForRead("THUMB", srcPath.c_str(), src)) return false;
  HalFile dst;
  if (!Storage.openFileForWrite("THUMB", dstPath.c_str(), dst)) return false;

  bool ok = false;
  if (FsHelpers::hasPngExtension(srcPath)) {
    ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(src, dst, kMaxDim, kMaxDim);
  } else if (FsHelpers::hasJpgExtension(srcPath)) {
    ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(src, dst, kMaxDim, kMaxDim);
  }
  if (!ok) LOG_DBG("THUMB", "No preview for %s", srcPath.c_str());
  return ok;
}

// Previews currently on screen. std::map nodes are stable, so ThumbBits
// pointers handed to draw targets survive later insertions; the only removal
// paths are whole-cache clears issued before a frame refills (see header).
std::map<std::string, ThumbBits>& cacheMap() {
  static std::map<std::string, ThumbBits> instance;
  return instance;
}

}  // namespace

bool isPreviewable(const std::string& name) {
  return name.back() != '/' &&
         (FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name) || FsHelpers::hasBmpExtension(name));
}

const ThumbBits* get(const std::string& fullPath) {
  auto& cache = cacheMap();

  const auto hit = cache.find(fullPath);
  if (hit != cache.end()) return &hit->second;
  if (cache.size() >= kCacheCap) return nullptr;
  if (!isPreviewable(fullPath)) return nullptr;

  ThumbBits bits;
  bool ok = false;
  if (FsHelpers::hasBmpExtension(fullPath)) {
    // The source is already a BMP the Bitmap reader understands.
    ok = decodeToBits(fullPath, bits);
  } else {
    const std::string cached = cachePathFor(fullPath);
    switch (inspectFile(cached)) {
      case -1:
        if (!generateCacheFile(fullPath, cached)) return nullptr;
        [[fallthrough]];
      case 1: {
        ok = decodeToBits(cached, bits);
        if (!ok) {
          // Truncated or corrupt cache file: delete it and retry generation
          // once (mirrors the cover loader's self-heal).
          Storage.remove(cached.c_str());
          if (generateCacheFile(fullPath, cached)) ok = decodeToBits(cached, bits);
        }
        break;
      }
      case 0:
        return nullptr;  // terminal marker from an earlier failed decode
    }
  }
  if (!ok) return nullptr;

  const auto inserted = cache.emplace(fullPath, std::move(bits));
  return &inserted.first->second;
}

void clearVisibleCache() { cacheMap().clear(); }

void onPathChanged(const std::string& oldFullPath) {
  cacheMap().erase(oldFullPath);
  // Drop the stale cached BMP so a recycled file name re-generates instead of
  // showing the old content.
  const std::string cached = cachePathFor(oldFullPath);
  if (inspectFile(cached) >= 0) Storage.remove(cached.c_str());
}

}  // namespace ImageThumbLoader

#endif  // FREEINK_DEVICE_WAVESHARE_EPAPER_397
