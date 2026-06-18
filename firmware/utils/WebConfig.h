#pragma once

/*
Purpose: Minimal HTTP server (port 80) that serves the configuration web UI
and a streaming serial log panel.  Built on WiFiServer/WiFiClient to avoid
any framework library dependency beyond what the project already uses.

Routes:
  GET  /                  — single-page UI (HTML)
  GET  /api/config        — current g_config as JSON
  POST /api/config        — update g_config from JSON body; saves to NVS
  POST /api/config/reset  — reset g_config to compile-time defaults
  GET  /api/log?cursor=N  — log lines since sequence number N + next cursor

Usage:
  Call g_webConfig.begin() once after WiFi connects.
  Call g_webConfig.loop() on every iteration of loop().
*/

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "models/FlightInfo.h"

class WebConfig
{
public:
    void begin(uint16_t port = 80);
    void loop();

    // Called from main.cpp after each displayFlights() tick so the web preview
    // always reflects the card currently on screen.  Pass nullptr when the
    // flight list is empty (shows no-flight state in the browser).
    void setCurrentFlight(const FlightInfo *f);

private:
    WiFiServer _server{80};

    // Current card pushed by setCurrentFlight()
    FlightInfo              _displayFlight;
    bool                    _displayActive  = false;
    bool                    _displayHasLogo = false;
    // Logo pixels kept separately for the web canvas preview.
    // _displayFlight.airline_logo_rgb565 is still cleared to avoid heap fragmentation.
    std::vector<uint16_t>   _displayLogo;

    // Parsed HTTP request context
    struct Req {
        String method;
        String path;
        String query;
        String body;
    };

    // Helpers
    static void   sendHttp(WiFiClient &c, int code,
                            const char *contentType, const String &body);
    static void   sendHtmlDirect(WiFiClient &c, const char *html, size_t len);
    static String qparam(const String &query, const char *key);

    // Route handlers
    void handleRoot(WiFiClient &c);
    void handleGetConfig(WiFiClient &c);
    void handlePostConfig(WiFiClient &c, const Req &r);
    void handleResetConfig(WiFiClient &c);
    void handleRestart(WiFiClient &c);
    void handleWifiReset(WiFiClient &c);
    void handleGetLog(WiFiClient &c, const Req &r);
    void handleGetDisplay(WiFiClient &c);
};

extern WebConfig g_webConfig;
