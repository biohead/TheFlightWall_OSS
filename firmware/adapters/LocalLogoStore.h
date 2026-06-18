#pragma once
/*
Purpose: Load pre-converted airline logo bitmaps from LittleFS.
File format: /logos/{ICAO_UPPERCASE}.bin — AirlineLogo::WIDTH * HEIGHT * 2 bytes,
             little-endian RGB565 pixels, row-major. Magenta (0xF81F) = transparent.
Populate: run 'pio run --target uploadfs' after running tools/build_logos.py.
*/
#include "interfaces/BaseLogoStore.h"

class LocalLogoStore : public BaseLogoStore
{
public:
    bool initialize();

    bool getAirlineLogo(const String &airlineIcao,
                        std::vector<uint16_t> &outPixels) override;

private:
    bool _mounted = false;
};
