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
