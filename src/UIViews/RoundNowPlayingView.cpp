/*-------------------------------------------------------------------------------------------------
** RoundNowPlayingView.cpp — circle-native now-playing view. See header for the
** design and the painting discipline.
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
// still representable. This one is this view's look and nothing else's.
constexpr TFTColor SPOTIFY_GREEN = static_cast<TFTColor>(0x1DCA);  // 29,185,84

// On-art text rows (inside the scrimmed bottom of the 300px art).
constexpr int32_t TITLE_Y  = 306;
constexpr int32_t ARTIST_Y = 346;

// Scrim: bottom rows of the art that get dimmed behind the text.
constexpr int32_t SCRIM_Y = 296;
constexpr int32_t SCRIM_H = 94;   // art ends at y=390

// Clock row, above the art.
constexpr int32_t CLOCK_Y = 38;

// Volume readout: label + bar in the black zone between the art bottom (390)
// and the bezel. Never touches the art.
constexpr int32_t VLABEL_Y = 398;
constexpr int32_t VBAR_W   = 160;
constexpr int32_t VBAR_H   = 10;
constexpr int32_t VBAR_X   = 240 - VBAR_W / 2;
constexpr int32_t VBAR_Y   = 432;
// One rect that covers the whole readout, for dismissal.
constexpr int32_t VZONE_X = 140;
constexpr int32_t VZONE_Y = 394;
constexpr int32_t VZONE_W = 200;
constexpr int32_t VZONE_H = (VBAR_Y + VBAR_H + 6) - VZONE_Y;

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
    _shownVolume    = -1;
    _waitingShowing = false;
    _volumeShowing  = false;
    _pUI->setBackground(TFTColor::Black, true);
    _pUI->markUIDirty(true);
}

/*
** ===================================================================
** Painters
** ===================================================================
*/

void RoundNowPlayingView::paintArtWithText()
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();
    PlayingMetadata &mutablePlaying = const_cast<PlayingMetadata &>(playing);

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

    // Scrim, then text painted with NO clearing rect — a full-width clear
    // would cut a black band through the art (and did, in earlier layouts).
    _pUI->dimRect(ART_X, SCRIM_Y, ART_SIZE, SCRIM_H);

    String title = playing.trackName;
    if (title.length() > 20)
    {
        title = title.substring(0, 19) + "..";
    }

    _pUI->drawStringNoClear(title.c_str(), CX, TITLE_Y, 28,
                            TFTColor::White, TFTColor::Black);
    _pUI->drawStringNoClear(mutablePlaying.getArtistsList(26).c_str(), CX, ARTIST_Y,
                            18, TFTColor::LightGrey, TFTColor::Black);
}

void RoundNowPlayingView::paintClock(bool force)
{
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
        // Clear only the clock's own cell — the zone above the art is black,
        // but a narrow clear keeps the habit that nothing wipes full rows.
        _pUI->drawBlankButton(170, CLOCK_Y - 4, 140, 30, 0,
                              TFTColor::Black, false);
        _pUI->drawStringNoClear(text, CX, CLOCK_Y, 18,
                                TFTColor::DarkGrey, TFTColor::Black);
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

    const bool wasPlaying = _shownIsPlaying;
    _shownIsPlaying = isPlaying;

    if (!isPlaying)
    {
        // Pause: dim the whole art and put the bars in the middle. The scrim
        // region dims a second time, which reads as intended hierarchy.
        _pUI->dimRect(ART_X, ART_Y, ART_SIZE, ART_SIZE);
        _pUI->drawPauseTrackIcon(CX - 28, CY - 40, 56, 64);
    }
    else if (force || !wasPlaying)
    {
        // Resume: the art under the dim is gone; repaint it (one JPEG decode).
        paintArtWithText();
    }
}

/*
** ===================================================================
** Volume readout — the only transient chrome on the view
** ===================================================================
*/

void RoundNowPlayingView::paintVolume()
{
    char label[8];
    snprintf(label, sizeof(label), "%d%%", _volumePercent);

    // Clear just the label cell (the bar repaints fully; it is tiny).
    _pUI->drawBlankButton(VZONE_X, VZONE_Y, VZONE_W, 30, 0,
                          TFTColor::Black, false);
    _pUI->drawStringNoClear(label, CX, VLABEL_Y, 22,
                            TFTColor::White, TFTColor::Black);

    if (_volumePercent < _shownVolume)
    {
        // drawProgressBar never un-fills; clear the bar before a shrink.
        _pUI->drawBlankButton(VBAR_X, VBAR_Y, VBAR_W, VBAR_H, 0,
                              TFTColor::Black, false);
    }
    _pUI->drawProgressBar(VBAR_X, VBAR_Y, VBAR_W, VBAR_H,
                          (uint8_t)_volumePercent,
                          TFTColor::DarkGrey, SPOTIFY_GREEN);

    _shownVolume     = _volumePercent;
    _volumeDirty     = false;
    _volumePaintedMs = millis();
}

void RoundNowPlayingView::dismissVolume()
{
    _volumeShowing = false;
    _shownVolume   = -1;
    // The readout lives entirely in the black zone, so dismissal is one rect —
    // nothing else needs repainting.
    _pUI->drawBlankButton(VZONE_X, VZONE_Y, VZONE_W, VZONE_H, 0,
                          TFTColor::Black, false);
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
    _pUI->drawStringNoClear("Waiting for music", CX, 216, 24,
                            TFTColor::White, TFTColor::Black);
    _pUI->drawStringNoClear("play something on Spotify", CX, 254, 17,
                            TFTColor::DarkGrey, TFTColor::Black);
}

void RoundNowPlayingView::fullRepaint()
{
    const PlayingMetadata &playing =
        SpotifyPlayer::getInstance().getCurrentlyPlayingMetadata();

    _volumeShowing = false;
    _shownVolume   = -1;

    _pUI->setBackground(TFTColor::Black, true);
    paintArtWithText();
    paintClock(true);
    paintPlayState(true);

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

    paintClock(false);
    paintPlayState(false);
}

void RoundNowPlayingView::handle_UM_IDLE(SCUIMessage *pMessage)
{
    const uint32_t now = millis();

    // Volume: all painting happens here, coalesced. A fast twist updates
    // _volumePercent many times per tick but paints at most every 50ms.
    if (_volumeShowing)
    {
        if ((now - _volumeTouchedMs) >= VOLUME_HOLD_MS)
        {
            dismissVolume();
        }
        else if (_volumeDirty && (now - _volumePaintedMs) >= VOLUME_PAINT_MS)
        {
            paintVolume();
        }
    }

    if (now > _nextDrawMs)
    {
        drawUI();
        _nextDrawMs = now + DRAW_PERIOD_MS;
    }
}

void RoundNowPlayingView::handle_UM_STATUS_BOX(SCUIMessage *pMessage)
{
    // Suppressed: the network status box is dev chrome, not owner UI.
}

void RoundNowPlayingView::handle_UM_DOWNLOAD_BOX(SCUIMessage *pMessage)
{
    // Suppressed, as above.
}

void RoundNowPlayingView::handleMessage(SCUIMessage *pMessage)
{
    if (pMessage != nullptr && pMessage->type == SCUIMessageType::UM_VOLUME)
    {
        // State only — no painting on the input path. See header.
        _volumePercent   = pMessage->num;
        _volumeShowing   = true;
        _volumeDirty     = true;
        _volumeTouchedMs = millis();
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
        paintPlayState(false);
    }
}

void RoundNowPlayingView::onTouchUp()
{
    // Deliberately a no-op. The base implementation renders the rectangular
    // button set, and showTouchUp() paints edge bars across the full screen —
    // both would stamp over the art on this layout.
}
