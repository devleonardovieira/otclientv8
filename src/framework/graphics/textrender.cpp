#include "painter.h"
#include "textrender.h"
#include <framework/core/logger.h>
#include <framework/core/eventdispatcher.h>
#include "truetypefont.h"
#include "image.h"
#include "texture.h"
#include <framework/core/resourcemanager.h>
#include <framework/stdext/string.h>

TextRender g_text;

void TextRender::init()
{

}

void TextRender::terminate()
{
    for (auto& cache : m_cache) {
        cache.clear();
    }
}

void TextRender::poll()
{
    static int iteration = 0;
    int index = (iteration++) % INDEXES;
    std::lock_guard<std::mutex> lock(m_mutex[index]);
    auto& cache = m_cache[index];
    if (cache.size() < 100)
        return;

    ticks_t dropPoint = g_clock.millis();
    if (cache.size() > 500)
        dropPoint -= 10;
    else if (cache.size() > 250)
        dropPoint -= 100;
    else
        dropPoint -= 1000;

    for (auto it = cache.begin(); it != cache.end(); ) {
        if (it->second->lastUse < dropPoint) {
            it = cache.erase(it);
            continue;
        }
        ++it;
    }
}

uint64_t TextRender::addText(BitmapFontPtr font, const std::string& text, const Size& size, Fw::AlignmentFlag align)
{
    if (!font || text.empty() || !size.isValid()) 
        return 0;
    uint64_t hash = 1125899906842597ULL;
    for (size_t i = 0; i < text.length(); ++i) {
        hash = hash * 31 + text[i];
    }
    hash = hash * 31 + size.width();
    hash = hash * 31 + size.height();
    hash = hash * 31 + (uint64_t)align;
    hash = hash * 31 + (uint64_t)font->getId();

    int index = hash % INDEXES;
    m_mutex[index].lock();
    auto it = m_cache[index].find(hash);
    if (it == m_cache[index].end()) {
        auto cache = std::make_shared<TextRenderCache>(TextRenderCache{ font, text, size, align, font->getTexture(), CoordsBuffer(), g_clock.millis() });

        // Lazy UTF-8 fallback: if input is valid UTF-8 and contains non-ASCII bytes,
        // and the font has a TTF source, pre-rasterize the full string to an image.
        auto containsNonAscii = [](const std::string& s) {
            for (unsigned char c : s) {
                if (c >= 0x80) return true;
            }
            return false;
        };

        if (stdext::is_valid_utf8(text) && containsNonAscii(text)) {
            const std::string& ttfPath = font->getTTFSource();
            if (!ttfPath.empty()) {
                std::string ttfData = g_resources.readFileContents(ttfPath, true);
                if (!ttfData.empty()) {
                    std::wstring wtext = stdext::utf8_to_utf16(text);
                    ImagePtr img = TrueTypeFont::rasterizeString(reinterpret_cast<const uint8_t*>(ttfData.data()), (int)ttfData.size(), font->getName(), font->getGlyphHeight(), wtext, font->getYOffset());
                    if (img) {
                        TexturePtr tex = std::make_shared<Texture>(img);
                        tex->setSmooth(true);
                        cache->texture = tex;
                        // Build a single quad aligned inside the provided size.
                        const int imgW = img->getSize().width();
                        const int imgH = img->getSize().height();

                        int x = 0;
                        int y = 0;
                        if (align & Fw::AlignRight) x = size.width() - imgW;
                        else if (align & Fw::AlignHorizontalCenter) x = (size.width() - imgW) / 2;
                        // else AlignLeft: x = 0;

                        if (align & Fw::AlignBottom) y = size.height() - imgH;
                        else if (align & Fw::AlignVerticalCenter) y = (size.height() - imgH) / 2;
                        // else AlignTop: y = 0;

                        Rect dest(x, y, imgW, imgH);
                        Rect clip(0, 0, size.width(), size.height());
                        Rect src(0, 0, imgW, imgH);

                        // Clip dest to bounding rect, adjust src accordingly.
                        if (clip.intersects(dest)) {
                            if (dest.left() < clip.left()) {
                                int dx = clip.left() - dest.left();
                                dest.setLeft(clip.left());
                                src.setLeft(src.left() + dx);
                            }
                            if (dest.top() < clip.top()) {
                                int dy = clip.top() - dest.top();
                                dest.setTop(clip.top());
                                src.setTop(src.top() + dy);
                            }
                            if (dest.right() > clip.right()) {
                                int dx = dest.right() - clip.right();
                                dest.setRight(clip.right());
                                src.setRight(src.right() - dx);
                            }
                            if (dest.bottom() > clip.bottom()) {
                                int dy = dest.bottom() - clip.bottom();
                                dest.setBottom(clip.bottom());
                                src.setBottom(src.bottom() - dy);
                            }

                            cache->coords.clear();
                            cache->coords.addRect(dest, src);
                            cache->coords.cache();

                            // Avoid glyph-based path
                            cache->text.clear();
                            cache->font.reset();
                        }
                    }
                }
            }
        }

        m_cache[index][hash] = cache;
    }
    m_mutex[index].unlock();
    return hash;
}

void TextRender::drawText(const Rect& rect, const std::string& text, BitmapFontPtr font, const Color& color, Fw::AlignmentFlag align, bool shadow)
{
    VALIDATE_GRAPHICS_THREAD();
    uint64_t hash = addText(font, text, rect.size(), align);
    drawText(rect.topLeft(), hash, color, shadow);
}

void TextRender::drawText(const Point& pos, uint64_t hash, const Color& color, bool shadow)
{
    VALIDATE_GRAPHICS_THREAD();
    int index = hash % INDEXES;
    m_mutex[index].lock();
    auto _it = m_cache[index].find(hash);
    if (_it == m_cache[index].end()) {
        m_mutex[index].unlock();
        return;
    }
    auto it = _it->second;
    it->lastUse = g_clock.millis();
    m_mutex[index].unlock();
    if (it->font) { // calculate text coords
        it->font->calculateDrawTextCoords(it->coords, it->text, Rect(0, 0, it->size), it->align);
        it->coords.cache();
        it->text.clear();
        it->font.reset();
    }

    if (shadow) {
        auto shadowPos = Point(pos);
        shadowPos.x += 1;
        shadowPos.y += 1;
        g_painter->drawText(shadowPos, it->coords, Color::black, it->texture);
    }

    g_painter->drawText(pos, it->coords, color, it->texture);
}

void TextRender::drawColoredText(const Point& pos, uint64_t hash, const std::vector<std::pair<int, Color>>& colors, bool shadow)
{
    VALIDATE_GRAPHICS_THREAD();
    if (colors.empty())
        return drawText(pos, hash, Color::white);
    int index = hash % INDEXES;
    m_mutex[index].lock();
    auto _it = m_cache[index].find(hash);
    if (_it == m_cache[index].end()) {
        m_mutex[index].unlock();
        return;
    }
    auto it = _it->second;
    it->lastUse = g_clock.millis();
    m_mutex[index].unlock();
    if (it->font) { // calculate text coords
        it->font->calculateDrawTextCoords(it->coords, it->text, Rect(0, 0, it->size), it->align);
        it->coords.cache();
        it->text.clear();
        it->font.reset();
    }

    // Fallback path (pre-rendered single texture): cannot apply per-glyph coloring.
    // Use the first color entry for uniform tint instead, and draw optional shadow.
    if (!it->font && it->coords.getVertexCount() == 6) {
        if (shadow) {
            auto shadowPos = Point(pos);
            shadowPos.x += 1;
            shadowPos.y += 1;
            g_painter->drawText(shadowPos, it->coords, Color::black, it->texture);
        }
        g_painter->drawText(pos, it->coords, colors.front().second, it->texture);
        return;
    }

    g_painter->drawText(pos, it->coords, colors, it->texture);
}

