#pragma once

/*
Purpose: Lightweight log capture — writes to Serial and maintains a ring-buffer
of recent log lines that the web UI can stream via /api/log.

Usage:
  Log.print() / Log.println() / Log.printf() — identical call surface to Serial.
  The WebConfig class reads the ring-buffer to serve the log panel.
*/

#include <Arduino.h>
#include <deque>
#include <vector>
#include <freertos/semphr.h>

class TelnetLogger : public Print
{
public:
    TelnetLogger();

    // Number of completed log lines retained in the ring-buffer.
    static constexpr size_t MAX_LINES = 150;

    // Print overrides — write to Serial AND the ring-buffer.
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;

    // Retrieve log lines starting at sequence number `cursor`.
    // Fills `out` with any lines in [cursor, end).
    // Sets `nextCursor` to the sequence number to pass on the next call.
    // Thread-safe: may be called from a different task than write().
    void getLines(uint32_t cursor,
                  std::vector<String> &out,
                  uint32_t &nextCursor) const;

private:
    std::deque<String> _lines;    // completed lines (newest at back)
    String             _pending;  // characters received since last '\n'
    uint32_t           _seqBase = 0;  // sequence number of _lines.front()
    uint32_t           _seqNext = 0;  // sequence number for the next new line

    // Protects _lines, _pending, _seqBase, _seqNext, _lineStart across tasks.
    // Declared mutable so const getLines() can acquire/release it.
    mutable SemaphoreHandle_t _mutex = nullptr;

    bool _lineStart = true;  // true when the next byte begins a new line

    // Internal helpers — must be called with _mutex held.
    void pushLine(const String &line);
    void writeByte(uint8_t c);
};

// Global singleton — include this header and call Log.printf() etc.
extern TelnetLogger Log;
