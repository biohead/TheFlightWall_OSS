/*
Purpose: Load pre-converted airline logo bitmaps from LittleFS.
Responsibilities:
- Mount LittleFS once at startup.
- Open /logos/{ICAO}.bin, validate exact expected byte count, read into vector.
- Return false silently for missing airlines.
*/
#include "adapters/LocalLogoStore.h"
#include "models/FlightInfo.h"
#include <LittleFS.h>
#include "utils/TelnetLogger.h"

bool LocalLogoStore::initialize()
{
    if (!LittleFS.begin(false))
    {
        Log.println("LocalLogoStore: LittleFS mount failed. "
                    "Run 'pio run --target uploadfs' to upload logo data.");
        _mounted = false;
        return false;
    }
    _mounted = true;
    Log.println("LocalLogoStore: LittleFS mounted OK.");

    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    Log.printf("LocalLogoStore: %u bytes total, %u used (%.1f%%)\n",
               (unsigned)total, (unsigned)used,
               total > 0 ? (used * 100.0f / total) : 0.0f);
    return true;
}

bool LocalLogoStore::getAirlineLogo(const String &airlineIcao,
                                    std::vector<uint16_t> &outPixels)
{
    outPixels.clear();

    if (!_mounted || airlineIcao.length() == 0)
        return false;

    String icao = airlineIcao;
    icao.toUpperCase();

    String path = String("/logos/") + icao + ".bin";

    if (!LittleFS.exists(path))
    {
        Log.printf("LocalLogoStore: No logo for %s\n", icao.c_str());
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f)
    {
        Log.printf("LocalLogoStore: Failed to open %s\n", path.c_str());
        return false;
    }

    const size_t expectedBytes = (size_t)AirlineLogo::WIDTH * AirlineLogo::HEIGHT * 2;

    if (f.size() != expectedBytes)
    {
        Log.printf("LocalLogoStore: Size mismatch for %s: got %u, expected %u\n",
                   path.c_str(), (unsigned)f.size(), (unsigned)expectedBytes);
        f.close();
        return false;
    }

    const size_t pixelCount = (size_t)AirlineLogo::WIDTH * AirlineLogo::HEIGHT;
    outPixels.resize(pixelCount);

    size_t bytesRead = f.read(reinterpret_cast<uint8_t *>(outPixels.data()), expectedBytes);
    f.close();

    if (bytesRead != expectedBytes)
    {
        Log.printf("LocalLogoStore: Short read for %s: got %u bytes\n",
                   path.c_str(), (unsigned)bytesRead);
        outPixels.clear();
        return false;
    }

    // ESP32 is little-endian and files are written as little-endian uint16_t —
    // the in-place cast above is already correct, no byte-swapping needed.
    Log.printf("LocalLogoStore: Loaded logo for %s (%u pixels)\n",
               icao.c_str(), (unsigned)pixelCount);
    return true;
}
