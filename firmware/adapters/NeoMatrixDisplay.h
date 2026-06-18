#pragma once

#include <stdint.h>
#include <vector>
#include "interfaces/BaseDisplay.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

class NeoMatrixDisplay : public BaseDisplay
{
public:
    NeoMatrixDisplay();
    ~NeoMatrixDisplay() override;

    bool initialize() override;
    void clear() override;
    void displayFlights(const std::vector<FlightInfo> &flights) override;
    void displayMessage(const String &message);
    void showLoading();
    // Signal that new flight data has arrived.
    // Only the two telemetry lines at the bottom are repainted — the card
    // (logo, airline, route, aircraft) is left intact until the natural cycle
    // advance, which triggers a full fillScreen + redraw.
    void invalidate() { _needsTelemetryUpdate = true; }

    // Index of the flight card currently rendered on screen.
    size_t currentFlightIndex() const { return _currentFlightIndex; }

private:
    uint16_t _matrixWidth = 0;
    uint16_t _matrixHeight = 0;
    MatrixPanel_I2S_DMA *_matrix = nullptr;

    size_t        _currentFlightIndex  = 0;
    unsigned long _lastCycleMs         = 0;
    // Full redraw: set on first paint, cycle advance, or identity change.
    bool          _needsFullRedraw     = true;
    // Telemetry-only repaint: set by invalidate() when new data arrives.
    bool          _needsTelemetryUpdate = false;
    // Callsign of the flight whose card is currently on screen.
    // Used to detect when the flight at _currentFlightIndex changes identity.
    String        _renderedCallsign;

    uint8_t _lastBrightness = 0;
    bool    _lastFlip       = false;

    void drawTextLine(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t size = 1);
    // Like drawTextLine but drops from preferredSize to size-1 if the text won't fit in maxW
    void drawTextLineFit(int16_t x, int16_t y, const String &text, int maxW,
                         uint16_t color, uint8_t preferredSize = 2);
    void drawOuterBorder(uint16_t color);
    void drawLogo(int16_t x, int16_t y, const std::vector<uint16_t> &pixels);
    void drawAirplaneIcon(int16_t x, int16_t y, uint16_t color);
    void drawDirectionArrow(int16_t x, int16_t y, uint8_t sectorIdx, uint16_t color);
    // Draw lines 4 & 5 (telemetry) at the standard positions.
    // Called by both drawFlightText() and updateTelemetryLines().
    void drawTelemetryLines(int16_t textY, const FlightInfo &f);
    // Clear just the telemetry rows and redraw them in-place.
    void updateTelemetryLines(const FlightInfo &f);
    void drawFlightText(int16_t x, int16_t y, int maxW, const FlightInfo &f, uint16_t color);
    String buildTelemetryLine1(const FlightInfo &f);
    String buildTelemetryLine2(const FlightInfo &f);
    String truncate(const String &text, int maxChars);
    void displaySingleFlightCard(const FlightInfo &f);
    void displayLoadingScreen();
};
