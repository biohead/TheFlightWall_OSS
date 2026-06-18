/*
Purpose: Look up human-friendly names and airport codes from the FlightWall CDN.
Responsibilities:
- HTTPS GET small JSON blobs for airline names, aircraft type names, and airport IATA codes.
- Called by FlightDataFetcher on AeroAPI cache misses; results are then cached for 30 min.
Inputs: Airline ICAO code, aircraft ICAO type code, or airport ICAO code.
Outputs: Display name / IATA code strings via out parameters.
*/
#include "adapters/FlightWallFetcher.h"
#include "utils/TelnetLogger.h"

bool FlightWallFetcher::httpGetJson(const String &url, String &outPayload)
{
    WiFiClientSecure client;
    if (APIConfiguration::FLIGHTWALL_INSECURE_TLS)
    {
        client.setInsecure();
    }

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);  // 8 s — prevents hangs on CDN timeouts
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != 200)
    {
        http.end();
        return false;
    }
    outPayload = http.getString();
    http.end();
    return true;
}

bool FlightWallFetcher::getAirlineName(const String &airlineIcao, String &outDisplayNameFull)
{
    outDisplayNameFull = String("");
    if (airlineIcao.length() == 0)
        return false;

    String url = String(APIConfiguration::FLIGHTWALL_CDN_BASE_URL) + "/oss/lookup/airline/" + airlineIcao + ".json";
    String payload;
    if (!httpGetJson(url, payload))
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
        return false;

    if (doc["display_name_full"].is<const char *>())
    {
        outDisplayNameFull = String(doc["display_name_full"].as<const char *>());
        return outDisplayNameFull.length() > 0;
    }
    return false;
}

bool FlightWallFetcher::getAircraftName(const String &aircraftIcao,
                                        String &outDisplayNameShort,
                                        String &outDisplayNameFull)
{
    outDisplayNameShort = String("");
    outDisplayNameFull = String("");
    if (aircraftIcao.length() == 0)
        return false;

    String url = String(APIConfiguration::FLIGHTWALL_CDN_BASE_URL) + "/oss/lookup/aircraft/" + aircraftIcao + ".json";
    String payload;
    if (!httpGetJson(url, payload))
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
        return false;

    if (doc["display_name_short"].is<const char *>())
        outDisplayNameShort = String(doc["display_name_short"].as<const char *>());
    if (doc["display_name_full"].is<const char *>())
        outDisplayNameFull = String(doc["display_name_full"].as<const char *>());

    return outDisplayNameShort.length() > 0 || outDisplayNameFull.length() > 0;
}

bool FlightWallFetcher::getAirportIata(const String &airportIcao, String &outIata)
{
    outIata = String("");
    if (airportIcao.length() == 0)
        return false;

    String url = String(APIConfiguration::FLIGHTWALL_CDN_BASE_URL) +
                 "/oss/lookup/airport/" + airportIcao + ".json";
    String payload;
    if (!httpGetJson(url, payload))
        return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload))
        return false;

    if (doc["code_iata"].is<const char *>())
    {
        outIata = String(doc["code_iata"].as<const char *>());
        return outIata.length() > 0;
    }
    return false;
}

