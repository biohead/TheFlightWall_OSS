#pragma once

/*
Purpose: First-boot WiFi provisioning via a captive-portal access point.

When no WiFi credentials exist (neither compile-time nor NVS-saved) the device
broadcasts a "FlightWall-Setup" AP.  Any connected client is redirected to a
setup page where the user can:
  • Pick a nearby network and enter its password.
  • Enter latitude, longitude, search radius, and UTC offset.

On save the credentials and basic location config are persisted to NVS and the
device restarts into normal station mode.

Credentials are stored in NVS namespace "fw_wifi" (keys "ssid" / "pass"),
separate from the main config namespace so they survive a config reset.

Usage:
  // In setup(), before the normal WiFi connect block:
  String ssid, pass;
  if (!WifiProvisioner::loadCredentials(ssid, pass) &&
      strlen(WiFiConfiguration::WIFI_SSID) == 0)
  {
      WifiProvisioner prov;
      prov.setStatusCallback([](const char *m){ g_display.displayMessage(m); });
      prov.run("FlightWall-Setup");   // blocks; calls ESP.restart() on success
  }
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <DNSServer.h>
#include <vector>

class WifiProvisioner
{
public:
    // ── NVS credential helpers ────────────────────────────────────────────────

    /// Returns true if a non-empty SSID has been saved by a previous provisioning run.
    static bool    hasCredentials();

    /// Fills ssid/password from NVS.  Returns false if nothing is saved.
    static bool    loadCredentials(String &ssid, String &password);

    /// Persist credentials to NVS.
    static void    saveCredentials(const String &ssid, const String &password);

    /// Erase saved credentials (e.g. triggered by a "Forget WiFi" button).
    static void    clearCredentials();

    // ── Provisioning AP ──────────────────────────────────────────────────────

    /// Optional callback invoked with short status strings for the display.
    /// Must be a plain function pointer (no capture) so it can be stored cheaply.
    void setStatusCallback(void (*cb)(const char *)) { _onStatus = cb; }

    /// Start the captive-portal AP and block until the user saves credentials.
    /// Calls ESP.restart() on success — does not return.
    void run(const String &apName = "FlightWall-Setup");

private:
    WiFiServer _server{80};
    DNSServer  _dns;
    void (*_onStatus)(const char *) = nullptr;

    void status(const char *msg) { if (_onStatus) _onStatus(msg); }

    static String buildPage(const String &apName,
                            const std::vector<String> &nets);
    static String buildSavedPage(const String &ssid);

    static void   sendHtml(WiFiClient &c, int code, const String &html);
    static void   sendRedirect(WiFiClient &c, const String &location);
    static String formField(const String &body, const String &key);
    static String urlDecode(const String &s);
};
