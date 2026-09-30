#pragma once

class GfxRenderer;

// Reading-time reminder: chime + banner once the active reading session passes
// the configured threshold (Settings > Reader, audio-capable boards only).
// Both entry points are intentional no-ops when CROSSPOINT_CAP_SOUND_FEEDBACK
// is off, so reader activities can call them unconditionally.
namespace ReadingReminder {
// Advance threshold state; call next to ReadingStatsStore::tickActiveSession().
void onTick();
// Draws the one-shot reminder popup if a threshold just fired; clears the flag.
void drawPendingPopup(GfxRenderer& renderer);
}  // namespace ReadingReminder
