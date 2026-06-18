#pragma once
/*
Purpose: Enrich flight data using the free hexdb.io API — no API key required.
Up to four lookups per newly-seen callsign:
  1. GET /api/v1/aircraft/{icao24}      → registration, ICAO type code, operator
  2. GET /api/v1/route/icao/{callsign}  → origin/destination ICAO pair ("EGKK-EHAM")
  3. GET /api/v1/airport/icao/{origin}  → IATA code for origin   (cached permanently)
  4. GET /api/v1/airport/icao/{dest}    → IATA code for dest     (cached permanently)

Airport IATA lookups are cached in _iataCache for the device's lifetime.  Airport
codes never change, so no TTL is needed.  After the first cycle every origin and
destination resolves instantly from the map — zero extra HTTP calls.

A persistent WiFiClientSecure is reused across all calls to hexdb.io within a
single fetch cycle so TLS is only negotiated once (same keep-alive trick as
AeroAPIFetcher, avoids repeated ~40 KB mbedTLS context allocations).
*/

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <map>
#include "interfaces/BaseFlightFetcher.h"

class HexDbFetcher : public BaseFlightFetcher
{
public:
    HexDbFetcher()  = default;
    ~HexDbFetcher() override = default;

    bool fetchFlightInfo(const String &callsign,
                         const String &icao24,
                         FlightInfo   &outInfo) override;

private:
    WiFiClientSecure         _client;
    bool                     _configured = false;

    // Circuit breaker: after kFailThreshold consecutive failures, pause all
    // hexdb requests for kBlackoutMs to let rate-limiting windows expire.
    static constexpr uint8_t  kFailThreshold = 3;
    static constexpr uint32_t kBlackoutMs    = 60000;
    uint8_t                   _consecutiveFails = 0;
    uint32_t                  _blackoutUntilMs  = 0;

    // ICAO → IATA airport code cache.  Stored as "" when the airport has no
    // IATA code (e.g. small GA fields) so we never retry a known miss.
    std::map<String, String> _iataCache;

    bool httpGet(const String &url, String &outPayload);

    // Looks up the IATA code for an ICAO airport code.  Returns true and fills
    // outIata when a code is found.  Result (including empty-string misses) is
    // stored in _iataCache so subsequent calls for the same airport are free.
    bool resolveIata(const String &icao, String &outIata);
};
