/*-------------------------------------------------------------------------------------------------
** DevicePickerView.cpp — playback-device picker. See the header for the controls
** and the painting rules.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "DevicePickerView.h"

#include <stdio.h>
#include <string.h>

#include "SCLogger.h"
#include "UIViewManager.h"
#include "logTags.h"

namespace {

constexpr TFTColor SPOTIFY_GREEN = static_cast<TFTColor>(0x1DCA);  // 29,185,84
constexpr TFTColor PILL_TEXT_DIM = static_cast<TFTColor>(0x18E3);  // dark text on the pill

constexpr int32_t CX = 240;

// Header and footer, inside the circle.
constexpr int32_t TITLE_Y    = 62;
constexpr int32_t POSITION_Y = 94;
constexpr int32_t HINT_Y     = 388;

// Body: everything between header and footer is cleared as one rect.
constexpr int32_t BODY_X = 40;
constexpr int32_t BODY_Y = 90;
constexpr int32_t BODY_W = 400;
constexpr int32_t BODY_H = 330;

// Rows: VISIBLE_ROWS of ROW_H, centered on the panel.
constexpr int32_t ROW_H     = 50;
constexpr int32_t ROWS_TOP  = 240 - (5 * ROW_H) / 2;
constexpr int32_t PILL_X    = 80;
constexpr int32_t PILL_W    = 320;
constexpr int32_t PILL_H    = 46;
constexpr int32_t NAME_CHARS = 20;

// Two-line message, centered.
constexpr int32_t MSG_Y1 = 206;
constexpr int32_t MSG_Y2 = 246;

}  // namespace

DevicePickerView::DevicePickerView(DisplayUI *pUI) : UIView(pUI) {}

void DevicePickerView::initializeUIElements()
{
    // No rectangular buttons (the base class still expects its defaults).
    UIView::initializeUIElements();
}

void DevicePickerView::enteringView()
{
    spLogI(LOGTAG_INPUT, "DEVICES picker open");
    _networkDue  = false;
    _lastPaintMs = 0;
    _picker.open(millis());

    _pUI->setBackground(TFTColor::Black, true);
    _pUI->drawStringNoClear("Play on", CX, TITLE_Y, 22, TFTColor::LightGrey, TFTColor::Black);
    // Paint "Finding devices..." now; the fetch runs on the next idle tick.
    paint();
    _picker.consumeDirty();
    _networkDue = true;
}

/*
** ===================================================================
** Input — state only, never painting
** ===================================================================
*/

bool DevicePickerView::onKnob(const KnobEvent &event)
{
    const uint32_t now = millis();

    switch (event.type)
    {
        case KnobEventType::Rotate:
            _picker.rotate(event.delta, now);
            break;

        case KnobEventType::Press:
            _picker.press(now);
            break;

        case KnobEventType::LongPress:
            spLogI(LOGTAG_INPUT, "DEVICES picker cancelled (hold)");
            _picker.cancel();
            break;

        default:
            // VeryLongPress is the tail of the hold that opened (or closed)
            // this view. Swallow it.
            break;
    }
    return true;  // the picker owns the knob while it is open
}

void DevicePickerView::onTouchDown(const TS_Point & /*point*/)
{
    spLogI(LOGTAG_INPUT, "DEVICES picker cancelled (tap)");
    _picker.cancel();
}

void DevicePickerView::onTouchUp()
{
    // No-op: the base paints button chrome over the whole screen.
}

/*
** ===================================================================
** Idle tick — painting, then the network step it announced
** ===================================================================
*/

void DevicePickerView::handle_UM_IDLE(SCUIMessage * /*pMessage*/)
{
    const uint32_t now = millis();

    if (_picker.tick(now))
    {
        close();
        return;
    }

    if (_networkDue)
    {
        _networkDue = false;
        if (_picker.state() == DevicePicker::State::Loading)
        {
            loadDevices();
        }
        else if (_picker.state() == DevicePicker::State::Switching)
        {
            transfer();
        }
        return;
    }

    if ((now - _lastPaintMs) >= PAINT_MS && _picker.consumeDirty())
    {
        paint();
        _lastPaintMs = now;

        const DevicePicker::State s = _picker.state();
        _networkDue = (s == DevicePicker::State::Loading || s == DevicePicker::State::Switching);
    }
}

void DevicePickerView::drawUI()
{
    // Painting is driven by the idle tick; nothing to do on a forced draw.
}

// The background now-playing poll keeps posting while the picker is open.
// None of it belongs on this screen; RoundNowPlayingView repaints in full
// when the picker closes.
void DevicePickerView::handle_UM_STATUS_BOX(SCUIMessage * /*pMessage*/) {}
void DevicePickerView::handle_UM_DOWNLOAD_BOX(SCUIMessage * /*pMessage*/) {}
void DevicePickerView::handle_UM_MARK_DIRTY(SCUIMessage * /*pMessage*/) {}
void DevicePickerView::handle_UM_PLAYER_REFRESH(SCUIMessage * /*pMessage*/) {}

/*
** ===================================================================
** Network steps
** ===================================================================
*/

void DevicePickerView::loadDevices()
{
    const int status = _spotifyPlayer.fetchDevices(_picker);
    _picker.finishLoad(status == 200, millis());
}

void DevicePickerView::transfer()
{
    char targetId[sizeof(PickerDevice::id)];
    strlcpy(targetId, _picker.targetId(), sizeof(targetId));

    bool ok = _spotifyPlayer.transferPlaybackTo(targetId);
    bool stillListed = true;

    if (ok)
    {
        // Volume lives on the device, so the shadow belongs to the old one.
        _spotifyPlayer.refreshVolumeFromDevice();
    }
    else
    {
        // Re-list: a device that is gone explains the failure (the API's
        // 404), and the list the owner returns to is then current.
        const int status = _spotifyPlayer.fetchDevices(_picker);
        stillListed = (status != 200) || _picker.contains(targetId);
    }

    _picker.finishTransfer(ok, stillListed, millis());
}

void DevicePickerView::close()
{
    _networkDue = false;
    _picker.cancel();
    if (!UIViewManager::getInstance().exitView())
    {
        UIViewManager::getInstance().setViewID(UIViewManager::ViewID::Home);
    }
}

/*
** ===================================================================
** Painters
** ===================================================================
*/

void DevicePickerView::clearBody()
{
    _pUI->drawBlankButton(BODY_X, BODY_Y, BODY_W, BODY_H, 0, TFTColor::Black, false);
}

void DevicePickerView::paintMessage(const char *line1, const char *line2)
{
    clearBody();
    _pUI->drawStringNoClear(line1, CX, MSG_Y1, 24, TFTColor::White, TFTColor::Black);
    if (line2 != nullptr && line2[0] != '\0')
    {
        _pUI->drawStringNoClear(line2, CX, MSG_Y2, 17, TFTColor::DarkGrey, TFTColor::Black);
    }
}

void DevicePickerView::paintList()
{
    clearBody();

    const size_t count = _picker.count();
    const size_t first = _picker.firstVisible(VISIBLE_ROWS);

    if (count > VISIBLE_ROWS)
    {
        char pos[16];
        snprintf(pos, sizeof(pos), "%u of %u", (unsigned)(_picker.selectedIndex() + 1),
                 (unsigned)count);
        _pUI->drawStringNoClear(pos, CX, POSITION_Y, 14, TFTColor::DarkGrey, TFTColor::Black);
    }

    for (size_t row = 0; row < VISIBLE_ROWS && first + row < count; row++)
    {
        const size_t        i    = first + row;
        const PickerDevice *d    = _picker.device(i);
        const bool          sel  = (i == _picker.selectedIndex());
        const int32_t       y    = ROWS_TOP + (int32_t)row * ROW_H;
        const TFTColor      bg   = sel ? SPOTIFY_GREEN : TFTColor::Black;

        if (sel)
        {
            _pUI->fillRoundRectAt(PILL_X, y + 2, PILL_W, PILL_H, PILL_H / 2, SPOTIFY_GREEN);
        }

        char name[96];
        DevicePicker::truncateUtf8(d->name, NAME_CHARS, name, sizeof(name));
        const TFTColor nameColor = sel ? TFTColor::Black
                                 : (d->isRestricted ? TFTColor::DarkGrey : TFTColor::White);
        _pUI->drawStringNoClear(name, CX, y + 6, 21, nameColor, bg);

        char detail[48];
        snprintf(detail, sizeof(detail), "%s%s", DevicePicker::typeLabel(d->type),
                 d->isRestricted ? " - no remote control"
                                 : (d->isActive ? " - playing now" : ""));
        const TFTColor detailColor = sel ? PILL_TEXT_DIM
                                   : (d->isActive ? SPOTIFY_GREEN : TFTColor::DarkGrey);
        _pUI->drawStringNoClear(detail, CX, y + 30, 14, detailColor, bg);

        if (d->isActive)
        {
            // The "now playing here" dot, left of the name.
            _pUI->fillCircleAt(PILL_X + 20, y + 2 + PILL_H / 2, 5,
                               sel ? TFTColor::Black : SPOTIFY_GREEN);
        }
    }

    _pUI->drawStringNoClear("press: play here   hold: back", CX, HINT_Y, 15,
                            TFTColor::DarkGrey, TFTColor::Black);
}

void DevicePickerView::paint()
{
    char name[96];
    DevicePicker::truncateUtf8(_picker.targetName(), NAME_CHARS, name, sizeof(name));

    switch (_picker.state())
    {
        case DevicePicker::State::Loading:
            paintMessage("Finding devices...", "");
            break;
        case DevicePicker::State::List:
            paintList();
            break;
        case DevicePicker::State::Empty:
            paintMessage("No devices found", "Open Spotify on a phone or speaker");
            break;
        case DevicePicker::State::LoadError:
            paintMessage("Couldn't load devices", "Check Wi-Fi, then try again");
            break;
        case DevicePicker::State::Refused:
            paintMessage(name, "can't be controlled from here");
            break;
        case DevicePicker::State::Switching:
            paintMessage("Switching to", name);
            break;
        case DevicePicker::State::Switched:
            paintMessage("Playing on", name);
            break;
        case DevicePicker::State::Gone:
            paintMessage(name, "is no longer available");
            break;
        case DevicePicker::State::Failed:
            paintMessage("Couldn't switch to", name);
            break;
        case DevicePicker::State::Closed:
        default:
            break;
    }
}
