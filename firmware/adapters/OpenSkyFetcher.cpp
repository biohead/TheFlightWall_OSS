/*
Purpose: Fetch ADS-B state vectors from OpenSky Network (OAuth-protected API).
Responsibilities:
- Manage OAuth2 client_credentials token lifecycle with early refresh.
- Build geographic bounding box around a center point and query states/all.
- Parse JSON into StateVector objects and compute distance/bearing.
- Filter by radius and bearing using GeoUtils helpers.
Inputs: centerLat, centerLon, radiusKm, min/max bearing; APIConfiguration creds/URLs.
Outputs: Populates outStateVectors with filtered results (distance_km, bearing_deg set).
*/
#include "adapters/OpenSkyFetcher.h"
#include "config/RuntimeConfig.h"
#include "config/TimingConfiguration.h"
#include "utils/TelnetLogger.h"

// Runtime credential helpers — NVS values override compile-time constants.
static const char *oskyId()
{
    return (strlen(g_config.opensky_client_id) > 0)
        ? g_config.opensky_client_id
        : APIConfiguration::OPENSKY_CLIENT_ID;
}
static const char *oskySec()
{
    return (strlen(g_config.opensky_client_secret) > 0)
        ? g_config.opensky_client_secret
        : APIConfiguration::OPENSKY_CLIENT_SECRET;
}

static String urlEncodeForm(const String &value)
{
    String out;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < value.length(); ++i)
    {
        char c = value[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += c;
        }
        else if (c == ' ')
        {
            out += '+';
        }
        else
        {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

bool OpenSkyFetcher::ensureAccessToken(bool forceRefresh)
{
    const bool oauthConfigured = (strlen(oskyId()) > 0) && (strlen(oskySec()) > 0);
    if (!oauthConfigured)
    {
        Log.println("OpenSkyFetcher: OAuth credentials are required but not configured");
        return false;
    }

    unsigned long nowMs = millis();
    const unsigned long safetySkewMs = 60UL * 1000UL; // refresh 60s early
    if (!forceRefresh && m_accessToken.length() > 0 && nowMs + safetySkewMs < m_tokenExpiryMs)
    {
        Log.printf("OpenSkyFetcher: Using cached token. ms until refresh window: %ld\n",
                   (long)(m_tokenExpiryMs - safetySkewMs - nowMs));
        return true;
    }

    Log.println(forceRefresh ? "OpenSkyFetcher: Refreshing token (forced)" : "OpenSkyFetcher: Fetching new token");
    String newToken;
    unsigned long newExpiryMs = 0;
    if (!requestAccessToken(newToken, newExpiryMs))
    {
        Log.println("OpenSkyFetcher: Failed to obtain OAuth access token");
        return false;
    }

    m_accessToken = newToken;
    m_tokenExpiryMs = newExpiryMs;
    Log.printf("OpenSkyFetcher: Token cached. Expires at ms: %ld\n", (long)m_tokenExpiryMs);
    return true;
}

bool OpenSkyFetcher::ensureAuthenticated(bool forceRefresh)
{
    return ensureAccessToken(forceRefresh);
}

bool OpenSkyFetcher::requestAccessToken(String &outToken, unsigned long &outExpiryMs)
{
    if (strlen(oskyId()) == 0 || strlen(oskySec()) == 0)
    {
        Log.println("OpenSkyFetcher: OAuth credentials not configured");
        return false;
    }

    WiFiClientSecure client;
    if (APIConfiguration::OPENSKY_INSECURE_TLS)
        client.setInsecure();

    HTTPClient http;
    Log.printf("OpenSkyFetcher: Token URL: %s\n", APIConfiguration::OPENSKY_TOKEN_URL);
    http.begin(client, APIConfiguration::OPENSKY_TOKEN_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Accept", "application/json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    String body = String("grant_type=client_credentials&client_id=") + urlEncodeForm(String(oskyId())) +
                  "&client_secret=" + urlEncodeForm(String(oskySec()));

    // Debug: show request (without exposing secret)
    Log.printf("OpenSkyFetcher: Using client_id: %s\n", oskyId());
    Log.printf("OpenSkyFetcher: client_secret length: %d\n", (int)strlen(oskySec()));
    Log.printf("OpenSkyFetcher: POST body length: %d\n", (int)body.length());
    http.setTimeout(TimingConfiguration::OPENSKY_TIMEOUT_MS);

    int code = http.POST(body);
    String payload = http.getString();
    if (code != 200)
    {
        Log.printf("OpenSkyFetcher: Token request failed, code: %d\n", code);
        Log.printf("OpenSkyFetcher: Error payload: %s\n",
                   payload.length() > 0 ? payload.c_str() : "<empty>");
        http.end();
        return false;
    }
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Log.printf("OpenSkyFetcher: Token JSON parse error: %s\n", err.c_str());
        Log.printf("OpenSkyFetcher: Raw token response: %s\n", payload.c_str());
        return false;
    }

    String tokenStr = doc["access_token"].as<String>();
    int expiresIn = doc["expires_in"] | 1800; // seconds; default 30min
    if (tokenStr.length() == 0)
    {
        Log.println("OpenSkyFetcher: access_token missing in response");
        Log.printf("OpenSkyFetcher: Full response: %s\n", payload.c_str());
        if (doc.is<JsonObject>())
        {
            Log.println("OpenSkyFetcher: Response keys:");
            for (JsonPair kv : doc.as<JsonObject>())
                Log.printf(" - %s\n", kv.key().c_str());
        }
        return false;
    }

    outToken = tokenStr;
    outExpiryMs = millis() + (unsigned long)expiresIn * 1000UL;
    Log.printf("OpenSkyFetcher: Obtained access token, length: %d\n", (int)outToken.length());
    Log.printf("OpenSkyFetcher: Token expires in (s): %d\n", expiresIn);
    return true;
}

bool OpenSkyFetcher::fetchStateVectors(double centerLat,
                                       double centerLon,
                                       double radiusKm,
                                       std::vector<StateVector> &outStateVectors)
{
    // Ensure OAuth token if configured
    if (!ensureAccessToken(false))
    {
        Log.println("OpenSkyFetcher: ensureAccessToken failed before GET");
        return false;
    }

    double latMin, latMax, lonMin, lonMax;
    centeredBoundingBox(centerLat, centerLon, radiusKm, latMin, latMax, lonMin, lonMax);

    String url = String(APIConfiguration::OPENSKY_BASE_URL) + "/api/states/all?lamin=" + String(latMin, 6) +
                 "&lamax=" + String(latMax, 6) +
                 "&lomin=" + String(lonMin, 6) +
                 "&lomax=" + String(lonMax, 6);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(TimingConfiguration::OPENSKY_TIMEOUT_MS);
    // OAuth Bearer required
    http.addHeader("Authorization", String("Bearer ") + m_accessToken);

    int code = http.GET();
    if (code != 200)
    {
        bool attemptedRefresh = false;
        if (code == 401 && m_accessToken.length() > 0)
        {
            // Try refresh once
            http.end();
            if (ensureAccessToken(true))
            {
                HTTPClient retry;
                retry.begin(url);
                retry.addHeader("Authorization", String("Bearer ") + m_accessToken);
                code = retry.GET();
                if (code != 200)
                {
                    Log.printf("OpenSkyFetcher: HTTP retry failed with code: %d\n", code);
                    retry.end();
                    return false;
                }
                String payload = retry.getString();
                retry.end();

                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (err)
                {
                    Log.printf("OpenSkyFetcher: JSON deserialization error: %s\n", err.c_str());
                    return false;
                }

                JsonArray states = doc["states"].as<JsonArray>();
                if (states.isNull())
                {
                    return true; // no states is not an error
                }

                for (JsonVariant v : states)
                {
                    if (!v.is<JsonArray>())
                    {
                        Log.println("OpenSkyFetcher: Expected array element in states");
                        continue;
                    }
                    JsonArray a = v.as<JsonArray>();
                    if (a.size() < 17)
                    {
                        Log.println("OpenSkyFetcher: State vector array has insufficient elements");
                        continue;
                    }

                    StateVector s;
                    s.icao24 = a[0].as<const char *>();
                    s.callsign = a[1].isNull() ? String("") : String(a[1].as<const char *>());
                    s.callsign.trim();
                    s.origin_country = a[2].isNull() ? String("") : String(a[2].as<const char *>());
                    s.time_position = a[3].isNull() ? 0 : a[3].as<long>();
                    s.last_contact = a[4].isNull() ? 0 : a[4].as<long>();
                    s.lon = a[5].isNull() ? NAN : a[5].as<double>();
                    s.lat = a[6].isNull() ? NAN : a[6].as<double>();
                    s.baro_altitude = a[7].isNull() ? NAN : a[7].as<double>();
                    s.on_ground = a[8].isNull() ? false : a[8].as<bool>();
                    s.velocity = a[9].isNull() ? NAN : a[9].as<double>();
                    s.heading = a[10].isNull() ? NAN : a[10].as<double>();
                    s.vertical_rate = a[11].isNull() ? NAN : a[11].as<double>();
                    s.sensors = a[12].isNull() ? 0 : a[12].as<long>();
                    s.geo_altitude = a[13].isNull() ? NAN : a[13].as<double>();
                    s.squawk = a[14].isNull() ? String("") : String(a[14].as<const char *>());
                    s.spi = a[15].isNull() ? false : a[15].as<bool>();
                    s.position_source = a[16].isNull() ? 0 : a[16].as<int>();

                    if (isnan(s.lat) || isnan(s.lon))
                    {
                        Log.println("OpenSkyFetcher: Skipping state vector with invalid coordinates");
                        continue;
                    }

                    s.distance_km = haversineKm(centerLat, centerLon, s.lat, s.lon);
                    if (s.distance_km > radiusKm)
                        continue;
                    s.bearing_deg = computeBearingDeg(centerLat, centerLon, s.lat, s.lon);

                    outStateVectors.push_back(s);
                }

                return true;
            }
            attemptedRefresh = true;
        }

        Log.printf("OpenSkyFetcher: HTTP request failed with code: %d\n", code);
        http.end();
        if (attemptedRefresh)
            Log.println("OpenSkyFetcher: Token refresh attempt failed");
        return false;
    }
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err)
    {
        Log.printf("OpenSkyFetcher: JSON deserialization error: %s\n", err.c_str());
        return false;
    }

    JsonArray states = doc["states"].as<JsonArray>();
    if (states.isNull())
    {
        return true; // no states is not an error
    }

    for (JsonVariant v : states)
    {
        if (!v.is<JsonArray>())
        {
            Log.println("OpenSkyFetcher: Expected array element in states");
            continue;
        }
        JsonArray a = v.as<JsonArray>();
        if (a.size() < 17)
        {
            Log.println("OpenSkyFetcher: State vector array has insufficient elements");
            continue;
        }

        StateVector s;
        s.icao24 = a[0].as<const char *>();
        s.callsign = a[1].isNull() ? String("") : String(a[1].as<const char *>());
        s.callsign.trim();
        s.origin_country = a[2].isNull() ? String("") : String(a[2].as<const char *>());
        s.time_position = a[3].isNull() ? 0 : a[3].as<long>();
        s.last_contact = a[4].isNull() ? 0 : a[4].as<long>();
        s.lon = a[5].isNull() ? NAN : a[5].as<double>();
        s.lat = a[6].isNull() ? NAN : a[6].as<double>();
        s.baro_altitude = a[7].isNull() ? NAN : a[7].as<double>();
        s.on_ground = a[8].isNull() ? false : a[8].as<bool>();
        s.velocity = a[9].isNull() ? NAN : a[9].as<double>();
        s.heading = a[10].isNull() ? NAN : a[10].as<double>();
        s.vertical_rate = a[11].isNull() ? NAN : a[11].as<double>();
        s.sensors = a[12].isNull() ? 0 : a[12].as<long>();
        s.geo_altitude = a[13].isNull() ? NAN : a[13].as<double>();
        s.squawk = a[14].isNull() ? String("") : String(a[14].as<const char *>());
        s.spi = a[15].isNull() ? false : a[15].as<bool>();
        s.position_source = a[16].isNull() ? 0 : a[16].as<int>();

        if (isnan(s.lat) || isnan(s.lon))
        {
            Log.println("OpenSkyFetcher: Skipping state vector with invalid coordinates");
            continue;
        }

        s.distance_km = haversineKm(centerLat, centerLon, s.lat, s.lon);
        if (s.distance_km > radiusKm)
            continue;
        s.bearing_deg = computeBearingDeg(centerLat, centerLon, s.lat, s.lon);

        outStateVectors.push_back(s);
    }

    return true;
}

// ---------------------------------------------------------------------------
bool OpenSkyFetcher::fetchFlightRoute(const String &icao24,
                                       String       &outOriginIcao,
                                       String       &outDestIcao)
{
    // Need a real wall-clock time from NTP; millis() is not sufficient.
    time_t now = time(nullptr);
    if (now < 1000000000L)
    {
        Log.println("OpenSkyFetcher: NTP not synced — skipping flight route lookup");
        return false;
    }

    if (!ensureAccessToken(false))
        return false;

    // Search the last 24 hours for this aircraft.
    String hex = icao24;
    hex.toLowerCase();
    time_t begin = now - 86400L;

    String url = String(APIConfiguration::OPENSKY_BASE_URL)
               + "/api/flights/aircraft?icao24=" + hex
               + "&begin=" + String((long)begin)
               + "&end="   + String((long)now);

    WiFiClientSecure client;
    if (APIConfiguration::OPENSKY_INSECURE_TLS)
        client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(TimingConfiguration::OPENSKY_TIMEOUT_MS);
    http.addHeader("Authorization", String("Bearer ") + m_accessToken);

    int code = http.GET();
    if (code != 200)
    {
        Log.printf("OpenSkyFetcher: flights endpoint HTTP %d for %s\n", code, hex.c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Response is a top-level JSON array of flight records.
    // Pick the entry with the highest firstSeen (most recent leg).
    JsonDocument doc;
    if (deserializeJson(doc, payload) || !doc.is<JsonArray>())
        return false;

    JsonArray arr = doc.as<JsonArray>();
    long     bestTime = -1;
    String   bestDep, bestArr;

    for (JsonVariant v : arr)
    {
        long        firstSeen = v["firstSeen"] | 0L;
        const char *dep       = v["estDepartureAirport"] | (const char *)nullptr;
        const char *arr2      = v["estArrivalAirport"]   | (const char *)nullptr;

        if (dep && firstSeen > bestTime)
        {
            bestTime = firstSeen;
            bestDep  = dep;
            bestArr  = arr2 ? arr2 : "";
        }
    }

    if (bestDep.length() == 0)
    {
        Log.printf("OpenSkyFetcher: no route found for %s\n", hex.c_str());
        return false;
    }

    bestDep.toUpperCase();
    bestArr.toUpperCase();
    outOriginIcao = bestDep;
    outDestIcao   = bestArr;
    Log.printf("OpenSkyFetcher: route for %s → %s-%s\n",
               hex.c_str(), outOriginIcao.c_str(), outDestIcao.c_str());
    return true;
}
