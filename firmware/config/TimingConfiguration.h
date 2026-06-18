#pragma once

#include <Arduino.h>

namespace TimingConfiguration
{
    // Fetch cadence when using OpenSky/AeroAPI — limited by monthly API quotas.
    static const uint32_t FETCH_INTERVAL_SECONDS = 120; // seconds

    // Fetch cadence when a local ADS-B receiver is the active source.
    // No rate-limit concerns; keep this tight for fresh position data.
    static const uint32_t LOCAL_FETCH_INTERVAL_SECONDS = 10; // seconds

    // HTTP timeouts — local LAN can afford a tight deadline; internet APIs need more slack.
    static const uint32_t LOCAL_ADSB_TIMEOUT_MS =  5000;  // tar1090 on LAN
    static const uint32_t AEROAPI_TIMEOUT_MS    =  8000;  // FlightAware AeroAPI
    static const uint32_t OPENSKY_TIMEOUT_MS    = 15000;  // OpenSky OAuth token + state vectors

    // Display cycling configuration
    static const uint32_t DISPLAY_CYCLE_SECONDS = 30; // seconds per flight when multiple flights

    // How long to cache a *successful* AeroAPI enrichment (route, airline, aircraft, logos).
    // Telemetry is always refreshed from OpenSky regardless of this setting.
    static const uint32_t AEROAPI_CACHE_TTL_SECONDS = 1800; // 30 minutes

    // How long to suppress retries for a callsign that returned no route data
    // (GA aircraft, 429s, etc.).  Short enough to recover from transient errors.
    static const uint32_t AEROAPI_FAIL_CACHE_TTL_SECONDS = 300; // 5 minutes
}
