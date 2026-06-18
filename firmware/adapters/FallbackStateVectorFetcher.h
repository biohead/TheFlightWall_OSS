#pragma once

/*
Purpose: Try a primary BaseStateVectorFetcher; on failure transparently fall
back to a secondary fetcher.

A "failure" is defined as the primary returning false (HTTP error, parse error,
host unconfigured).  A successful response that happens to contain zero aircraft
within the radius is NOT a failure — the device genuinely sees nothing nearby
and there is no reason to burden the secondary (OpenSky) unnecessarily.

Usage:
    static Tar1090Fetcher             g_tar1090;
    static OpenSkyFetcher             g_openSky;
    static FallbackStateVectorFetcher g_stateFetcher(&g_tar1090, &g_openSky);
    g_fetcher = new FlightDataFetcher(&g_stateFetcher, ...);
*/

#include "interfaces/BaseStateVectorFetcher.h"
#include "utils/TelnetLogger.h"

class FallbackStateVectorFetcher : public BaseStateVectorFetcher
{
public:
    FallbackStateVectorFetcher(BaseStateVectorFetcher *primary,
                                BaseStateVectorFetcher *secondary)
        : _primary(primary), _secondary(secondary) {}

    bool fetchStateVectors(double centerLat,
                           double centerLon,
                           double radiusKm,
                           std::vector<StateVector> &outStateVectors) override
    {
        if (_primary->fetchStateVectors(centerLat, centerLon, radiusKm, outStateVectors))
        {
            _usedFallback = false;
            return true;
        }

        // Primary unavailable (host not configured, network error, parse failure).
        Log.println("FallbackStateVectorFetcher: primary unavailable — falling back to OpenSky");
        outStateVectors.clear();    // discard any partial results
        _usedFallback = true;
        return _secondary->fetchStateVectors(centerLat, centerLon, radiusKm, outStateVectors);
    }

    /// True when the most recent fetch used the secondary (OpenSky) source.
    /// main.cpp uses this to select the appropriate fetch interval.
    bool usedFallback() const { return _usedFallback; }

private:
    BaseStateVectorFetcher *_primary;
    BaseStateVectorFetcher *_secondary;
    bool _usedFallback = true;  // start conservative — assume API until first local success
};
