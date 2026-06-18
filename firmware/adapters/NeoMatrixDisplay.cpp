/*
Purpose: Render flight info on a HUB75 matrix panel via ESP32-HUB75-MatrixPanel-I2S-DMA.
Responsibilities:
- Initialize HUB75 matrix based on HardwareConfiguration.
- Render a bordered flight card with a 32x32 airline logo (or airplane icon fallback)
  on the left and five lines of flight/telemetry text on the right.
- Cycle through multiple flights at a configurable interval.
*/
#include "adapters/NeoMatrixDisplay.h"
#include "config/HardwareConfiguration.h"
#include "config/RuntimeConfig.h"

// ── Layout constants ─────────────────────────────────────────────────────────
static constexpr int kOuterBorder = 1;
static constexpr int kGap         = 1;
static constexpr int kInset       = kOuterBorder + kGap;  // 2px

static constexpr int kLogoGap  = 1;
static constexpr int kLogoColW = AirlineLogo::WIDTH + kLogoGap;  // 33px

// Y offsets relative to textY (top of content area, after inset + 1px top pad)
static constexpr int kLine1OffsetY = 0;   // Airline name  (size-1, 8px tall)
static constexpr int kLine2OffsetY = 9;   // Route IATA    (size-2, 16px tall)
static constexpr int kLine3OffsetY = 25;  // Aircraft type (size-2, 16px tall)
static constexpr int kLine3bOffsetY = 34;  // Aircraft reg   (size-1, only if non-empty)
static constexpr int kLine4OffsetY  = 43;  // Telemetry 1   (size-1)
static constexpr int kLine5OffsetY  = 51;  // Telemetry 2   (size-1)

static constexpr int kCharW1 = 6;
static constexpr int kCharH1 = 8;
static constexpr int kCharW2 = 12;
static constexpr int kCharH2 = 16;

static constexpr uint16_t kTransparentRGB565 = 0xF81F;  // magenta = transparent

// Arrow dimensions (fits within one size-1 character cell height)
static constexpr int kArrowW   = 7;
static constexpr int kArrowGap = 2;  // gap between arrow and distance text

// ── 8-direction bearing arrows, 7×7 px ───────────────────────────────────────
// Index: 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW
// Each uint8_t is one row (top→bottom); bit 6 = leftmost pixel (col 0).
static const uint8_t kDirectionArrows[8][7] = {
    {0x08, 0x1C, 0x2A, 0x08, 0x08, 0x08, 0x00},  // N  ↑
    {0x07, 0x03, 0x05, 0x08, 0x10, 0x20, 0x00},  // NE ↗
    {0x00, 0x04, 0x02, 0x7F, 0x02, 0x04, 0x00},  // E  →
    {0x20, 0x10, 0x08, 0x05, 0x03, 0x07, 0x00},  // SE ↘
    {0x08, 0x08, 0x08, 0x2A, 0x1C, 0x08, 0x00},  // S  ↓
    {0x02, 0x04, 0x08, 0x50, 0x60, 0x70, 0x00},  // SW ↙
    {0x00, 0x10, 0x20, 0x7F, 0x20, 0x10, 0x00},  // W  ←
    {0x70, 0x60, 0x50, 0x08, 0x04, 0x02, 0x00},  // NW ↖
};

// Convert an absolute compass bearing (0–359°) to an 8-sector arrow index
// (0=N … 7=NW) relative to the screen's facing direction from UserConfiguration.
static uint8_t bearingToSector(float bearingDeg)
{
    static const struct { const char *name; float deg; } kFacings[] = {
        {"N",0},{"NNE",22.5f},{"NE",45},{"ENE",67.5f},
        {"E",90},{"ESE",112.5f},{"SE",135},{"SSE",157.5f},
        {"S",180},{"SSW",202.5f},{"SW",225},{"WSW",247.5f},
        {"W",270},{"WNW",292.5f},{"NW",315},{"NNW",337.5f},
    };
    float facingDeg = 0.0f;
    for (const auto &f : kFacings)
        if (strcmp(g_config.screen_facing, f.name) == 0)
            { facingDeg = f.deg; break; }

    // Relative bearing: 0° = towards what the top of the screen faces
    float rel = fmodf(bearingDeg - facingDeg + 360.0f, 360.0f);
    return (uint8_t)((int)((rel + 22.5f) / 45.0f) % 8);
}

// ── Airplane fallback icon ────────────────────────────────────────────────────
static const uint8_t kAirplaneIcon[32][32] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

// Returns the brightness the panel should be set to right now.
static uint8_t effectiveBrightness()
{
    return isNightActive() ? g_config.night_brightness : g_config.display_brightness;
}

NeoMatrixDisplay::NeoMatrixDisplay() {}

NeoMatrixDisplay::~NeoMatrixDisplay()
{
    if (_matrix)
    {
        delete _matrix;
        _matrix = nullptr;
    }
}

bool NeoMatrixDisplay::initialize()
{
    _matrixWidth  = HardwareConfiguration::DISPLAY_MATRIX_WIDTH;
    _matrixHeight = HardwareConfiguration::DISPLAY_MATRIX_HEIGHT;

    HUB75_I2S_CFG mxconfig(
        HardwareConfiguration::DISPLAY_MATRIX_WIDTH,
        HardwareConfiguration::DISPLAY_MATRIX_HEIGHT,
        HardwareConfiguration::DISPLAY_TILES_X
    );
    mxconfig.gpio.e    = 18;
    mxconfig.driver    = HUB75_I2S_CFG::ICN2038S;
    mxconfig.clkphase  = false;
    mxconfig.latch_blanking = 1;

    _matrix = new MatrixPanel_I2S_DMA(mxconfig);
    _matrix->begin();
    _matrix->setTextWrap(false);
    _matrix->setTextSize(1);
    _matrix->setRotation(g_config.display_flip ? 2 : 0);
    _lastFlip = g_config.display_flip;
    _matrix->setBrightness8(g_config.display_brightness);
    _lastBrightness = g_config.display_brightness;
    clear();
    _currentFlightIndex = 0;
    _lastCycleMs = millis();
    return true;
}

void NeoMatrixDisplay::clear()
{
    if (_matrix)
        _matrix->fillScreen(0);
}

// ── Internal helpers ─────────────────────────────────────────────────────────

void NeoMatrixDisplay::drawTextLine(int16_t x, int16_t y, const String &text,
                                    uint16_t color, uint8_t size)
{
    _matrix->setTextSize(size);
    _matrix->setCursor(x, y);
    _matrix->setTextColor(color);
    for (size_t i = 0; i < (size_t)text.length(); ++i)
        _matrix->write(text[i]);
}

void NeoMatrixDisplay::drawTextLineFit(int16_t x, int16_t y, const String &text,
                                       int maxW, uint16_t color, uint8_t preferredSize)
{
    // Pick the largest size where the text fits; always fall back to size-1
    uint8_t size   = preferredSize;
    int     charW  = (size >= 2) ? kCharW2 : kCharW1;

    if ((int)text.length() * charW > maxW && size > 1)
    {
        size  = 1;
        charW = kCharW1;
    }

    drawTextLine(x, y, truncate(text, maxW / charW), color, size);
}

void NeoMatrixDisplay::drawOuterBorder(uint16_t color)
{
    for (int t = 0; t < kOuterBorder; ++t)
        _matrix->drawRect(t, t, _matrixWidth - 2 * t, _matrixHeight - 2 * t, color);
}

void NeoMatrixDisplay::drawLogo(int16_t originX, int16_t originY,
                                const std::vector<uint16_t> &pixels)
{
    if (pixels.size() != (size_t)AirlineLogo::WIDTH * AirlineLogo::HEIGHT)
        return;
    for (uint8_t row = 0; row < AirlineLogo::HEIGHT; ++row)
        for (uint8_t col = 0; col < AirlineLogo::WIDTH; ++col)
        {
            uint16_t c = pixels[row * AirlineLogo::WIDTH + col];
            if (c == kTransparentRGB565) continue;

            // PNG sources (Airhex CDN and most online converters) emit pixels
            // whose R and B channels are swapped relative to the RGB565 bit
            // layout this display expects (R in bits 15:11, B in bits 4:0).
            // Swap them here so the stored .bin files don't need re-generation.
            uint16_t r = (c >> 11) & 0x1F;
            uint16_t g = (c >>  5) & 0x3F;
            uint16_t b =  c        & 0x1F;
            c = (b << 11) | (g << 5) | r;

            int16_t px = originX + col, py = originY + row;
            if (px < 0 || px >= (int16_t)_matrixWidth  ||
                py < 0 || py >= (int16_t)_matrixHeight) continue;
            _matrix->drawPixel(px, py, c);
        }
}

void NeoMatrixDisplay::drawAirplaneIcon(int16_t originX, int16_t originY, uint16_t color)
{
    for (uint8_t row = 0; row < 32; ++row)
        for (uint8_t col = 0; col < 32; ++col)
            if (kAirplaneIcon[row][col])
                _matrix->drawPixel(originX + col, originY + row, color);
}

String NeoMatrixDisplay::truncate(const String &text, int maxChars)
{
    if ((int)text.length() <= maxChars)
        return text;
    if (maxChars <= 3)
        return text.substring(0, maxChars);
    return text.substring(0, maxChars - 3) + String("...");
}

void NeoMatrixDisplay::drawDirectionArrow(int16_t x, int16_t y,
                                          uint8_t sectorIdx, uint16_t color)
{
    if (sectorIdx >= 8) return;
    const uint8_t *rows = kDirectionArrows[sectorIdx];
    for (uint8_t row = 0; row < 7; ++row)
    {
        uint8_t bits = rows[row];
        for (uint8_t col = 0; col < 7; ++col)
            if (bits & (1u << (6 - col)))
                _matrix->drawPixel(x + col, y + row, color);
    }
}

String NeoMatrixDisplay::buildTelemetryLine1(const FlightInfo &f)
{
    // Format: "36,100ft  494kt"
    // Altitude and speed only — heading moves to line 2.
    // Worst case: "42,000ft  900kt" = 15 chars, well within the 20-char limit.
    String s;
    if (!isnan(f.baro_altitude))
    {
        int ft = (int)round(f.baro_altitude);
        if (ft >= 1000)
        {
            int rem = ft % 1000;
            s += String(ft / 1000) + ",";
            if (rem < 100) s += "0";
            if (rem < 10)  s += "0";
            s += String(rem);
        }
        else
        {
            s += String(ft);
        }
        s += "ft";
    }
    if (!isnan(f.velocity))
    {
        if (s.length()) s += "  ";
        s += String((int)round(f.velocity * 1.15078f)) + "mph";
    }
    return s.length() ? s : String("No data");
}

String NeoMatrixDisplay::buildTelemetryLine2(const FlightInfo &f)
{
    // Format: "332*  +121fpm"
    // Heading on the left, vertical rate after — distance is drawn separately, right-anchored.
    // '*' substitutes for the degree symbol (0xB0) which the built-in GFX font does not have.
    String s;
    if (!isnan(f.heading))
        s += String((int)round(f.heading)) + "*";

    if (!isnan(f.vertical_rate))
    {
        if (s.length()) s += "  ";
        int vr = (int)round(f.vertical_rate);
        if (vr >= 0) s += "+";
        s += String(vr) + "fpm";
    }
    return s;
}

// ── Flight card rendering ─────────────────────────────────────────────────────

void NeoMatrixDisplay::drawFlightText(int16_t x, int16_t y, int maxW,
                                      const FlightInfo &f, uint16_t color)
{
    // Line 1: Flight number (when logo is present and config says so), else airline name
    String line1;
    if (g_config.show_flight_number_with_logo && !f.airline_logo_rgb565.empty())
    {
        // Logo is taking the left column — use that space freed on line 1 for the
        // flight number.  Prefer IATA ident (e.g. "U2 73CA"), fall back to callsign.
        line1 = f.ident_iata.length() ? f.ident_iata : f.ident;
    }
    else
    {
        line1 = f.airline_display_name_full.length() ? f.airline_display_name_full
              : (f.operator_iata.length()            ? f.operator_iata
              : (f.operator_icao.length()            ? f.operator_icao : f.operator_code));
    }
    drawTextLineFit(x, y + kLine1OffsetY, line1, maxW, color, 1);

    // Line 2: Route IATA/ICAO — size-2 when it fits, size-1 otherwise
    String orig = f.origin.code_iata.length()      ? f.origin.code_iata      : f.origin.code_icao;
    String dest = f.destination.code_iata.length() ? f.destination.code_iata : f.destination.code_icao;
    drawTextLineFit(x, y + kLine2OffsetY, orig + "-" + dest, maxW, color, 2);

    // Lines 3 / 3b: aircraft type and registration.
    // Each can be individually disabled via config.  When one is disabled the
    // other is promoted to line 3 so the space is used efficiently.
    {
        const String acft = f.aircraft_display_name_short.length() ? f.aircraft_display_name_short
                          : f.aircraft_code.length()               ? f.aircraft_code
                          : String();

        const bool wantType = g_config.show_aircraft_type && acft.length() > 0;
        const bool wantReg  = g_config.show_aircraft_registration && f.registration.length() > 0;

        String line3Text, line3bText;
        if (wantType && wantReg)
        {
            // Both visible — swap order if configured, otherwise type above reg
            if (g_config.swap_aircraft_type_reg)
            {
                line3Text  = f.registration;
                line3bText = acft;
            }
            else
            {
                line3Text  = acft;
                line3bText = f.registration;
            }
        }
        else if (wantType)
        {
            line3Text = acft;            // reg disabled — type on line 3
        }
        else if (wantReg)
        {
            line3Text = f.registration;  // type disabled — reg promoted to line 3
        }

        if (line3Text.length())
            drawTextLine(x, y + kLine3OffsetY,  truncate(line3Text,  maxW / kCharW1), color, 1);
        if (line3bText.length())
            drawTextLine(x, y + kLine3bOffsetY, truncate(line3bText, maxW / kCharW1), color, 1);
    }

    // Lines 4 & 5 — delegate to shared helper (also used by updateTelemetryLines)
    drawTelemetryLines(y, f);
}

// Draw lines 4 & 5 (altitude/speed and heading/vspeed/distance) at the
// standard Y positions relative to textY.  Both full-card renders and
// in-place telemetry updates call this.
void NeoMatrixDisplay::drawTelemetryLines(int16_t textY, const FlightInfo &f)
{
    // Telemetry spans the full content width — including under the logo column
    const int16_t btX      = kInset;
    const int     btW      = _matrixWidth - 2 * kInset;
    const int16_t rightEdge = (int16_t)(_matrixWidth - kInset);

    const uint16_t color = _matrix->color565(g_config.text_color_r,
                                             g_config.text_color_g,
                                             g_config.text_color_b);
    const uint16_t grey  = _matrix->color565(100, 100, 100);

    // Line 4: Altitude  Speed  [bearing arrow right-aligned]
    const bool hasArrow  = !isnan(f.bearing_deg);
    const int  line4MaxW = btW - (hasArrow ? kArrowW + kArrowGap : 0);
    drawTextLine(btX, textY + kLine4OffsetY,
                 truncate(buildTelemetryLine1(f), line4MaxW / kCharW1), color, 1);
    if (hasArrow)
        drawDirectionArrow(rightEdge - kArrowW, textY + kLine4OffsetY,
                           bearingToSector(f.bearing_deg), grey);

    // Line 5: Heading*  VSpeed  [distance right-aligned]
    String distStr;
    if (!isnan(f.distance_km))
        distStr = String((int)round(f.distance_km * 0.621371f)) + "mi";
    const int line5MaxW = btW - (int)distStr.length() * kCharW1;
    drawTextLine(btX, textY + kLine5OffsetY,
                 truncate(buildTelemetryLine2(f), line5MaxW / kCharW1), color, 1);
    if (distStr.length())
        drawTextLine(rightEdge - (int16_t)((int)distStr.length() * kCharW1),
                     textY + kLine5OffsetY, distStr, grey, 1);
}

// Clear only the telemetry rows and repaint them from fresh data.
// The rest of the card (logo, airline, route, aircraft) is untouched.
void NeoMatrixDisplay::updateTelemetryLines(const FlightInfo &f)
{
    const int16_t textY = kInset + 1;
    const int16_t lineY = textY + kLine4OffsetY;           // first pixel of line 4
    const int16_t lineH = (kLine5OffsetY - kLine4OffsetY)  // gap between lines
                        + kCharH1;                          // height of line 5

    // Black out the telemetry band
    _matrix->fillRect(0, lineY, (int16_t)_matrixWidth, lineH, 0);

    // Restore the left/right border pixels within that band if border is on
    if (g_config.display_border)
    {
        const uint16_t white = _matrix->color565(255, 255, 255);
        for (int16_t y = lineY; y < lineY + lineH; ++y)
        {
            _matrix->drawPixel(0, y, white);
            _matrix->drawPixel((int16_t)(_matrixWidth - 1), y, white);
        }
    }

    drawTelemetryLines(textY, f);
}

void NeoMatrixDisplay::displaySingleFlightCard(const FlightInfo &f)
{
    const uint16_t color = _matrix->color565(g_config.text_color_r,
                                             g_config.text_color_g,
                                             g_config.text_color_b);
    const uint16_t white = _matrix->color565(255, 255, 255);

    if (g_config.display_border)
        drawOuterBorder(white);

    const int16_t textY = kInset + 1;  // 1px top pad within content area

    // Logo/icon: vertically centred within the top three-line text block
    const int16_t topBlockH = kLine3OffsetY + kCharH2;  // 41px
    const int16_t iconY     = textY + (topBlockH - (int16_t)AirlineLogo::HEIGHT) / 2;

    if (!f.airline_logo_rgb565.empty())
        drawLogo(kInset, iconY, f.airline_logo_rgb565);
    else
        drawAirplaneIcon(kInset, iconY, _matrix->color565(0, 100, 255));

    // Text in the right column
    const int cW    = _matrixWidth - 2 * kInset;
    drawFlightText(kInset + kLogoColW, textY, cW - kLogoColW, f, color);
}

void NeoMatrixDisplay::displayFlights(const std::vector<FlightInfo> &flights)
{
    if (_matrix == nullptr)
        return;

    // Apply brightness — respects night mode schedule when enabled
    uint8_t targetBrightness = effectiveBrightness();
    if (targetBrightness != _lastBrightness)
    {
        _lastBrightness = targetBrightness;
        _matrix->setBrightness8(targetBrightness);
    }

    // Apply 180° flip changes from web UI — triggers a full redraw
    if (g_config.display_flip != _lastFlip)
    {
        _lastFlip = g_config.display_flip;
        _matrix->setRotation(g_config.display_flip ? 2 : 0);
        _needsFullRedraw = true;
        _needsTelemetryUpdate = false;
    }

    if (!flights.empty())
    {
        const unsigned long now        = millis();
        const unsigned long intervalMs = g_config.display_cycle_seconds * 1000UL;

        // Advance to the next flight when the cycle timer expires.
        // This triggers a full card redraw, not just a telemetry refresh.
        if (flights.size() > 1 && now - _lastCycleMs >= intervalMs)
        {
            _lastCycleMs        = now;
            _currentFlightIndex = (_currentFlightIndex + 1) % flights.size();
            _needsFullRedraw    = true;
            _needsTelemetryUpdate = false;
        }
        else if (flights.size() == 1)
        {
            _currentFlightIndex = 0;
        }

        // Guard against the flight list shrinking while an index is live
        if (_currentFlightIndex >= flights.size())
        {
            _currentFlightIndex = 0;
            _needsFullRedraw    = true;
            _needsTelemetryUpdate = false;
        }

        const FlightInfo &f = flights[_currentFlightIndex];

        // If the flight identity at the current slot changed (e.g. a plane
        // left range and a new one took its position), upgrade to a full redraw
        // so the logo and route block reflects the new aircraft.
        if (_needsTelemetryUpdate && f.ident != _renderedCallsign)
        {
            _needsFullRedraw    = true;
            _needsTelemetryUpdate = false;
        }

        if (_needsFullRedraw)
        {
            _needsFullRedraw    = false;
            _needsTelemetryUpdate = false;
            _renderedCallsign   = f.ident;
            _matrix->fillScreen(0);
            displaySingleFlightCard(f);
        }
        else if (_needsTelemetryUpdate)
        {
            // New data arrived but the same flight card is showing — update
            // only the two telemetry lines without touching the rest of the card.
            _needsTelemetryUpdate = false;
            updateTelemetryLines(f);
        }
    }
    else
    {
        if (_needsFullRedraw || _needsTelemetryUpdate)
        {
            _needsFullRedraw    = false;
            _needsTelemetryUpdate = false;
            _renderedCallsign   = "";
            displayLoadingScreen();
        }
    }
}

void NeoMatrixDisplay::displayLoadingScreen()
{
    if (_matrix == nullptr)
        return;

    _matrix->fillScreen(0);

    if (g_config.display_border)
    {
        const uint16_t borderColor = _matrix->color565(255, 255, 255);
        drawOuterBorder(borderColor);
    }

    const String loadingText = "...";
    const int textWidth = loadingText.length() * kCharW1;
    const int16_t x = (_matrixWidth  - textWidth) / 2;
    const int16_t y = (_matrixHeight - kCharH1)   / 2 - 2;

    const uint16_t textColor = _matrix->color565(g_config.text_color_r,
                                                  g_config.text_color_g,
                                                  g_config.text_color_b);
    drawTextLine(x, y, loadingText, textColor, 1);
}

void NeoMatrixDisplay::displayMessage(const String &message)
{
    if (_matrix == nullptr)
        return;

    _matrix->fillScreen(0);

    const uint16_t textColor = _matrix->color565(g_config.text_color_r,
                                                  g_config.text_color_g,
                                                  g_config.text_color_b);
    const int maxCols = _matrixWidth / kCharW1;
    const int16_t y   = (_matrixHeight - kCharH1) / 2;
    drawTextLine(0, y, truncate(message, maxCols), textColor, 1);
}

void NeoMatrixDisplay::showLoading()
{
    displayLoadingScreen();
}
