/*
Purpose: Firmware entry point for ESP32.
Responsibilities:
- Initialize serial, connect to Wi‑Fi, and construct fetchers and display.
- Periodically fetch state vectors (OpenSky), enrich flights (AeroAPI), and render.
Configuration: UserConfiguration (location/filters/colors), TimingConfiguration (intervals),
               WiFiConfiguration (SSID/password), HardwareConfiguration (display specs).
*/
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config/WiFiConfiguration.h"
#include "adapters/OpenSkyFetcher.h"
#include "adapters/Tar1090Fetcher.h"
#include "adapters/FallbackStateVectorFetcher.h"
#include "adapters/AeroAPIFetcher.h"
#include "adapters/HexDbFetcher.h"
#include "adapters/OpenSkyRouteFetcher.h"
#include "adapters/FallbackFlightFetcher.h"
#include "adapters/LocalLogoStore.h"
#include "core/FlightDataFetcher.h"
#include "adapters/NeoMatrixDisplay.h"
#include "utils/TelnetLogger.h"
#include "utils/WebConfig.h"
#include "utils/WifiProvisioner.h"
#include "config/RuntimeConfig.h"

static OpenSkyFetcher             g_openSky;
static Tar1090Fetcher             g_tar1090;
static FallbackStateVectorFetcher g_stateFetcher(&g_tar1090, &g_openSky);
static HexDbFetcher               g_hexDb;
static AeroAPIFetcher             g_aeroApi;
static OpenSkyRouteFetcher        g_openSkyRoute(g_openSky);
// Chain: hexdb → AeroAPI (if key set) → OpenSky flights endpoint (if creds set)
static FallbackFlightFetcher      g_aeroApiFallback(&g_aeroApi, &g_openSkyRoute);
static FallbackFlightFetcher      g_flightFetcher(&g_hexDb, &g_aeroApiFallback);
static LocalLogoStore             g_logoStore;
static FlightDataFetcher         *g_fetcher = nullptr;
static NeoMatrixDisplay g_display;

static unsigned long g_lastFetchMs = 0;
static unsigned long g_lastDisplayMs = 0;
static std::vector<StateVector> g_states;
static std::vector<FlightInfo> g_flights;
static bool   g_wasNightSuppressed = false;  // tracks night-mode suppression for wake-up flush
static String g_wifiSsid;                   // active SSID  (NVS > compile-time)
static String g_wifiPass;                   // active password

void setup()
{
    Serial.begin(115200);
    delay(200);

    // Load config first so g_config is fully populated before any component reads it.
    // initialize() calls setBrightness8(g_config.display_brightness) — must be non-zero.
    loadConfig();

    // Reconfigure the flight-fetcher chain based on the saved priority setting.
    // Default:          hexdb → AeroAPI → OpenSky
    // opensky_priority: OpenSky → AeroAPI → hexdb
    if (g_config.opensky_priority)
    {
        g_aeroApiFallback.setPrimary(&g_aeroApi);
        g_aeroApiFallback.setSecondary(&g_hexDb);
        g_flightFetcher.setPrimary(&g_openSkyRoute);
        g_flightFetcher.setSecondary(&g_aeroApiFallback);
    }

    g_display.initialize();
    g_display.displayMessage(String("FlightWall"));

    // Mount LittleFS for local logo storage. Failure is non-fatal.
    g_logoStore.initialize();

    // Resolve WiFi credentials: NVS-saved (provisioner) overrides compile-time constants.
    // If neither has an SSID, launch the captive-portal AP so the user can configure.
    g_wifiSsid = String(WiFiConfiguration::WIFI_SSID);
    g_wifiPass = String(WiFiConfiguration::WIFI_PASSWORD);
    {
        String savedSsid, savedPass;
        if (WifiProvisioner::loadCredentials(savedSsid, savedPass))
        {
            g_wifiSsid = savedSsid;
            g_wifiPass = savedPass;
        }
    }
    if (g_wifiSsid.length() == 0)
    {
        g_display.displayMessage("WiFi Setup");
        delay(800);
        WifiProvisioner provisioner;
        provisioner.setStatusCallback([](const char *m) { g_display.displayMessage(m); });
        provisioner.run("FlightWall-Setup");
        // run() calls ESP.restart() — execution never reaches here
    }

    if (g_wifiSsid.length() > 0)
    {
        WiFi.mode(WIFI_STA);
        g_display.displayMessage(String("WiFi: ") + g_wifiSsid);
        WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());
        Log.print("Connecting to WiFi");
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50)
        {
            delay(200);
            Log.print(".");
            attempts++;
        }
        Log.println();
        if (WiFi.status() == WL_CONNECTED)
        {
            Log.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

            // Synchronise to UTC wall-clock time for night-mode scheduling.
            // SNTP syncs in the background; night-mode reads time(nullptr) and
            // applies g_config.utc_offset_minutes, so only UTC is needed here.
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            Log.println("NTP sync started (pool.ntp.org)");

            g_webConfig.begin(80);  // start HTTP config + log UI

            // Run the web server on a dedicated FreeRTOS task pinned to the
            // application core (core 1, same as loop()).  Single-core scheduling
            // means no true parallelism — no data-race risk — but the web task
            // gets CPU during every blocking socket/HTTP operation in the main loop,
            // keeping the UI responsive throughout the AeroAPI fetch cycle.
            xTaskCreatePinnedToCore(
                [](void *) { for (;;) { g_webConfig.loop(); delay(5); } },
                "webcfg",
                8192,       // stack: HTML send + ArduinoJson + log build
                nullptr,
                1,          // same priority as loop()
                nullptr,
                APP_CPU_NUM // core 1
            );

            g_display.displayMessage(String("WiFi OK ") + WiFi.localIP().toString());
            delay(1000);
            g_display.displayMessage(String("Web: ") + WiFi.localIP().toString());
            delay(1500);
            g_display.showLoading();
        }
        else
        {
            Log.println("WiFi not connected; proceeding without network");
            g_display.displayMessage(String("WiFi FAIL"));
        }
    }

    // hexdb.io is always the primary enrichment source (free, no key required).
    // AeroAPI is used only as a fallback when hexdb has no route for a callsign,
    // and only when a key has been saved via the web config UI.
    if (g_config.opensky_priority)
        Log.printf("Flight enrichment: OpenSky primary%s + hexdb fallback\n",
                   strlen(g_config.aeroapi_key) > 0 ? " + AeroAPI" : "");
    else
        Log.printf("Flight enrichment: hexdb.io primary%s%s\n",
                   strlen(g_config.aeroapi_key)       > 0 ? " + AeroAPI fallback"       : "",
                   strlen(g_config.opensky_client_id) > 0 ? " + OpenSky route fallback" : "");
    g_fetcher = new FlightDataFetcher(&g_stateFetcher, &g_flightFetcher, &g_logoStore);

    // Force the first fetch to fire on the very first loop() iteration rather
    // than waiting a full FETCH_INTERVAL_SECONDS from boot.
    // Unsigned wraparound is intentional — now - g_lastFetchMs will equal
    // exactly FETCH_INTERVAL_SECONDS * 1000 on the first call.
    g_lastFetchMs = millis() -
        (unsigned long)(g_config.fetch_interval_seconds * 1000UL);
}

// Reconnect WiFi if the connection has dropped.
// Returns true when connected (either was already up or just recovered).
static bool ensureWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    if (g_wifiSsid.length() == 0)
        return false;

    Log.println("WiFi lost — reconnecting...");
    g_display.displayMessage("WiFi...");

    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());

    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i)
        delay(500);

    if (WiFi.status() == WL_CONNECTED)
    {
        Log.printf("WiFi reconnected: %s\n", WiFi.localIP().toString().c_str());
        g_display.displayMessage("WiFi OK");
        delay(1000);
        g_display.invalidate();
        return true;
    }

    Log.println("WiFi reconnect failed");
    g_display.displayMessage("WiFi FAIL");
    return false;
}

void loop()
{
    // Use the local interval when tar1090 was the active source last cycle,
    // and the (slower) API interval when OpenSky was used as fallback.
    const unsigned long intervalMs = g_stateFetcher.usedFallback()
        ? g_config.fetch_interval_seconds       * 1000UL
        : g_config.local_fetch_interval_seconds * 1000UL;
    const unsigned long now = millis();

    // Night-mode transition check — runs every loop() tick, independently of the
    // fetch timer.  When the device wakes from a screen-off night period, reset the
    // fetch timer so the very next timer check fires immediately rather than waiting
    // up to a full fetch_interval_seconds.
    const bool screenOffAtNight = isNightActive() && g_config.night_brightness == 0;
    if (screenOffAtNight && !g_wasNightSuppressed)
    {
        Log.println("Night mode — API calls suppressed (screen off)");
        g_wasNightSuppressed = true;
    }
    else if (!screenOffAtNight && g_wasNightSuppressed)
    {
        Log.println("Night mode ended — queuing immediate fetch");
        g_wasNightSuppressed = false;
        // Back-date the timer so the fetch block below fires this iteration
        g_lastFetchMs = now - intervalMs;
    }

    if (now - g_lastFetchMs >= intervalMs)
    {
        g_lastFetchMs = now;

        if (screenOffAtNight)
        {
            // Nothing to do — already logged the suppression above.
        }
        else if (!ensureWiFi())
        {
            Log.println("Skipping fetch — no WiFi");
        }
        else
        {
        size_t enriched = g_fetcher->fetchFlights(g_states, g_flights);
        g_display.invalidate();

        Log.printf("OpenSky state vectors: %d\n", (int)g_states.size());
        Log.printf("Flights to display: %d\n", (int)enriched);

        for (const auto &s : g_states)
            Log.printf("  %s @ %.1fkm bearing %.1f\n",
                       s.callsign.c_str(), s.distance_km, s.bearing_deg);

        for (const auto &f : g_flights)
        {
            Log.println("=== FLIGHT INFO ===");
            Log.printf("Ident: %s\n", f.ident.c_str());
            Log.printf("Ident ICAO: %s\n", f.ident_icao.c_str());
            Log.printf("Ident IATA: %s\n", f.ident_iata.c_str());
            Log.printf("Airline: %s\n", f.airline_display_name_full.c_str());
            Log.printf("Aircraft: %s\n",
                       (f.aircraft_display_name_short.length()
                           ? f.aircraft_display_name_short
                           : f.aircraft_code).c_str());
            Log.printf("Operator Code: %s\n", f.operator_code.c_str());
            Log.printf("Operator ICAO: %s\n", f.operator_icao.c_str());
            Log.printf("Operator IATA: %s\n", f.operator_iata.c_str());

            String org = f.origin.code_iata.length() ? f.origin.code_iata : f.origin.code_icao;
            if (f.origin.code_iata.length() && f.origin.code_icao.length())
                org += " (" + f.origin.code_icao + ")";
            Log.printf("Origin: %s\n", org.c_str());

            String dst = f.destination.code_iata.length() ? f.destination.code_iata : f.destination.code_icao;
            if (f.destination.code_iata.length() && f.destination.code_icao.length())
                dst += " (" + f.destination.code_icao + ")";
            Log.printf("Destination: %s\n", dst.c_str());

            Log.println("===================");
            Log.printf("Altitude: %.0f\n", f.baro_altitude);
            Log.printf("Speed: %.0f\n", f.velocity);
            Log.printf("Heading: %.0f\n", f.heading);
        }
        } // else (ensureWiFi)
    }

    // Update display once per second — the HUB75 DMA engine refreshes the panel
    // continuously from the framebuffer, so we only need to redraw when content changes.
    // Use a fresh millis() here: the fetch block above can take several seconds,
    // which would make the captured `now` stale and cause the display tick to be
    // skipped entirely on the cycle immediately following a long fetch.
    {
        const unsigned long displayNow = millis();
        if (displayNow - g_lastDisplayMs >= 1000UL)
        {
            g_lastDisplayMs = displayNow;
            g_display.displayFlights(g_flights);

            // Keep the web preview in sync with the card currently on screen.
            const size_t idx = g_display.currentFlightIndex();
            if (!g_flights.empty() && idx < g_flights.size())
                g_webConfig.setCurrentFlight(&g_flights[idx]);
            else
                g_webConfig.setCurrentFlight(nullptr);
        }
    }
    delay(10);
}