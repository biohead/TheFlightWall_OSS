#pragma once

/*
Purpose: Fetch ADS-B state vectors from a local tar1090 / readsb receiver.
Reads http://<host>/tar1090/data/aircraft.json — plain HTTP, no auth, LAN speeds.

If g_config.tar1090_host is empty the fetch immediately returns false so the
FallbackStateVectorFetcher can hand off to OpenSky.

Units note: tar1090 reports altitude in feet, speed in knots, vertical rate in
ft/min.  StateVector uses SI (metres, m/s) to match OpenSky convention so that
FlightDataFetcher's unit-conversion lines work unchanged regardless of source.
*/

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "interfaces/BaseStateVectorFetcher.h"
#include "utils/GeoUtils.h"

class Tar1090Fetcher : public BaseStateVectorFetcher
{
public:
    bool fetchStateVectors(double centerLat,
                           double centerLon,
                           double radiusKm,
                           std::vector<StateVector> &outStateVectors) override;
};
