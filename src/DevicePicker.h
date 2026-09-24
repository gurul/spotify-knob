/*-------------------------------------------------------------------------------------------------
**
** DevicePicker.h
**
**    State for the playback-device picker: the list from
**    GET /v1/me/player/devices, the knob's selection over it, and the
**    short-lived result screens. It holds no network code and no drawing
**    code, and it has no Arduino dependency. Time comes in as a parameter.
**    That keeps it testable on the host (test/test_device_picker).
**
**    DevicePickerView owns one instance. The view draws it and does the
**    network work that the Loading and Switching states ask for.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

/// One entry from GET /v1/me/player/devices. Fixed buffers: the list is at
/// most MAX_DEVICES long, so the whole picker is under 2 KB with no heap use.
struct PickerDevice
{
    char id[48];     ///< Spotify device ids are 40 hex characters
    char name[64];   ///< UTF-8; cut on a character boundary if longer
    char type[20];   ///< "Computer", "Smartphone", "Speaker", ...
    bool isActive;
    bool isRestricted;  ///< Spotify refuses Web API control of this device
};

class DevicePicker
{
public:
    enum class State : uint8_t
    {
        Closed,     ///< not showing; tick() reports "close"
        Loading,    ///< the view must fetch the device list
        List,       ///< the list is showing and the knob moves the selection
        Empty,      ///< the fetch worked but Spotify has no devices
        LoadError,  ///< the fetch failed
        Refused,    ///< the selected device is restricted; returns to List
        Switching,  ///< the view must transfer playback to selected()
        Switched,   ///< transfer worked (or it was already active); closes
        Gone,       ///< transfer failed and the device is no longer listed
        Failed,     ///< transfer failed for another reason; returns to List
    };

    static constexpr size_t   MAX_DEVICES     = 12;
    static constexpr uint32_t IDLE_TIMEOUT_MS = 10000;  ///< no input: close
    static constexpr uint32_t RESULT_SHOW_MS  = 1500;   ///< how long a result screen shows

    // ---- lifecycle ---------------------------------------------------------

    /// Starts a fresh session in Loading. Clears the previous list.
    void open(uint32_t nowMs);

    /// Leaves the picker without changing anything. tick() then reports close.
    void cancel();

    // ---- device list -------------------------------------------------------

    /// Empties the list before a (re)fetch. The state does not change.
    void clearDevices();

    /// Adds one device from the API response. Returns false and skips it when
    /// the list is full, the id is missing, or the id does not fit (a cut id
    /// cannot be transferred to). A missing name becomes "Unnamed device".
    bool addDevice(const char *id, const char *name, const char *type,
                   bool isActive, bool isRestricted);

    /// Ends Loading. ok=false means the request failed (LoadError).
    /// Otherwise List, or Empty when no device was added. The selection
    /// starts on the active device, so a press with no turn changes nothing.
    void finishLoad(bool ok, uint32_t nowMs);

    // ---- input -------------------------------------------------------------

    /// Moves the selection by delta rows, clamped to the list.
    void rotate(int delta, uint32_t nowMs);

    /// Acts on the selection. Restricted -> Refused. Already active ->
    /// Switched with no network call. Otherwise -> Switching.
    void press(uint32_t nowMs);

    // ---- transfer result ---------------------------------------------------

    /// Ends Switching. When ok is false, the caller re-fetches the list first
    /// (clearDevices/addDevice), and stillListed says whether the target
    /// device is still in it. That tells "device went offline" (Gone) from
    /// any other failure (Failed).
    void finishTransfer(bool ok, bool stillListed, uint32_t nowMs);

    // ---- time --------------------------------------------------------------

    /// Advances timers. Returns true when the view should close now.
    bool tick(uint32_t nowMs);

    // ---- read side ---------------------------------------------------------

    State               state() const { return _state; }
    size_t              count() const { return _count; }
    size_t              selectedIndex() const { return _selected; }
    const PickerDevice *device(size_t i) const { return (i < _count) ? &_devices[i] : nullptr; }
    const PickerDevice *selected() const { return device(_selected); }

    /// The id of the device a Switching transfer targets. Kept apart from
    /// the list, because a failed transfer re-fetches (and so rewrites) it.
    const char         *targetId() const { return _targetId; }
    const char         *targetName() const { return _targetName; }

    /// True when a device with this id is in the list.
    bool                contains(const char *id) const;

    /// First row to draw so that the selection is inside a window of
    /// `rows` rows. The selection sits in the middle row where it can.
    size_t              firstVisible(size_t rows) const;

    /// True once after any change the view must repaint. Clears the flag.
    bool                consumeDirty();

    // ---- text helpers (pure, shared with the view) -------------------------

    /// Copies src into out (size outSize), cut to at most maxChars UTF-8
    /// characters. When cut, ".." is appended. Never splits a character.
    static void truncateUtf8(const char *src, size_t maxChars, char *out, size_t outSize);

    /// Short lowercase label for a Spotify device type ("Smartphone" ->
    /// "phone"). Unknown or missing types give "device".
    static const char *typeLabel(const char *type);

private:
    void setState(State s, uint32_t nowMs);

    PickerDevice _devices[MAX_DEVICES];
    size_t       _count          = 0;
    size_t       _selected       = 0;
    State        _state          = State::Closed;
    uint32_t     _stateSinceMs   = 0;
    uint32_t     _lastInputMs    = 0;
    bool         _dirty          = false;
    char         _targetId[sizeof(PickerDevice::id)]     = {0};
    char         _targetName[sizeof(PickerDevice::name)] = {0};
};
