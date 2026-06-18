/*
Purpose: Retrieve detailed flight metadata from AeroAPI over HTTPS.
Responsibilities:
- Perform authenticated GET to /flights/{ident} using API key.
- Parse minimal fields into FlightInfo (ident/operator/aircraft and ICAO codes).
- Handle TLS (optionally insecure for dev) and JSON errors gracefully.
Input: flight ident (e.g., callsign).
Output: Populates FlightInfo on success and returns true.
*/
#include "adapters/AeroAPIFetcher.h"
#include "config/RuntimeConfig.h"
#include "config/TimingConfiguration.h"
#include "utils/TelnetLogger.h"

// Returns the active AeroAPI key: NVS runtime value (g_config) takes precedence
// over the compile-time constant so the web UI can update it without reflashing.
static const char *aeroKey()
{
    return (strlen(g_config.aeroapi_key) > 0)
        ? g_config.aeroapi_key
        : APIConfiguration::AEROAPI_KEY;
}

static String safeGetString(JsonVariant v, const char *key)
{
    if (!v[key].is<const char *>())
        return String("");
    return String(v[key].as<const char *>());
}

bool AeroAPIFetcher::fetchFlightInfo(const String &flightIdent,
                                     const String & /*icao24*/,
                                     FlightInfo   &outInfo)
{
    if (strlen(aeroKey()) == 0)
    {
        Log.println("AeroAPIFetcher: No API key configured");
        return false;
    }

    // Configure TLS once; the client object persists across calls so the
    // ESP32 HTTP stack can reuse the established TLS connection (keep-alive).
    if (!_configured)
    {
        if (APIConfiguration::AEROAPI_INSECURE_TLS)
            _client.setInsecure();
        _configured = true;
    }

    HTTPClient http;
    String url = String(APIConfiguration::AEROAPI_BASE_URL) + "/flights/" + flightIdent;
    http.begin(_client, url);
    http.setTimeout(TimingConfiguration::AEROAPI_TIMEOUT_MS);
    http.addHeader("x-apikey", aeroKey());
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != 200)
    {
        Log.printf("AeroAPIFetcher: HTTP request failed with code %d for flight %s\n", code, flightIdent.c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Filter to only the fields we use — avoids holding the full response in the parsed doc
    JsonDocument filter;
    filter["flights"][0]["ident"] = true;
    filter["flights"][0]["ident_icao"] = true;
    filter["flights"][0]["ident_iata"] = true;
    filter["flights"][0]["operator"] = true;
    filter["flights"][0]["operator_icao"] = true;
    filter["flights"][0]["operator_iata"] = true;
    filter["flights"][0]["aircraft_type"] = true;
    filter["flights"][0]["registration"] = true;
    filter["flights"][0]["origin"]["code_icao"] = true;
    filter["flights"][0]["origin"]["code_iata"] = true;
    filter["flights"][0]["destination"]["code_icao"] = true;
    filter["flights"][0]["destination"]["code_iata"] = true;
    filter["flights"][0]["last_position"]["altitude"] = true;
    filter["flights"][0]["last_position"]["groundspeed"] = true;
    filter["flights"][0]["last_position"]["heading"] = true;
    filter["flights"][0]["last_position"]["vertical_rate"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err)
    {
        Log.printf("AeroAPIFetcher: JSON parsing failed for flight %s: %s\n", flightIdent.c_str(), err.c_str());
        return false;
    }

    JsonArray flights = doc["flights"].as<JsonArray>();
    if (flights.isNull() || flights.size() == 0)
    {
        Log.printf("AeroAPIFetcher: No flights found in response for %s\n", flightIdent.c_str());
        return false;
    }

    JsonObject f = flights[0].as<JsonObject>();
    outInfo.ident = safeGetString(f, "ident");
    outInfo.ident_icao = safeGetString(f, "ident_icao");
    outInfo.ident_iata = safeGetString(f, "ident_iata");
    outInfo.operator_code = safeGetString(f, "operator");
    outInfo.operator_icao = safeGetString(f, "operator_icao");
    outInfo.operator_iata = safeGetString(f, "operator_iata");
    outInfo.aircraft_code = safeGetString(f, "aircraft_type");
    outInfo.registration  = safeGetString(f, "registration");

    // The last_position object contains live position data
    if (f["last_position"].is<JsonObject>())
    {
        JsonObject pos = f["last_position"].as<JsonObject>();
        if (!pos["altitude"].isNull())
            outInfo.baro_altitude = pos["altitude"].as<double>();
        if (!pos["groundspeed"].isNull())
            outInfo.velocity = pos["groundspeed"].as<double>();
        if (!pos["heading"].isNull())
            outInfo.heading = pos["heading"].as<double>();
        if (!pos["vertical_rate"].isNull())
            outInfo.vertical_rate = pos["vertical_rate"].as<double>();
    }

    if (f["origin"].is<JsonObject>())
    {
        JsonObject o = f["origin"].as<JsonObject>();
        outInfo.origin.code_icao = safeGetString(o, "code_icao");
        outInfo.origin.code_iata = safeGetString(o, "code_iata");
    }

    if (f["destination"].is<JsonObject>())
    {
        JsonObject d = f["destination"].as<JsonObject>();
        outInfo.destination.code_icao = safeGetString(d, "code_icao");
        outInfo.destination.code_iata = safeGetString(d, "code_iata");
    }

    return true;
}
