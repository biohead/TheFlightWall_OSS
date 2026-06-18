/*
Purpose: Flight enrichment via hexdb.io (free, no API key).
Flow per uncached callsign:
  1. Aircraft lookup by ICAO24 hex  → registration, type, operator
  2. Route lookup by callsign       → origin/destination ICAO pair
  3. IATA resolution for each airport (cached — zero extra calls after first cycle)
Returns true when at least one HTTP call succeeded so FlightDataFetcher knows
to cache the result (with the appropriate success or fail-TTL).
*/
#include "adapters/HexDbFetcher.h"
#include "config/TimingConfiguration.h"
#include "utils/TelnetLogger.h"
#include <ArduinoJson.h>

static constexpr const char *kBase = "https://hexdb.io/api/v1";

// ---------------------------------------------------------------------------
bool HexDbFetcher::httpGet(const String &url, String &outPayload)
{
    // Circuit breaker: stop all requests while in blackout window.
    if (_blackoutUntilMs && millis() < _blackoutUntilMs)
    {
        Log.printf("HexDbFetcher: circuit open, skipping %s (%.0f s remaining)\n",
                   url.c_str(),
                   (_blackoutUntilMs - millis()) / 1000.0f);
        return false;
    }
    _blackoutUntilMs = 0;

    if (!_configured)
    {
        _client.setInsecure();
        _configured = true;
    }

    HTTPClient http;
    http.begin(_client, url);
    http.setTimeout(TimingConfiguration::AEROAPI_TIMEOUT_MS);
    http.addHeader("Accept", "application/json");

    int code = http.GET();

    // -1 on the first try usually means a stale keep-alive socket from a
    // previous cycle.  Drop the client and retry once with a fresh connection.
    if (code == -1)
    {
        http.end();
        _client.stop();
        _configured = false;

        _client.setInsecure();
        _configured = true;
        http.begin(_client, url);
        http.setTimeout(TimingConfiguration::AEROAPI_TIMEOUT_MS);
        http.addHeader("Accept", "application/json");
        code = http.GET();
    }

    if (code != 200)
    {
        Log.printf("HexDbFetcher: HTTP %d for %s\n", code, url.c_str());
        http.end();
        _consecutiveFails++;
        if (_consecutiveFails >= kFailThreshold)
        {
            _blackoutUntilMs = millis() + kBlackoutMs;
            Log.printf("HexDbFetcher: %u consecutive failures — circuit open for %u s\n",
                       _consecutiveFails, kBlackoutMs / 1000);
            _consecutiveFails = 0;
            _client.stop();
            _configured = false;
        }
        return false;
    }

    _consecutiveFails = 0;
    outPayload = http.getString();
    http.end();
    return true;
}

// ---------------------------------------------------------------------------
bool HexDbFetcher::resolveIata(const String &icao, String &outIata)
{
    if (icao.length() == 0)
        return false;

    // Cache hit — return immediately (even for known-empty entries)
    auto it = _iataCache.find(icao);
    if (it != _iataCache.end())
    {
        outIata = it->second;
        return outIata.length() > 0;
    }

    // Cache miss — query hexdb
    // Response: { "iata": "LHR", "icao": "EGLL", "airport": "Heathrow", ... }
    String payload;
    if (!httpGet(String(kBase) + "/airport/icao/" + icao, payload))
    {
        _iataCache[icao] = "";   // cache the miss so we never retry this airport
        return false;
    }

    JsonDocument doc;
    String iata;
    if (!deserializeJson(doc, payload) && doc["iata"].is<const char*>())
        iata = doc["iata"].as<const char*>();

    _iataCache[icao] = iata;     // cache hit OR known-empty
    Log.printf("HexDbFetcher: airport %s → IATA %s\n",
               icao.c_str(), iata.length() ? iata.c_str() : "(none)");

    outIata = iata;
    return iata.length() > 0;
}

// ---------------------------------------------------------------------------
bool HexDbFetcher::fetchFlightInfo(const String &callsign,
                                   const String &icao24,
                                   FlightInfo   &outInfo)
{
    outInfo.ident = callsign;
    bool ok = false;

    // ── 1. Aircraft static data via ICAO24 hex address ───────────────────────
    // Returns: Registration, ICAOTypeCode, Type, RegisteredOwners, OperatorFlagCode
    if (icao24.length() > 0)
    {
        String hex = icao24;
        hex.toUpperCase();

        String payload;
        if (httpGet(String(kBase) + "/aircraft/" + hex, payload))
        {
            JsonDocument doc;
            if (!deserializeJson(doc, payload))
            {
                auto safeStr = [&](const char *key) -> String {
                    return (doc[key].is<const char*>() &&
                            strlen(doc[key].as<const char*>()) > 0)
                        ? String(doc[key].as<const char*>()) : String();
                };

                outInfo.registration  = safeStr("Registration");
                outInfo.aircraft_code = safeStr("ICAOTypeCode");
                outInfo.operator_icao = safeStr("OperatorFlagCode");

                // RegisteredOwners is a fallback display name — FlightWallFetcher
                // will overwrite it with the CDN name if operator_icao resolves.
                String owners = safeStr("RegisteredOwners");
                if (owners.length())
                    outInfo.airline_display_name_full = owners;

                ok = true;
            }
        }
    }

    // ── 2. Route (origin → destination) via callsign ─────────────────────────
    // Response: { "flight": "EZY73CA", "route": "EGKK-EHAM", "updatetime": ... }
    if (callsign.length() > 0)
    {
        String payload;
        if (httpGet(String(kBase) + "/route/icao/" + callsign, payload))
        {
            ok = true;   // 200 → hexdb knows this callsign; cache the result
            JsonDocument doc;
            if (!deserializeJson(doc, payload) && doc["route"].is<const char*>())
            {
                String route = doc["route"].as<const char*>();
                int sep = route.indexOf('-');
                if (sep > 0 && sep < (int)route.length() - 1)
                {
                    outInfo.origin.code_icao      = route.substring(0, sep);
                    outInfo.destination.code_icao = route.substring(sep + 1);

                    // ── 3 & 4. Resolve IATA codes (cached after first lookup) ──
                    resolveIata(outInfo.origin.code_icao,      outInfo.origin.code_iata);
                    resolveIata(outInfo.destination.code_icao, outInfo.destination.code_iata);
                }
            }
        }
    }

    Log.printf("HexDbFetcher: %s  %s(%s)→%s(%s)  reg=%s type=%s op=%s\n",
               callsign.c_str(),
               outInfo.origin.code_iata.length()      ? outInfo.origin.code_iata.c_str()
                                                      : outInfo.origin.code_icao.c_str(),
               outInfo.origin.code_icao.c_str(),
               outInfo.destination.code_iata.length() ? outInfo.destination.code_iata.c_str()
                                                      : outInfo.destination.code_icao.c_str(),
               outInfo.destination.code_icao.c_str(),
               outInfo.registration.c_str(),
               outInfo.aircraft_code.c_str(),
               outInfo.operator_icao.c_str());

    return ok;
}
