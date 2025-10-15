/*
 * Minimal TTF atlas builder interface. Real rasterization may use stb_truetype.
 */

#ifndef TRUETYPEFONT_H
#define TRUETYPEFONT_H

#include "declarations.h"
#include "image.h"

struct TrueTypeAtlasResult {
    ImagePtr image;
    int tileWidth = 0;
    int tileHeight = 0;
    int glyphHeight = 0;
    int yOffset = 0;
    int spaceWidth = 0;
    int underlineOffset = 0;
};

class TrueTypeFont {
public:
    static bool rasterizeAtlas(const uint8_t* ttfData,
                               int ttfSize,
                               const std::string& fontFamilyName,
                               int pixelHeight,
                               int firstGlyph,
                               int lastGlyph,
                               int spacingX,
                               int spacingY,
                               int yOffset,
                               int spaceWidth,
                               TrueTypeAtlasResult& out);
};

#endif