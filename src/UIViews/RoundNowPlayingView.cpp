/*-------------------------------------------------------------------------------------------------
** RoundNowPlayingView.cpp — circle-native now-playing view. See header for the layout.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "RoundNowPlayingView.h"

#include <time.h>

#include "SCLogger.h"
#include "SpotifyArtMgr.h"
#include "logTags.h"

namespace {

// TFTColor is an enum class over RGB565, so values outside the named set are
// still representable. These two are part of this view's look and nothing
// else's, which is why they live here and not in the shared enum.
constexpr TFTColor SPOTIFY_GREEN = static_cast<TFTColor>(0x1DCA);  // 29,185,84
constexpr TFTColor RING_TRACK    = static_cast<TFTColor>(0x2104);  // near-black grey

// Rows chosen so no two dynamic fields share a text row: cDrawString with an
// empty clear mask wipes the full width of its row before drawing.
constexpr int32_t CLOCK_Y  = 26;   // clock text row, centered
constexpr int32_t STATE_Y  = 58;   // play/pause glyph + network status row
constexpr int32_t TITLE_Y  = 392;  // track title row
constexpr int32_t ARTIST_Y = 430;  // artists row

}  // namespace

RoundNowPlayingView::RoundNowPlayingView(DisplayUI *pUI) : UIView(pUI) {}

void RoundNowPlayingView::initializeUIElements()
{
    // No rectangular buttons: the knob is the primary control and touch is
    // handled as coarse zones in onTouchDown(). The base defaults are still
    // created because the base class dereferences them unconditionally.
    UIView::initializeUIElements();
}

void RoundNowPlayingView::enteringView()
{
    _shownTrackUri  = "";
    _shownClock     = "";
    _waitingShowing = false;
    _volumeShowing  = false;
    _pUI->setBackground(TFTColor::Black, true);
    _pUI->markUIDirty(true);
}

/*
** ===================================================================
** Painters. Each owns one region and diffs against what it last drew,
** so the 500ms idle cadence repaints only what changed.
** ===================================================================
*/

float RoundNowPlayingView::progressFraction() const
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();

    if (playing.durationMs <= 0)
    {
        return 0.0f;
    }

    long progress = playing.progressMs;
    if (playing.isPlaying)
    {
        progress += (long)(millis() - playing.lastRefreshMs);
    }

    if (progress < 0)                   progress = 0;
    if (progress > playing.durationMs)  progress = playing.durationMs;

    return (float)progress / (float)playing.durationMs;
}

void RoundNowPlayingView::paintRingTrack()
{
    _pUI->fillArc(CX, CY, RING_OUTER, RING_INNER, 0, 360, RING_TRACK);
    _shownProgressDeg = 0.0f;
}

void RoundNowPlayingView::paintProgress(bool force)
{
    if (_volumeShowing)
    {
        return;   // the ring belongs to the volume overlay right now
    }

    const float deg = 360.0f * progressFraction();

    if (force || deg < _shownProgressDeg - 0.5f)
    {
        // Track change, seek backwards, or a forced repaint: lay the dark
        // track down again and draw the sweep from the top.
        paintRingTrack();
    }

    if (deg - _shownProgressDeg >= 0.7f)
    {
        // Only the newly covered sweep is drawn, so the ring never flickers.
        _pUI->fillArc(CX, CY, RING_OUTER, RING_INNER,
                      270.0f + _shownProgressDeg, 270.0f + deg, SPOTIFY_GREEN);
        _shownProgressDeg = deg;
    }
}

void RoundNowPlayingView::paintArt()
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();

    String filePath = SP_NO_COVER_JPG_FILENAME;
    for (int i = 0; i < playing.numImages; i++)
    {
        if (playing.albumImages[i].width  == ART_SIZE &&
            playing.albumImages[i].height == ART_SIZE)
        {
            filePath = SpotifyArtMgr::getInstance()->getLocalFileName(
                playing.albumImages[i].url);
            break;
        }
    }

    _pUI->setJpgScaleToSmall(false);    // 1:1 — the 300px art draws at 300px
    _pUI->drawAlbumArt(ART_X, ART_Y, filePath);
}

void RoundNowPlayingView::paintTitleBand()
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();
    PlayingMetadata &mutablePlaying = const_cast<PlayingMetadata &>(playing);

    // The chord narrows fast this low on the circle: ~308px at the title row,
    // ~224px at the artist row. Truncation keeps text off the bezel.
    String title = playing.trackName;
    if (title.length() > 22)
    {
        title = title.substring(0, 21) + "..";
    }

    _pUI->cDrawString(title.c_str(), CX, TITLE_Y, 26,
                      TFTColor::White, TFTColor::Black, "");
    _pUI->cDrawString(mutablePlaying.getArtistsList(24).c_str(), CX, ARTIST_Y, 17,
                      TFTColor::LightGrey, TFTColor::Black, "");
}

void RoundNowPlayingView::paintClock(bool force)
{
    // The clock row doubles as the volume readout while the knob is turning.
    if (_volumeShowing)
    {
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0))
    {
        return;
    }

    char buf[16];
    strftime(buf, sizeof(buf), "%l:%M %p", &timeinfo);
    const char *text = (buf[0] == ' ') ? buf + 1 : buf;

    if (force || _shownClock != text)
    {
        _shownClock = text;
        _pUI->cDrawString(text, CX, CLOCK_Y, 20,
                          TFTColor::DarkGrey, TFTColor::Black, "");
    }
}

void RoundNowPlayingView::paintPlayState(bool force)
{
    const bool isPlaying =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata().isPlaying;

    if (!force && isPlaying == _shownIsPlaying)
    {
        return;
    }
    _shownIsPlaying = isPlaying;

    // Clear the glyph's own cell, then draw. Sits left of center on the
    // STATE_Y row; the network status box owns the right of the same row.
    _pUI->drawBlankButton(164, STATE_Y, 22, 22, 0, TFTColor::Black, false);
    if (isPlaying)
    {
        _pUI->drawPlayTrackIcon(164, STATE_Y, 20, 20);
    }
    else
    {
        _pUI->drawPauseTrackIcon(164, STATE_Y, 20, 20);
    }
}

void RoundNowPlayingView::paintVolumeOverlay()
{
    // The perimeter ring becomes the volume gauge: white sweep from the top,
    // and the clock row shows the number. Progress repaints on expiry.
    paintRingTrack();
    const float deg = 3.6f * (float)_volumePercent;
    if (deg >= 0.7f)
    {
        _pUI->fillArc(CX, CY, RING_OUTER, RING_INNER, 270.0f, 270.0f + deg,
                      TFTColor::White);
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "Vol %d%%", _volumePercent);
    _pUI->cDrawString(buf, CX, CLOCK_Y, 20,
                      TFTColor::White, TFTColor::Black, "");
}

void RoundNowPlayingView::clearVolumeOverlay()
{
    _volumeShowing = false;
    _shownClock    = "";       // force the clock back
    paintRingTrack();
    paintProgress(true);
    paintClock(true);
}

void RoundNowPlayingView::paintWaiting()
{
    if (_waitingShowing)
    {
        return;
    }
    _waitingShowing = true;
    _shownTrackUri  = "";

    _pUI->setBackground(TFTColor::Black, true);
    paintRingTrack();
    _pUI->cDrawString("Waiting for music", CX, 210, 24,
                      TFTColor::White, TFTColor::Black, "");
    _pUI->cDrawString("play something on Spotify", CX, 250, 17,
                      TFTColor::DarkGrey, TFTColor::Black, "");
}

void RoundNowPlayingView::fullRepaint()
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();

    _pUI->setBackground(TFTColor::Black, true);
    paintRingTrack();
    paintArt();
    paintTitleBand();
    paintClock(true);
    paintPlayState(true);
    paintProgress(true);

    _shownTrackUri = playing.trackUri;
    _pUI->markUIDirty(false);
}

/*
** ===================================================================
** View plumbing
** ===================================================================
*/

void RoundNowPlayingView::drawUI()
{
    SpotifyPlayer *pSP = &SpotifyPlayer::getInstance();

    if (!pSP->isMusicAvailable())
    {
        paintWaiting();
        return;
    }

    const PlayingMetadata &playing = pSP->getCurrentlyPlayingMetadata();

    // Track identity is diffed directly rather than via isNewTrackReady(),
    // which latches true and would force a JPEG decode every cadence tick.
    const bool newTrack = (_shownTrackUri != playing.trackUri);

    if (_waitingShowing || newTrack || _pUI->isUIDirty())
    {
        _waitingShowing = false;
        fullRepaint();
        return;
    }

    paintProgress(false);
    paintClock(false);
    paintPlayState(false);
}

void RoundNowPlayingView::handle_UM_IDLE(SCUIMessage *pMessage)
{
    // Volume overlay expiry is checked at the idle cadence (every ~16ms), not
    // the draw cadence, so the ring returns promptly after the knob settles.
    if (_volumeShowing && (millis() - _volumeShownAtMs) >= VOLUME_OVERLAY_MS)
    {
        clearVolumeOverlay();
    }

    if (millis() > _nextDrawMs)
    {
        drawUI();
        _nextDrawMs = millis() + DRAW_PERIOD_MS;
    }
}

void RoundNowPlayingView::handleMessage(SCUIMessage *pMessage)
{
    if (pMessage != nullptr && pMessage->type == SCUIMessageType::UM_VOLUME)
    {
        _volumePercent  = pMessage->num;
        _volumeShowing  = true;
        _volumeShownAtMs = millis();
        paintVolumeOverlay();
        return;
    }

    UIView::handleMessage(pMessage);
}

void RoundNowPlayingView::onTouchDown(const TS_Point &point)
{
    SpotifyPlayer &sp = SpotifyPlayer::getInstance();

    // Coarse thirds — this is a knob device, touch is the backup control.
    if (point.x < 160)
    {
        spLogI(LOGTAG_INPUT, "TRANSPORT skip prev (touch)");
        sp.previousSong();
    }
    else if (point.x > 320)
    {
        spLogI(LOGTAG_INPUT, "TRANSPORT skip next (touch)");
        sp.nextSong();
    }
    else
    {
        sp.togglePlayPause();
        paintPlayState(true);
    }
}
