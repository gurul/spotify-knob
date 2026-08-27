/*-------------------------------------------------------------------------------------------------
**
** RoundNowPlayingView.h
**
**    Circle-native now-playing view for the CrowPanel 2.1" rotary display.
**    Replaces HomeView as the default view: HomeView's layout is the ThingPulse
**    480x320 landscape design, which a round 480x480 bezel clips at every
**    corner and leaves the bottom third of the circle empty.
**
**    Layout (center 240,240, visible radius 240):
**
**        perimeter ring  r 226..238   track progress; doubles as the volume
**                                     indicator while the knob is turning
**        album art       300x300 at (90,90) — its half-diagonal is 212, so
**                                     the whole square sits inside the circle
**        top band        clock, play/pause glyph, network status box
**        bottom band     track title, then artists, centered where the chord
**                                     is still wide enough to carry text
**
**    Touch: left third = previous, right third = next, middle = play/pause.
**    The knob is the primary control; touch zones are deliberately coarse.
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

protected:
    void initializeUIElements() override;
    void enteringView() override;
    void handle_UM_IDLE(SCUIMessage *pMessage) override;

private:
    // Geometry
    static constexpr int16_t CX          = 240;
    static constexpr int16_t CY          = 240;
    static constexpr int16_t RING_OUTER  = 238;
    static constexpr int16_t RING_INNER  = 226;
    static constexpr int16_t ART_X       = 90;
    static constexpr int16_t ART_Y       = 90;
    static constexpr int16_t ART_SIZE    = 300;

    // Cadence
    static constexpr uint32_t DRAW_PERIOD_MS      = 500;
    static constexpr uint32_t VOLUME_OVERLAY_MS   = 1200;

    void fullRepaint();
    void paintArt();
    void paintTitleBand();
    void paintClock(bool force);
    void paintPlayState(bool force);
    void paintRingTrack();
    void paintProgress(bool force);
    void paintVolumeOverlay();
    void clearVolumeOverlay();
    void paintWaiting();

    float progressFraction() const;

    // State the incremental painters diff against
    String   _shownTrackUri;
    bool     _shownIsPlaying    = true;
    float    _shownProgressDeg  = 0.0f;   // sweep already painted, degrees
    String   _shownClock;
    bool     _waitingShowing    = false;

    // Volume overlay
    bool     _volumeShowing     = false;
    int      _volumePercent     = 0;
    uint32_t _volumeShownAtMs   = 0;

    uint32_t _nextDrawMs        = 0;
};
