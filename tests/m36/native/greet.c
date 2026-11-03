// A C library that calls the C library (D428): cromo links the programs that use
// it with glibc. Without headers, as the other libraries of the tests.
typedef unsigned long size_t;
int snprintf(char* out, size_t size, const char* format, ...);
void* malloc(size_t size);
void free(void* memory);
size_t strlen(const char* text);
int puts(const char* text);
int atexit(void (*function)(void));

static void goodbye(void) { puts("goodbye from atexit"); }

// "hello <n> from glibc" in out, with its length.
long greet(char* out, long size, long n) {
    char* text = malloc(64);
    snprintf(text, 64, "hello %ld from glibc", n);
    long length = (long)strlen(text);
    for (long i = 0; i <= length && i < size; i++) out[i] = text[i];
    free(text);
    return length;
}

// A line in the buffer of glibc, written when the program ends, and a function
// for atexit, which is in libc_nonshared.a and so in runtime/glibc.c.
void later(void) {
    puts("printed by glibc");
    atexit(goodbye);
}
