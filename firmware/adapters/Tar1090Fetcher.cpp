/*
Purpose: Fetch ADS-B state vectors from a local tar1090 receiver over HTTP.
Responsibilities:
- GET http://<g_config.tar1090_host>/tar1090/data/aircraft.json
- Discard stale entries (seen_pos > 30 s) and entries without a position fix.
- Filter to radius, compute distance/bearing, fill StateVector vector.
- Convert tar1090 native units (ft, kts, ft/min) to StateVector SI convention.
Returns false (signals fallback) if host is unconfigured or the request fails.
*/
#include "adapters/Tar1090Fetcher.h"
#include "config/RuntimeConfig.h"
#include "config/TimingConfiguration.h"
#include "utils/TelnetLogger.h"

bool Tar1090Fetcher::fetchStateVectors(double centerLat,
                                        double centerLon,
                                        double radiusKm,
                                        std::vector<StateVector> &outStateVectors)
{
    if (strlen(g_config.tar1090_host) == 0)
        return false;   // not configured — caller should fall back to OpenSky

    String url = String("http://") + g_config.tar1090_host + "/tar1090/data/aircraft.json";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(TimingConfiguration::LOCAL_ADSB_TIMEOUT_MS);

    int code = http.GET();
    if (code != 200)
    {
        Log.printf("Tar1090Fetcher: HTTP %d from %s\n", code, url.c_str());
        http.end();
        return false;
    }

    // Filter to only the fields we need — keeps the parsed doc small even when
    // hundreds of aircraft are tracked.
    JsonDocument filter;
    filter["aircraft"][0]["hex"]       = true;
    filter["aircraft"][0]["flight"]    = true;
    filter["aircraft"][0]["r"]         = true;   // registration / tail number
    filter["aircraft"][0]["t"]         = true;   // ICAO aircraft type code
    filter["aircraft"][0]["lat"]       = true;
    filter["aircraft"][0]["lon"]       = true;
    filter["aircraft"][0]["alt_baro"]  = true;
    filter["aircraft"][0]["gs"]        = true;
    filter["aircraft"][0]["track"]     = true;
    filter["aircraft"][0]["baro_rate"] = true;
    filter["aircraft"][0]["seen_pos"]  = true;
    filter["aircraft"][0]["on_ground"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err)
    {
        Log.printf("Tar1090Fetcher: JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray aircraft = doc["aircraft"].as<JsonArray>();
    if (aircraft.isNull())
    {
        Log.println("Tar1090Fetcher: no aircraft array in response");
        return false;
    }

    int tracked = 0, inRadius = 0;
    for (JsonObject a : aircraft)
    {
        ++tracked;

        // Discard entries with stale position (> 30 s since last position fix)
        float seenPos = a["seen_pos"] | 999.0f;
        if (seenPos > 30.0f)
            continue;

        if (a["lat"].isNull() || a["lon"].isNull())
            continue;

        double lat = a["lat"].as<double>();
        double lon = a["lon"].as<double>();

        double dist = haversineKm(centerLat, centerLon, lat, lon);
        if (dist > radiusKm)
            continue;

        ++inRadius;

        StateVector s;
        s.icao24 = a["hex"] | "";

        String cs = a["flight"] | "";
        cs.trim();
        s.callsign = cs;

        // Local database enrichment — not always present, especially for
        // aircraft that haven't been seen before or military/private registrations.
        String reg = a["r"] | "";
        reg.trim();
        s.registration = reg;

        String typ = a["t"] | "";
        typ.trim();
        s.aircraft_type = typ;

        s.lat       = lat;
        s.lon       = lon;
        s.on_ground = a["on_ground"] | false;

        // tar1090 native units → StateVector SI units (matches OpenSky convention).
        // FlightDataFetcher converts back to imperial for display, so we must
        // normalise here rather than passing raw tar1090 values.
        if (!a["alt_baro"].isNull())
            s.baro_altitude = a["alt_baro"].as<double>() / 3.28084;   // ft  → m
        if (!a["gs"].isNull())
            s.velocity      = a["gs"].as<double>()       / 1.94384;   // kts → m/s
        if (!a["track"].isNull())
            s.heading       = a["track"].as<double>();                  // degrees unchanged
        if (!a["baro_rate"].isNull())
            s.vertical_rate = a["baro_rate"].as<double>() / 196.85;   // ft/min → m/s

        s.distance_km = dist;
        s.bearing_deg = computeBearingDeg(centerLat, centerLon, lat, lon);

        outStateVectors.push_back(s);
    }

    Log.printf("Tar1090Fetcher: %d tracked, %d within %.0f km\n",
               tracked, inRadius, radiusKm);
    return true;
}
