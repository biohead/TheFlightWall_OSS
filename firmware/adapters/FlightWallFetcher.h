#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config/APIConfiguration.h"

class FlightWallFetcher
{
public:
    FlightWallFetcher() = default;
    ~FlightWallFetcher() = default;

    bool getAirlineName(const String &airlineIcao, String &outDisplayNameFull);

    bool getAircraftName(const String &aircraftIcao,
                         String &outDisplayNameShort,
                         String &outDisplayNameFull);

    // Looks up the IATA code for an airport given its ICAO code.
    bool getAirportIata(const String &airportIcao, String &outIata);

private:
    bool httpGetJson(const String &url, String &outPayload);
};
