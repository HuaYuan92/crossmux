#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalOtaSlot.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontCache.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "components/FontPreloadView.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/BootArt.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  if (mode_ == Mode::PostOta) {
    runPostOta();
    return;
  }

  renderSplash();
}

void BootActivity::renderSplash() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Full-screen ink-wash boot art. drawPixel() takes logical, orientation-aware
  // coordinates, so the upright BOOTART bitmap fills the portrait panel; it is
  // centered when a device's logical size differs from the art's native size.
  const int artX = (pageWidth - BOOTART_WIDTH) / 2;
  const int artY = (pageHeight - BOOTART_HEIGHT) / 2;
  constexpr int artRowBytes = (BOOTART_WIDTH + 7) / 8;
  for (int ay = 0; ay < BOOTART_HEIGHT; ++ay) {
    const int sy = artY + ay;
    if (sy < 0 || sy >= pageHeight) continue;
    const uint8_t* src = BootArt + ay * artRowBytes;
    for (int ax = 0; ax < BOOTART_WIDTH; ++ax) {
      const int sx = artX + ax;
      if (sx < 0 || sx >= pageWidth) continue;
      const bool black = (src[ax >> 3] >> (7 - (ax & 7))) & 0x1;
      renderer.drawPixel(sx, sy, black);
    }
  }

  // Centered welcome text on a white plate so it stays legible over the art.
  const char* welcome = tr(STR_BOOT_WELCOME_SUBTITLE);
  const int titleY = pageHeight / 2;
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, welcome, EpdFontFamily::BOLD);
  renderer.fillRect((pageWidth - titleW) / 2 - 8, titleY - 4, titleW + 16, titleH + 8, /*state=*/false);
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, welcome, true, EpdFontFamily::BOLD);

  // Firmware version at the bottom, also on a small white plate. Hidden on the
  // Waveshare 3.97 build (user preference); other devices keep it, so the shared
  // X3/X4 image is unaffected.
#if !FREEINK_DEVICE_WAVESHARE_EPAPER_397
  const int verY = pageHeight - 30;
  const int verH = renderer.getLineHeight(SMALL_FONT_ID);
  const int verW = renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION);
  renderer.fillRect((pageWidth - verW) / 2 - 6, verY - 2, verW + 12, verH + 4, /*state=*/false);
  renderer.drawCenteredText(SMALL_FONT_ID, verY, CROSSPOINT_VERSION);
#endif
#if FREEINK_DEVICE_EEGO_A4
  // A4: the panel is being powered on for the first time here, and a FAST
  // refresh on a freshly powered panel doesn't establish the frame (observed:
  // splash never appears). Force the first paint to a full waveform.
  renderer.requestNextFullRefresh();
#endif
  renderer.displayBuffer();
}

void BootActivity::runPostOta() {
  requestUpdateAndWait();
  if (!HalOtaSlot::confirmRunningImage()) {
    failure_.store(Failure::Confirm);
    stage_.store(Stage::Failed);
    requestUpdateAndWait();
    return;
  }
  LOG_INF("OTA", "Running image confirmed after startup display");

  if (!allowAutoPreload_ || SETTINGS.sdFontFlashPreload == 0 || SETTINGS.sdFontFamilyName[0] == '\0') return;

  const auto* family = sdFontSystem.registry().findFamily(SETTINGS.sdFontFamilyName);
  const auto* file = family ? family->findNearestSize(SETTINGS.fontPointSize) : nullptr;
  if (!file || SdCardFontCache::isValidFor(file->path.c_str())) return;

  {
    RenderLock lock(*this);
    sdFontSystem.releaseLoadedFont(renderer);
    completed_.store(0);
    total_.store(1);
    lastRequestedPercent_ = 0;
    preloadPointSize_ = file->pointSize;
    stage_.store(Stage::Copying);
  }
  requestUpdateAndWait();

  const auto result = SdCardFontCache::preload(
      file->path.c_str(),
      [](size_t completed, size_t total, void* context) {
        auto* self = static_cast<BootActivity*>(context);
        self->completed_.store(completed);
        self->total_.store(total);

        const size_t sourceSize = total / 2;
        const Stage nextStage = completed <= sourceSize ? Stage::Copying : Stage::Verifying;
        const bool phaseChanged = self->stage_.exchange(nextStage) != nextStage;
        const unsigned percent = total > 0 ? static_cast<unsigned>(completed * 100 / total) : 0;
        if (phaseChanged || percent == 100 || percent >= self->lastRequestedPercent_ + 10) {
          self->lastRequestedPercent_ = percent;
          self->requestUpdate(true);
        }
      },
      this);

  // The callback only queues refreshes. Keep the verified 100% state visible
  // until the panel has physically completed that frame before showing Ready.
  if (result == SdCardFontCache::Result::Ok) requestUpdateAndWait();

  const bool succeeded = result == SdCardFontCache::Result::Ok || result == SdCardFontCache::Result::AlreadyCached;
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer, succeeded);
    if (succeeded) {
      stage_.store(Stage::Ready);
    } else {
      switch (result) {
        case SdCardFontCache::Result::TooLarge:
          failure_.store(Failure::TooLarge);
          break;
        case SdCardFontCache::Result::Oom:
          failure_.store(Failure::Memory);
          break;
        case SdCardFontCache::Result::OpenFailed:
        case SdCardFontCache::Result::InvalidFont:
        case SdCardFontCache::Result::ReadFailed:
          failure_.store(Failure::SdRead);
          break;
        case SdCardFontCache::Result::EraseFailed:
        case SdCardFontCache::Result::WriteFailed:
          failure_.store(Failure::FlashWrite);
          break;
        case SdCardFontCache::Result::VerifyFailed:
          failure_.store(Failure::Verify);
          break;
        case SdCardFontCache::Result::NotSafe:
          failure_.store(Failure::Confirm);
          break;
        case SdCardFontCache::Result::Ok:
        case SdCardFontCache::Result::AlreadyCached:
          break;
      }
      stage_.store(Stage::Failed);
    }
  }

  LOG_INF("SDFCACHE", "Post-OTA preload for %s: %s", file->path.c_str(), SdCardFontCache::resultName(result));
  requestUpdateAndWait();
}

void BootActivity::render(RenderLock&&) {
  if (mode_ != Mode::PostOta) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const Stage stage = stage_.load();

  const int centerY = pageHeight / 2 - lineHeight;
  switch (stage) {
    case Stage::Confirming:
      renderer.clearScreen();
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOTING));
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_VALIDATING_FIRMWARE));
      break;
    case Stage::Copying:
    case Stage::Verifying:
      fontpreload::draw(renderer, SETTINGS.sdFontFamilyName, preloadPointSize_, completed_.load(), total_.load(),
                        fontpreload::State::Progress);
      break;
    case Stage::Ready:
      fontpreload::draw(renderer, SETTINGS.sdFontFamilyName, preloadPointSize_, completed_.load(), total_.load(),
                        fontpreload::State::Ready);
      break;
    case Stage::Failed: {
      renderer.clearScreen();
      StrId reason = StrId::STR_OTA_CONFIRM_FAILED;
      switch (failure_.load()) {
        case Failure::Confirm:
          reason = StrId::STR_OTA_CONFIRM_FAILED;
          break;
        case Failure::TooLarge:
          reason = StrId::STR_FONT_CACHE_TOO_LARGE;
          break;
        case Failure::Memory:
          reason = StrId::STR_MEMORY_ERROR;
          break;
        case Failure::SdRead:
          reason = StrId::STR_FONT_CACHE_SD_READ_FAILED;
          break;
        case Failure::FlashWrite:
          reason = StrId::STR_FONT_CACHE_FLASH_WRITE_FAILED;
          break;
        case Failure::Verify:
          reason = StrId::STR_FONT_CACHE_VERIFY_FAILED;
          break;
      }
      const StrId header = failure_.load() == Failure::Confirm ? StrId::STR_BOOTING : StrId::STR_FONT_PRELOADING;
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, I18N.get(header));
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, I18N.get(reason), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, tr(STR_FONT_CACHE_USING_SD));
      break;
    }
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
