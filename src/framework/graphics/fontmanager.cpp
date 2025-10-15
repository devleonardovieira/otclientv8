/*
 * Copyright (c) 2010-2017 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "atlas.h"
#include "fontmanager.h"
#include "texture.h"
#include "image.h"
#include "bitmapfont.h"
#include "texturemanager.h"
#include "../otml/otml.h"
#include "truetypefont.h"

#include <framework/core/eventdispatcher.h>
#include <framework/core/resourcemanager.h>
#include <framework/otml/otml.h>

FontManager g_fonts;

FontManager::FontManager()
{
    m_defaultFont = std::make_shared<BitmapFont>("emptyfont");
}

void FontManager::terminate()
{
    m_fonts.clear();
    m_defaultFont = nullptr;
}

void FontManager::clearFonts()
{
    m_fonts.clear();
    m_defaultFont = std::make_shared<BitmapFont>("emptyfont");
}

void FontManager::importFont(std::string file)
{
    if (g_graphicsThreadId != std::this_thread::get_id()) {
        g_graphicsDispatcher.addEvent(std::bind(&FontManager::importFont, this, file));
        return;
    }
    try {
        file = g_resources.guessFilePath(file, "otfont");

        OTMLDocumentPtr doc = OTMLDocument::parse(file);
        OTMLNodePtr fontNode = doc->at("Font");

        std::string name = fontNode->valueAt("name");
        if (fontExists(name))
            return;

        // remove any font with the same name
        for(auto it = m_fonts.begin(); it != m_fonts.end(); ++it) {
            if((*it)->getName() == name) {
                m_fonts.erase(it);
                break;
            }
        }

        auto font = std::make_shared<BitmapFont>(name);
        font->load(fontNode);
        m_fonts.push_back(font);

        // set as default if needed
        if(!m_defaultFont || fontNode->valueAt<bool>("default", false))
            m_defaultFont = font;

    } catch(stdext::exception& e) {
        g_logger.error(stdext::format("Unable to load font from file '%s': %s", file, e.what()));
    }
}

void FontManager::importTTFFont(const std::string& ttfFile,
                       const std::string& fontName,
                       int pixelHeight,
                       int yOffset,
                       Size glyphSpacing,
                       int spaceWidth,
                       int firstGlyph,
                       int lastGlyph,
                       bool setDefault)
{
    if (g_graphicsThreadId != std::this_thread::get_id()) {
        g_graphicsDispatcher.addEvent(std::bind(&FontManager::importTTFFont, this, ttfFile, fontName, pixelHeight, yOffset, glyphSpacing, spaceWidth, firstGlyph, lastGlyph, setDefault));
        return;
    }

    try {
        // Lua binder fills missing args with nil -> 0/Size(), so fix defaults here
        if (glyphSpacing == Size()) glyphSpacing = Size(1, 0);
        if (spaceWidth <= 0) spaceWidth = 3;
        if (firstGlyph <= 0) firstGlyph = 32;
        if (lastGlyph <= 0 || lastGlyph < firstGlyph) lastGlyph = 255;

        if (fontExists(fontName)) {
            // remove existing font with same name
            for (auto it = m_fonts.begin(); it != m_fonts.end(); ++it) {
                if ((*it)->getName() == fontName) {
                    m_fonts.erase(it);
                    break;
                }
            }
        }

        // Read TTF as raw buffer
        std::string ttfData = g_resources.readFileContents(ttfFile, true);
        if (ttfData.empty()) {
            g_logger.error(stdext::format("Unable to read TTF '%s'", ttfFile));
            return;
        }

        // Build atlas image from TTF
        TrueTypeAtlasResult atlasRes;
        bool ok = TrueTypeFont::rasterizeAtlas(reinterpret_cast<const uint8_t*>(ttfData.data()), (int)ttfData.size(),
                                               fontName,
                                               pixelHeight, firstGlyph, lastGlyph,
                                               glyphSpacing.width(), glyphSpacing.height(), yOffset,
                                               spaceWidth, atlasRes);

        if (!ok || !atlasRes.image) {
            g_logger.error(stdext::format("Failed to rasterize TTF '%s'", ttfFile));
            return;
        }

        // Persist atlas PNG and otfont in user write dir: generated/fonts
        g_resources.makeDir("generated");
        g_resources.makeDir("generated/fonts");

        std::string textureBaseRel = stdext::format("generated/fonts/%s_cp1252", fontName);
        std::string texturePngRel = textureBaseRel + ".png";
        atlasRes.image->savePNG(texturePngRel);

        // Compose OTML
        OTMLDocumentPtr doc = OTMLDocument::create();
        OTMLNodePtr fontNode = OTMLNode::create("Font");
        fontNode->addChild(OTMLNode::create("name", fontName));
        // Use absolute path for texture so it resolves correctly at runtime
        std::string textureBaseAbs = stdext::format("/generated/fonts/%s_cp1252", fontName);
        fontNode->addChild(OTMLNode::create("texture", textureBaseAbs));

        {
            OTMLNodePtr n = OTMLNode::create("height");
            n->write<int>(atlasRes.glyphHeight);
            fontNode->addChild(n);
        }

        {
            OTMLNodePtr n = OTMLNode::create("glyph-size");
            n->write<Size>(Size(atlasRes.tileWidth, atlasRes.tileHeight));
            fontNode->addChild(n);
        }

        {
            OTMLNodePtr n = OTMLNode::create("first-glyph");
            n->write<int>(firstGlyph);
            fontNode->addChild(n);
        }

        {
            OTMLNodePtr n = OTMLNode::create("space-width");
            n->write<int>(atlasRes.spaceWidth);
            fontNode->addChild(n);
        }

        if (atlasRes.yOffset != 0) {
            OTMLNodePtr n = OTMLNode::create("y-offset");
            n->write<int>(atlasRes.yOffset);
            fontNode->addChild(n);
        }

        if (glyphSpacing != Size()) {
            OTMLNodePtr n = OTMLNode::create("spacing");
            n->write<Size>(glyphSpacing);
            fontNode->addChild(n);
        }

        if (atlasRes.underlineOffset != 0) {
            OTMLNodePtr n = OTMLNode::create("underline-offset");
            n->write<int>(atlasRes.underlineOffset);
            fontNode->addChild(n);
        }

        if (setDefault) {
            OTMLNodePtr n = OTMLNode::create("default");
            n->write<bool>(true);
            fontNode->addChild(n);
        }
        doc->addChild(fontNode);

        std::string otfontPathRel = stdext::format("generated/fonts/%s.otfont", fontName);
        g_resources.writeFileContents(otfontPathRel, doc->emit());

        // Now import the font definition to register in memory
        importFont(stdext::format("/generated/fonts/%s.otfont", fontName));
    } catch (stdext::exception& e) {
        g_logger.error(stdext::format("Unable to import TTF font '%s': %s", ttfFile, e.what()));
    }
}

bool FontManager::fontExists(const std::string& fontName)
{
    for(const BitmapFontPtr& font : m_fonts) {
        if(font->getName() == fontName)
            return true;
    }
    return false;
}

BitmapFontPtr FontManager::getFont(const std::string& fontName)
{
    // find font by name
    for(const BitmapFontPtr& font : m_fonts) {
        if(font->getName() == fontName)
            return font;
    }

    // when not found, fallback to default font
    g_logger.error(stdext::format("font '%s' not found", fontName));
    return getDefaultFont();
}
