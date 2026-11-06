// The part of std.gui in C (D441): the hit test of SDL, which takes a function of
// C; the text, with stb_truetype and the fonts of Inter in fonts.S (D433, D440);
// the symbols of the buttons of the title bar (D431), of the check boxes and of
// the combo boxes; the boxes with rounded corners of the components (D439), their
// shadows and their gradients (D443); and what the end of main waits for (D442).
// Without the C library and without the headers of SDL, as the rest of the
// runtime: the functions of SDL are declared here, and stb_truetype takes its
// mathematics from SDL. The icons of Lucide are a third font (D448); the images
// are in image.c (D446).

typedef unsigned char u8;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;
typedef float f32;
typedef unsigned long long size_t;

// ---- SDL ----

typedef struct {
    i32 x;
    i32 y;
} Point;

typedef struct {
    f32 x;
    f32 y;
    f32 w;
    f32 h;
} FRect;

void* SDL_malloc(size_t size);
void* SDL_calloc(size_t count, size_t size);
void SDL_free(void* memory);
double SDL_floor(double x);
double SDL_ceil(double x);
double SDL_sqrt(double x);
double SDL_pow(double x, double y);
double SDL_fmod(double x, double y);
double SDL_cos(double x);
double SDL_acos(double x);
double SDL_fabs(double x);
size_t SDL_strlen(const char* text);
_Bool SDL_SetWindowHitTest(void* window, i32 (*test)(void* window, const Point* point, void* data), void* data);
_Bool SDL_GetWindowSize(void* window, i32* width, i32* height);
u64 SDL_GetWindowFlags(void* window);
void* SDL_CreateTexture(void* renderer, u32 format, i32 access, i32 width, i32 height);
_Bool SDL_UpdateTexture(void* texture, const void* rect, const void* pixels, i32 pitch);
_Bool SDL_SetTextureBlendMode(void* texture, u32 mode);
_Bool SDL_SetTextureColorMod(void* texture, u8 r, u8 g, u8 b);
_Bool SDL_SetTextureAlphaMod(void* texture, u8 alpha);
_Bool SDL_RenderTexture(void* renderer, void* texture, const FRect* source, const FRect* target);
_Bool SDL_SetRenderDrawColor(void* renderer, u8 r, u8 g, u8 b, u8 a);
_Bool SDL_RenderFillRect(void* renderer, const FRect* rect);
void SDL_DestroyTexture(void* texture);

void* memcpy(void* target, const void* source, size_t size);  // in the runtime (D19)
void* memset(void* target, i32 value, size_t size);

#define PIXELS_RGBA 0x16762004u  // SDL_PIXELFORMAT_RGBA32, the bytes R, G, B, A
#define BLEND 1u                 // SDL_BLENDMODE_BLEND
#define MAXIMIZED 0x80ull        // SDL_WINDOW_MAXIMIZED

#define NULL ((void*)0)
#define STBTT_ifloor(x) ((int)SDL_floor(x))
#define STBTT_iceil(x) ((int)SDL_ceil(x))
#define STBTT_sqrt(x) SDL_sqrt(x)
#define STBTT_pow(x, y) SDL_pow(x, y)
#define STBTT_fmod(x, y) SDL_fmod(x, y)
#define STBTT_cos(x) SDL_cos(x)
#define STBTT_acos(x) SDL_acos(x)
#define STBTT_fabs(x) SDL_fabs(x)
#define STBTT_malloc(x, u) ((void)(u), SDL_malloc(x))
#define STBTT_free(x, u) ((void)(u), SDL_free(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_strlen(x) SDL_strlen(x)
#define STBTT_memcpy memcpy
#define STBTT_memset memset
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"  // the code of others, as it is
#include "stb_truetype.h"
#pragma clang diagnostic pop

// ---- The hit test (D431) ----
//
// The bar moves the window and a double click maximizes it, the edges resize it:
// SDL gives these to the system. The sizes are in the coordinates of the window,
// which std.gui updates at every frame; the buttons stay to the program.

typedef struct {
    f32 bar;      // the height of the title bar
    f32 buttons;  // the width of its buttons, on the right
    f32 edge;     // the width of the edges that resize
    i32 resizable;
} Chrome;

static i32 hitTest(void* window, const Point* point, void* data) {
    const Chrome* chrome = data;
    i32 w, h;
    SDL_GetWindowSize(window, &w, &h);
    f32 x = (f32)point->x;
    f32 y = (f32)point->y;
    if (chrome->resizable && !(SDL_GetWindowFlags(window) & MAXIMIZED)) {
        i32 left = x < chrome->edge, right = x >= w - chrome->edge, top = y < chrome->edge, bottom = y >= h - chrome->edge;
        if (top && left) return 2;      // SDL_HITTEST_RESIZE_TOPLEFT
        if (top && right) return 4;     // TOPRIGHT
        if (bottom && right) return 6;  // BOTTOMRIGHT
        if (bottom && left) return 8;   // BOTTOMLEFT
        if (top) return 3;
        if (right) return 5;
        if (bottom) return 7;
        if (left) return 9;
    }
    if (y < chrome->bar && x < w - chrome->buttons) return 1;  // SDL_HITTEST_DRAGGABLE
    return 0;                                                   // SDL_HITTEST_NORMAL
}

void* cromo_gui_chrome(void* window) {
    Chrome* chrome = SDL_calloc(1, sizeof(Chrome));
    if (chrome) SDL_SetWindowHitTest(window, hitTest, chrome);
    return chrome;
}

void cromo_gui_chrome_size(void* data, f32 bar, f32 buttons, f32 edge, _Bool resizable) {
    Chrome* chrome = data;
    chrome->bar = bar;
    chrome->buttons = buttons;
    chrome->edge = edge;
    chrome->resizable = resizable;
}

void cromo_gui_chrome_free(void* chrome) { SDL_free(chrome); }

// ---- The text (D433) ----
//
// A font of a renderer, at a size and at the density of its display: the glyphs
// are rasterized when they are first drawn, at the pixels of the display, into
// one texture, white with the coverage as alpha, and colored when they are drawn.
// The positions are in logical pixels, which the scale of the renderer multiplies.

extern const u8 cromo_gui_inter_regular[];
extern const u8 cromo_gui_inter_semibold[];
extern const u8 cromo_gui_lucide[];

#define ATLAS 1024      // the side of the texture of a font of text
#define ICON_ATLAS 512  // of a font of icons, which has fewer glyphs
#define GLYPHS 4096     // the slots of the table of the glyphs, a power of 2

typedef struct {
    i32 code;  // -1 for a free slot
    i32 x, y, w, h;
    i32 left, top;  // from the pen, in pixels
    f32 advance;    // in pixels
} Glyph;

typedef struct {
    void* renderer;
    void* texture;
    stbtt_fontinfo info;
    f32 density;  // pixels for each logical pixel
    f32 scale;    // of stb_truetype, for the size in pixels
    f32 ascent;   // in logical pixels, as the others
    f32 descent;
    f32 gap;
    i32 atlas;  // the side of its texture
    i32 shelfX, shelfY, shelfHeight;
    i32 count;
    Glyph glyphs[GLYPHS];
} Font;

// A font of text, face 0 regular and 1 semibold, whose size is the height of its
// letters; or with face 2 the font of the icons, whose size is that of the square
// of an icon.
void* cromo_gui_font(void* renderer, i32 face, f32 size, f32 density) {
    Font* font = SDL_malloc(sizeof(Font));
    if (!font) return 0;
    const u8* data = face == 2 ? cromo_gui_lucide : face ? cromo_gui_inter_semibold : cromo_gui_inter_regular;
    if (!stbtt_InitFont(&font->info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        SDL_free(font);
        return 0;
    }
    font->atlas = face == 2 ? ICON_ATLAS : ATLAS;
    font->texture = SDL_CreateTexture(renderer, PIXELS_RGBA, 0, font->atlas, font->atlas);  // SDL_TEXTUREACCESS_STATIC
    if (!font->texture) {
        SDL_free(font);
        return 0;
    }
    SDL_SetTextureBlendMode(font->texture, BLEND);
    font->renderer = renderer;
    font->density = density;
    font->scale = face == 2 ? stbtt_ScaleForMappingEmToPixels(&font->info, size * density)
                            : stbtt_ScaleForPixelHeight(&font->info, size * density);
    i32 ascent, descent, gap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
    font->ascent = ascent * font->scale / density;
    font->descent = -descent * font->scale / density;
    font->gap = gap * font->scale / density;
    font->shelfX = font->shelfY = font->shelfHeight = 0;
    font->count = 0;
    for (i32 i = 0; i < GLYPHS; ++i) font->glyphs[i].code = -1;
    return font;
}

void cromo_gui_font_free(void* data) {
    Font* font = data;
    if (!font) return;
    SDL_DestroyTexture(font->texture);
    SDL_free(font);
}

void cromo_gui_font_metrics(void* data, f32* ascent, f32* descent, f32* gap) {
    const Font* font = data;
    *ascent = font->ascent;
    *descent = font->descent;
    *gap = font->gap;
}

// Starts again with an empty texture, when it is full: the glyphs are drawn again.
static void clearAtlas(Font* font) {
    for (i32 i = 0; i < GLYPHS; ++i) font->glyphs[i].code = -1;
    font->shelfX = font->shelfY = font->shelfHeight = 0;
    font->count = 0;
}

static Glyph* findGlyph(Font* font, i32 code) {
    u32 slot = ((u32)code * 2654435761u) & (GLYPHS - 1);
    for (;;) {
        Glyph* glyph = &font->glyphs[slot];
        if (glyph->code == code) return glyph;
        if (glyph->code == -1) break;
        slot = (slot + 1) & (GLYPHS - 1);
    }
    if (font->count >= GLYPHS / 2) {
        clearAtlas(font);
        return findGlyph(font, code);
    }
    i32 index = stbtt_FindGlyphIndex(&font->info, code);
    i32 advance, bearing, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&font->info, index, &advance, &bearing);
    stbtt_GetGlyphBitmapBox(&font->info, index, font->scale, font->scale, &x0, &y0, &x1, &y1);
    i32 w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        if (font->shelfX + w + 1 > font->atlas) {
            font->shelfX = 0;
            font->shelfY += font->shelfHeight + 1;
            font->shelfHeight = 0;
        }
        if (font->shelfY + h > font->atlas) {
            clearAtlas(font);
            return findGlyph(font, code);
        }
    }
    Glyph* glyph = &font->glyphs[slot];
    glyph->code = code;
    glyph->x = font->shelfX;
    glyph->y = font->shelfY;
    glyph->w = w > 0 ? w : 0;
    glyph->h = h > 0 ? h : 0;
    glyph->left = x0;
    glyph->top = y0;
    glyph->advance = advance * font->scale;
    font->count++;
    if (w > 0 && h > 0) {
        u8* coverage = SDL_malloc((size_t)w * h);
        u8* pixels = SDL_malloc((size_t)w * h * 4);
        if (coverage && pixels) {
            stbtt_MakeGlyphBitmap(&font->info, coverage, w, h, w, font->scale, font->scale, index);
            for (i32 i = 0; i < w * h; ++i) {
                pixels[4 * i] = pixels[4 * i + 1] = pixels[4 * i + 2] = 255;
                pixels[4 * i + 3] = coverage[i];
            }
            struct {
                i32 x, y, w, h;
            } rect = {glyph->x, glyph->y, w, h};
            SDL_UpdateTexture(font->texture, &rect, pixels, w * 4);
        }
        SDL_free(coverage);
        SDL_free(pixels);
        font->shelfX += w + 1;
        if (h > font->shelfHeight) font->shelfHeight = h;
    }
    return glyph;
}

// The next code point of UTF-8; U+FFFD for a byte that does not start one.
static i32 nextCode(const u8** text, const u8* end) {
    const u8* s = *text;
    i32 code = *s++;
    i32 more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
    if (code >= 0x80 && more == 0) code = 0xFFFD;
    if (more) code &= 0x3F >> more;
    for (; more > 0 && s < end && (*s & 0xC0) == 0x80; --more) code = code << 6 | (*s++ & 0x3F);
    if (more) code = 0xFFFD;
    *text = s;
    return code;
}

// The width of a text in logical pixels, with the kerning.
f32 cromo_gui_text_width(void* data, const u8* text, i64 length) {
    Font* font = data;
    const u8* end = text + length;
    f32 pen = 0;
    i32 previous = 0;
    while (text < end) {
        i32 code = nextCode(&text, end);
        if (previous) pen += stbtt_GetCodepointKernAdvance(&font->info, previous, code) * font->scale;
        pen += findGlyph(font, code)->advance;
        previous = code;
    }
    return pen / font->density;
}

// The text with its baseline at y, each glyph on a whole pixel of the display.
void cromo_gui_text(void* data, const u8* text, i64 length, f32 x, f32 y, u8 r, u8 g, u8 b, u8 alpha) {
    Font* font = data;
    const u8* end = text + length;
    SDL_SetTextureColorMod(font->texture, r, g, b);
    SDL_SetTextureAlphaMod(font->texture, alpha);
    f32 pen = x * font->density;
    f32 baseline = (f32)SDL_floor(y * font->density + 0.5);
    i32 previous = 0;
    while (text < end) {
        i32 code = nextCode(&text, end);
        if (previous) pen += stbtt_GetCodepointKernAdvance(&font->info, previous, code) * font->scale;
        Glyph* glyph = findGlyph(font, code);
        if (glyph->w > 0) {
            f32 left = (f32)SDL_floor(pen + 0.5) + glyph->left;
            FRect source = {(f32)glyph->x, (f32)glyph->y, (f32)glyph->w, (f32)glyph->h};
            FRect target = {left / font->density, (baseline + glyph->top) / font->density, glyph->w / font->density,
                            glyph->h / font->density};
            SDL_RenderTexture(font->renderer, font->texture, &source, &target);
        }
        pen += glyph->advance;
        previous = code;
    }
}

// ---- The icons (D448) ----
//
// The icons of Lucide are the characters of a font, whose square of 1000 units
// stands on the baseline: a font of the icons for each renderer, size and
// density, made the first time.

typedef struct IconFont {
    struct IconFont* next;
    void* renderer;
    f32 size;
    f32 density;
    Font* font;
} IconFont;

static IconFont* iconFonts;

// An icon whose square of `size` logical pixels has its center at (x, y).
void cromo_gui_icon(void* renderer, i32 code, f32 x, f32 y, f32 size, f32 density, u8 r, u8 g, u8 b, u8 alpha) {
    IconFont* icons = iconFonts;
    while (icons && !(icons->renderer == renderer && icons->size == size && icons->density == density)) icons = icons->next;
    if (!icons) {
        icons = SDL_calloc(1, sizeof(IconFont));
        if (!icons) return;
        icons->renderer = renderer;
        icons->size = size;
        icons->density = density;
        icons->font = cromo_gui_font(renderer, 2, size, density);
        icons->next = iconFonts;
        iconFonts = icons;
    }
    Font* font = icons->font;
    if (!font) return;
    Glyph* glyph = findGlyph(font, code);
    if (glyph->w <= 0) return;
    SDL_SetTextureColorMod(font->texture, r, g, b);
    SDL_SetTextureAlphaMod(font->texture, alpha);
    f32 left = (f32)SDL_floor((x - size / 2) * density + 0.5) + glyph->left;
    f32 baseline = (f32)SDL_floor((y + size / 2) * density + 0.5);
    FRect source = {(f32)glyph->x, (f32)glyph->y, (f32)glyph->w, (f32)glyph->h};
    FRect target = {left / density, (baseline + glyph->top) / density, glyph->w / density, glyph->h / density};
    SDL_RenderTexture(renderer, font->texture, &source, &target);
}

// The fonts of the icons of a renderer, before it is destroyed.
void cromo_gui_icons_free(void* renderer) {
    IconFont** link = &iconFonts;
    while (*link) {
        IconFont* icons = *link;
        if (icons->renderer == renderer) {
            *link = icons->next;
            cromo_gui_font_free(icons->font);
            SDL_free(icons);
        } else {
            link = &icons->next;
        }
    }
}

// ---- The symbols of the title bar (D440) ----
//
// Minimize, maximize, restore and close, as the codicons of Visual Studio Code:
// lines one logical pixel wide, in a square of 10, rasterized at the pixels of
// the display with their coverage, so that the diagonals of close are smooth.


typedef struct Symbol {
    struct Symbol* next;
    void* renderer;
    i32 kind;
    f32 density;
    i32 size;  // in pixels
    void* texture;
} Symbol;

static Symbol* symbols;

typedef struct {
    f32 x0, y0, x1, y1;
} Segment;

// The distance from (x, y) to a segment.
static f32 distance(const Segment* s, f32 x, f32 y) {
    f32 dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    f32 length = dx * dx + dy * dy;
    f32 t = length > 0 ? ((x - s->x0) * dx + (y - s->y0) * dy) / length : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    f32 ex = s->x0 + t * dx - x, ey = s->y0 + t * dy - y;
    return (f32)SDL_sqrt(ex * ex + ey * ey);
}

static void* drawSymbol(void* renderer, i32 kind, f32 density, i32* size) {
    // The mark of a check box and the arrow of a combo box are bolder.
    i32 line = (i32)SDL_floor((kind == 4 ? 1.75 : kind == 5 ? 1.5 : 1) * density + 0.5);
    if (line < 1) line = 1;
    i32 extent = (i32)SDL_floor(10 * density + 0.5);  // the side of the square, in pixels
    i32 margin = line + 1;
    i32 n = extent + 2 * margin;
    // Coordinates at the centers of the pixels when the line is odd: crisp lines.
    f32 half = (line % 2) ? 0.5f : 0.0f;
    f32 a = margin + half, z = margin + extent - (line % 2 ? 1 : 0) + half;
    f32 step = (f32)SDL_floor(2 * density + 0.5);
    Segment segments[8];
    i32 count = 0;
    if (kind == 0) {  // minimize: a line in the middle
        f32 y = (f32)SDL_floor((a + z) / 2) + half;
        segments[count++] = (Segment){a, y, z, y};
    } else if (kind == 1) {  // maximize: a square
        segments[count++] = (Segment){a, a, z, a};
        segments[count++] = (Segment){z, a, z, z};
        segments[count++] = (Segment){z, z, a, z};
        segments[count++] = (Segment){a, z, a, a};
    } else if (kind == 2) {  // restore: a square, and the corner of another behind it
        f32 top = a + step, right = z - step;
        segments[count++] = (Segment){a, top, right, top};
        segments[count++] = (Segment){right, top, right, z};
        segments[count++] = (Segment){right, z, a, z};
        segments[count++] = (Segment){a, z, a, top};
        segments[count++] = (Segment){a + step, top, a + step, a};
        segments[count++] = (Segment){a + step, a, z, a};
        segments[count++] = (Segment){z, a, z, z - step};
        segments[count++] = (Segment){z, z - step, right, z - step};
    } else if (kind == 3) {  // close: two diagonals
        segments[count++] = (Segment){a, a, z, z};
        segments[count++] = (Segment){z, a, a, z};
    } else if (kind == 4) {  // check: the mark of a check box
        f32 d = z - a;
        segments[count++] = (Segment){a + 0.05f * d, a + 0.55f * d, a + 0.35f * d, a + 0.85f * d};
        segments[count++] = (Segment){a + 0.35f * d, a + 0.85f * d, a + 0.95f * d, a + 0.15f * d};
    } else {  // the arrow down of a combo box
        f32 d = z - a;
        segments[count++] = (Segment){a + 0.15f * d, a + 0.35f * d, a + 0.5f * d, a + 0.7f * d};
        segments[count++] = (Segment){a + 0.5f * d, a + 0.7f * d, a + 0.85f * d, a + 0.35f * d};
    }
    u8* pixels = SDL_malloc((size_t)n * n * 4);
    if (!pixels) return 0;
    f32 radius = line / 2.0f;
    for (i32 y = 0; y < n; ++y) {
        for (i32 x = 0; x < n; ++x) {
            f32 nearest = 1e9f;
            for (i32 i = 0; i < count; ++i) {
                f32 d = distance(&segments[i], x + 0.5f, y + 0.5f);
                if (d < nearest) nearest = d;
            }
            f32 cover = radius + 0.5f - nearest;
            cover = cover < 0 ? 0 : cover > 1 ? 1 : cover;
            u8* p = pixels + 4 * (y * n + x);
            p[0] = p[1] = p[2] = 255;
            p[3] = (u8)(cover * 255 + 0.5f);
        }
    }
    void* texture = SDL_CreateTexture(renderer, PIXELS_RGBA, 0, n, n);
    if (texture) {
        SDL_SetTextureBlendMode(texture, BLEND);
        SDL_UpdateTexture(texture, 0, pixels, n * 4);
    }
    SDL_free(pixels);
    *size = n;
    return texture;
}

// A symbol centered at (x, y), in logical pixels: 0 minimize, 1 maximize, 2 restore,
// 3 close, 4 the mark of a check box, 5 the arrow of a combo box.
void cromo_gui_symbol(void* renderer, i32 kind, f32 x, f32 y, f32 density, u8 r, u8 g, u8 b) {
    Symbol* symbol = symbols;
    while (symbol && !(symbol->renderer == renderer && symbol->kind == kind && symbol->density == density)) {
        symbol = symbol->next;
    }
    if (!symbol) {
        symbol = SDL_calloc(1, sizeof(Symbol));
        if (!symbol) return;
        symbol->renderer = renderer;
        symbol->kind = kind;
        symbol->density = density;
        symbol->texture = drawSymbol(renderer, kind, density, &symbol->size);
        symbol->next = symbols;
        symbols = symbol;
    }
    if (!symbol->texture) return;
    SDL_SetTextureColorMod(symbol->texture, r, g, b);
    f32 side = symbol->size / density;
    f32 left = (f32)SDL_floor((x - side / 2) * density + 0.5) / density;
    f32 top = (f32)SDL_floor((y - side / 2) * density + 0.5) / density;
    FRect target = {left, top, side, side};
    SDL_RenderTexture(renderer, symbol->texture, 0, &target);
}

// The textures of the symbols of a renderer, before it is destroyed.
void cromo_gui_symbols_free(void* renderer) {
    Symbol** link = &symbols;
    while (*link) {
        Symbol* symbol = *link;
        if (symbol->renderer == renderer) {
            *link = symbol->next;
            if (symbol->texture) SDL_DestroyTexture(symbol->texture);
            SDL_free(symbol);
        } else {
            link = &symbol->next;
        }
    }
}

// ---- The boxes of the components (D439) ----
//
// A rectangle with rounded corners, filled and with a border: the corners are the
// quarters of a circle, or of a ring for the border, rasterized at the pixels of
// the display with its coverage, each drawn from its part of the texture without
// flipping it, which some renderers do a pixel off; the rest is rectangles. The colors are 0xAARRGGBB, and an alpha of 0 draws nothing. On
// them, the soft shadows and the boxes with a gradient of the look (D443).

typedef struct Corner {
    struct Corner* next;
    void* renderer;
    f32 radius;
    f32 border;
    f32 density;
    i32 size;    // of a corner, in pixels
    void* fill;  // the whole circle, of 2 * size pixels
    void* line;  // the whole ring
} Corner;

static Corner* corners;

// The coverage of a disc of `outer` pixels, minus the one of `inner` when it is
// not negative, with its center at (n, n) of a texture of 2n by 2n.
static void* cornerTexture(void* renderer, i32 n, f32 outer, f32 inner) {
    i32 side = 2 * n;
    u8* pixels = SDL_malloc((size_t)side * side * 4);
    if (!pixels) return 0;
    for (i32 y = 0; y < side; ++y) {
        for (i32 x = 0; x < side; ++x) {
            f32 dx = n - (x + 0.5f), dy = n - (y + 0.5f);
            f32 d = (f32)SDL_sqrt(dx * dx + dy * dy);
            f32 cover = outer + 0.5f - d;
            cover = cover < 0 ? 0 : cover > 1 ? 1 : cover;
            if (inner >= 0) {
                f32 hole = inner + 0.5f - d;
                hole = hole < 0 ? 0 : hole > 1 ? 1 : hole;
                cover -= hole;
                if (cover < 0) cover = 0;
            }
            u8* p = pixels + 4 * (y * side + x);
            p[0] = p[1] = p[2] = 255;
            p[3] = (u8)(cover * 255 + 0.5f);
        }
    }
    void* texture = SDL_CreateTexture(renderer, PIXELS_RGBA, 0, side, side);
    if (texture) {
        SDL_SetTextureBlendMode(texture, BLEND);
        SDL_UpdateTexture(texture, 0, pixels, side * 4);
    }
    SDL_free(pixels);
    return texture;
}

static Corner* findCorner(void* renderer, f32 radius, f32 border, f32 density) {
    Corner* corner = corners;
    while (corner && !(corner->renderer == renderer && corner->radius == radius && corner->border == border &&
                       corner->density == density)) {
        corner = corner->next;
    }
    if (corner) return corner;
    corner = SDL_calloc(1, sizeof(Corner));
    if (!corner) return 0;
    corner->renderer = renderer;
    corner->radius = radius;
    corner->border = border;
    corner->density = density;
    corner->size = (i32)SDL_ceil(radius * density);
    if (corner->size < 1) corner->size = 1;
    f32 outer = radius * density;
    corner->fill = cornerTexture(renderer, corner->size, outer, -1);
    corner->line = cornerTexture(renderer, corner->size, outer, outer - border * density);
    corner->next = corners;
    corners = corner;
    return corner;
}

static void setColor(void* renderer, u32 color) {
    SDL_SetRenderDrawColor(renderer, (u8)(color >> 16), (u8)(color >> 8), (u8)color, (u8)(color >> 24));
}

static void fillRect(void* renderer, f32 x, f32 y, f32 w, f32 h) {
    if (w <= 0 || h <= 0) return;
    FRect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
}

// The four corners of a texture of corners of n pixels, tinted: the two above with
// `upper`, the two below with `lower`.
static void drawCorners(void* renderer, void* texture, i32 n, f32 x, f32 y, f32 w, f32 h, f32 side, u32 upper,
                        u32 lower) {
    if (!texture) return;
    f32 k = (f32)n;
    FRect places[4] = {{x, y, side, side}, {x + w - side, y, side, side}, {x, y + h - side, side, side},
                       {x + w - side, y + h - side, side, side}};
    FRect parts[4] = {{0, 0, k, k}, {k, 0, k, k}, {0, k, k, k}, {k, k, k, k}};
    for (i32 i = 0; i < 4; ++i) {
        u32 color = i < 2 ? upper : lower;
        SDL_SetTextureColorMod(texture, (u8)(color >> 16), (u8)(color >> 8), (u8)color);
        SDL_SetTextureAlphaMod(texture, (u8)(color >> 24));
        SDL_RenderTexture(renderer, texture, &parts[i], &places[i]);
    }
}

// On whole pixels of the display.
static void snap(f32* x, f32* y, f32* w, f32* h, f32 density) {
    f32 left = (f32)SDL_floor(*x * density + 0.5) / density, top = (f32)SDL_floor(*y * density + 0.5) / density;
    f32 right = (f32)SDL_floor((*x + *w) * density + 0.5) / density;
    f32 bottom = (f32)SDL_floor((*y + *h) * density + 0.5) / density;
    *x = left;
    *y = top;
    *w = right - left;
    *h = bottom - top;
}

void cromo_gui_box(void* renderer, f32 x, f32 y, f32 w, f32 h, f32 radius, f32 border, f32 density, u32 fill, u32 line) {
    snap(&x, &y, &w, &h, density);
    if (w <= 0 || h <= 0) return;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    Corner* corner = radius > 0 ? findCorner(renderer, radius, border, density) : 0;
    f32 side = corner ? corner->size / density : 0;
    f32 b = (f32)SDL_floor(border * density + 0.5) / density;
    if (fill >> 24) {
        setColor(renderer, fill);
        fillRect(renderer, x + side, y, w - 2 * side, h);
        fillRect(renderer, x, y + side, side, h - 2 * side);
        fillRect(renderer, x + w - side, y + side, side, h - 2 * side);
        if (corner) drawCorners(renderer, corner->fill, corner->size, x, y, w, h, side, fill, fill);
    }
    if ((line >> 24) && b > 0) {
        setColor(renderer, line);
        fillRect(renderer, x + side, y, w - 2 * side, b);
        fillRect(renderer, x + side, y + h - b, w - 2 * side, b);
        fillRect(renderer, x, y + side, b, h - 2 * side);
        fillRect(renderer, x + w - b, y + side, b, h - 2 * side);
        if (corner) drawCorners(renderer, corner->line, corner->size, x, y, w, h, side, line, line);
    }
}

// A soft shadow under a box: layers about 2 pixels of the display apart, at least
// five, each larger by its part of `blur` and with its part of the alpha of `color`,
// moved down by `dy`.
void cromo_gui_shadow(void* renderer, f32 x, f32 y, f32 w, f32 h, f32 radius, f32 density, u32 color, f32 blur, f32 dy) {
    if (!(color >> 24)) return;
    i32 layers = (i32)(blur * density / 2);
    if (layers < 5) layers = 5;
    if (layers > 24) layers = 24;
    u32 alpha = (color >> 24) / layers;
    if (alpha == 0) alpha = 1;
    u32 layer = (alpha << 24) | (color & 0xFFFFFF);
    for (i32 i = layers; i >= 1; --i) {
        f32 grow = blur * i / layers;
        cromo_gui_box(renderer, x - grow, y - grow + dy, w + 2 * grow, h + 2 * grow, radius + grow, 0, density, layer, 0);
    }
}

// The textures of the corners of a renderer, before it is destroyed.
void cromo_gui_boxes_free(void* renderer) {
    Corner** link = &corners;
    while (*link) {
        Corner* corner = *link;
        if (corner->renderer == renderer) {
            *link = corner->next;
            if (corner->fill) SDL_DestroyTexture(corner->fill);
            if (corner->line) SDL_DestroyTexture(corner->line);
            SDL_free(corner);
        } else {
            link = &corner->next;
        }
    }
}

// ---- The bars of the system on Android (D456) ----
//
// The activity of Cromo (runtime/android) colors them with the color of the bars of
// std.gui, their icons dark when it is light. Elsewhere there is nothing to do.

#ifdef __ANDROID__
_Bool SDL_SendAndroidMessage(u32 command, i32 param);
#endif

void cromo_gui_system_bars(u32 rgb, _Bool light) {
#ifdef __ANDROID__
    SDL_SendAndroidMessage(0x8001, (i32)((rgb & 0xFFFFFF) | (light ? 0x1000000u : 0)));
#else
    (void)rgb;
    (void)light;
#endif
}

// ---- The end of main (D442) ----
//
// The runtime calls cromo_at_main_end, when it is set, before the program ends:
// std.gui sets it to its function that waits for the windows.

extern void (*cromo_at_main_end)(void);
void cromo_gui_finish(void);  // [[export]] of std.gui

void cromo_gui_wait_at_end(void) { cromo_at_main_end = cromo_gui_finish; }
