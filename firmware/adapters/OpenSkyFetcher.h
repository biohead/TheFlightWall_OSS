#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "interfaces/BaseStateVectorFetcher.h"
#include "utils/GeoUtils.h"
#include "config/APIConfiguration.h"
#include "config/UserConfiguration.h"

class OpenSkyFetcher : public BaseStateVectorFetcher
{
public:
    OpenSkyFetcher() = default;
    ~OpenSkyFetcher() override = default;

    bool fetchStateVectors(double centerLat,
                           double centerLon,
                           double radiusKm,
                           std::vector<StateVector> &outStateVectors) override;

    bool ensureAuthenticated(bool forceRefresh = false);

    // Query the OpenSky flights endpoint for the most recent flight by ICAO24
    // hex address.  Fills outOriginIcao and outDestIcao with 4-char ICAO airport
    // codes when found.  Requires NTP to be synced and OAuth credentials set.
    bool fetchFlightRoute(const String &icao24,
                          String       &outOriginIcao,
                          String       &outDestIcao);

private:
    String m_accessToken;
    unsigned long m_tokenExpiryMs = 0;

    bool ensureAccessToken(bool forceRefresh = false);
    bool requestAccessToken(String &outToken, unsigned long &outExpiryMs);
};
