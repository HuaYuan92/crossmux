#include "util/ReadingReminder.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <SoundFeedback.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"

#if CROSSPOINT_CAP_SOUND_FEEDBACK
namespace {
// Maps the persisted Enum option index (SettingsList label order) to minutes;
// index 0 disables the reminder.
constexpr uint16_t kReminderOptionMinutes[6] = {0, 5, 15, 30, 45, 60};
constexpr uint64_t kNever = UINT64_MAX;

uint64_t nextDueMs = kNever;
uint16_t pendingMinutes = 0;
}  // namespace

void ReadingReminder::onTick() {
  const uint8_t option = SETTINGS.readingReminderMinutes;
  const uint16_t minutes = option < 6 ? kReminderOptionMinutes[option] : 0;
  if (minutes == 0) {
    nextDueMs = kNever;
    pendingMinutes = 0;
    return;
  }
  const uint64_t stepMs = static_cast<uint64_t>(minutes) * 60u * 1000u;
  const uint64_t activeMs = READING_STATS.getActiveSessionAccumulatedMs();
  if (activeMs == 0) {
    // Fresh session (no credited time yet): arm the first threshold.
    nextDueMs = stepMs;
    return;
  }
  if (activeMs < nextDueMs) return;

  SoundFeedback::playAlert(SETTINGS.soundFeedbackLevel);
  pendingMinutes = minutes;
  if (SETTINGS.readingReminderRepeat) {
    while (nextDueMs <= activeMs) nextDueMs += stepMs;
  } else {
    nextDueMs = kNever;
  }
}

void ReadingReminder::drawPendingPopup(GfxRenderer& renderer) {
  if (pendingMinutes == 0) return;
  const int minutes = pendingMinutes;
  pendingMinutes = 0;
  char banner[96];
  if (snprintf(banner, sizeof(banner), tr(STR_READING_REMINDER_BANNER), minutes) >=
      static_cast<int>(sizeof(banner))) {
    return;  // Translation outgrew the buffer; skip rather than show a truncated line.
  }
  GUI.drawPopup(renderer, banner);
}
#else
void ReadingReminder::onTick() {}
void ReadingReminder::drawPendingPopup(GfxRenderer&) {}
#endif
