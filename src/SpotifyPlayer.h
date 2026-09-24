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
#include <atomic>
#include "DisplayUI.h"
#include "PlayingMetadata.h"
#include "scui.h"

#include "DevicePicker.h"   // a full type: the job slot holds one by value

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

    /// Asks the background task to re-seed the volume. Returns at once: the UI
    /// loop is also the only reader of the knob switch, so it must never block
    /// on the network (a blocking re-seed at 0.6 s swallowed the 1.5 s hold).
    void   requestVolumeRefresh() { _volumeRefreshRequested = true; }

    // ---- playback-device switching ------------------------------------------

    /// GET /v1/me/player/devices into picker (clears it first). Returns the
    /// HTTP status: 200 on success, <= 0 for a connection or JSON failure.
    /// On 401 the access token is refreshed and the request sent once more.
    int    fetchDevices(DevicePicker &picker);

    /// PUT /v1/me/player {"device_ids":[id],"play":true}. True on 204.
    /// The library reports only success or failure here, so any failure
    /// refreshes the access token and tries once more; that covers a 401.
    /// The caller tells a vanished device (404) apart by re-listing.
    bool   transferPlaybackTo(const char *deviceId);

    /// The picker's network work, run on the SongRefresh task (the task every
    /// working Spotify call already runs on) instead of the UI task. Measured
    /// 2026-09-23: the same GET that returns 200 from SongRefresh got no status
    /// line within SPOTIFY_TIMEOUT from UIHandler. One job at a time; a request
    /// while one is in flight is refused (returns false).
    bool   requestDeviceList();
    bool   requestTransfer(const char *deviceId);
    /// True once the job has finished; then copies its device list into `into`
    /// (cleared first) and reports status / transfer result. Clears the slot.
    bool   takeDeviceJob(DevicePicker &into, int &status, bool &transferOk, bool &stillListed);
    /// Drops a finished or pending result nobody will read (the picker closed).
    void   discardDeviceJob();

    // status
    bool   isMusicAvailable();

    // Call this to get the stable DTO copy
    const PlayingMetadata &getCurrentlyPlayingMetadata();       

    const bool   isNewTrackReady();

private:
    // ---- picker job slot (served by refreshCurrentSongTask) -----------------
    enum : uint8_t { JOB_NONE = 0, JOB_LIST = 1, JOB_TRANSFER = 2 };
    std::atomic<uint8_t> _job{JOB_NONE};       // set by UIHandler, cleared by SongRefresh when done
    std::atomic<bool>    _jobDone{false};      // published by SongRefresh after the result is complete
    DevicePicker         _jobDevices;          // written only by SongRefresh while a job runs
    char                 _jobTarget[48] = {0};
    int                  _jobStatus = 0;
    bool                 _jobOk = false;
    bool                 _jobStillListed = true;
    void                 serveDeviceJob();

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
    volatile bool       _volumeRefreshRequested = false;   // set by the UI loop, served by the background task
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
