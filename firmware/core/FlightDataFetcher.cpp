/*
Purpose: Orchestrate fetching and enrichment of flight data for display.
Flow:
1) Use BaseStateVectorFetcher to fetch nearby state vectors by geo filter.
2) For every callsign, fetch enrichment data (hexdb primary, AeroAPI fallback).
3) Filter: discard flights without both origin and destination (GA, unidentified).
4) Sort by distance (nearest first); optionally trim to nearest only.
Output: Returns displayable flight count; fills outStates/outFlights.
*/
#include <algorithm>
#include "core/FlightDataFetcher.h"
#include "config/RuntimeConfig.h"
#include "adapters/FlightWallFetcher.h"
#include "utils/TelnetLogger.h"

FlightDataFetcher::FlightDataFetcher(BaseStateVectorFetcher *stateFetcher,
                                     BaseFlightFetcher      *flightFetcher,
                                     BaseLogoStore          *logoStore)
    : _stateFetcher(stateFetcher), _flightFetcher(flightFetcher), _logoStore(logoStore) {}

size_t FlightDataFetcher::fetchFlights(std::vector<StateVector> &outStates,
                                       std::vector<FlightInfo> &outFlights)
{
    outStates.clear();
    outFlights.clear();

    bool ok = _stateFetcher->fetchStateVectors(
        g_config.center_lat,
        g_config.center_lon,
        g_config.radius_km,
        outStates);
    if (!ok)
        return 0;

    // Evict stale cache entries so RAM doesn't accumulate across long uptimes
    const unsigned long nowMs = millis();
    for (auto it = _flightCache.begin(); it != _flightCache.end(); )
        it = (nowMs >= it->second.expiryMs) ? _flightCache.erase(it) : std::next(it);

    const unsigned long cacheTtlMs =
        (unsigned long)g_config.aeroapi_cache_ttl_seconds * 1000UL;

    Log.printf("FlightDataFetcher: free heap: %u B  max block: %u B\n",
               (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    // Process nearest aircraft first so hexdb budget goes to the most likely
    // display candidates.
    std::sort(outStates.begin(), outStates.end(),
        [](const StateVector &a, const StateVector &b) {
            return a.distance_km < b.distance_km;
        });

    // Returns true for ICAO commercial flight numbers: 2-3 letter airline code
    // (e.g. BAW, EZY, AA) immediately followed by a digit at position 2 or 3.
    // Filters out registrations (GBIZG, N1234X) and all-alpha squawks that
    // hexdb will never have route data for.
    auto isCommercialCallsign = [](const String &cs) -> bool {
        if (cs.length() < 3) return false;
        for (int i = 0; i < (int)cs.length(); ++i) {
            if (isdigit((unsigned char)cs[i]))
                return i >= 2 && i <= 3;
            if (i >= 3) break;
        }
        return false;
    };

    FlightWallFetcher fw;
    const size_t totalStates = outStates.size();
    size_t stateIdx = 0;
    for (const StateVector &s : outStates)
    {
        ++stateIdx;
        if (s.callsign.length() == 0)
            continue;

        if (!isCommercialCallsign(s.callsign))
        {
            Log.printf("FlightDataFetcher: [%u/%u] skipping %s (non-commercial)\n",
                       stateIdx, totalStates, s.callsign.c_str());
            continue;
        }

        FlightInfo info;

        auto it = _flightCache.find(s.callsign);
        if (it != _flightCache.end() && nowMs < it->second.expiryMs)
        {
            // Cache hit — reuse static enrichment data (logos are NOT cached,
            // see below — they are reloaded from LittleFS on every cycle)
            info = it->second.info;
            Log.printf("FlightDataFetcher: [%u/%u] cache hit for %s\n",
                       stateIdx, totalStates, s.callsign.c_str());
        }
        else
        {
            // Cache miss — fetch from hexdb (primary) / AeroAPI (fallback)
            Log.printf("FlightDataFetcher: [%u/%u] fetching %s\n",
                       stateIdx, totalStates, s.callsign.c_str());
            const bool fetchOk = _flightFetcher->fetchFlightInfo(s.callsign, s.icao24, info);

            if (info.ident.length() == 0)
                info.ident = s.callsign;

            // Resolve human-readable aircraft type name (e.g. "A320neo") from CDN.
            // Airline name and airport IATA codes come from hexdb directly.
            if (info.aircraft_code.length())
            {
                String aircraftShort, aircraftFull;
                if (fw.getAircraftName(info.aircraft_code, aircraftShort, aircraftFull))
                    if (aircraftShort.length())
                        info.aircraft_display_name_short = aircraftShort;
            }

            // Only cache when AeroAPI responded successfully.  Transient failures
            // (network error, timeout, truncated JSON) are NOT cached so the next
            // cycle retries the flight fresh.  A successful 200 with no route data
            // (GA / unknown) uses the shorter fail-TTL to avoid repeated lookups.
            if (fetchOk)
            {
                const bool hasRoute = info.origin.code_icao.length() > 0 &&
                                      info.destination.code_icao.length() > 0;
                const unsigned long ttlMs = hasRoute
                    ? cacheTtlMs
                    : (unsigned long)g_config.aeroapi_fail_cache_ttl_seconds * 1000UL;
                CachedFlightEntry entry;
                entry.info     = info;       // airline_logo_rgb565 is empty here — good
                entry.expiryMs = nowMs + ttlMs;
                _flightCache[s.callsign] = entry;
            }
            else
            {
                Log.printf("FlightDataFetcher: transient error for %s — skipping cache, will retry\n",
                           s.callsign.c_str());
            }
        }

        // Load logo fresh from LittleFS on every cycle (cache hit or miss).
        // This keeps logo pixels out of the heap-resident cache and avoids the
        // fragmentation that causes MBEDTLS_ERR_SSL_ALLOC_FAILED (-32512).
        if (_logoStore && info.operator_icao.length())
            _logoStore->getAirlineLogo(info.operator_icao, info.airline_logo_rgb565);

        // Always apply live telemetry from the current StateVector
        if (!isnan(s.baro_altitude)) info.baro_altitude  = s.baro_altitude * 3.28084;
        if (!isnan(s.velocity))      info.velocity       = s.velocity      * 1.94384;
        if (!isnan(s.heading))       info.heading        = s.heading;
        if (!isnan(s.vertical_rate)) info.vertical_rate  = s.vertical_rate * 196.85;
        info.distance_km = s.distance_km;
        info.bearing_deg = s.bearing_deg;

        // Apply local ADS-B enrichment as fallback — only fills gaps left by
        // AeroAPI (or when AeroAPI is not configured / unreachable).
        // AeroAPI values always take priority since they are more authoritative.
        if (info.registration.length() == 0 && s.registration.length() > 0)
            info.registration = s.registration;
        if (info.aircraft_code.length() == 0 && s.aircraft_type.length() > 0)
            info.aircraft_code = s.aircraft_type;

        outFlights.push_back(info);

        // Early exit when showing only the nearest flight — stop as soon as we
        // have one routed result so distant aircraft don't consume hexdb calls.
        if (g_config.display_nearest_only)
        {
            const bool hasRoute = info.origin.code_icao.length() > 0 &&
                                  info.destination.code_icao.length() > 0;
            if (hasRoute)
            {
                Log.printf("FlightDataFetcher: nearest routed flight (%s) — stopping early\n",
                           s.callsign.c_str());
                break;
            }
        }
    }

    // Filter: discard flights without a complete route (general aviation, unidentified)
    const size_t beforeFilter = outFlights.size();
    outFlights.erase(
        std::remove_if(outFlights.begin(), outFlights.end(),
            [](const FlightInfo &f) {
                return f.origin.code_icao.length() == 0 ||
                       f.destination.code_icao.length() == 0;
            }),
        outFlights.end());
    Log.printf("FlightDataFetcher: %u flights before filter, %u after\n",
               (unsigned)beforeFilter, (unsigned)outFlights.size());

    // Sort nearest first
    std::sort(outFlights.begin(), outFlights.end(),
        [](const FlightInfo &a, const FlightInfo &b) {
            return a.distance_km < b.distance_km;
        });

    // Trim to nearest if configured
    if (g_config.display_nearest_only && !outFlights.empty())
        outFlights.resize(1);

    return outFlights.size();
}
