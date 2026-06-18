#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include "interfaces/BaseStateVectorFetcher.h"
#include "interfaces/BaseFlightFetcher.h"
#include "interfaces/BaseLogoStore.h"
#include "models/StateVector.h"
#include "models/FlightInfo.h"

struct CachedFlightEntry
{
    FlightInfo       info;
    unsigned long    expiryMs = 0;
};

class FlightDataFetcher
{
public:
    // logoStore may be nullptr — logo lookup is simply skipped.
    FlightDataFetcher(BaseStateVectorFetcher *stateFetcher,
                      BaseFlightFetcher      *flightFetcher,
                      BaseLogoStore          *logoStore = nullptr);

    size_t fetchFlights(std::vector<StateVector> &outStates,
                        std::vector<FlightInfo>  &outFlights);

private:
    BaseStateVectorFetcher *_stateFetcher;
    BaseFlightFetcher      *_flightFetcher;
    BaseLogoStore          *_logoStore;

    // Keyed by trimmed callsign; stores enriched static data between fetch cycles
    std::map<String, CachedFlightEntry> _flightCache;
};
