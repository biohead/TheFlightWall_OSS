#pragma once
/*
Purpose: Try the primary flight fetcher (hexdb.io); fall back to the secondary
(AeroAPI) only when the primary returns without a complete route.

A "complete route" means both origin and destination ICAO codes are non-empty.
If hexdb knows the route → AeroAPI is never called (zero paid API usage).
If hexdb has no route    → AeroAPI is attempted and its fields are merged in:
    • Route / idents / operator codes → AeroAPI wins (it's more authoritative).
    • Registration / aircraft type   → AeroAPI fills any gap hexdb left empty.
This merge means a GA aircraft that hexdb knows but has no route data will still
benefit from the registration/type hexdb already fetched, without an AeroAPI call
(since that flight will be filtered out by FlightDataFetcher anyway).

Usage in main.cpp:
    static HexDbFetcher           g_hexDb;
    static AeroAPIFetcher         g_aeroApi;
    static FallbackFlightFetcher  g_flightFetcher(&g_hexDb, &g_aeroApi);
    g_fetcher = new FlightDataFetcher(&g_stateFetcher, &g_flightFetcher, &g_logoStore);
*/

#include "interfaces/BaseFlightFetcher.h"
#include "models/FlightInfo.h"
#include "utils/TelnetLogger.h"

class FallbackFlightFetcher : public BaseFlightFetcher
{
public:
    FallbackFlightFetcher(BaseFlightFetcher *primary,
                          BaseFlightFetcher *secondary)
        : _primary(primary), _secondary(secondary) {}

    bool fetchFlightInfo(const String &callsign,
                         const String &icao24,
                         FlightInfo   &outInfo) override
    {
        // ── Primary (hexdb.io) ─────────────────────────────────────────────
        bool primaryOk = _primary->fetchFlightInfo(callsign, icao24, outInfo);

        const bool hasRoute = outInfo.origin.code_icao.length()      > 0 &&
                              outInfo.destination.code_icao.length() > 0;
        if (hasRoute)
            return true;   // complete — secondary never touched

        // ── Fallback (AeroAPI) ─────────────────────────────────────────────
        Log.printf("FallbackFlightFetcher: %s — no route from hexdb, trying AeroAPI\n",
                   callsign.c_str());

        FlightInfo secondary;
        bool secondaryOk = _secondary->fetchFlightInfo(callsign, icao24, secondary);

        if (secondaryOk)
        {
            // Merge: AeroAPI wins on authoritative fields; hexdb data already in
            // outInfo fills any gap AeroAPI leaves empty.
            if (secondary.ident.length())         outInfo.ident         = secondary.ident;
            if (secondary.ident_icao.length())    outInfo.ident_icao    = secondary.ident_icao;
            if (secondary.ident_iata.length())    outInfo.ident_iata    = secondary.ident_iata;
            if (secondary.operator_code.length()) outInfo.operator_code = secondary.operator_code;
            if (secondary.operator_icao.length()) outInfo.operator_icao = secondary.operator_icao;
            if (secondary.operator_iata.length()) outInfo.operator_iata = secondary.operator_iata;
            if (secondary.origin.code_icao.length())
                outInfo.origin = secondary.origin;
            if (secondary.destination.code_icao.length())
                outInfo.destination = secondary.destination;
            // Backfill display name — not set by every source (hexdb uses RegisteredOwners;
            // AeroAPI uses operator long name); keep whichever the primary already found.
            if (outInfo.airline_display_name_full.length() == 0 && secondary.airline_display_name_full.length())
                outInfo.airline_display_name_full = secondary.airline_display_name_full;
            // Only backfill registration/type from AeroAPI if hexdb left them empty
            if (outInfo.registration.length()  == 0) outInfo.registration  = secondary.registration;
            if (outInfo.aircraft_code.length() == 0) outInfo.aircraft_code = secondary.aircraft_code;
        }

        return primaryOk || secondaryOk;
    }

    void setPrimary(BaseFlightFetcher *p)   { _primary   = p; }
    void setSecondary(BaseFlightFetcher *s) { _secondary = s; }

private:
    BaseFlightFetcher *_primary;
    BaseFlightFetcher *_secondary;
};
