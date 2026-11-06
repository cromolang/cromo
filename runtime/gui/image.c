// The images of std.gui (D446): stb_image decodes PNG, JPEG, BMP and GIF, its
// first frame, from bytes in memory, into pixels R, G, B, A, and nanosvg draws the
// SVG images (D484). Without the C
// library: the headers are those of runtime/libc, as for mbedTLS (D233), and the
// memory comes from SDL. In an object of its own, which only the programs that
// show images link.

#include <stddef.h>

typedef unsigned char u8;
typedef int i32;
typedef long long i64;

void* SDL_malloc(size_t size);
void* SDL_realloc(void* memory, size_t size);
void SDL_free(void* memory);

#define STBI_MALLOC(size) SDL_malloc(size)
#define STBI_REALLOC(memory, size) SDL_realloc(memory, size)
#define STBI_FREE(memory) SDL_free(memory)
#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS  // its reason of a failure is one for the program
#define STBI_FAILURE_USERMSG
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define abs(x) ((x) < 0 ? -(x) : (x))  // its only function of <stdlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"  // the code of others, as it is
#include "stb_image.h"
#pragma clang diagnostic pop

// Writes a reason of a failure in `reason`, a string of C of `room` bytes.
static void tell(char* reason, i64 room, const char* text) {
    i64 n = 0;
    while (text[n] && n < room - 1) {
        reason[n] = text[n];
        ++n;
    }
    reason[n] = 0;
}

// The pixels of an image of `size` bytes, 4 bytes for each, with its width and
// its height; 0 when it cannot be read, with the reason in `reason`.
void* cromo_gui_image_decode(const u8* bytes, i64 size, i32* width, i32* height, char* reason, i64 room) {
    *width = 0;
    *height = 0;
    tell(reason, room, "");
    if (size <= 0 || size > 0x7FFFFFFF) {
        tell(reason, room, size <= 0 ? "the file is empty" : "the file is too large");
        return 0;
    }
    i32 channels = 0;
    u8* pixels = stbi_load_from_memory(bytes, (i32)size, width, height, &channels, 4);
    if (!pixels) tell(reason, room, stbi_failure_reason());
    return pixels;
}

void cromo_gui_image_free(void* pixels) { SDL_free(pixels); }

// The square at the center of an image, scaled to `size` pixels by the average of
// up to 8 by 8 samples for each, and made round, its edge smoothed over about a
// pixel: the round image of an item of a list (D469). Its pixels, or 0.
void* cromo_gui_image_round(const u8* pixels, i32 width, i32 height, i32 size) {
    i32 side = width < height ? width : height;
    if (side <= 0 || size <= 0) return 0;
    i32 left = (width - side) / 2;
    i32 top = (height - side) / 2;
    u8* out = SDL_malloc((size_t)size * (size_t)size * 4);
    if (!out) return 0;
    float scale = (float)side / (float)size;
    i32 samples = (i32)scale + 1;
    if (samples > 8) samples = 8;
    float radius = (float)size / 2;
    float outer = (radius + 0.5f) * (radius + 0.5f);
    for (i32 y = 0; y < size; ++y) {
        for (i32 x = 0; x < size; ++x) {
            unsigned sum[4] = {0, 0, 0, 0};
            for (i32 sy = 0; sy < samples; ++sy) {
                i32 py = top + (i32)(((float)y + ((float)sy + 0.5f) / (float)samples) * scale);
                if (py >= top + side) py = top + side - 1;
                for (i32 sx = 0; sx < samples; ++sx) {
                    i32 px = left + (i32)(((float)x + ((float)sx + 0.5f) / (float)samples) * scale);
                    if (px >= left + side) px = left + side - 1;
                    const u8* p = pixels + ((size_t)py * (size_t)width + (size_t)px) * 4;
                    for (i32 c = 0; c < 4; ++c) sum[c] += p[c];
                }
            }
            unsigned count = (unsigned)(samples * samples);
            // The part of the pixel inside the circle, from its distance squared:
            // (r + 0.5)^2 - d^2 is about 2r (r + 0.5 - d) at the edge.
            float dx = (float)x + 0.5f - radius;
            float dy = (float)y + 0.5f - radius;
            float inside = (outer - dx * dx - dy * dy) / (2 * radius);
            if (inside < 0) inside = 0;
            if (inside > 1) inside = 1;
            u8* q = out + ((size_t)y * (size_t)size + (size_t)x) * 4;
            for (i32 c = 0; c < 3; ++c) q[c] = (u8)(sum[c] / count);
            q[3] = (u8)((float)(sum[3] / count) * inside + 0.5f);
        }
    }
    return out;
}

// ---- SVG (D484) ----
//
// nanosvg parses an SVG image into shapes and draws them with its rasterizer,
// smoothed. It has no C library either: its memory, its strings and its math are
// those of SDL, and it never opens a file.

long SDL_strtol(const char* text, char** end, int base);
long long SDL_strtoll(const char* text, char** end, int base);
void SDL_qsort(void* items, size_t count, size_t size, int (*compare)(const void*, const void*));
int SDL_sscanf(const char* text, const char* format, ...);
size_t SDL_strlen(const char* text);
int SDL_strcmp(const char* a, const char* b);
int SDL_strncmp(const char* a, const char* b, size_t count);
char* SDL_strchr(const char* text, int value);
char* SDL_strstr(const char* text, const char* part);
void* SDL_memcpy(void* target, const void* source, size_t size);
void* SDL_memset(void* target, int value, size_t size);
float SDL_sqrtf(float x);
double SDL_sqrt(double x);
double SDL_fabs(double x);
float SDL_fabsf(float x);
float SDL_tanf(float x);
float SDL_cosf(float x);
float SDL_sinf(float x);
float SDL_acosf(float x);
float SDL_atan2f(float y, float x);
float SDL_fmodf(float x, float y);
float SDL_ceilf(float x);
float SDL_floorf(float x);
float SDL_roundf(float x);
double SDL_pow(double x, double y);

// The strncpy of C: at most `count` bytes, the rest of them zeros.
static char* copyText(char* target, const char* source, size_t count) {
    size_t i = 0;
    for (; i < count && source[i]; ++i) target[i] = source[i];
    for (; i < count; ++i) target[i] = 0;
    return target;
}

#define malloc(size) SDL_malloc(size)
#define realloc(memory, size) SDL_realloc(memory, size)
#define free(memory) SDL_free(memory)
#define strtol SDL_strtol
#define strtoll SDL_strtoll
#define qsort SDL_qsort
#define sscanf SDL_sscanf
#define strlen SDL_strlen
#define strcmp SDL_strcmp
#define strncmp SDL_strncmp
#define strchr SDL_strchr
#define strstr SDL_strstr
#define strncpy copyText
#define memcpy SDL_memcpy
#define memset SDL_memset
#define sqrtf SDL_sqrtf
#define sqrt SDL_sqrt
#define fabs SDL_fabs
#define fabsf SDL_fabsf
#define tanf SDL_tanf
#define cosf SDL_cosf
#define sinf SDL_sinf
#define acosf SDL_acosf
#define atan2f SDL_atan2f
#define fmodf SDL_fmodf
#define ceilf SDL_ceilf
#define floorf SDL_floorf
#define roundf SDL_roundf
#define pow SDL_pow
#define isnan(x) ((x) != (x))
// nsvgParseFromFile, which the library does not call, finds no file.
#define FILE void
#define fopen(name, mode) ((void*)0)
#define fseek(file, at, from) (-1)
#define ftell(file) (-1L)
#define fread(target, size, count, file) ((size_t)0)
#define fclose(file) 0
#define SEEK_SET 0
#define SEEK_END 2
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"  // the code of others, as it is
#include "nanosvg.h"
#include "nanosvgrast.h"
#pragma clang diagnostic pop

// The shapes of an SVG image of `size` bytes, read from a copy, which nanosvg
// changes while it reads it; 0 when they cannot be read.
static NSVGimage* parseSvg(const u8* bytes, i64 size) {
    if (size <= 0 || size > 0x7FFFFFFF) return 0;
    char* text = SDL_malloc((size_t)size + 1);
    if (!text) return 0;
    SDL_memcpy(text, bytes, (size_t)size);
    text[size] = 0;
    NSVGimage* image = nsvgParse(text, "px", 96.0f);
    SDL_free(text);
    return image;
}

// The size of an SVG image in pixels, from its width and its height or else from
// its viewBox; false when it is not an SVG image that can be read.
_Bool cromo_gui_svg_size(const u8* bytes, i64 size, float* width, float* height) {
    *width = 0;
    *height = 0;
    NSVGimage* image = parseSvg(bytes, size);
    if (!image) return 0;
    *width = image->width;
    *height = image->height;
    nsvgDelete(image);
    return *width > 0 && *height > 0;
}

// The pixels R, G, B, A of an SVG image drawn in `width` by `height` pixels, as
// large as they allow with its proportions, at their center, transparent around
// it; 0 when it cannot be read. cromo_gui_image_free frees them.
void* cromo_gui_svg_draw(const u8* bytes, i64 size, i32 width, i32 height) {
    if (width <= 0 || height <= 0) return 0;
    NSVGimage* image = parseSvg(bytes, size);
    if (!image) return 0;
    u8* pixels = 0;
    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (rasterizer && image->width > 0 && image->height > 0) {
        pixels = SDL_malloc((size_t)width * (size_t)height * 4);
        if (pixels) {
            float sx = (float)width / image->width;
            float sy = (float)height / image->height;
            float scale = sx < sy ? sx : sy;
            float x = ((float)width - image->width * scale) / 2;
            float y = ((float)height - image->height * scale) / 2;
            nsvgRasterize(rasterizer, image, x, y, scale, pixels, width, height, width * 4);
        }
    }
    if (rasterizer) nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);
    return pixels;
}
