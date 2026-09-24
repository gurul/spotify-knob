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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "esp_task_wdt.h"
#include "SpotifyPlayer.h"
#include "logTags.h"
#include "SCLogger.h"
#include "Core/spotify.h"
#include "Monitor.h"
#include "SpotifyArtMgr.h"
#include "SCFileIO.h"

#include <TJpg_Decoder.h> // Ensure you include the required decoder library

// Spotify related
#define SP_SPOTIFY_MARKET         "IE"

/*
** ===================================================================
** Callback routine for spotify.getCurrentlyPlaying() method.
** ===================================================================
*/
void SpotifyPlayer::getCurrentlyPlayingCallback(CurrentlyPlaying currentlyPlaying)
{
    spLogD(LOGTAG_PLAYER, "getCurrentlyPlayingCallback - callback routine for spotify.getCurrentlyPlaying() method.");
    SpotifyPlayer::getInstance().refreshCurrentSong(currentlyPlaying);
}

/*
** ===================================================================
** Constructor and Destructor
** ===================================================================
*/
SpotifyPlayer::SpotifyPlayer()
{
    // Initialization code (if needed)
}

SpotifyPlayer::~SpotifyPlayer() 
{
}

/*
** ===================================================================
** getInstance()
** ===================================================================
*/
SpotifyPlayer& SpotifyPlayer::getInstance() 
{
    static SpotifyPlayer instance; // Guaranteed to be thread-safe in C++11 and later
    return instance;
}

/*
** ===================================================================
** initialize()
** ===================================================================
*/

void SpotifyPlayer::initialize(QueueHandle_t    *pScuiQueue)
{
    _pScuiQueue = pScuiQueue;

    // keep reference since SpotifyArduino is dependent on values
    // remaining in memory
    _spotifyClientId     = Vault::getInstance().getSpotifyClientID();
    _spotifyClientSecret = Vault::getInstance().getSpotifyClientSecret();

    spotify.lateInit(_spotifyClientId.c_str(), _spotifyClientSecret.c_str(), _spotifyRefreshToken.c_str());

    // client is defined in Core/spotify.h
    // Both api.spotify.com and accounts.spotify.com; see the bundle's notes.
    client.setCACert(spotify_api_root_certs);
    
}

/*
** ===================================================================
** startBackgroundRefreshes()
** ===================================================================
*/

void SpotifyPlayer::startBackgroundRefreshes()
{

    // The ESP32 is a dual-core processor with two Xtensa LX6 cores:
    // 
    // - **Core 0 (PRO CPU)** → Handles system tasks (Wi-Fi, Bluetooth, FreeRTOS)
    // - **Core 1 (APP CPU)** → Runs the Arduino framework (`setup()` and `loop()`)
    //
    // By default, all Arduino code executes on **Core 1**.

    // Create task to refresh song information
    spLogI(LOGTAG_MULTITASK, "creating background task - pinned to core 1");

    // Note: Core 0 had stability issues despite best attempts
    // to address.  Moved UI loop which doesn't block the way
    // that the song refresh logic does to run on Core 0.
    xTaskCreatePinnedToCore(
        SpotifyPlayer::refreshCurrentSongTask, // Task function
        "SongRefresh",          // Task name
        8192,                   // Stack size
        NULL,                   // Task parameter
        1,                      // Task priority
        &_refreshTaskHandle,    // Task handle
        1                       // Core ID
    );    
    
    spLogI(LOGTAG_MULTITASK, "background task created");

}

/*
** ===================================================================
** isRefreshTokenAvailable()
**
** Returns whether the refresh token is available.  This will also
** internally initialize the token if it exists.
** ===================================================================
*/
bool SpotifyPlayer::isRefreshTokenAvailable()
{
   _spotifyRefreshToken = SCFileIO::getInstance().readFsString(SPOTIFY_REFRESH_TOKEN_FILE_NAME);
   
   if (_spotifyRefreshToken == "")
   {
        spLogI(LOGTAG_PLAYER, "Spotify refresh token not found.");
        return false;
   }
   else
   {
        spLogI(LOGTAG_PLAYER, "Spotify refresh token found.");
        return true;
   }
}

/*
** ===================================================================
** requestRefreshToken()
**
** Requests a fresh refresh token.  A refresh token is a security 
** credential that allows client applications to obtain new access
** tokens without requiring users to reauthorize the application.
** ===================================================================
*/
bool SpotifyPlayer::requestRefreshToken()
{

    spLogI(LOGTAG_PLAYER, "Requesting Spotify refresh token through the browser via auth code.");

    String spotifyAuthCode = fetchSpotifyAuthCode();
    _spotifyRefreshToken = spotify.requestAccessTokens(spotifyAuthCode.c_str(), SPOTIFY_REDIRECT_URI);
    SCFileIO::getInstance().saveFsString(SPOTIFY_REFRESH_TOKEN_FILE_NAME, _spotifyRefreshToken);

    return true;

}

/*
** ===================================================================
** getNodeName()
** ===================================================================
*/
String SpotifyPlayer::getNodeName()
{
    return SPOTIFY_ESPOTIFIER_NODE_NAME;
}

/*
** ===================================================================
** login()
** ===================================================================
*/
void SpotifyPlayer::login()
{
    // The Spotify library
    // - keeps track of the refresh token and its TTL internally
    // - automatically renews the actual access token using the refresh token
    // -> see SpotifyArduino.h#autoTokenRefresh and SpotifyArduino::checkAndRefreshAccessToken() (called before every API function)
    spotify.setRefreshToken(_spotifyRefreshToken.c_str());
    spotify.refreshAccessToken();
    // Never log the refresh token itself: it is a long-lived credential.
    spLogI(LOGTAG_PLAYER, "Authentication against Spotify done. Refresh token length: %u",
           (unsigned)_spotifyRefreshToken.length());
}

/*
** ===================================================================
** postScuiMessage()
**
**     Sends a message to the SCUI queue with a specified type,
**     string data, and an integer value. The integer is appended
**     to the message data.
**
** Parameters:
**     type - The type of SCUI message.
**     str  - The string data to send.
**     num  - The integer value to append to the message.
**
** Returns:
**     None
** ===================================================================
*/
void SpotifyPlayer::postScuiMessage(SCUIMessageType type, const String& str, int num)
{
    if (_pScuiQueue != NULL)
    {
        SCUIMessage msg;
        msg.type = type;
        msg.str  = str;
        msg.num  = num;

        Monitor::start(MONITOR_ID_SCUI_QUEUE_DELAY, LOGTAG_MULTITASK, "postScuiMessage()");
        if (xQueueSend(*_pScuiQueue, &msg, pdMS_TO_TICKS(10)) != pdPASS)
        {
            spLogE(LOGTAG_PLAYER, "Failed to send SCUI message to queue");
        }
    }
}

/*
** ===================================================================
** nextSong()
** ===================================================================
*/
void SpotifyPlayer::nextSong()
{
    // Code to skip to the next song using Spotify's API
    // Example: Sending an API request or invoking a method in the client
    spLogI(LOGTAG_PLAYER, "Skipping to the next song.");

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "Making Call",
                    static_cast<int>(TFTColor::SC_NetworkInProgress));
    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY)) 
    {
        if (spotify.nextTrack())
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "",
                            static_cast<int>(TFTColor::SC_NetworkSuccess));
            spLogD(LOGTAG_PLAYER, "next track call succesful");
        }
        else
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "",
                            static_cast<int>(TFTColor::SC_NetworkFailure));            
            spLogD(LOGTAG_PLAYER, "next track call failed"); // 
        }
        xSemaphoreGive(_xSemaphoreNetwork);
    }
    else
    {
        spLogI(LOGTAG_MULTITASK,"Unable to take _xSemaphoreNetwork.");
    }    

}

/*
** ===================================================================
** previousSong()
** ===================================================================
*/
void SpotifyPlayer::previousSong()
{
    // Code to go back to the previous song using Spotify's API
    // Example: Sending an API request or invoking a method in the client
    spLogI(LOGTAG_PLAYER, "Going back to the previous song.");

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "Making Call",
                    static_cast<int>(TFTColor::SC_NetworkInProgress));    

    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY)) 
    {
        if (spotify.previousTrack())
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "",
                            static_cast<int>(TFTColor::SC_NetworkSuccess));
            spLogD(LOGTAG_PLAYER, "previous track call succesful");
        }
        else
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "",
                            static_cast<int>(TFTColor::SC_NetworkFailure));    
            spLogD(LOGTAG_PLAYER, "previous track call failed");
        }
        xSemaphoreGive(_xSemaphoreNetwork);
    }
    else
    {
        spLogI(LOGTAG_MULTITASK,"Unable to take _xSemaphoreNetwork.");
    }


}

/*
** ===================================================================
** pauseSong()
** ===================================================================
*/
void SpotifyPlayer::pauseSong()
{
    // Code to pause playback using Spotify's API
    // Example: Sending an API request or invoking a method in the client
    spLogI(LOGTAG_PLAYER, "Pausing the current song.");

    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY)) 
    {
        if (_isPlaying)
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Making Call",
                            static_cast<int>(TFTColor::SC_NetworkInProgress));    
            if (spotify.pause())
            {
                postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                                "",
                                static_cast<int>(TFTColor::SC_NetworkSuccess));
                spLogD(LOGTAG_PLAYER, "pause track call succesful");
            }
            else
            {
                postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                                "",
                                static_cast<int>(TFTColor::SC_NetworkFailure));   
                spLogD(LOGTAG_PLAYER, "pause track call failed"); // 
            }                    
        }
        else
        {
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Making Call",
                            static_cast<int>(TFTColor::SC_NetworkInProgress));    
            if (spotify.play())
            {
                postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                                "",
                                static_cast<int>(TFTColor::SC_NetworkSuccess));
                spLogD(LOGTAG_PLAYER, "pause track call succesful");
            }
            else
            {
                postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                                "",
                                static_cast<int>(TFTColor::SC_NetworkFailure));  
                spLogD(LOGTAG_PLAYER, "pause track call failed"); // 
            }                      
        }     
        xSemaphoreGive(_xSemaphoreNetwork);
    }
    else
    {
        spLogI(LOGTAG_MULTITASK,"Unable to take _xSemaphoreNetwork.");
    }
}

/*
** ===================================================================
** refreshCurrentTrack()
** ===================================================================
*/

void SpotifyPlayer::refreshCurrentTrack()
{
        spLogD(LOGTAG_MULTITASK, "Free Heap: ");
        spLogD(LOGTAG_MULTITASK, "%d", ESP.getFreeHeap());
       
        postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                        "Making Call",
                        static_cast<int>(TFTColor::SC_NetworkInProgress));    

        spLogI(LOGTAG_MULTITASK, "getting currently playing song:");
        // Market can be excluded if you want e.g. spotify.getCurrentlyPlaying()
        int status = -777;
        
        if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY))
        {
            // `client` is a global WiFiClientSecure reused for every poll, and
            // WiFiClientSecure::connect() does NOT close a still-open session
            // before reusing the object. Left alone it logs
            //   ssl_client.cpp _handle_error(): data_to_read() (-76)
            //   NetworkClientSecure.cpp available(): Closing connection on
            //   failed available check
            // after every single refresh, and a control call landing mid
            // teardown can fail with a connection reset. Closing it first makes
            // each poll start from a clean session.
            //
            // Independently hit and fixed the same way in KonradIT/espotify
            // (commit af917b6).
            client.stop();

            spLogI(LOGTAG_MULTITASK, "Invoking spotify.getCurrentlyPlaying(...)");
            Monitor::start(MONITOR_ID_SPOTIFY_GET_CURRENTLY_PLAYING, LOGTAG_METRICS, "spotify.getCurrentlyPlaying(...)");
            status = spotify.getCurrentlyPlaying(SpotifyPlayer::getCurrentlyPlayingCallback, SP_SPOTIFY_MARKET);
            Monitor::stop(MONITOR_ID_SPOTIFY_GET_CURRENTLY_PLAYING);
            xSemaphoreGive(_xSemaphoreNetwork);
        }
        else
        {
            spLogI(LOGTAG_MULTITASK,"Unable to take _xSemaphoreNetwork.");
        }


        // One line per change of outcome, not per poll: enough to see a
        // 204 (nothing playing) turn into a 200 without flooding the log.
        static int lastLoggedStatus = 0;
        if (status != lastLoggedStatus)
        {
            spLogI(LOGTAG_PLAYER, "NOWPLAYING currently-playing -> %d", status);
            lastLoggedStatus = status;
        }

        if (status == 200)
        {
            spLogI(LOGTAG_MULTITASK, "Successfully refreshed current song.");
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Successful",
                            static_cast<int>(TFTColor::SC_NetworkSuccess));
        }
        else if (status == 204)
        {
            spLogI(LOGTAG_MULTITASK, "Doesn't seem to be anything playing");
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Nothing playing",
                            static_cast<int>(TFTColor::SC_AlertStatus));

        }
        else if (status == -777)
        {
            spLogI(LOGTAG_MULTITASK, "No, really, unable to get semaphore.  status still -777");
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Unable to get semaphore",
                            static_cast<int>(TFTColor::SC_AlertStatus));
        }
        else
        {
            spLogE(LOGTAG_MULTITASK, "Error: ");
            spLogE(LOGTAG_MULTITASK, "Status: %d", status);
            postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                            "Failure",
                            static_cast<int>(TFTColor::SC_NetworkFailure));
        }

        refreshCoverArt();

        if (_isNewTrack)
        {
                // Track is loaded and copied.
                // At least one pass made at downloading the art.
                _isNewTrackReady = true;
        }

        postScuiMessage(SCUIMessageType::UM_PLAYER_REFRESH,
                        "Refresh",
                        _isNewTrackReady);
}

/*
** ===================================================================
** refreshCoverArt()
** 
**    This method handles refreshing the cover art for the currently
** playing track. It checks if a new track is detected or if cover
** art is unavailable and downloads the appropriate album art. If
** the download is successful, it updates the background color to
** match the average color of the album art. It also ensures the 
** UI is marked dirty to trigger a repaint.
**
** Parameters:
**    None
**
** Returns:
**    None
**
** Notes:
**    - The method determines if the currently playing track has 
**      changed by comparing track URIs.
**    - If cover art is downloaded successfully, the background 
**      color is updated based on the average color of the image.
**    - UI is marked dirty whenever the cover art is refreshed.
**
** ===================================================================
*/

void SpotifyPlayer::refreshCoverArt()
{
    bool bNewTrack = false;

    if (_currentTrackUri != _currentlyPlayingMetadata.trackUri)
    {
        spLogI(LOGTAG_GENERAL, "New song detected: %s", _currentlyPlayingMetadata.trackName);
        bNewTrack                = true;
        _isCoverArtAvailable     = false;
        _currentTrackUri         = _currentlyPlayingMetadata.trackUri; 
    }

    // if new track, download and draw it
    spLogD(LOGTAG_GENERAL, "if (bNewTrack || !_isCoverArtAvailable)... bNewTrack=%d, _isCoverArtAvailable=%d", (int)bNewTrack, (int)_isCoverArtAvailable);
    if (bNewTrack 
    || !_isCoverArtAvailable)
    {
        const int TARGET_IMAGE_SIZE  = 300;
        String    filePath; 
        
        for (int i = 0; i < _currentlyPlayingMetadata.numImages; i++)
        {
            if ((_currentlyPlayingMetadata.albumImages[i].height == TARGET_IMAGE_SIZE)
            &&  (_currentlyPlayingMetadata.albumImages[i].width  == TARGET_IMAGE_SIZE))
            {
                spLogV(LOGTAG_GENERAL, "*** Attempting image download ***");

                postScuiMessage(SCUIMessageType::UM_DOWNLOAD_BOX,"",false);
                Monitor::start(MONITOR_ID_FETCH_ALBUM_ART, LOGTAG_METRICS, "Download Art If Needed.");
                filePath = SpotifyArtMgr::getInstance()->acquireAlbumArt(_currentlyPlayingMetadata.albumImages[i].url);
                Monitor::stop(MONITOR_ID_FETCH_ALBUM_ART);
                postScuiMessage(SCUIMessageType::UM_DOWNLOAD_BOX,"",true);

                // Check if the acquired file is valid
                if (filePath != SP_NO_COVER_JPG_FILENAME 
                && !filePath.isEmpty())
                {
                    _isCoverArtAvailable = true;
                    spLogV(LOGTAG_GENERAL, "*** Cover art is available: %s ***", filePath.c_str());
                }
                else
                {
                    _isCoverArtAvailable = false;
                    spLogW(LOGTAG_GENERAL, "*** Cover art is unavailable ***");
                }                

                spLogV(LOGTAG_GENERAL, "*** downloadFile() finished executing ***");
                break;
            }
        }
        if (_isCoverArtAvailable)
        {
            // TFTColor c = _pUI->calculateAverageColor(filePath.c_str());
            // _pUI->setBackground(c, false);
        }
        else
        {
            spLogI(LOGTAG_MULTITASK, "*** %dx%d image not found to download. ***",TARGET_IMAGE_SIZE,TARGET_IMAGE_SIZE); 
        }

        // request updated UI
        postScuiMessage(SCUIMessageType::UM_MARK_DIRTY,
                        "",
                        true);        

    }
    
}

/*
** ===================================================================
** refreshCurrentSong()
**
**     This is the callback for what is playing from the Spotify API.
** ===================================================================
*/
void SpotifyPlayer::refreshCurrentSong(CurrentlyPlaying currentlyPlaying)
{

    spLogI(LOGTAG_MULTITASK, "Refreshing current song.  SpotifyPlayer::refreshCurrentSong(CurrentlyPlaying currentlyPlaying)");

    // If not a track or episode, update the UI accordingly and indicate music isn't available
    if ((!currentlyPlaying.currentlyPlayingType == SpotifyPlayingType::track)
    &&  (!currentlyPlaying.currentlyPlayingType == SpotifyPlayingType::episode))
    {
        spLogI(LOGTAG_GENERAL, " _isMusicAvailable set to false. currentlyPlayingType is not a supported type." );
        _isMusicAvailable = false; 
        // force a refresh to get the waiting message
        postScuiMessage(SCUIMessageType::UM_MARK_DIRTY,
                        "",
                        true);        
        return;
    }

    if (xSemaphoreTake(_xSemaphoreDataCopy, portMAX_DELAY)) 
    {
        // copy currentlyPlaying over for use
        if (currentlyPlaying.trackUri == nullptr)
        {
            spLogV(LOGTAG_GENERAL, "currentlyPlaying.trackUri == nullptr. skipping new track ready logic.");
        }
        else
        {
            if (strcmp(currentlyPlaying.trackUri, _currentlyPlayingMetadata.trackUri) != 0)
            {
                // New track has just been loaded
                _isNewTrack = true;
                spLogV(LOGTAG_GENERAL, "New Track!!!  _isNewTrack = true");
            }
            else
            {
                _isNewTrack = false;
            }
        }
        _currentlyPlayingMetadata.copyFrom(currentlyPlaying);
        xSemaphoreGive(_xSemaphoreDataCopy);
    }
    else
    {
        spLogI(LOGTAG_MULTITASK, "Unable to take _xSemaphoreDataCopy.");
    }

    _isPlaying = currentlyPlaying.isPlaying;
    
    if (currentlyPlaying.trackName == nullptr)
    {
        spLogI(LOGTAG_MULTITASK, "refreshCurrentSong() - Empty track detected");
        _isMusicAvailable = false; 
        // force a refresh to get the waiting message
        postScuiMessage(SCUIMessageType::UM_MARK_DIRTY,
                        "",
                        true);
        return;
    }
    else
    {
        // spLogI(LOGTAG_GENERAL, " _isMusicAvailable = true; currentlyPlaying.trackName ='%s' with length = %u", currentlyPlaying.trackName, strlen(currentlyPlaying.trackName) );
        _isMusicAvailable = true; 
        printCurrentlyPlayingToSerial(currentlyPlaying);
    }

}

/*
** ===================================================================
** isMusicAvailable()
**    Answer whether music is currently available to display.
** ===================================================================
*/
bool SpotifyPlayer::isMusicAvailable()
{
    return _isMusicAvailable;
}

/*
** ===================================================================
** printCurrentlyPlayingToSerial()
**    Prints the currently playing track details to serial output.
** ===================================================================
*/
void SpotifyPlayer::printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{

    spLogI(LOGTAG_SONG_DATA, "--------- Currently Playing ---------");

    spLogI(LOGTAG_SONG_DATA, "Track: %s", currentlyPlaying.trackName);
    spLogV(LOGTAG_SONG_DATA, "");

    spLogV(LOGTAG_SONG_DATA, "Artists: ");
    for (int i = 0; i < currentlyPlaying.numArtists; i++)
    {
        spLogV(LOGTAG_SONG_DATA, "  Name: %s", currentlyPlaying.artists[i].artistName);
        spLogV(LOGTAG_SONG_DATA, "");
    }

    spLogV(LOGTAG_SONG_DATA, "Album: %s", currentlyPlaying.albumName);
    spLogV(LOGTAG_SONG_DATA, "");

    spLogI(LOGTAG_SONG_DATA, "------------------------");    

}

/*
** ===================================================================
** getCurrentlyPlayingMetadata()
**    Returns a stable copy of the currently playing metadata.
**    This method ensures that the returned data does not change
**    unexpectedly while in use.  It will have the side effect
**    of reseting isNewTrackReady() to false.
**
** Returns:
**    A reference to the stable DTO copy of PlayingMetadata.
** ===================================================================
*/
const PlayingMetadata &SpotifyPlayer::getCurrentlyPlayingMetadata() 
{
    copyMetadataToDTO();
    return _currentlyPlayingMetadataDTO;
}  

/*
** ===================================================================
** copyMetadataToDTO()
**    Copies the currently playing metadata into a stable DTO.
**    This method ensures thread safety by using a semaphore
**    to prevent data races.
**
** Locks:
**    - _xSemaphoreDataCopy: Ensures safe access to shared data.
**
** Behavior:
**    - If the semaphore is available, it performs a deep copy.
**    - If the semaphore is not available, logs an informational message.
** ===================================================================
*/
void SpotifyPlayer::copyMetadataToDTO() 
{
    if (xSemaphoreTake(_xSemaphoreDataCopy, portMAX_DELAY)) 
    {
        // copy currentlyPlaying over for use
        _currentlyPlayingMetadataDTO.copyFrom(_currentlyPlayingMetadata);
        _isNewTrackReady = false;
        xSemaphoreGive(_xSemaphoreDataCopy);
    }
    else
    {
        spLogI(LOGTAG_MULTITASK, "Unable to take _xSemaphoreDataCopy.");
    }        
    
}    

/*
** ===================================================================
** isNewTrackReady()
**     Answers whether a new track has become ready and 
**   getCurrentlyPlayingMetadata() should be called to populdate
**   the DTO.
**  
** ===================================================================
*/
const bool   SpotifyPlayer::isNewTrackReady()
{
    return _isNewTrackReady;
}

void SpotifyPlayer::saveCache()
{

    SpotifyArtMgr *pSAM = SpotifyArtMgr::getInstance();

    if (pSAM->isCacheDirty())
    {
        postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                        "",
                        static_cast<int>(TFTColor::SC_CacheSave));  
        pSAM->saveCacheIndex(); // persist the updated file index
    }

    // clear status.  this assumes that this is the last
    // update which slightly breaks encapsulation and
    // is also brittle; however, not sure there is a cleaner
    // approach that isn't worse.
    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "",
                    -1);  // -1 will use background color         

}

/*
** ===================================================================
** refreshCurrentSongTask()
**    Background task responsible for periodically refreshing the
**    currently playing song information from Spotify. The task 
**    includes a blocking HTTP call to fetch data and resets the 
**    Task Watchdog Timer to prevent timeout.
**
** Parameters:
**    pvParameters - Pointer to task parameters (not used in this task).
**
** Notes:
**    - Executes in an infinite loop with a specified delay between
**      iterations to avoid overloading the system.
**    - Calls the refreshCurrentTrack() method of SpotifyPlayer to 
**      perform the actual data retrieval.
**    - Resets the Task Watchdog Timer on each iteration.
** ===================================================================
*/

void SpotifyPlayer::refreshCurrentSongTask(void *pvParameters) 
{
    spLogI(LOGTAG_MULTITASK, "background task executing.  about to enter loop.");

    // NO task-watchdog subscription — deliberately, and learned the hard way.
    //
    // This loop blocks on TLS handshakes and album-art downloads that
    // routinely exceed the watchdog window, so subscribing the task arms a
    // panic around I/O that is merely slow, not hung:
    //   Task watchdog got triggered -> panic -> reboot mid-frame
    // (three crashes in minutes when it was tried; the screen "glitch" was
    // the board rebooting). The old esp_task_wdt_reset() in the loop only
    // ever appeared to work because the task was never subscribed and the
    // call was a no-op with an error message. Both halves are now gone: no
    // add, no reset — the honest form of the no-protection that always was.

    // TODO: work this out better so it just simply starts when ready
    vTaskDelay(pdMS_TO_TICKS(2000)); // 2000 ms delay to get started and wait for everything to process
    while (true) {
        spLogI(LOGTAG_MULTITASK, "refreshCurrentSongTask is running");

        // Perform the task (blocking HTTP call)
        SpotifyPlayer::getInstance().refreshCurrentTrack();

        // Give the UI time to refresh since might be marked dirty
        // Also give time for other tasks to do work.
        vTaskDelay(pdMS_TO_TICKS(1500)); 

        SpotifyPlayer::getInstance().saveCache();

        // Allow other tasks to execute
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*
** ===================================================================
** CrowPanel rotary port — volume and play/pause
**
**    The original remote was touch-only: it could skip and pause,
**    but never resume and never set volume. A knob needs both.
** ===================================================================
*/

/*
** ===================================================================
** togglePlayPause()
**
**    Flips the transport and returns the state it moved to, so the UI
**    can repaint immediately instead of waiting for the next refresh.
** ===================================================================
*/
bool SpotifyPlayer::togglePlayPause()
{
    const bool wantPlaying = !_currentlyPlayingMetadataDTO.isPlaying;

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "Making Call",
                    static_cast<int>(TFTColor::SC_NetworkInProgress));

    bool ok = false;
    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY))
    {
        ok = wantPlaying ? spotify.play() : spotify.pause();
        xSemaphoreGive(_xSemaphoreNetwork);
    }

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "",
                    static_cast<int>(ok ? TFTColor::SC_NetworkSuccess
                                        : TFTColor::SC_NetworkFailure));

    if (ok)
    {
        // Reflect it locally so the icon flips now. The next refresh will
        // reconcile if something else changed the player meanwhile.
        if (xSemaphoreTake(_xSemaphoreDataCopy, portMAX_DELAY))
        {
            _currentlyPlayingMetadataDTO.isPlaying = wantPlaying;
            _currentlyPlayingMetadata.isPlaying    = wantPlaying;
            xSemaphoreGive(_xSemaphoreDataCopy);
        }
        _isPlaying = wantPlaying;
    }

    spLogI(LOGTAG_PLAYER, "TRANSPORT toggle -> %s (%s)",
           wantPlaying ? "play" : "pause", ok ? "ok" : "FAILED");

    return wantPlaying;
}

/*
** ===================================================================
** refreshVolumeFromDevice()
**
**    Seeds the volume shadow from whichever device is active. Spotify
**    reports volume on the device, not on the track, so this is a
**    separate call from the now-playing refresh.
** ===================================================================
*/
void SpotifyPlayer::refreshVolumeFromDevice()
{
    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY))
    {
        spotify.getPlayerDetails([](PlayerDetails details) {
            const int v = details.device.volumePercent;
            if (v >= 0 && v <= 100)
            {
                SpotifyPlayer::getInstance()._volumePercent = v;
                spLogI(LOGTAG_PLAYER, "VOLUME seeded=%d device=%s", v,
                       details.device.name ? details.device.name : "?");
            }
        });
        xSemaphoreGive(_xSemaphoreNetwork);
    }
}

/*
** ===================================================================
** nudgeVolume()
**
**    Moves the local shadow immediately and marks it for a debounced
**    push. Returns the new percentage so the caller can repaint at
**    once — waiting on the network here would make the knob feel dead.
** ===================================================================
*/
int SpotifyPlayer::nudgeVolume(int delta)
{
    if (_volumePercent < 0)
    {
        // Not seeded yet. Start from a sane midpoint rather than blocking the
        // knob on a network round trip.
        _volumePercent = 50;
    }

    int next = _volumePercent + delta;
    if (next < 0)   next = 0;
    if (next > 100) next = 100;

    if (next != _volumePercent)
    {
        _volumePercent   = next;
        _pendingVolume   = next;
        _volumeDirtyAtMs = millis();
    }

    return _volumePercent;
}

/*
** ===================================================================
** commitPendingVolume()
**
**    Pushes a pending volume once the knob has been still long enough.
**    One API write per detent would rate-limit the account within a
**    single flick of the wheel, so only the settled value is sent.
** ===================================================================
*/
void SpotifyPlayer::commitPendingVolume()
{
    if (_pendingVolume < 0)
    {
        return;
    }

    if ((millis() - _volumeDirtyAtMs) < VOLUME_DEBOUNCE_MS)
    {
        return;
    }

    const int target = _pendingVolume;
    _pendingVolume   = -1;   // cleared before the call, so a failure does not
                             // spin retrying the same value forever

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "Making Call",
                    static_cast<int>(TFTColor::SC_NetworkInProgress));

    bool ok = false;
    if (xSemaphoreTake(_xSemaphoreNetwork, portMAX_DELAY))
    {
        ok = spotify.setVolume(target);
        xSemaphoreGive(_xSemaphoreNetwork);
    }

    postScuiMessage(SCUIMessageType::UM_STATUS_BOX,
                    "",
                    static_cast<int>(ok ? TFTColor::SC_NetworkSuccess
                                        : TFTColor::SC_NetworkFailure));

    spLogI(LOGTAG_PLAYER, "VOLUME set=%d (%s)", target, ok ? "ok" : "FAILED");
}
