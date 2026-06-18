#pragma once

#include <Arduino.h>
#include "models/FlightInfo.h"

class BaseFlightFetcher
{
public:
    virtual ~BaseFlightFetcher() = default;

    // callsign : ICAO callsign (e.g. "EZY73CA")
    // icao24   : 24-bit Mode-S hex address (e.g. "4CA84B") — may be empty when
    //            unavailable; implementations must tolerate an empty string.
    virtual bool fetchFlightInfo(const String &callsign,
                                 const String &icao24,
                                 FlightInfo   &outInfo) = 0;
};
