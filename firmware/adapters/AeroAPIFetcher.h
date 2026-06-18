#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "interfaces/BaseFlightFetcher.h"
#include "config/APIConfiguration.h"

class AeroAPIFetcher : public BaseFlightFetcher
{
public:
    AeroAPIFetcher() = default;
    ~AeroAPIFetcher() override = default;

    bool fetchFlightInfo(const String &callsign,
                         const String &icao24,
                         FlightInfo   &outInfo) override;

private:
    // Persistent TLS client — shared across all calls in a cycle so that only
    // the first request in each cycle pays for the full TLS handshake.
    // Subsequent calls reuse the keep-alive connection (HTTP/1.1 default), which
    // avoids the repeated ~40 KB mbedTLS context allocations that cause code -1
    // (HTTPC_ERROR_CONNECTION_REFUSED) when the heap is fragmented.
    WiFiClientSecure _client;
    bool             _configured = false;
};
