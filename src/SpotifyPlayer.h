/*-------------------------------------------------------------------------------------------------
**
** SpotifyPlayer.cpp
**
**    Instances of this class control Spotify once it has started.
**    This includes UI interactions. Due to dependencies used by
**    this class it should be treated as a Singleton.
**
** SPDX-FileCopyrightText: 2025 ThingPulse Ltd., https://thingpulse.com
** SPDX-License-Identifier: MIT
**
** ------------------------------------------------------------------------------------------------
** Change Log:
**    2024-12-28 - Electric Diversions - Initial creation.
** ------------------------------------------------------------------------------------------------
*/
#pragma once

#include <Arduino.h>
#include "DisplayUI.h"
#include "PlayingMetadata.h"
#include "scui.h"
class SpotifyPlayer {
public:
    // Public method to access the singleton instance
    static SpotifyPlayer& getInstance();

    // initialization related
    void   initialize(QueueHandle_t    *pScuiQueue);
    bool   isRefreshTokenAvailable();
    bool   requestRefreshToken();
    String getNodeName();
    void   login();
    void   startBackgroundRefreshes();


    // controls
    void   nextSong();
    void   previousSong();
    void   pauseSong();

    // ---- added for the CrowPanel rotary port -------------------------------
    // The original remote was touch-only, so it had no volume control
    // and no resume — pauseSong() only ever paused.

    /// Toggles play/pause against the live player. Returns the state it moved
    /// to, so the caller can update the UI without waiting for a refresh.
    bool   togglePlayPause();

    /// Nudges volume by a signed step and returns the resulting percentage.
    ///
    /// Applies to a local shadow immediately and pushes to Spotify on a short
    /// debounce (see commitPendingVolume). A knob emits detents far faster than
    /// the Web API will accept writes, so sending one request per detent gets
    /// the device rate-limited within a single flick.
    int    nudgeVolume(int delta);

    /// Current volume shadow, 0-100. -1 until seeded from the API.
    int    getVolume() const { return _volumePercent; }

    /// Pushes a debounced volume change if one is due. Call from the UI loop.
    void   commitPendingVolume();

    /// Seeds the volume shadow from the active device. Safe to call repeatedly.
    void   refreshVolumeFromDevice();

    // status
    bool   isMusicAvailable();

    // Call this to get the stable DTO copy
    const PlayingMetadata &getCurrentlyPlayingMetadata();       

    const bool   isNewTrackReady();

private:
    // Member variables
    String              _spotifyRefreshToken   = "";
    QueueHandle_t       *_pScuiQueue; 
    String              _currentTrackUri       = "";        // Tracks the currently playing song
    bool                _isPlaying             = false;
    bool                _isCoverArtAvailable   = false;
    bool                _isMusicAvailable      = false;
    bool                _isNewTrackReady       = false;
    bool                _isNewTrack            = false;
    PlayingMetadata     _currentlyPlayingMetadata;          // Frequently updated instance
    PlayingMetadata     _currentlyPlayingMetadataDTO;       // Stable DTO instance
    SemaphoreHandle_t   _xSemaphoreNetwork     = xSemaphoreCreateMutex();
    SemaphoreHandle_t   _xSemaphoreDataCopy    = xSemaphoreCreateMutex();
    TaskHandle_t        _refreshTaskHandle;
    String              _spotifyClientId;
    String              _spotifyClientSecret;

    // ---- volume shadow (CrowPanel rotary port) -----------------------------
    int                 _volumePercent      = -1;     // -1 = not yet seeded
    int                 _pendingVolume      = -1;     // -1 = nothing to push
    uint32_t            _volumeDirtyAtMs    = 0;
    static constexpr uint32_t VOLUME_DEBOUNCE_MS = 400;

    // Methods

    // Private constructor and destructor
    SpotifyPlayer();
    ~SpotifyPlayer();

    static void getCurrentlyPlayingCallback(CurrentlyPlaying currentlyPlaying);
    static void refreshCurrentSongTask(void *pvParameters);

    void   copyMetadataToDTO();
    void   printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying);    
    void   refreshCurrentTrack();
    void   refreshCurrentSong(CurrentlyPlaying currentlyPlaying); // Call back
    void   refreshCoverArt();
    void   postScuiMessage(SCUIMessageType type, const String& str, int num);
    void   saveCache();

};
