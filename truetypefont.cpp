/*
 * TrueType (.ttf) import delegando para TrueTypeFont::rasterizeAtlas (único ponto).
 */

#include "fontmanager.h"
#include "bitmapfont.h"
#include "image.h"

#include <framework/core/resourcemanager.h>
#include <framework/otml/otml.h>

#include <algorithm>
#include <sstream>
#include <vector>
#include <cmath>

// Usa a implementação central do rasterizador
#include "src/framework/graphics/truetypefont.h"

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool FontManager::importTTFFont(const std::string& fontName,
                                const std::string& ttfFile,
                                const int pixelHeight,
                                const int spacingX,
                                const int spacingY,
                                const int yOffset)
{
    // Ler arquivo TTF/OTF
    std::string ttfPath = ttfFile;
    if (!(g_resources.isFileType(ttfPath, "ttf") || g_resources.isFileType(ttfPath, "otf"))) {
        // tentar adivinhar extensão quando não fornecida
        ttfPath = g_resources.guessFilePath(ttfFile, "ttf");
        if (!g_resources.fileExists(ttfPath))
            ttfPath = g_resources.guessFilePath(ttfFile, "otf");
    }
    std::string ttfData;
    try {
        ttfData = g_resources.readFileContents(ttfPath);
    } catch (const stdext::exception& e) {
        g_logger.error("Unable to read TTF '{}': {}", ttfPath, e.what());
        return false;
    }

    // Rasterizar atlas via implementação central
    const int firstGlyph = 32;
    const int lastGlyph = 255;
    const int spaceWidthInitial = std::max(1, pixelHeight / 3);
    TrueTypeAtlasResult atlasRes;
    const bool ok = TrueTypeFont::rasterizeAtlas(reinterpret_cast<const uint8_t*>(ttfData.data()),
                                                 static_cast<int>(ttfData.size()),
                                                 pixelHeight,
                                                 firstGlyph,
                                                 lastGlyph,
                                                 spacingX,
                                                 spacingY,
                                                 yOffset,
                                                 spaceWidthInitial,
                                                 atlasRes);
    if (!ok || !atlasRes.image) {
        g_logger.error("Failed to rasterize TTF font '{}': {}", ttfPath, "atlas generation failed");
        return false;
    }

    // Persistir PNG e .otfont em generated/fonts usando sufixo _cp1252
    g_resources.makeDir("generated");
    g_resources.makeDir("generated/fonts");

    const std::string textureBaseRel = std::string("generated/fonts/") + fontName + "_cp1252";
    const std::string texturePngRel = textureBaseRel + ".png";
    try {
        atlasRes.image->savePNG(texturePngRel);
    } catch (const stdext::exception& e) {
        g_logger.error("Unable to save TTF atlas '{}': {}", texturePngRel, e.what());
        return false;
    }

    // Montar OTML
    const auto doc = OTMLDocument::create();
    const auto fontNode = OTMLNode::create("Font");
    fontNode->addChild(OTMLNode::create("name", fontName));
    fontNode->addChild(OTMLNode::create("texture", "/" + textureBaseRel));
    fontNode->addChild(OTMLNode::create("height", std::to_string(pixelHeight)));
    fontNode->addChild(OTMLNode::create("glyph-size", std::to_string(atlasRes.tileWidth) + " " + std::to_string(atlasRes.tileHeight)));
    fontNode->addChild(OTMLNode::create("space-width", std::to_string(atlasRes.spaceWidth)));
    if (spacingX != 0 || spacingY != 0)
        fontNode->addChild(OTMLNode::create("spacing", std::to_string(spacingX) + " " + std::to_string(spacingY)));
    if (yOffset != 0)
        fontNode->addChild(OTMLNode::create("y-offset", std::to_string(yOffset)));
    if (atlasRes.underlineOffset > 0)
        fontNode->addChild(OTMLNode::create("underline-offset", std::to_string(atlasRes.underlineOffset)));

    doc->addChild(fontNode);

    const std::string otfontPathRel = std::string("generated/fonts/") + fontName + ".otfont";
    g_resources.writeFileContents(otfontPathRel, doc->emit());

    // Registrar a fonte em memória
    importFont(std::string("/generated/fonts/") + fontName + ".otfont");
    if (!m_defaultFont)
        m_defaultFont = getFont(fontName);
    return true;
}