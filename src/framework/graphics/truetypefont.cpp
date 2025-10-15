#include "truetypefont.h"
#include "declarations.h"
#include "image.h"
#include <framework/util/color.h>

#include <algorithm>
#include <vector>
#include <cmath>

#ifdef _WIN32
#  include <windows.h>
#  include <gdiplus.h>
#  pragma comment(lib, "gdiplus.lib")
#endif

// Rasterização via GDI+ (Windows) com antialiasing e alpha.
bool TrueTypeFont::rasterizeAtlas(const uint8_t* ttfData,
                                  int ttfSize,
                                  const std::string& fontFamilyName,
                                  int pixelHeight,
                                  int firstGlyph,
                                  int lastGlyph,
                                  int spacingX,
                                  int spacingY,
                                  int yOffset,
                                  int spaceWidth,
                                  TrueTypeAtlasResult& out)
{
    if (pixelHeight <= 0)
        return false;

    const int glyphCount = std::max(0, lastGlyph - firstGlyph + 1);
    if (glyphCount <= 0)
        return false;

#ifndef _WIN32
    // Sem GDI+, aborta em plataformas não-Windows.
    return false;
#else
    // Inicializa GDI+
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
        return false;

    bool ok = false;
    do {
        // Carrega fonte em memória
        Gdiplus::PrivateFontCollection pfc;
        if (pfc.AddMemoryFont((void*)ttfData, ttfSize) != Gdiplus::Ok)
            break;

        int familyCount = pfc.GetFamilyCount();
        if (familyCount <= 0)
            break;

        std::vector<Gdiplus::FontFamily> families(familyCount);
        pfc.GetFamilies(familyCount, families.data(), &familyCount);

        // Seleciona família pelo nome (se encontrado) ou a primeira
        Gdiplus::FontFamily* chosenFamily = &families[0];
        if (!fontFamilyName.empty()) {
            for (int i = 0; i < familyCount; ++i) {
                WCHAR name[LF_FACESIZE] = {0};
                families[i].GetFamilyName(name);
                std::wstring wname(name);
                if (std::wstring(fontFamilyName.begin(), fontFamilyName.end()) == wname) {
                    chosenFamily = &families[i];
                    break;
                }
            }
        }

        Gdiplus::Font font(chosenFamily, (Gdiplus::REAL)pixelHeight, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (!font.IsAvailable())
            break;

        // Medir largura via caminho vetorial e altura usando métricas tipográficas
        const int padX = std::max(1, spacingX);
        const int padY = std::max(0, spacingY);

        const int columns = 16;
        int maxGlyphW = 0;
        float measuredSpaceW = (float)spaceWidth;

        // métricas tipográficas para altura sem sobreposição
        const int em = chosenFamily->GetEmHeight(Gdiplus::FontStyleRegular);
        const int ascent = chosenFamily->GetCellAscent(Gdiplus::FontStyleRegular);
        const int descent = chosenFamily->GetCellDescent(Gdiplus::FontStyleRegular);
        const int lineSpacing = chosenFamily->GetLineSpacing(Gdiplus::FontStyleRegular);
        const float ascentPx = (em > 0) ? (pixelHeight * (float)ascent / (float)em) : (float)pixelHeight * 0.8f;
        const float descentPx = (em > 0) ? (pixelHeight * (float)descent / (float)em) : (float)pixelHeight * 0.2f;
        const float lineSpacingPx = (em > 0) ? (pixelHeight * (float)lineSpacing / (float)em) : (float)pixelHeight;

        {
            Gdiplus::Bitmap scratch(512, 512, PixelFormat32bppARGB);
            Gdiplus::Graphics g(&scratch);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
            fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoFontFallback);

            for (int cp = firstGlyph; cp <= lastGlyph; ++cp) {
                WCHAR ch[2] = { (WCHAR)cp, 0 };
                Gdiplus::GraphicsPath path;
                path.AddString(ch, 1, chosenFamily, Gdiplus::FontStyleRegular, (Gdiplus::REAL)pixelHeight,
                               Gdiplus::PointF(0.0f, 0.0f), &fmt);
                Gdiplus::RectF bounds;
                path.GetBounds(&bounds);
                int w = (int)std::ceil(bounds.Width);
                if (w > maxGlyphW) maxGlyphW = w;
            }

            // mede largura do espaço quando não fornecida
            WCHAR sp[2] = { L' ', 0 };
            Gdiplus::GraphicsPath spath;
            spath.AddString(sp, 1, chosenFamily, Gdiplus::FontStyleRegular, (Gdiplus::REAL)pixelHeight,
                            Gdiplus::PointF(0.0f, 0.0f), &fmt);
            Gdiplus::RectF sbounds;
            spath.GetBounds(&sbounds);
            measuredSpaceW = sbounds.Width;
        }

        const int tileWidth = std::max(pixelHeight, maxGlyphW) + padX * 2;
        const int tileHeight = (int)std::ceil(lineSpacingPx) + padY * 2;
        const int rows = (glyphCount + columns - 1) / columns;
        const int atlasWidth = columns * tileWidth;
        const int atlasHeight = rows * tileHeight;

        // Desenha todos os glifos em um Bitmap GDI+
        Gdiplus::Bitmap gdibmp(atlasWidth, atlasHeight, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(&gdibmp);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));

            Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
            fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoFontFallback);

            for (int i = 0; i < glyphCount; ++i) {
                int cp = firstGlyph + i;
                int destTileX = (i % columns) * tileWidth;
                int destTileY = (i / columns) * tileHeight;
                float destX = (float)(destTileX + padX);
                // Desenha alinhando pela linha de base: topo do tile + padY coloca a linha de base em ascentPx
                float destY = (float)(destTileY + padY + yOffset);
                WCHAR ch[2] = { (WCHAR)cp, 0 };
                // usar caminho vetorial para evitar variações de layout
                Gdiplus::GraphicsPath path;
                path.AddString(ch, 1, chosenFamily, Gdiplus::FontStyleRegular, (Gdiplus::REAL)pixelHeight,
                               Gdiplus::PointF(destX, destY), &fmt);
                g.FillPath(&brush, &path);
            }
        }

        // Copia alpha para nossa Image (branco com alpha)
        ImagePtr atlas = std::make_shared<Image>(Size(atlasWidth, atlasHeight));
        for (int y = 0; y < atlasHeight; ++y) {
            for (int x = 0; x < atlasWidth; ++x) {
                Gdiplus::Color c;
                gdibmp.GetPixel(x, y, &c);
                uint8 a = (uint8)c.GetA();
                if (a)
                    atlas->setPixel(x, y, Color((uint8)255, (uint8)255, (uint8)255, a));
                else
                    atlas->setPixel(x, y, Color((uint8)0, (uint8)0, (uint8)0, (uint8)0));
            }
        }

        if (spaceWidth <= 0)
            spaceWidth = std::max(3, (int)std::ceil(measuredSpaceW));

        out.image = atlas;
        out.tileWidth = tileWidth;
        out.tileHeight = tileHeight;
        out.glyphHeight = (int)std::ceil(ascentPx + descentPx);
        out.yOffset = yOffset;
        out.spaceWidth = (int)std::ceil(measuredSpaceW);
        out.underlineOffset = std::max(1, pixelHeight / 6);
        ok = true;
    } while (false);

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return ok;
#endif
}