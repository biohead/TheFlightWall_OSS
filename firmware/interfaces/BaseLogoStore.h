#pragma once
/*
Purpose: Abstract interface for airline logo retrieval.
Implementations: LocalLogoStore (LittleFS).
Contract: getAirlineLogo returns true and populates outPixels with exactly
          AirlineLogo::WIDTH * AirlineLogo::HEIGHT RGB565 pixels on success.
          Returns false and leaves outPixels empty on any failure.
*/
#include <Arduino.h>
#include <vector>
#include "models/FlightInfo.h"

class BaseLogoStore
{
public:
    virtual ~BaseLogoStore() = default;

    virtual bool getAirlineLogo(const String &airlineIcao,
                                std::vector<uint16_t> &outPixels) = 0;
};
