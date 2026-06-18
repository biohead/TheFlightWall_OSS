/*
Purpose: WebConfig — minimal HTTP server, config API, and log streaming.
Uses WiFiServer/WiFiClient (already a project dependency) instead of the
WebServer library to avoid framework include-path issues.
*/
#include "utils/WebConfig.h"
#include "utils/TelnetLogger.h"
#include "utils/WifiProvisioner.h"
#include "config/RuntimeConfig.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>

WebConfig g_webConfig;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WebConfig::begin(uint16_t port)
{
    _server = WiFiServer(port);
    _server.begin();
    Log.printf("WebConfig: UI at http://%s/\n", WiFi.localIP().toString().c_str());
}

void WebConfig::loop()
{
    WiFiClient client = _server.accept();
    if (!client) return;

    // ── Read request headers (until CRLFCRLF) ─────────────────────────────
    String raw;
    raw.reserve(512);
    bool headersRead = false;
    unsigned long t0 = millis();
    while (client.connected() && millis() - t0 < 2000 && !headersRead)
    {
        while (client.available() && !headersRead)
        {
            raw += (char)client.read();
            if (raw.endsWith("\r\n\r\n"))
                headersRead = true;
        }
        if (!headersRead) delay(1);
    }
    if (!headersRead) { client.stop(); return; }

    // ── Parse request line ─────────────────────────────────────────────────
    int sp1 = raw.indexOf(' ');
    int sp2 = raw.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { client.stop(); return; }

    Req r;
    r.method = raw.substring(0, sp1);
    String fullPath = raw.substring(sp1 + 1, sp2);
    int qpos = fullPath.indexOf('?');
    if (qpos >= 0)
    {
        r.path  = fullPath.substring(0, qpos);
        r.query = fullPath.substring(qpos + 1);
    }
    else
    {
        r.path = fullPath;
    }

    // ── Read POST body (Content-Length driven) ─────────────────────────────
    if (r.method == "POST")
    {
        int clIdx = raw.indexOf("Content-Length: ");
        if (clIdx >= 0)
        {
            int clEnd = raw.indexOf("\r\n", clIdx + 16);
            int bodyLen = raw.substring(clIdx + 16, clEnd).toInt();
            if (bodyLen > 0 && bodyLen < 8192)
            {
                r.body.reserve(bodyLen + 1);
                unsigned long bt = millis();
                while ((int)r.body.length() < bodyLen && millis() - bt < 3000)
                {
                    if (client.available()) r.body += (char)client.read();
                    else delay(1);
                }
            }
        }
    }

    // ── Route ──────────────────────────────────────────────────────────────
    if      (r.path == "/"                  && r.method == "GET")  handleRoot(client);
    else if (r.path == "/api/config"        && r.method == "GET")  handleGetConfig(client);
    else if (r.path == "/api/config"        && r.method == "POST") handlePostConfig(client, r);
    else if (r.path == "/api/config/reset"  && r.method == "POST") handleResetConfig(client);
    else if (r.path == "/api/restart"       && r.method == "POST") handleRestart(client);
    else if (r.path == "/api/wifi/reset"    && r.method == "POST") handleWifiReset(client);
    else if (r.path == "/api/log"           && r.method == "GET")  handleGetLog(client, r);
    else if (r.path == "/api/display"       && r.method == "GET")  handleGetDisplay(client);
    else sendHttp(client, 404, "text/plain", "Not found");

    client.flush();
    client.stop();
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

void WebConfig::sendHttp(WiFiClient &c, int code,
                          const char *contentType, const String &body)
{
    const char *reason = (code == 200) ? "OK"
                       : (code == 400) ? "Bad Request"
                       :                 "Not Found";
    c.printf("HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n\r\n",
             code, reason, contentType, (unsigned)body.length());
    c.print(body);
}

void WebConfig::sendHtmlDirect(WiFiClient &c, const char *html, size_t len)
{
    c.printf("HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n\r\n",
             (unsigned)len);
    // Send in 512-byte chunks to avoid any single large write
    const size_t kChunk = 512;
    for (size_t sent = 0; sent < len; sent += kChunk)
        c.write((const uint8_t *)html + sent, min(kChunk, len - sent));
}

String WebConfig::qparam(const String &query, const char *key)
{
    String k = String(key) + "=";
    int pos = query.indexOf(k);
    if (pos < 0) return "";
    int start = pos + k.length();
    int end = query.indexOf('&', start);
    return end < 0 ? query.substring(start) : query.substring(start, end);
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

// Declared at the bottom of this file as a raw string literal
extern const char kHtmlPage[];
extern const size_t kHtmlPageLen;

void WebConfig::handleRoot(WiFiClient &c)
{
    sendHtmlDirect(c, kHtmlPage, kHtmlPageLen);
}

void WebConfig::handleGetConfig(WiFiClient &c)
{
    JsonDocument doc;
    doc["tar1090_host"]                  = String(g_config.tar1090_host);
    doc["center_lat"]                    = g_config.center_lat;
    doc["center_lon"]                    = g_config.center_lon;
    doc["radius_km"]                     = g_config.radius_km;
    doc["display_brightness"]            = g_config.display_brightness;
    doc["text_color_r"]                  = g_config.text_color_r;
    doc["text_color_g"]                  = g_config.text_color_g;
    doc["text_color_b"]                  = g_config.text_color_b;
    doc["display_nearest_only"]          = g_config.display_nearest_only;
    doc["display_border"]                = g_config.display_border;
    doc["show_flight_number_with_logo"]  = g_config.show_flight_number_with_logo;
    doc["show_aircraft_type"]            = g_config.show_aircraft_type;
    doc["show_aircraft_registration"]    = g_config.show_aircraft_registration;
    doc["swap_aircraft_type_reg"]        = g_config.swap_aircraft_type_reg;
    doc["screen_facing"]                 = String(g_config.screen_facing);
    doc["display_flip"]                  = g_config.display_flip;
    doc["night_mode_enabled"]            = g_config.night_mode_enabled;
    doc["night_start_minutes"]           = g_config.night_start_minutes;
    doc["night_end_minutes"]             = g_config.night_end_minutes;
    doc["night_brightness"]              = g_config.night_brightness;
    doc["utc_offset_minutes"]            = g_config.utc_offset_minutes;
    doc["fetch_interval_seconds"]        = g_config.fetch_interval_seconds;
    doc["local_fetch_interval_seconds"]  = g_config.local_fetch_interval_seconds;
    doc["display_cycle_seconds"]         = g_config.display_cycle_seconds;
    doc["aeroapi_cache_ttl_seconds"]     = g_config.aeroapi_cache_ttl_seconds;
    doc["aeroapi_fail_cache_ttl_seconds"] = g_config.aeroapi_fail_cache_ttl_seconds;

    // API keys: client ID is not secret; secrets are masked so they are never
    // transmitted back to the browser, but a "***" sentinel signals they are set.
    doc["opensky_client_id"]     = String(g_config.opensky_client_id);
    doc["opensky_client_secret"] = (strlen(g_config.opensky_client_secret) > 0) ? "***" : "";
    doc["opensky_priority"]      = g_config.opensky_priority;
    doc["aeroapi_key"]           = (strlen(g_config.aeroapi_key)           > 0) ? "***" : "";

    String out;
    serializeJson(doc, out);
    sendHttp(c, 200, "application/json", out);
}

void WebConfig::handlePostConfig(WiFiClient &c, const Req &r)
{
    if (r.body.isEmpty())
    {
        sendHttp(c, 400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, r.body))
    {
        sendHttp(c, 400, "application/json", "{\"ok\":false,\"error\":\"JSON parse error\"}");
        return;
    }

    // ArduinoJson "| current" idiom: keeps existing value when key is absent
    {
        const char *v = doc["tar1090_host"] | g_config.tar1090_host;
        strncpy(g_config.tar1090_host, v, sizeof(g_config.tar1090_host) - 1);
        g_config.tar1090_host[sizeof(g_config.tar1090_host) - 1] = '\0';
    }
    g_config.center_lat   = doc["center_lat"]   | g_config.center_lat;
    g_config.center_lon   = doc["center_lon"]   | g_config.center_lon;
    g_config.radius_km    = doc["radius_km"]    | g_config.radius_km;

    g_config.display_brightness = (uint8_t)(doc["display_brightness"] | (int)g_config.display_brightness);
    g_config.text_color_r       = (uint8_t)(doc["text_color_r"]       | (int)g_config.text_color_r);
    g_config.text_color_g       = (uint8_t)(doc["text_color_g"]       | (int)g_config.text_color_g);
    g_config.text_color_b       = (uint8_t)(doc["text_color_b"]       | (int)g_config.text_color_b);

    g_config.display_nearest_only         = doc["display_nearest_only"]         | g_config.display_nearest_only;
    g_config.display_border               = doc["display_border"]               | g_config.display_border;
    g_config.show_flight_number_with_logo = doc["show_flight_number_with_logo"] | g_config.show_flight_number_with_logo;
    g_config.show_aircraft_type           = doc["show_aircraft_type"]           | g_config.show_aircraft_type;
    g_config.show_aircraft_registration   = doc["show_aircraft_registration"]   | g_config.show_aircraft_registration;
    g_config.swap_aircraft_type_reg       = doc["swap_aircraft_type_reg"]       | g_config.swap_aircraft_type_reg;

    const char *facing = doc["screen_facing"] | g_config.screen_facing;
    strncpy(g_config.screen_facing, facing, sizeof(g_config.screen_facing) - 1);
    g_config.screen_facing[sizeof(g_config.screen_facing) - 1] = '\0';

    g_config.display_flip          = doc["display_flip"]          | g_config.display_flip;
    g_config.night_mode_enabled    = doc["night_mode_enabled"]    | g_config.night_mode_enabled;
    g_config.night_start_minutes   = (uint16_t)(doc["night_start_minutes"] | (int)g_config.night_start_minutes);
    g_config.night_end_minutes     = (uint16_t)(doc["night_end_minutes"]   | (int)g_config.night_end_minutes);
    g_config.night_brightness      = (uint8_t)(doc["night_brightness"]     | (int)g_config.night_brightness);
    g_config.utc_offset_minutes    = doc["utc_offset_minutes"]    | g_config.utc_offset_minutes;

    g_config.opensky_priority              = doc["opensky_priority"]              | g_config.opensky_priority;

    g_config.fetch_interval_seconds        = doc["fetch_interval_seconds"]        | g_config.fetch_interval_seconds;
    g_config.local_fetch_interval_seconds  = doc["local_fetch_interval_seconds"]  | g_config.local_fetch_interval_seconds;
    g_config.display_cycle_seconds         = doc["display_cycle_seconds"]         | g_config.display_cycle_seconds;
    g_config.aeroapi_cache_ttl_seconds     = doc["aeroapi_cache_ttl_seconds"]      | g_config.aeroapi_cache_ttl_seconds;
    g_config.aeroapi_fail_cache_ttl_seconds = doc["aeroapi_fail_cache_ttl_seconds"] | g_config.aeroapi_fail_cache_ttl_seconds;

    // API keys — only update when the posted value is non-empty and not the
    // mask sentinel "***".  Empty / absent = keep the current value intact.
    {
        const char *v = doc["opensky_client_id"] | "";
        if (strlen(v) > 0 && strcmp(v, "***") != 0) {
            strncpy(g_config.opensky_client_id, v, sizeof(g_config.opensky_client_id) - 1);
            g_config.opensky_client_id[sizeof(g_config.opensky_client_id) - 1] = '\0';
        }
    }
    {
        const char *v = doc["opensky_client_secret"] | "";
        if (strlen(v) > 0 && strcmp(v, "***") != 0) {
            strncpy(g_config.opensky_client_secret, v, sizeof(g_config.opensky_client_secret) - 1);
            g_config.opensky_client_secret[sizeof(g_config.opensky_client_secret) - 1] = '\0';
        }
    }
    {
        const char *v = doc["aeroapi_key"] | "";
        if (strlen(v) > 0 && strcmp(v, "***") != 0) {
            strncpy(g_config.aeroapi_key, v, sizeof(g_config.aeroapi_key) - 1);
            g_config.aeroapi_key[sizeof(g_config.aeroapi_key) - 1] = '\0';
        }
    }

    saveConfig();
    Log.println("WebConfig: settings updated and saved to NVS");
    sendHttp(c, 200, "application/json", "{\"ok\":true}");
}

void WebConfig::handleResetConfig(WiFiClient &c)
{
    resetConfig();
    Log.println("WebConfig: settings reset to firmware defaults");
    sendHttp(c, 200, "application/json", "{\"ok\":true}");
}

void WebConfig::handleRestart(WiFiClient &c)
{
    Log.println("WebConfig: restart requested via web UI");
    sendHttp(c, 200, "application/json", "{\"ok\":true}");
    c.flush();
    c.stop();
    delay(200);
    ESP.restart();
}

void WebConfig::handleWifiReset(WiFiClient &c)
{
    Log.println("WebConfig: WiFi credentials cleared via web UI — restarting into setup AP");
    sendHttp(c, 200, "application/json", "{\"ok\":true}");
    c.flush();
    c.stop();
    delay(200);
    WifiProvisioner::clearCredentials();
    ESP.restart();
}

void WebConfig::setCurrentFlight(const FlightInfo *f)
{
    _displayActive  = (f != nullptr);
    _displayHasLogo = f && !f->airline_logo_rgb565.empty();
    if (f)
    {
        _displayFlight = *f;
        // Keep a separate copy for the web canvas preview, then clear from the
        // flight copy so the persistent heap object doesn't accumulate logo vectors
        // (which fragment the allocator and trigger MBEDTLS_ERR_SSL_ALLOC_FAILED).
        _displayLogo = _displayHasLogo ? f->airline_logo_rgb565 : std::vector<uint16_t>{};
        _displayFlight.airline_logo_rgb565.clear();
    }
    else
    {
        _displayLogo.clear();
    }
}

void WebConfig::handleGetDisplay(WiFiClient &c)
{
    JsonDocument doc;
    doc["active"] = _displayActive;

    if (_displayActive)
    {
        const FlightInfo &f = _displayFlight;

        const String ident = f.ident.length() ? f.ident : f.ident_icao;
        doc["ident"]      = ident;
        doc["ident_iata"] = f.ident_iata.length() ? f.ident_iata : ident;

        // Airline name: prefer full display name, then IATA/ICAO operator codes
        doc["airline_name"] = f.airline_display_name_full.length() ? f.airline_display_name_full
                            : f.operator_iata.length()             ? f.operator_iata
                            : f.operator_icao.length()             ? f.operator_icao
                            : f.operator_code;

        // Airport codes: prefer IATA, fall back to ICAO
        doc["origin"] = f.origin.code_iata.length()      ? f.origin.code_iata      : f.origin.code_icao;
        doc["dest"]   = f.destination.code_iata.length() ? f.destination.code_iata : f.destination.code_icao;

        // Aircraft: prefer human-readable short name, fall back to ICAO type code
        doc["aircraft_name"] = f.aircraft_display_name_short.length()
                                 ? f.aircraft_display_name_short : f.aircraft_code;
        doc["registration"]  = f.registration;

        // Telemetry — FlightInfo stores display-ready units (ft, kt, deg, fpm).
        // Convert kt→mph here to match what buildTelemetryLine1() shows on screen.
        doc["altitude_ft"] = isnan(f.baro_altitude) ? 0 : (int)round(f.baro_altitude);
        doc["speed_mph"]   = isnan(f.velocity)       ? 0 : (int)round(f.velocity * 1.15078f);
        doc["heading_deg"] = isnan(f.heading)         ? 0 : (int)round(f.heading);
        doc["vspeed_fpm"]  = isnan(f.vertical_rate)   ? 0 : (int)round(f.vertical_rate);
        doc["distance_mi"] = isnan(f.distance_km)     ? 0 : (int)round(f.distance_km * 0.621371f);
        doc["bearing_deg"] = isnan(f.bearing_deg)     ? 0.0 : (double)f.bearing_deg;
        doc["has_logo"]    = _displayHasLogo;
    }

    String out;
    serializeJson(doc, out);

    // Append logo pixels after ArduinoJson serialisation to avoid a large
    // internal copy inside JsonDocument.  Each of the 1024 RGB565 pixels is
    // encoded as 4 lowercase hex digits; the JS canvas preview decodes them.
    if (_displayHasLogo && !_displayLogo.empty())
    {
        static const char kHex[] = "0123456789abcdef";
        out.remove(out.length() - 1);  // strip trailing '}'
        out += ",\"logo_hex\":\"";
        out.reserve(out.length() + _displayLogo.size() * 4 + 3);
        for (uint16_t v : _displayLogo)
        {
            out += kHex[(v >> 12) & 0xF];
            out += kHex[(v >>  8) & 0xF];
            out += kHex[(v >>  4) & 0xF];
            out += kHex[ v        & 0xF];
        }
        out += "\"}";
    }

    sendHttp(c, 200, "application/json", out);
}

void WebConfig::handleGetLog(WiFiClient &c, const Req &r)
{
    uint32_t cursor = 0;
    String cv = qparam(r.query, "cursor");
    if (cv.length()) cursor = (uint32_t)cv.toInt();

    std::vector<String> lines;
    uint32_t nextCursor = 0;
    Log.getLines(cursor, lines, nextCursor);

    // Build JSON manually — avoids large ArduinoJson allocation for log dumps
    String json;
    json.reserve(lines.size() * 80 + 32);
    json = "{\"cursor\":";
    json += String(nextCursor);
    json += ",\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i) json += ',';
        json += '"';
        for (char ch : lines[i])
        {
            if      (ch == '"')  json += "\\\"";
            else if (ch == '\\') json += "\\\\";
            else if (ch == '\r') { /* skip */ }
            else                 json += ch;
        }
        json += '"';
    }
    json += "]}";
    sendHttp(c, 200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Embedded single-page UI
// ---------------------------------------------------------------------------

const char kHtmlPage[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>FlightWall</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#0d1117;color:#c9d1d9;font:13px/1.5 monospace;height:100vh;display:flex;flex-direction:column}"
"header{padding:10px 16px;background:#161b22;border-bottom:1px solid #30363d;font-size:15px}"
".main{display:flex;flex:1;overflow:hidden}"
".pane{overflow-y:auto}"
".cfg{width:340px;min-width:240px;border-right:1px solid #30363d;padding:14px}"
".log{flex:1;display:flex;flex-direction:column;padding:12px}"
"h2{color:#8b949e;font-size:11px;text-transform:uppercase;letter-spacing:.06em;margin:14px 0 6px;padding-bottom:4px;border-bottom:1px solid #21262d}"
"h2:first-child{margin-top:0}"
".r2{display:grid;gap:8px;grid-template-columns:1fr 1fr;margin-bottom:8px}"
".r3{display:grid;gap:8px;grid-template-columns:1fr 1fr 1fr}"
".f{margin-bottom:8px}"
"label{display:block;color:#8b949e;font-size:11px;margin-bottom:3px}"
"input,select{width:100%;background:#161b22;border:1px solid #30363d;color:#c9d1d9;padding:5px 7px;border-radius:4px;font:inherit}"
"input:focus,select:focus{outline:1px solid #388bfd;border-color:#388bfd}"
".ck{display:flex;align-items:center;margin-bottom:7px;font-size:12px;cursor:pointer}"
".ck input{width:auto;margin-right:6px;accent-color:#238636;cursor:pointer}"
".acts{display:flex;align-items:center;gap:8px;margin-top:14px;flex-wrap:wrap}"
".btn{border:none;padding:7px 14px;border-radius:5px;cursor:pointer;font:inherit;font-size:12px}"
".bs{background:#238636;color:#fff}.br{background:#21262d;border:1px solid #30363d;color:#c9d1d9}"
".btn:hover{opacity:.85}"
"#st{font-size:12px;color:#8b949e}"
"#lb{height:160px;background:#161b22;border:1px solid #21262d;border-radius:4px;overflow-y:auto;padding:8px;font-size:11px;line-height:1.4}"
".ll{color:#8b949e;white-space:pre-wrap;word-break:break-all}"
".lln{color:#c9d1d9}"
"#lc{font-size:11px;color:#484f58;margin-top:4px}"
"@media(max-width:680px){.main{flex-direction:column;height:auto}.cfg{width:100%;border-right:none;border-bottom:1px solid #30363d}.log{min-height:400px}}"
"input[type=time]::-webkit-calendar-picker-indicator{filter:invert(.7)}"
"#dmp{font:12px/12px 'Courier New',monospace;zoom:0.333;display:block;align-self:flex-start;white-space:pre;background:#000;border:1px solid #333;margin-bottom:8px}"
"</style>"
"</head>"
"<body>"
"<header>&#9992; FlightWall</header>"
"<div class=\"main\">"
"<div class=\"pane cfg\">"
"<h2>WiFi</h2>"
"<div class=\"acts\" style=\"margin-bottom:12px\">"
"<button class=\"btn br\" onclick=\"wifiReset()\">Change WiFi network&hellip;</button>"
"</div>"
"<h2>Local ADS-B</h2>"
"<div class=\"f\"><label>tar1090 host (IP or hostname, blank = OpenSky only)</label>"
"<input id=\"tar1090_host\" autocomplete=\"off\" spellcheck=\"false\" placeholder=\"e.g. 192.168.1.10\"></div>"
"<h2>Location</h2>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Latitude</label><input type=\"number\" step=\"any\" id=\"center_lat\"></div>"
"<div class=\"f\"><label>Longitude</label><input type=\"number\" step=\"any\" id=\"center_lon\"></div>"
"</div>"
"<div class=\"f\"><label>Search radius (km)</label><input type=\"number\" step=\"1\" min=\"1\" id=\"radius_km\"></div>"
"<h2>Display</h2>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Brightness (0-255)</label><input type=\"number\" min=\"0\" max=\"255\" id=\"display_brightness\"></div>"
"<div class=\"f\"><label>Screen facing</label>"
"<select id=\"screen_facing\">"
"<option>N</option><option>NNE</option><option>NE</option><option>ENE</option>"
"<option>E</option><option>ESE</option><option>SE</option><option>SSE</option>"
"<option>S</option><option>SSW</option><option>SW</option><option>WSW</option>"
"<option>W</option><option>WNW</option><option>NW</option><option>NNW</option>"
"</select></div>"
"</div>"
"<div class=\"f\"><label>Text colour (R / G / B)</label>"
"<div class=\"r3\">"
"<input type=\"number\" min=\"0\" max=\"255\" id=\"text_color_r\" placeholder=\"R\">"
"<input type=\"number\" min=\"0\" max=\"255\" id=\"text_color_g\" placeholder=\"G\">"
"<input type=\"number\" min=\"0\" max=\"255\" id=\"text_color_b\" placeholder=\"B\">"
"</div></div>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"display_nearest_only\">Nearest flight only</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"display_border\">Show border</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"show_flight_number_with_logo\">Flight number with logo</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"show_aircraft_type\">Show aircraft type</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"show_aircraft_registration\">Show aircraft registration</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"swap_aircraft_type_reg\">&nbsp;&nbsp;&#8627; Swap order (reg above type)</label>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"display_flip\">Flip display 180&#176; (USB on opposite side)</label>"
"<h2>Night mode</h2>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"night_mode_enabled\">Enable night dimming</label>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Night start</label><input type=\"time\" id=\"night_start_time\"></div>"
"<div class=\"f\"><label>Night end</label><input type=\"time\" id=\"night_end_time\"></div>"
"</div>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Night brightness (0-255)</label><input type=\"number\" min=\"0\" max=\"255\" id=\"night_brightness\"></div>"
"<div class=\"f\"><label>UTC offset (hours, e.g. -5, 5.5)</label><input type=\"number\" step=\"0.25\" min=\"-12\" max=\"14\" id=\"utc_offset_hours\"></div>"
"</div>"
"<h2>Timing</h2>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Fetch interval &mdash; local ADS-B (s)</label><input type=\"number\" min=\"1\" id=\"local_fetch_interval_seconds\"></div>"
"<div class=\"f\"><label>Fetch interval &mdash; OpenSky fallback (s)</label><input type=\"number\" min=\"30\" id=\"fetch_interval_seconds\"></div>"
"</div>"
"<div class=\"r2\">"
"<div class=\"f\"><label>Display cycle (s)</label><input type=\"number\" min=\"1\" id=\"display_cycle_seconds\"></div>"
"</div>"
"<div class=\"r2\">"
"<div class=\"f\"><label>API cache TTL (s)</label><input type=\"number\" min=\"60\" id=\"aeroapi_cache_ttl_seconds\"></div>"
"<div class=\"f\"><label>Fail cache TTL (s)</label><input type=\"number\" min=\"30\" id=\"aeroapi_fail_cache_ttl_seconds\"></div>"
"</div>"
"<h2>API Keys</h2>"
"<div class=\"f\"><label>OpenSky Client ID</label>"
"<input id=\"opensky_client_id\" autocomplete=\"off\" spellcheck=\"false\" placeholder=\"From OpenSky developer settings\"></div>"
"<div class=\"f\"><label>OpenSky Client Secret</label>"
"<input type=\"password\" id=\"opensky_client_secret\" autocomplete=\"new-password\" placeholder=\"Leave blank to keep current\"></div>"
"<label class=\"ck\"><input type=\"checkbox\" id=\"opensky_priority\">Prioritise OpenSky for route lookup (takes effect after restart)</label>"
"<div class=\"f\"><label>AeroAPI Key</label>"
"<input type=\"password\" id=\"aeroapi_key\" autocomplete=\"new-password\" placeholder=\"Leave blank to keep current\"></div>"
"<div class=\"acts\">"
"<button class=\"btn bs\" onclick=\"save()\">Save &amp; Apply</button>"
"<button class=\"btn br\" onclick=\"rst()\">Reset defaults</button>"
"<button class=\"btn\" onclick=\"reboot()\">Restart ESP32</button>"
"<span id=\"st\"></span>"
"</div>"
"</div>"
"<div class=\"pane log\">"
"<h2>Display Preview</h2>"
"<pre id=\"dmp\"></pre>"
"<h2>Serial log</h2>"
"<div id=\"lb\"></div>"
"<div id=\"lc\"></div>"
"</div>"
"</div>"
"<script>"
"var cur=0,tot=0,lb=document.getElementById('lb'),dispData=null;"
"function load(){"
"fetch('/api/config').then(function(r){return r.json();}).then(function(d){"
"Object.keys(d).forEach(function(k){"
"var e=document.getElementById(k);if(!e)return;"
"if(e.type==='checkbox')e.checked=!!d[k];else e.value=d[k];"
"});"
"var toTime=function(m){var h=Math.floor(m/60),n=m%60;return(h<10?'0'+h:''+h)+':'+(n<10?'0'+n:''+n);};"
"document.getElementById('night_start_time').value=toTime(d.night_start_minutes||0);"
"document.getElementById('night_end_time').value=toTime(d.night_end_minutes||0);"
"document.getElementById('utc_offset_hours').value=parseFloat((d.utc_offset_minutes/60).toFixed(2));"
// Secret fields: if the server returned "***" the key is set but we never
// echo the actual value.  Clear the field and update the placeholder instead.
"['opensky_client_secret','aeroapi_key'].forEach(function(k){"
"var e=document.getElementById(k);if(!e)return;"
"if(d[k]==='***'){e.value='';e.placeholder='(set - leave blank to keep current)';}"
"else{e.value='';}"
"});"
"updateDisp();"
"});"
"}"
"function save(){"
"var d={};"
"['center_lat','center_lon','radius_km'].forEach(function(k){d[k]=parseFloat(document.getElementById(k).value);});"
"['display_brightness','text_color_r','text_color_g','text_color_b',"
"'fetch_interval_seconds','local_fetch_interval_seconds','display_cycle_seconds',"
"'aeroapi_cache_ttl_seconds','aeroapi_fail_cache_ttl_seconds',"
"'night_brightness'].forEach(function(k){d[k]=parseInt(document.getElementById(k).value);});"
"['display_nearest_only','display_border','show_flight_number_with_logo',"
"'show_aircraft_type','show_aircraft_registration','swap_aircraft_type_reg',"
"'display_flip','night_mode_enabled','opensky_priority'].forEach(function(k){d[k]=document.getElementById(k).checked;});"
"d.screen_facing=document.getElementById('screen_facing').value;"
"d.tar1090_host=document.getElementById('tar1090_host').value;"
"var toMin=function(t){var p=(t||'00:00').split(':');return parseInt(p[0])*60+(parseInt(p[1])||0);};"
"d.night_start_minutes=toMin(document.getElementById('night_start_time').value);"
"d.night_end_minutes=toMin(document.getElementById('night_end_time').value);"
"d.utc_offset_minutes=Math.round(parseFloat(document.getElementById('utc_offset_hours').value||'0')*60);"
// API keys — only include in POST when the user typed something; blank = keep current.
"['opensky_client_id','opensky_client_secret','aeroapi_key'].forEach(function(k){"
"var v=document.getElementById(k).value;if(v.length>0)d[k]=v;"
"});"
"var s=document.getElementById('st');s.textContent='Saving...';"
"fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})"
".then(function(r){return r.json();})"
".then(function(j){s.textContent=j.ok?'Saved!':'Error: '+(j.error||'?');setTimeout(function(){s.textContent='';},3000);})"
".catch(function(){s.textContent='Network error';});"
"}"
"function rst(){"
"if(!confirm('Reset all settings to firmware defaults?'))return;"
"fetch('/api/config/reset',{method:'POST'})"
".then(function(){load();var s=document.getElementById('st');s.textContent='Reset!';setTimeout(function(){s.textContent='';},2000);});"
"}"
"function reboot(){"
"if(!confirm('Restart the ESP32 now?'))return;"
"var s=document.getElementById('st');s.textContent='Restarting…';"
"fetch('/api/restart',{method:'POST'}).catch(function(){});"
"setTimeout(function(){s.textContent='Reconnecting…';},1000);"
"setTimeout(function(){location.reload();},6000);"
"}"
"function wifiReset(){"
"if(!confirm('This will erase the saved WiFi credentials and restart the device as a setup hotspot (FlightWall-Setup).\\n\\nMake sure you are ready to connect to that network before continuing.'))return;"
"fetch('/api/wifi/reset',{method:'POST'}).catch(function(){});"
"document.getElementById('st').textContent='Credentials cleared — connect to FlightWall-Setup';"
"}"
"function poll(){"
"fetch('/api/log?cursor='+cur)"
".then(function(r){return r.json();})"
".then(function(d){"
"if(d.lines&&d.lines.length){"
"var ab=lb.scrollHeight-lb.scrollTop<=lb.clientHeight+8;"
"d.lines.forEach(function(l){"
"var v=document.createElement('div');v.className='ll lln';v.textContent=l;lb.appendChild(v);"
"tot++;if(tot>500)lb.removeChild(lb.firstChild);"
"});cur=d.cursor;"
"if(ab)lb.scrollTop=lb.scrollHeight;"
"document.getElementById('lc').textContent=tot+' lines';"
"}"
"}).catch(function(){}).finally(function(){setTimeout(poll,1000);});"
"}"
"function updateDisp(){renderDotMatrix(dispData);}"
// ── Poll /api/display every 3 s ────────────────────────────────────────────────
"function pollDisplay(){"
"fetch('/api/display')"
".then(function(r){return r.json();})"
".then(function(d){dispData=d;updateDisp();})"
".catch(function(){})"
".finally(function(){setTimeout(pollDisplay,3000);});"
"}"
"['text_color_r','text_color_g','text_color_b','display_brightness'].forEach(function(id){document.getElementById(id).addEventListener('input',updateDisp);});"
"['display_border','show_aircraft_type','show_aircraft_registration','swap_aircraft_type_reg','show_flight_number_with_logo'].forEach(function(id){document.getElementById(id).addEventListener('change',updateDisp);});"
"document.getElementById('screen_facing').addEventListener('change',updateDisp);"
// ── Canvas pixel preview ──────────────────────────────────────────────────────
"(function(){"
// GFX 5x7 font, chars 32-127, 5 bytes/char. bit0=top row, bit6=bottom row.
"var FH='000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c';"
"var F=[];for(var i=0;i<FH.length;i+=2)F.push(parseInt(FH.substr(i,2),16));"
// 8-direction arrows (7 rows each); bit6=leftmost pixel
"var AR=[[0x08,0x1C,0x2A,0x08,0x08,0x08,0x00],[0x07,0x03,0x05,0x08,0x10,0x20,0x00],"
"[0x00,0x04,0x02,0x7F,0x02,0x04,0x00],[0x20,0x10,0x08,0x05,0x03,0x07,0x00],"
"[0x08,0x08,0x08,0x2A,0x1C,0x08,0x00],[0x02,0x04,0x08,0x50,0x60,0x70,0x00],"
"[0x00,0x10,0x20,0x7F,0x20,0x10,0x00],[0x70,0x60,0x50,0x08,0x04,0x02,0x00]];"
// Airplane icon: 32 rows encoded as 32-bit masks (bit31=col0)
"var PL=[0x00018000,0x0003C000,0x0003C000,0x00018000,0x00018000,0x00018000,0x00018000,0x00018000,"
"0x00018000,0x001FE000,0x003FF800,0x07FFFFE0,0x07FFFFE0,0x1FFFFFF8,0x07FFFFE0,0x07FFFFE0,"
"0x001FF800,0x0007E000,0x00018000,0x00018000,0x00018000,0x00018000,0x00018000,0x00018000,"
"0x0007E000,0x003FFC00,0x003FFC00,0x0007E000,0x00018000,0,0,0];"
// Pixel buffer + per-pixel colour.  setCol() sets current colour; px() marks a
// pixel lit with that colour.  flush() draws each lit pixel as a coloured '*'
// character on the canvas — ASCII-art look with correct per-element colours.
"var BUF=new Uint8Array(128*64),CR=new Uint8Array(128*64),CG=new Uint8Array(128*64),CB=new Uint8Array(128*64);"
"var _r=255,_g=255,_b=255;"
"function setCol(r,g,b){_r=r;_g=g;_b=b;}"
"function px(x,y){if(x<0||x>=128||y<0||y>=64)return;var i=y*128+x;BUF[i]=1;CR[i]=_r;CG[i]=_g;CB[i]=_b;}"
"function dc(x,y,c,s){"
"  if(c<32||c>127)return;"
"  var idx=(c-32)*5;"
"  for(var col=0;col<5;col++){"
"    var b=F[idx+col];"
"    for(var row=0;row<7;row++)"
"      if((b>>row)&1)"
"        for(var sy=0;sy<s;sy++)for(var sx=0;sx<s;sx++)px(x+col*s+sx,y+row*s+sy);"
"  }"
"}"
"function ds(x,y,str,s){for(var i=0;i<str.length;i++){dc(x,y,str.charCodeAt(i),s);x+=6*s;}}"
"function df(x,y,str,mW,s){"
"  var cw=s>=2?12:6;"
"  if(str.length*cw>mW&&s>1){s=1;cw=6;}"
"  var mx=Math.floor(mW/cw);"
"  ds(x,y,str.length>mx?str.substring(0,mx):str,s);"
"}"
"function da(x,y,si){"
"  for(var r=0;r<7;r++){var b=AR[si][r];for(var c=0;c<7;c++)if((b>>(6-c))&1)px(x+c,y+r);}"
"}"
"function bsec(deg){"
"  var cd={N:0,NNE:22.5,NE:45,ENE:67.5,E:90,ESE:112.5,SE:135,SSE:157.5,"
"          S:180,SSW:202.5,SW:225,WSW:247.5,W:270,WNW:292.5,NW:315,NNW:337.5};"
"  var fc=document.getElementById('screen_facing').value||'N';"
"  var rel=((deg-(cd[fc]||0))%360+360)%360;"
"  return Math.round(rel/45)%8;"
"}"
"function renderDotMatrix(d){"
"  var el=document.getElementById('dmp');if(!el)return;"
"  BUF.fill(0);"
// Build innerHTML: unlit = space, lit = <span style="color:rgb(...)">*</span>.
// Same-colour runs are merged into one span.
// The pre is rendered at 12px then CSS zoom:0.333 shrinks it to ~4px apparent,
// bypassing the browser minimum-font-size that plagued smaller font-size values.
"  function flush(){var h='';for(var ry=0;ry<64;ry++){for(var rx=0;rx<128;){if(!BUF[ry*128+rx]){h+=' ';rx++;}else{var i=ry*128+rx,r=CR[i],g=CG[i],b=CB[i],x0=rx;while(rx<128&&BUF[ry*128+rx]&&CR[ry*128+rx]===r&&CG[ry*128+rx]===g&&CB[ry*128+rx]===b)rx++;h+='<span style=\"color:rgb('+r+','+g+','+b+')\">'+(new Array(rx-x0+1).join('*'))+'</span>';}}if(ry<63)h+='\\n';}el.innerHTML=h;}"
"  if(!d||!d.active){flush();return;}"
"  var kI=2,tX=35,tY=3,tW=91,rE=126;"
"  var cr=parseInt(document.getElementById('text_color_r').value)||255;"
"  var cg=parseInt(document.getElementById('text_color_g').value)||255;"
"  var cb=parseInt(document.getElementById('text_color_b').value)||255;"
// Border in white, matching the real display
"  if(document.getElementById('display_border').checked){"
"    setCol(255,255,255);"
"    for(var bx=0;bx<128;bx++){px(bx,0);px(bx,63);}"
"    for(var by=1;by<63;by++){px(0,by);px(127,by);}"
"  }"
// Logo / airplane icon (iconY = tY + (41-32)/2 = 7)
"  var iY=7;"
"  if(d.has_logo&&d.logo_hex&&d.logo_hex.length>=4096){"
// Decode RGB565 pixels to RGB888 for canvas.  The logo .bin files contain
// standard RGB565 (R in bits 15:11, G in bits 10:5, B in bits 4:0).
// NeoMatrixDisplay::drawLogo swaps R↔B to produce the BGR565 the HUB75
// panel driver expects; for the canvas we just decode standard RGB565 directly.
"    for(var pi=0;pi<1024;pi++){"
"      var v=parseInt(d.logo_hex.substr(pi*4,4),16);"
"      if(v===0xF81F)continue;"  // kTransparentRGB565 = magenta, skip
"      var r5=(v>>11)&0x1F,g6=(v>>5)&0x3F,b5=v&0x1F;"
"      setCol((r5<<3)|(r5>>2),(g6<<2)|(g6>>4),(b5<<3)|(b5>>2));"
"      px(kI+(pi%32),iY+Math.floor(pi/32));"
"    }"
"  }else{"
// Airplane fallback: same blue as NeoMatrixDisplay color565(0,100,255)
"    setCol(0,100,255);"
"    for(var pr=0;pr<32;pr++){var pv=PL[pr]||0;for(var pc=0;pc<32;pc++)if((pv>>>(31-pc))&1)px(kI+pc,iY+pr);}"
"  }"
// Text lines in configured colour
"  setCol(cr,cg,cb);"
// Line 1: airline name or flight ident
"  var fnl=document.getElementById('show_flight_number_with_logo').checked;"
"  df(tX,tY,(fnl&&d.has_logo)?(d.ident_iata||d.ident||''):(d.airline_name||''),tW,1);"
// Line 2: route (size-2 if fits)
"  df(tX,tY+9,(d.origin||'')+'-'+(d.dest||''),tW,2);"
// Lines 3/3b: aircraft type and registration
"  var sT=document.getElementById('show_aircraft_type').checked;"
"  var sR=document.getElementById('show_aircraft_registration').checked;"
"  var sw=document.getElementById('swap_aircraft_type_reg').checked;"
"  var ac=d.aircraft_name||'',rg=d.registration||'';"
"  var l3='',l3b='';"
"  if(sT&&sR){l3=sw?rg:ac;l3b=sw?ac:rg;}else if(sT){l3=ac;}else if(sR){l3=rg;}"
"  if(l3.length)df(tX,tY+25,l3,tW,1);"
"  if(l3b.length)df(tX,tY+34,l3b,tW,1);"
// Line 4: altitude  speed [arrow in grey, matching NeoMatrixDisplay]
"  var af=Math.round(d.altitude_ft||0),as2='';"
"  if(af>=1000){var rm=af%1000;as2=Math.floor(af/1000)+','+(rm<100?'0':'')+(rm<10?'0':'')+rm+'ft';}else as2=af+'ft';"
"  var l4=as2+'  '+Math.round(d.speed_mph||0)+'mph';"
"  var sec=bsec(d.bearing_deg||0);"
"  setCol(cr,cg,cb);"
"  ds(kI,tY+43,l4.length*6>120?l4.substring(0,20):l4,1);"
"  setCol(100,100,100);"  // grey for arrow
"  da(rE-7,tY+43,sec);"
// Line 5: heading  vspeed [distance in grey, right-aligned]
"  setCol(cr,cg,cb);"
"  var vs2=Math.round(d.vspeed_fpm||0);"
"  var l5=Math.round(d.heading_deg||0)+'*  '+(vs2>=0?'+':'')+vs2+'fpm';"
"  var ds2s=Math.round(d.distance_mi||0)+'mi';"
"  var l5mW=124-ds2s.length*6;"
"  ds(kI,tY+51,l5.length*6>l5mW?l5.substring(0,Math.floor(l5mW/6)):l5,1);"
"  setCol(100,100,100);"  // grey for distance
"  ds(rE-ds2s.length*6,tY+51,ds2s,1);"
"  flush();"
"}"
"window.renderDotMatrix=renderDotMatrix;"
"})();"
"load();poll();pollDisplay();"
"</script>"
"</body>"
"</html>";

const size_t kHtmlPageLen = sizeof(kHtmlPage) - 1;  // exclude null terminator
