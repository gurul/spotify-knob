/*-------------------------------------------------------------------------------------------------
**
** DevicePickerView.h
**
**    Chooses which Spotify device plays the music. Opened from any view by
**    holding the knob for 1.5 s. Lists GET /v1/me/player/devices:
**
**        turn          move the selection
**        press         play on the selected device (PUT /v1/me/player)
**        hold / tap    back out, nothing changed
**        10 s idle     back out, nothing changed
**
**    The state lives in DevicePicker (host-tested). This class draws it and
**    does the network work the state asks for. It follows the painting
**    rules of RoundNowPlayingView: input only changes state, and painting
**    happens on the idle tick. A network call runs on the tick AFTER the
**    screen that announces it is painted, so "Finding devices..." is on the
**    glass while the request blocks.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include "UIView.h"
#include "DevicePicker.h"

class DevicePickerView : public UIView
{
public:
    explicit DevicePickerView(DisplayUI *pUI);

    void drawUI() override;
    bool onKnob(const KnobEvent &event) override;
    void onTouchDown(const TS_Point &point) override;
    void onTouchUp() override;

protected:
    void initializeUIElements() override;
    void enteringView() override;
    void handle_UM_IDLE(SCUIMessage *pMessage) override;
    void handle_UM_STATUS_BOX(SCUIMessage *pMessage) override;
    void handle_UM_DOWNLOAD_BOX(SCUIMessage *pMessage) override;
    void handle_UM_MARK_DIRTY(SCUIMessage *pMessage) override;
    void handle_UM_PLAYER_REFRESH(SCUIMessage *pMessage) override;

private:
    static constexpr size_t   VISIBLE_ROWS = 5;
    static constexpr uint32_t PAINT_MS     = 50;   // coalesces a fast twist

    void paint();
    void paintList();
    void paintMessage(const char *line1, const char *line2);
    void clearBody();
    void loadDevices();
    void transfer();
    void close();

    DevicePicker _picker;
    bool         _networkDue    = false;  // the announcing screen is up; run the call
    uint32_t     _lastPaintMs   = 0;
};
