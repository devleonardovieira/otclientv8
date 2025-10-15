// Minimal subset redirect to system-provided stb_truetype if available.
// This header expects STB_TRUETYPE_IMPLEMENTATION to be defined in exactly one .cpp.
// For full functionality, embed the official stb_truetype.h content.

#ifndef STB_TRUETYPE_H_MINIMAL
#define STB_TRUETYPE_H_MINIMAL

// If the build already provides stb_truetype from elsewhere, include it.
#if __has_include(<stb_truetype.h>)
#include <stb_truetype.h>
#else

// Fallback: embed a trimmed forward-declaration-only header and rely on implementation being compiled here.
// Note: For production use, replace this with the full stb_truetype.h.

typedef unsigned char stbtt_uint8;
typedef signed char stbtt_int8;
typedef unsigned short stbtt_uint16;
typedef signed short stbtt_int16;
typedef unsigned int stbtt_uint32;
typedef signed int stbtt_int32;

typedef struct stbtt_fontinfo {
    void *userdata;
    const unsigned char *data;  // pointer to .ttf file
    int fontstart;              // offset of start of font
} stbtt_fontinfo;

extern "C" {

int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index);
int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset);
float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float height);
void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap);
void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *info, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);
unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff);
void stbtt_FreeBitmap(unsigned char *bitmap, void *userdata);

}

#endif // __has_include

#endif // STB_TRUETYPE_H_MINIMAL