#pragma once

#include <Arduino.h>

namespace HardwareConfiguration
{
    // Physical tile size (pixels per 128x64 for recommended panel)
    static const uint16_t DISPLAY_TILE_PIXEL_W = 128;
    static const uint16_t DISPLAY_TILE_PIXEL_H = 64;

    // Tile arrangement (number of tiles horizontally and vertically)
    static const uint8_t DISPLAY_TILES_X = 1; // e.g., 2 tiles wide -> 256px
    static const uint8_t DISPLAY_TILES_Y = 1;  // e.g., 2 tiles high -> 128px

    // Derived matrix dimensions
    static const uint16_t DISPLAY_MATRIX_WIDTH = DISPLAY_TILE_PIXEL_W * DISPLAY_TILES_X;
    static const uint16_t DISPLAY_MATRIX_HEIGHT = DISPLAY_TILE_PIXEL_H * DISPLAY_TILES_Y;
}
