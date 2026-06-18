#pragma once

/*
Purpose: Mutable runtime copy of all user-configurable settings.
Initialized from the compile-time defaults in UserConfiguration.h /
TimingConfiguration.h on first boot.  Changes persist across reboots via
ESP32 NVS (Preferences library).

Usage:
  1. Call loadConfig() once in setup() after LittleFS / filesystem is ready.
  2. Read g_config.xxx anywhere instead of UserConfiguration::XXX.
  3. The web UI writes updated values, calls saveConfig(), and they take effect
     immediately since all render/fetch code reads g_config at call time.
*/

#include <Arduino.h>
#include <time.h>

struct RuntimeConfig
{
    // Location
    double   center_lat;
    double   center_lon;
    double   radius_km;

    // Display
    uint8_t  display_brightness;
    uint8_t  text_color_r;
    uint8_t  text_color_g;
    uint8_t  text_color_b;
    bool     display_nearest_only;
    bool     display_border;
    bool     show_flight_number_with_logo;
    bool     show_aircraft_type;          // show ICAO type on line 3
    bool     show_aircraft_registration;  // show tail number on line 3b (or 3 if type hidden)
    bool     swap_aircraft_type_reg;      // swap order: reg on line 3, type on line 3b
    char     screen_facing[8];           // e.g. "N", "NNW" — null-terminated
    bool     display_flip;               // true = rotate 180° (USB on opposite side)

    // Night mode — optional time-based brightness reduction
    bool     night_mode_enabled;
    uint16_t night_start_minutes;        // local time of night start (minutes since midnight)
    uint16_t night_end_minutes;          // local time of night end
    uint8_t  night_brightness;           // brightness during night period (0 = screen off)
    int32_t  utc_offset_minutes;         // local time = UTC + this value (e.g. 60 = UTC+1)

    // Local ADS-B receiver (tar1090 / readsb / dump1090)
    // Set to IP or hostname, e.g. "10.11.10.84".  Empty = disabled (OpenSky only).
    char     tar1090_host[64];

    // API keys — runtime-configurable; override the compile-time APIConfiguration.h
    // constants when non-empty.  Stored separately in NVS so a config reset does
    // not wipe credentials entered via the web UI.
    char     opensky_client_id[64];
    char     opensky_client_secret[64];
    char     aeroapi_key[64];

    // Route lookup source priority
    // false (default): hexdb → AeroAPI → OpenSky
    // true:            OpenSky → AeroAPI → hexdb  (useful when hexdb is rate-limited)
    bool     opensky_priority;

    // Timing
    uint32_t fetch_interval_seconds;        // used when falling back to OpenSky
    uint32_t local_fetch_interval_seconds;  // used when local ADS-B is the active source
    uint32_t display_cycle_seconds;
    uint32_t aeroapi_cache_ttl_seconds;
    uint32_t aeroapi_fail_cache_ttl_seconds;
};

// Global mutable config — use this everywhere instead of UserConfiguration /
// TimingConfiguration constants.
extern RuntimeConfig g_config;

// Load saved values from NVS into g_config.
// Falls back to compile-time defaults (UserConfiguration.h) for any key that
// has never been saved.  Call once from setup() before connecting to WiFi.
void loadConfig();

// Persist the current g_config to NVS.
void saveConfig();

// Reset g_config to the compile-time defaults and persist to NVS.
void resetConfig();

// Returns true when night mode is enabled AND the current local time falls
// inside the configured night window.  Returns false if NTP has not yet synced
// (wall clock < year 2000) so night mode is never triggered on a cold boot.
bool isNightActive();
