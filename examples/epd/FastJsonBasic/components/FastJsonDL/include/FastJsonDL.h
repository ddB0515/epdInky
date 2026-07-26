// FastJsonDL — JSON Domain Language renderer for E-Ink displays
// Copyright (c) 2024, see LICENSE file for details
//
// Parses a JSON layout description and issues the corresponding drawing
// commands to a FASTEPD display instance.  Designed as an ESP-IDF component
// targeting ESP32 family MCUs.
//
// Example JSON:
//   {
//     "display_bpp": 4,
//     "clear": true,
//     "items": [
//       { "type": "drawString", "font": "Ubuntu40", "string": "Hello!",
//         "x": 10, "y": 70, "c": 0 }
//     ]
//   }
//
// NOTE — drawString y coordinate:
//   FastEPD BB-format fonts use y as the TEXT BASELINE.  For a 40 pt font the
//   ascender is ~50 px, so set y >= 50 to keep the text on-screen.
//
// Supported item types:
//   drawString  — x, y, string, c, [font]
//   fillRect    — x, y, w, h, c
//   drawRect    — x, y, w, h, c
//   drawLine    — x1, y1, x2, y2, c
//   fillCircle  — x, y, r, c
//   drawCircle  — x, y, r, c
//   p           — x, y, [c]   (drawPixel, short alias to keep JSON small)
//   loadG5Image — data, x, y, w, h, [fg], [bg]
//
#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Font registry entry
// ---------------------------------------------------------------------------
// Maps a JSON font name (e.g. "Ubuntu40") to the in-memory font data
// produced by the FastEPD font-convert tool.  The array passed to
// setFontRegistry() must remain valid for the lifetime of the FastJsonDL
// object.
struct FastJsonDLFont {
    const char* name;   ///< Name referenced in JSON "font" field
    const void* data;   ///< Font data pointer accepted by FASTEPD::setFont()
};

// ---------------------------------------------------------------------------
// Forward declaration — hides the cJSON dependency from consumers of this
// header while keeping the private render helpers strongly typed in the
// implementation file.
// ---------------------------------------------------------------------------
struct cJSON;
class FASTEPD;

// ---------------------------------------------------------------------------
// FastJsonDL
// ---------------------------------------------------------------------------
class FastJsonDL {
public:
    /// Construct with a reference to an already-initialised FASTEPD instance.
    explicit FastJsonDL(FASTEPD& epd);

    // --- Rendering ----------------------------------------------------------

    /// Parse and render a JSON layout buffer of known length.
    /// @return true on success; false on parse or render error.
    bool renderJson(const char* json, size_t len);

    /// Parse and render a null-terminated JSON layout string.
    /// @return true on success; false on parse or render error.
    bool renderJsonString(const char* json);

    // --- Error reporting ----------------------------------------------------

    /// Return a human-readable description of the last error, or an empty
    /// string if no error has occurred since the last successful render.
    const char* getLastError() const;

    // --- Configuration ------------------------------------------------------

    /// Override the logical display dimensions.
    /// By default they are read from the FASTEPD panel at construction time.
    void setDisplaySize(uint16_t width, uint16_t height);

    /// Set the default bits-per-pixel mode used when the JSON payload does not
    /// contain a "display_bpp" field.  Valid values: 1, 2, 4.
    /// Initial value inherits the current FASTEPD mode at construction time.
    void setDefaultBpp(uint8_t bpp);

    /// Register a named-font table.
    /// @param fonts  Pointer to an array of FastJsonDLFont entries.
    /// @param count  Number of entries in the array.
    void setFontRegistry(const FastJsonDLFont* fonts, size_t count);

private:
    FASTEPD& _epd;
    uint16_t _width;
    uint16_t _height;
    uint8_t  _bpp;
    char     _lastError[128];

    const FastJsonDLFont* _fonts;
    size_t                _fontCount;

    // Internal helpers
    bool parseAndRender(const char* json, size_t len);
    bool renderItem(cJSON* item);
    bool renderText(cJSON* item);
    bool renderFillRect(cJSON* item);
    bool renderDrawRect(cJSON* item);
    bool renderDrawLine(cJSON* item);
    bool renderFillCircle(cJSON* item);
    bool renderDrawCircle(cJSON* item);
    bool renderDrawPixel(cJSON* item);
    bool renderLoadG5Image(cJSON* item);

    const void* findFont(const char* name) const;
};
