/*-------------------------------------------------------------------------------------------------
** DevicePicker.cpp — playback-device picker state. See the header.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "DevicePicker.h"

#include <string.h>

namespace {

/// Copies at most dstSize-1 bytes, cut back to a UTF-8 character boundary.
void copyUtf8(char *dst, size_t dstSize, const char *src)
{
    if (dstSize == 0)
    {
        return;
    }
    size_t n = strlen(src);
    if (n >= dstSize)
    {
        n = dstSize - 1;
        // Step back over continuation bytes (10xxxxxx) so the cut lands on
        // the first byte of a character, and drop that partial character.
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80)
        {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

void DevicePicker::setState(State s, uint32_t nowMs)
{
    _state        = s;
    _stateSinceMs = nowMs;
    _dirty        = true;
}

void DevicePicker::open(uint32_t nowMs)
{
    clearDevices();
    _targetId[0]   = '\0';
    _targetName[0] = '\0';
    _lastInputMs   = nowMs;
    setState(State::Loading, nowMs);
}

void DevicePicker::cancel()
{
    _state = State::Closed;
    _dirty = false;
}

void DevicePicker::clearDevices()
{
    _count    = 0;
    _selected = 0;
}

bool DevicePicker::addDevice(const char *id, const char *name, const char *type,
                             bool isActive, bool isRestricted)
{
    if (_count >= MAX_DEVICES || id == nullptr || id[0] == '\0' ||
        strlen(id) >= sizeof(PickerDevice::id))
    {
        return false;
    }

    PickerDevice &d = _devices[_count];
    copyUtf8(d.id, sizeof(d.id), id);
    copyUtf8(d.name, sizeof(d.name), (name && name[0]) ? name : "Unnamed device");
    copyUtf8(d.type, sizeof(d.type), type ? type : "");
    d.isActive     = isActive;
    d.isRestricted = isRestricted;
    _count++;
    return true;
}

void DevicePicker::finishLoad(bool ok, uint32_t nowMs)
{
    if (!ok)
    {
        setState(State::LoadError, nowMs);
        return;
    }

    _selected = 0;
    for (size_t i = 0; i < _count; i++)
    {
        if (_devices[i].isActive)
        {
            _selected = i;
            break;
        }
    }
    setState(_count == 0 ? State::Empty : State::List, nowMs);
}

void DevicePicker::rotate(int delta, uint32_t nowMs)
{
    _lastInputMs = nowMs;
    if (_state != State::List || _count == 0 || delta == 0)
    {
        return;
    }

    long next = static_cast<long>(_selected) + delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= static_cast<long>(_count))
    {
        next = static_cast<long>(_count) - 1;
    }
    if (static_cast<size_t>(next) != _selected)
    {
        _selected = static_cast<size_t>(next);
        _dirty    = true;
    }
}

void DevicePicker::press(uint32_t nowMs)
{
    _lastInputMs = nowMs;
    if (_state != State::List)
    {
        return;
    }

    const PickerDevice *d = selected();
    if (d == nullptr)
    {
        return;
    }

    copyUtf8(_targetId, sizeof(_targetId), d->id);
    copyUtf8(_targetName, sizeof(_targetName), d->name);

    if (d->isRestricted)
    {
        setState(State::Refused, nowMs);
    }
    else if (d->isActive)
    {
        // Already playing there. A transfer would only restart it.
        setState(State::Switched, nowMs);
    }
    else
    {
        setState(State::Switching, nowMs);
    }
}

void DevicePicker::finishTransfer(bool ok, bool stillListed, uint32_t nowMs)
{
    if (_state != State::Switching)
    {
        return;
    }
    if (ok)
    {
        setState(State::Switched, nowMs);
    }
    else
    {
        setState(stillListed ? State::Failed : State::Gone, nowMs);
    }
}

bool DevicePicker::tick(uint32_t nowMs)
{
    switch (_state)
    {
        case State::Closed:
            return true;

        case State::List:
        case State::Empty:
        case State::LoadError:
            if ((nowMs - _lastInputMs) >= IDLE_TIMEOUT_MS)
            {
                _state = State::Closed;
                return true;
            }
            return false;

        case State::Switched:
            if ((nowMs - _stateSinceMs) >= RESULT_SHOW_MS)
            {
                _state = State::Closed;
                return true;
            }
            return false;

        case State::Refused:
        case State::Gone:
        case State::Failed:
            // Back to the (possibly re-fetched) list; the idle timer restarts.
            if ((nowMs - _stateSinceMs) >= RESULT_SHOW_MS)
            {
                if (_selected >= _count)
                {
                    _selected = (_count > 0) ? _count - 1 : 0;
                }
                _lastInputMs = nowMs;
                setState(_count == 0 ? State::Empty : State::List, nowMs);
            }
            return false;

        case State::Loading:
        case State::Switching:
        default:
            return false;
    }
}

bool DevicePicker::contains(const char *id) const
{
    if (id == nullptr)
    {
        return false;
    }
    for (size_t i = 0; i < _count; i++)
    {
        if (strcmp(_devices[i].id, id) == 0)
        {
            return true;
        }
    }
    return false;
}

size_t DevicePicker::firstVisible(size_t rows) const
{
    if (rows == 0 || _count <= rows)
    {
        return 0;
    }
    const size_t half = rows / 2;
    size_t first = (_selected > half) ? _selected - half : 0;
    if (first + rows > _count)
    {
        first = _count - rows;
    }
    return first;
}

bool DevicePicker::consumeDirty()
{
    const bool d = _dirty;
    _dirty = false;
    return d;
}

void DevicePicker::truncateUtf8(const char *src, size_t maxChars, char *out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    if (src == nullptr)
    {
        return;
    }

    // Walk characters (a character starts at any byte that is not 10xxxxxx).
    size_t chars = 0;
    size_t i     = 0;
    size_t cut   = 0;  // byte offset after maxChars characters
    while (src[i] != '\0')
    {
        if ((static_cast<unsigned char>(src[i]) & 0xC0) != 0x80)
        {
            if (chars == maxChars)
            {
                break;
            }
            chars++;
        }
        i++;
        cut = i;
    }

    bool   truncated = (src[i] != '\0');
    size_t n         = cut;

    // Fit into out, keeping room for ".." and the terminator. A cut forced by
    // the buffer is still a cut, so it gets the dots too.
    if (n + (truncated ? 2 : 0) + 1 > outSize)
    {
        truncated = true;
        n = (outSize > 3) ? outSize - 3 : 0;
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80)
        {
            n--;
        }
    }
    memcpy(out, src, n);
    if (truncated && n + 2 < outSize)
    {
        out[n++] = '.';
        out[n++] = '.';
    }
    out[n] = '\0';
}

const char *DevicePicker::typeLabel(const char *type)
{
    struct Entry { const char *api; const char *label; };
    static const Entry kLabels[] = {
        {"Computer", "computer"},   {"Smartphone", "phone"},     {"Tablet", "tablet"},
        {"Speaker", "speaker"},     {"TV", "TV"},                {"AVR", "receiver"},
        {"STB", "set-top box"},     {"AudioDongle", "dongle"},   {"GameConsole", "console"},
        {"CastVideo", "cast"},      {"CastAudio", "cast"},       {"Automobile", "car"},
        {"Smartwatch", "watch"},
    };

    if (type != nullptr)
    {
        for (const Entry &e : kLabels)
        {
            if (strcmp(e.api, type) == 0)
            {
                return e.label;
            }
        }
    }
    return "device";
}
