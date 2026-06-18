#pragma once
/*
Purpose: Adapt OpenSkyFetcher::fetchFlightRoute() as a BaseFlightFetcher so it
can slot into the FallbackFlightFetcher chain as a third-tier route source.

Only fills origin/destination ICAO and IATA codes — registration, aircraft type,
and operator fields are left empty (those come from hexdb in the primary tier).
IATA codes are resolved from the local PROGMEM table (IcaoIataTable.h) rather
than making additional network calls.

Requires:
  - OpenSky OAuth credentials configured (opensky_client_id / opensky_client_secret)
  - NTP synced (time(nullptr) > 0) so the 24-hour flight history window is correct
*/

#include "interfaces/BaseFlightFetcher.h"
#include "adapters/OpenSkyFetcher.h"
#include "adapters/FlightWallFetcher.h"
#include "config/IcaoIataTable.h"
#include "models/FlightInfo.h"
#include "utils/TelnetLogger.h"

class OpenSkyRouteFetcher : public BaseFlightFetcher
{
public:
    explicit OpenSkyRouteFetcher(OpenSkyFetcher &sky) : _sky(sky) {}

    bool fetchFlightInfo(const String &callsign,
                         const String &icao24,
                         FlightInfo   &outInfo) override
    {
        if (icao24.length() == 0)
            return false;

        String originIcao, destIcao;
        if (!_sky.fetchFlightRoute(icao24, originIcao, destIcao))
            return false;

        outInfo.origin.code_icao      = originIcao;
        outInfo.destination.code_icao = destIcao;

        // Resolve IATA codes from flash — no extra network calls needed.
        char iata[4];
        if (originIcao.length() == 4)
        {
            if (icaoToIata(originIcao.c_str(), iata))
                outInfo.origin.code_iata = String(iata);
        }
        if (destIcao.length() == 4)
        {
            if (icaoToIata(destIcao.c_str(), iata))
                outInfo.destination.code_iata = String(iata);
        }

        // Extract the ICAO airline code from the callsign prefix (letters before
        // the first digit: "MAY1234" → "MAY") and look up the full name from the
        // FlightWall CDN.  This fills the gap that the OpenSky flights endpoint
        // doesn't provide airline identity data.
        if (outInfo.operator_icao.length() == 0)
        {
            String prefix;
            for (int i = 0; i < (int)callsign.length() && i < 4; ++i)
            {
                if (isdigit((unsigned char)callsign[i])) break;
                prefix += callsign[i];
            }
            if (prefix.length() >= 2)
            {
                outInfo.operator_icao = prefix;
                if (outInfo.airline_display_name_full.length() == 0)
                {
                    FlightWallFetcher fw;
                    fw.getAirlineName(prefix, outInfo.airline_display_name_full);
                }
            }
        }

        Log.printf("OpenSkyRouteFetcher: %s  %s(%s)→%s(%s)\n",
                   callsign.c_str(),
                   outInfo.origin.code_iata.length()      ? outInfo.origin.code_iata.c_str()
                                                          : outInfo.origin.code_icao.c_str(),
                   outInfo.origin.code_icao.c_str(),
                   outInfo.destination.code_iata.length() ? outInfo.destination.code_iata.c_str()
                                                          : outInfo.destination.code_icao.c_str(),
                   outInfo.destination.code_icao.c_str());

        return outInfo.origin.code_icao.length() > 0;
    }

private:
    OpenSkyFetcher &_sky;
};
