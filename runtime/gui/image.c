// The images of std.gui (D446): stb_image decodes PNG, JPEG, BMP and GIF, its
// first frame, from bytes in memory, into pixels R, G, B, A. Without the C
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
