#include "FastJsonDL.h"
#include "FastEPD.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

// Optional DEFLATE decompression via the lbernstone__miniz ESP-IDF component.
// When the component is present, renderDeflatedJson() decompresses a raw
// DEFLATE stream (RFC 1951, no zlib/gzip header) before rendering.
#if __has_include("miniz.h")
#  include "miniz.h"
#  define FASTJSONDL_HAVE_MINIZ 1
#else
#  define FASTJSONDL_HAVE_MINIZ 0
#endif

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

static uint8_t modeToBpp(int mode)
{
    switch (mode) {
        case BB_MODE_2BPP: return 2;
        case BB_MODE_4BPP: return 4;
        default:           return 1;
    }
}

FastJsonDL::FastJsonDL(FASTEPD& epd)
    : _epd(epd)
    , _width(0)
    , _height(0)
    , _bpp(1)
    , _lastDecompMs(0)
    , _lastRenderMs(0)
    , _fonts(nullptr)
    , _fontCount(0)
{
    _lastError[0] = '\0';
    // Initialise logical dimensions from the panel; callers may override them
    // with setDisplaySize() if needed.
    _width  = static_cast<uint16_t>(_epd.width());
    _height = static_cast<uint16_t>(_epd.height());
    // Inherit the active FastEPD mode so omitted "display_bpp" does not
    // unexpectedly force 1BPP when the caller already selected 2/4BPP.
    _bpp = modeToBpp(_epd.getMode());
}

void FastJsonDL::setDisplaySize(uint16_t width, uint16_t height)
{
    _width  = width;
    _height = height;
}

void FastJsonDL::setDefaultBpp(uint8_t bpp)
{
    _bpp = bpp;
}

void FastJsonDL::setFontRegistry(const FastJsonDLFont* fonts, size_t count)
{
    _fonts     = fonts;
    _fontCount = count;
}

const char* FastJsonDL::getLastError() const
{
    return _lastError;
}

uint32_t FastJsonDL::getLastDecompMs() const
{
    return _lastDecompMs;
}

uint32_t FastJsonDL::getLastRenderMs() const
{
    return _lastRenderMs;
}

// ---------------------------------------------------------------------------
// Public render entry points
// ---------------------------------------------------------------------------

bool FastJsonDL::renderJsonString(const char* json)
{
    if (!json) {
        snprintf(_lastError, sizeof(_lastError), "Null JSON string");
        return false;
    }
    _lastDecompMs = 0;
    uint64_t t0 = esp_timer_get_time();
    bool result  = parseAndRender(json, strlen(json));
    _lastRenderMs = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    return result;
}

bool FastJsonDL::renderJson(const char* json, size_t len)
{
    if (!json) {
        snprintf(_lastError, sizeof(_lastError), "Null JSON buffer");
        return false;
    }
    _lastDecompMs = 0;
    uint64_t t0 = esp_timer_get_time();
    bool result  = parseAndRender(json, len);
    _lastRenderMs = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    return result;
}

bool FastJsonDL::renderDeflatedJson(const uint8_t* compressedData, size_t compressedLen)
{
    if (!compressedData || compressedLen == 0) {
        snprintf(_lastError, sizeof(_lastError),
                 "renderDeflatedJson: null or empty compressed buffer");
        return false;
    }

#if FASTJSONDL_HAVE_MINIZ
    // tinfl_decompress_mem_to_heap would place tinfl_decompressor (~34 KB) on
    // the task stack, causing a stack-overflow fault on small ESP32 tasks.
    // We replicate the same algorithm but allocate the decompressor on the heap.
    tinfl_decompressor* decomp = static_cast<tinfl_decompressor*>(
        malloc(sizeof(tinfl_decompressor)));
    if (!decomp) {
        snprintf(_lastError, sizeof(_lastError),
                 "renderDeflatedJson: out of memory allocating decompressor state");
        return false;
    }
    tinfl_init(decomp);

    mz_uint8* pBuf   = nullptr;
    size_t    outLen = 0;
    size_t    outCap = 0;
    size_t    srcOfs = 0;

    uint64_t t_decomp = esp_timer_get_time();

    for (;;) {
        size_t srcSize = compressedLen - srcOfs;
        size_t dstSize = outCap - outLen;

        tinfl_status status = tinfl_decompress(
            decomp,
            static_cast<const mz_uint8*>(compressedData) + srcOfs,
            &srcSize,
            pBuf,
            pBuf ? pBuf + outLen : nullptr,
            &dstSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF /* raw DEFLATE, no header */);

        if (status < 0 || status == TINFL_STATUS_NEEDS_MORE_INPUT) {
            free(pBuf);
            free(decomp);
            snprintf(_lastError, sizeof(_lastError),
                     "DEFLATE decompression failed (tinfl status %d)", static_cast<int>(status));
            return false;
        }

        srcOfs += srcSize;
        outLen += dstSize;

        if (status == TINFL_STATUS_DONE)
            break;

        // Grow the output buffer, mirroring tinfl_decompress_mem_to_heap.
        size_t newCap = outCap * 2;
        if (newCap < 128)
            newCap = 128;

        mz_uint8* pNew = static_cast<mz_uint8*>(realloc(pBuf, newCap));
        if (!pNew) {
            free(pBuf);
            free(decomp);
            snprintf(_lastError, sizeof(_lastError),
                     "DEFLATE decompression failed (out of memory growing output buffer)");
            return false;
        }
        pBuf   = pNew;
        outCap = newCap;
    }

    free(decomp);
    _lastDecompMs = (uint32_t)((esp_timer_get_time() - t_decomp) / 1000);

    if (!pBuf || outLen == 0) {
        free(pBuf);
        snprintf(_lastError, sizeof(_lastError),
                 "DEFLATE decompression produced empty output");
        return false;
    }

    uint64_t t_render = esp_timer_get_time();
    bool result = parseAndRender(reinterpret_cast<const char*>(pBuf), outLen);
    _lastRenderMs = (uint32_t)((esp_timer_get_time() - t_render) / 1000);
    free(pBuf);
    return result;
#else
    snprintf(_lastError, sizeof(_lastError),
             "DEFLATE support not compiled in — add lbernstone__miniz to your project");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Core parse-and-render pipeline
// ---------------------------------------------------------------------------

// Helper: map a bits-per-pixel integer to the FastEPD BB_MODE_* constant.
static int bppToMode(int bpp)
{
    switch (bpp) {
        case 2:  return BB_MODE_2BPP;
        case 4:  return BB_MODE_4BPP;
        default: return BB_MODE_1BPP;
    }
}

// Helper: map a bits-per-pixel mode to its "white" pixel value.
static uint8_t bppToWhite(int bpp)
{
    switch (bpp) {
        case 2:  return 0x03;
        case 4:  return 0x0f;
        default: return BBEP_WHITE; // 1BPP white
    }
}

bool FastJsonDL::parseAndRender(const char* json, size_t len)
{
    _lastError[0] = '\0';

    cJSON* root = cJSON_ParseWithLength(json, len);
    if (!root) {
        const char* errPtr = cJSON_GetErrorPtr();
        snprintf(_lastError, sizeof(_lastError),
                 "JSON parse error near: %.40s", errPtr ? errPtr : "(unknown)");
        return false;
    }

    // Honour an optional top-level "display_bpp" field; fall back to the
    // default bpp configured via setDefaultBpp().
    cJSON* bppNode = cJSON_GetObjectItemCaseSensitive(root, "display_bpp");
    int effectiveBpp = cJSON_IsNumber(bppNode) ? bppNode->valueint : _bpp;
    _epd.setMode(bppToMode(effectiveBpp));

    // Apply optional top-level "rotation" field only when present.
    cJSON* rotationNode = cJSON_GetObjectItemCaseSensitive(root, "rotation");
    if (cJSON_IsNumber(rotationNode)) {
        _epd.setRotation(rotationNode->valueint);
        // Re-read the logical display dimensions after rotation so that the
        // width/height stored in this instance reflect the rotated coordinate
        // space.  FastEPD swaps width() and height() for 90°/270° rotations,
        // which prevents portrait-mode items near the right/bottom edge from
        // being silently clipped.
        uint16_t rw = static_cast<uint16_t>(_epd.width());
        uint16_t rh = static_cast<uint16_t>(_epd.height());
        if (rw > 0 && rh > 0) {
            _width  = rw;
            _height = rh;
        }
    }

    // Optional top-level "clear" field: when true, fill the framebuffer with
    // white before rendering any items.  This avoids uninitialised pixel data
    // (visible as vertical stripes) when fullUpdate() is called after render.
    cJSON* clearNode = cJSON_GetObjectItemCaseSensitive(root, "clear");
    if (cJSON_IsTrue(clearNode)) {
        _epd.fillScreen(bppToWhite(effectiveBpp));
    }

    cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(items)) {
        snprintf(_lastError, sizeof(_lastError),
                 "No 'items' array found in JSON");
        cJSON_Delete(root);
        return false;
    }

    bool  success = true;
    cJSON* item   = nullptr;
    cJSON_ArrayForEach(item, items) {
        if (!renderItem(item)) {
            // _lastError has already been populated by the failing call.
            success = false;
            break;
        }
    }

    cJSON_Delete(root);
    return success;
}

// ---------------------------------------------------------------------------
// Item dispatcher
// ---------------------------------------------------------------------------

bool FastJsonDL::renderItem(cJSON* item)
{
    cJSON* typeNode = cJSON_GetObjectItemCaseSensitive(item, "type");
    if (!cJSON_IsString(typeNode) || !typeNode->valuestring) {
        snprintf(_lastError, sizeof(_lastError), "Item missing 'type' field");
        return false;
    }

    const char* type = typeNode->valuestring;

    if (strcmp(type, "drawString")  == 0) return renderText(item);
    if (strcmp(type, "fillRect")    == 0) return renderFillRect(item);
    if (strcmp(type, "drawRect")    == 0) return renderDrawRect(item);
    if (strcmp(type, "drawLine")    == 0) return renderDrawLine(item);
    if (strcmp(type, "fillCircle")  == 0) return renderFillCircle(item);
    if (strcmp(type, "drawCircle")  == 0) return renderDrawCircle(item);
    if (strcmp(type, "p")           == 0) return renderDrawPixel(item);
    if (strcmp(type, "loadG5Image") == 0) return renderLoadG5Image(item);
    snprintf(_lastError, sizeof(_lastError),
             "Unknown item type: %.64s", type);
    return false;
}

// ---------------------------------------------------------------------------
// Individual item renderers
// ---------------------------------------------------------------------------

bool FastJsonDL::renderText(cJSON* item)
{
    cJSON* fontNode   = cJSON_GetObjectItemCaseSensitive(item, "font");
    cJSON* stringNode = cJSON_GetObjectItemCaseSensitive(item, "string");
    cJSON* xNode      = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode      = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* colorNode  = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsString(stringNode) || !stringNode->valuestring) {
        snprintf(_lastError, sizeof(_lastError),
                 "drawString item missing 'string' field");
        return false;
    }

    int x     = cJSON_IsNumber(xNode)     ? xNode->valueint     : 0;
    int y     = cJSON_IsNumber(yNode)     ? yNode->valueint     : 0;
    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;

    // Resolve a named font from the registry when requested.
    if (cJSON_IsString(fontNode) && fontNode->valuestring) {
        const void* fontData = findFont(fontNode->valuestring);
        if (!fontData) {
            snprintf(_lastError, sizeof(_lastError),
                     "Font not found in registry: %.64s",
                     fontNode->valuestring);
            return false;
        }
        _epd.setFont(fontData);
    }

    _epd.setTextColor(color);
    _epd.drawString(stringNode->valuestring, x, y);
    return true;
}

bool FastJsonDL::renderFillRect(cJSON* item)
{
    cJSON* xNode     = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode     = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* wNode     = cJSON_GetObjectItemCaseSensitive(item, "w");
    cJSON* hNode     = cJSON_GetObjectItemCaseSensitive(item, "h");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) ||
        !cJSON_IsNumber(wNode) || !cJSON_IsNumber(hNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "fillRect item missing required numeric fields (x, y, w, h)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.fillRect(xNode->valueint, yNode->valueint,
                  wNode->valueint, hNode->valueint,
                  static_cast<uint8_t>(color));
    return true;
}

bool FastJsonDL::renderDrawRect(cJSON* item)
{
    cJSON* xNode     = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode     = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* wNode     = cJSON_GetObjectItemCaseSensitive(item, "w");
    cJSON* hNode     = cJSON_GetObjectItemCaseSensitive(item, "h");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) ||
        !cJSON_IsNumber(wNode) || !cJSON_IsNumber(hNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "drawRect item missing required numeric fields (x, y, w, h)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.drawRect(xNode->valueint, yNode->valueint,
                  wNode->valueint, hNode->valueint,
                  static_cast<uint8_t>(color));
    return true;
}

bool FastJsonDL::renderDrawLine(cJSON* item)
{
    cJSON* x1Node    = cJSON_GetObjectItemCaseSensitive(item, "x1");
    cJSON* y1Node    = cJSON_GetObjectItemCaseSensitive(item, "y1");
    cJSON* x2Node    = cJSON_GetObjectItemCaseSensitive(item, "x2");
    cJSON* y2Node    = cJSON_GetObjectItemCaseSensitive(item, "y2");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(x1Node) || !cJSON_IsNumber(y1Node) ||
        !cJSON_IsNumber(x2Node) || !cJSON_IsNumber(y2Node)) {
        snprintf(_lastError, sizeof(_lastError),
                 "drawLine item missing required numeric fields (x1, y1, x2, y2)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.drawLine(x1Node->valueint, y1Node->valueint,
                  x2Node->valueint, y2Node->valueint, color);
    return true;
}

bool FastJsonDL::renderFillCircle(cJSON* item)
{
    cJSON* xNode     = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode     = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* rNode     = cJSON_GetObjectItemCaseSensitive(item, "r");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) ||
        !cJSON_IsNumber(rNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "fillCircle item missing required numeric fields (x, y, r)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.fillCircle(xNode->valueint, yNode->valueint, rNode->valueint,
                    static_cast<uint32_t>(color));
    return true;
}

bool FastJsonDL::renderDrawCircle(cJSON* item)
{
    cJSON* xNode     = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode     = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* rNode     = cJSON_GetObjectItemCaseSensitive(item, "r");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) ||
        !cJSON_IsNumber(rNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "drawCircle item missing required numeric fields (x, y, r)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.drawCircle(xNode->valueint, yNode->valueint, rNode->valueint,
                    static_cast<uint32_t>(color));
    return true;
}

bool FastJsonDL::renderDrawPixel(cJSON* item)
{
    cJSON* xNode     = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode     = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* colorNode = cJSON_GetObjectItemCaseSensitive(item, "c");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "p (drawPixel) item missing required numeric fields (x, y)");
        return false;
    }

    int color = cJSON_IsNumber(colorNode) ? colorNode->valueint : BBEP_BLACK;
    _epd.drawPixel(xNode->valueint, yNode->valueint, static_cast<uint8_t>(color));
    return true;
}

static bool parseG5Bytes(cJSON* dataNode, std::vector<uint8_t>& out,
                         char* err, size_t errSize)
{
    if (!cJSON_IsArray(dataNode)) {
        snprintf(err, errSize, "loadG5Image item missing 'data' byte array");
        return false;
    }

    out.clear();
    out.reserve(static_cast<size_t>(cJSON_GetArraySize(dataNode)));

    int idx = 0;
    cJSON* byteNode = nullptr;
    cJSON_ArrayForEach(byteNode, dataNode) {
        long value = -1;
        if (cJSON_IsNumber(byteNode)) {
            value = static_cast<long>(byteNode->valueint);
        } else if (cJSON_IsString(byteNode) && byteNode->valuestring) {
            char* end = nullptr;
            // Always parse string tokens as hexadecimal so that bare 2-char
            // values like "bf" or "13" are treated as 0xbf / 0x13.
            // strtol with base 16 also accepts the optional "0x" prefix.
            value = strtol(byteNode->valuestring, &end, 16);
            if (!end || *end != '\0') {
                snprintf(err, errSize,
                         "loadG5Image data[%d] must be a hex byte (e.g. \"bf\" or \"0xbf\")", idx);
                return false;
            }
        } else {
            snprintf(err, errSize,
                     "loadG5Image data[%d] must be a byte number", idx);
            return false;
        }

        if (value < 0 || value > 255) {
            snprintf(err, errSize,
                     "loadG5Image data[%d] out of byte range (0..255)", idx);
            return false;
        }
        out.push_back(static_cast<uint8_t>(value));
        ++idx;
    }

    if (out.empty()) {
        snprintf(err, errSize, "loadG5Image item has empty 'data' array");
        return false;
    }

    return true;
}

bool FastJsonDL::renderLoadG5Image(cJSON* item)
{
    cJSON* dataNode = cJSON_GetObjectItemCaseSensitive(item, "data");
    cJSON* xNode    = cJSON_GetObjectItemCaseSensitive(item, "x");
    cJSON* yNode    = cJSON_GetObjectItemCaseSensitive(item, "y");
    cJSON* wNode    = cJSON_GetObjectItemCaseSensitive(item, "w");
    cJSON* hNode    = cJSON_GetObjectItemCaseSensitive(item, "h");
    cJSON* fgNode   = cJSON_GetObjectItemCaseSensitive(item, "fg");
    cJSON* bgNode   = cJSON_GetObjectItemCaseSensitive(item, "bg");

    if (!cJSON_IsNumber(xNode) || !cJSON_IsNumber(yNode) ||
        !cJSON_IsNumber(wNode) || !cJSON_IsNumber(hNode)) {
        snprintf(_lastError, sizeof(_lastError),
                 "loadG5Image item missing required numeric fields (x, y, w, h)");
        return false;
    }

    std::vector<uint8_t> g5Data;
    if (!parseG5Bytes(dataNode, g5Data, _lastError, sizeof(_lastError))) {
        return false;
    }

    int fg = cJSON_IsNumber(fgNode) ? fgNode->valueint : BBEP_BLACK;
    int bg = cJSON_IsNumber(bgNode) ? bgNode->valueint : BBEP_WHITE;
    _epd.loadG5Image(g5Data.data(), xNode->valueint, yNode->valueint, fg, bg, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Font registry lookup
// ---------------------------------------------------------------------------

const void* FastJsonDL::findFont(const char* name) const
{
    if (!_fonts || !name) {
        return nullptr;
    }
    for (size_t i = 0; i < _fontCount; ++i) {
        if (_fonts[i].name && strcmp(_fonts[i].name, name) == 0) {
            return _fonts[i].data;
        }
    }
    return nullptr;
}