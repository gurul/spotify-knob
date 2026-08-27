/*-------------------------------------------------------------------------------------------------
**
** RoundNowPlayingView.h
**
**    Circle-native now-playing view for the CrowPanel 2.1" rotary display.
**    Replaces HomeView as the default view: HomeView's layout is the ThingPulse
**    480x320 landscape design, which a round 480x480 bezel clips at every
**    corner and leaves the bottom third of the circle empty.
**
**    The design is deliberately sparse — the owner's calls, in order: no
**    volume on the perimeter, then no progress/time ring at all. What remains:
**
**        album art       300x300 dead-center (half-diagonal 212 < 240, fully
**                        inside the circle)
**        title/artists   ON the art, over a true 50% scrim of its bottom rows
**        clock           small and dim at the top
**        volume          only while the knob turns: bar in the black zone
**                        under the art, self-dismissing
**        paused          the whole art dims and pause bars appear; while
**                        playing there is no transport chrome at all
**
**    The network status box and download indicator are suppressed on this
**    view — dev chrome, not owner UI.
**
**    Painting discipline (v1 was laggy; these rules are why this one is not):
**      1. Every painter diffs against what is on screen.
**      2. Input events never paint — they set state; painting happens on the
**         idle tick, so a fast knob twist coalesces instead of queueing.
**      3. No full-width text clears. cDrawString's row clear is what stamped
**         black gaps across earlier layouts; on-art and near-art text uses
**         drawStringNoClear over its own narrow cleared region.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include "UIView.h"

class RoundNowPlayingView : public UIView
{
public:
    explicit RoundNowPlayingView(DisplayUI *pUI);

    void drawUI() override;
    void handleMessage(SCUIMessage *pMessage) override;
    void onTouchDown(const TS_Point &point) override;
    void onTouchUp() override;

protected:
    void initializeUIElements() override;
    void enteringView() override;
    void handle_UM_IDLE(SCUIMessage *pMessage) override;
    void handle_UM_STATUS_BOX(SCUIMessage *pMessage) override;
    void handle_UM_DOWNLOAD_BOX(SCUIMessage *pMessage) override;

private:
    // Geometry
    static constexpr int16_t CX       = 240;
    static constexpr int16_t CY       = 240;
    static constexpr int16_t ART_X    = 90;
    static constexpr int16_t ART_Y    = 90;
    static constexpr int16_t ART_SIZE = 300;

    // Cadence
    static constexpr uint32_t DRAW_PERIOD_MS  = 250;
    static constexpr uint32_t VOLUME_HOLD_MS  = 1200;
    static constexpr uint32_t VOLUME_PAINT_MS = 50;

    void fullRepaint();
    void paintArtWithText();
    void paintClock(bool force);
    void paintPlayState(bool force);
    void paintVolume();
    void dismissVolume();
    void paintWaiting();

    // On-screen state the painters diff against
    String   _shownTrackUri;
    bool     _shownIsPlaying    = true;
    String   _shownClock;
    bool     _waitingShowing    = false;

    // Volume readout
    bool     _volumeShowing     = false;
    int      _volumePercent     = 0;
    int      _shownVolume       = -1;
    bool     _volumeDirty       = false;
    uint32_t _volumeTouchedMs   = 0;
    uint32_t _volumePaintedMs   = 0;

    uint32_t _nextDrawMs        = 0;
};
