// gzip (RFC 1952) with deflate (RFC 1951), for std.gzip and the client of std.http
// (D367). Inflate reads the three kinds of block; deflate finds the repeats with
// chains of hashes in a window of 32 KB and writes them with the fixed codes, in
// one block. The memory comes from the runtime, as for the rest of the library.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;

void* cromo_alloc(i64 size);
void* cromo_realloc(void* memory, i64 size);
void cromo_free(void* memory);
void* memcpy(void* dest, const void* source, __SIZE_TYPE__ count);
void* memset(void* dest, int value, __SIZE_TYPE__ count);

// What went wrong, for std.gzip: written in place of the size.
#define GZIP_NOT_GZIP -1
#define GZIP_DAMAGED -2
#define GZIP_CHECK -3
#define GZIP_TOO_LARGE -4

// ---- The output, which grows ----

typedef struct {
    u8* data;
    i64 size;
    i64 capacity;
    i64 limit;
    int full;  // it would go beyond the limit
} Buffer;

static int reserve(Buffer* out, i64 extra) {
    if (out->size + extra > out->limit) {
        out->full = 1;
        return 0;
    }
    if (out->size + extra <= out->capacity) return 1;
    i64 capacity = out->capacity ? out->capacity * 2 : 4096;
    while (capacity < out->size + extra) capacity *= 2;
    out->data = cromo_realloc(out->data, capacity);
    out->capacity = capacity;
    return 1;
}

static int putByte(Buffer* out, u8 byte) {
    if (out->size == out->capacity && !reserve(out, 1)) return 0;
    if (out->size >= out->limit) {
        out->full = 1;
        return 0;
    }
    out->data[out->size++] = byte;
    return 1;
}

static u32 crc32(const u8* data, i64 size) {
    u32 table[256];
    for (u32 n = 0; n < 256; ++n) {
        u32 c = n;
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    u32 crc = 0xFFFFFFFFu;
    for (i64 i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---- The tables of deflate ----

static const u16 lengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 distanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 distanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// ---- Inflate ----

typedef struct {
    const u8* data;
    i64 size;
    i64 at;
    u32 bits;
    int count;
    int failed;  // the data ended too soon
} Input;

static u32 readBits(Input* in, int n) {
    while (in->count < n) {
        if (in->at >= in->size) {
            in->failed = 1;
            return 0;
        }
        in->bits |= (u32)in->data[in->at++] << in->count;
        in->count += 8;
    }
    u32 value = in->bits & ((1u << n) - 1);
    in->bits >>= n;
    in->count -= n;
    return value;
}

// A canonical code: how many codes of each length, and the symbols in their order.
typedef struct {
    u16 counts[16];
    u16 symbols[288];
} Huffman;

static int buildHuffman(Huffman* code, const u8* lengths, int n) {
    memset(code->counts, 0, sizeof code->counts);
    for (int i = 0; i < n; ++i) code->counts[lengths[i]]++;
    code->counts[0] = 0;
    int left = 1;
    for (int length = 1; length < 16; ++length) {
        left = (left << 1) - code->counts[length];
        if (left < 0) return 0;  // more codes than the lengths allow
    }
    u16 offsets[16];
    offsets[1] = 0;
    for (int length = 1; length < 15; ++length) offsets[length + 1] = offsets[length] + code->counts[length];
    for (int i = 0; i < n; ++i) {
        if (lengths[i]) code->symbols[offsets[lengths[i]]++] = (u16)i;
    }
    return 1;
}

// The codes are read a bit at a time, the first bit the highest of the code.
static int decodeSymbol(Input* in, const Huffman* code) {
    int value = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length < 16; ++length) {
        value |= (int)readBits(in, 1);
        if (in->failed) return -1;
        int count = code->counts[length];
        if (value - count < first) return code->symbols[index + (value - first)];
        index += count;
        first = (first + count) << 1;
        value <<= 1;
    }
    return -1;
}

static int inflateCodes(Input* in, Buffer* out, const Huffman* literals, const Huffman* distances) {
    for (;;) {
        int symbol = decodeSymbol(in, literals);
        if (symbol < 0) return GZIP_DAMAGED;
        if (symbol < 256) {
            if (!putByte(out, (u8)symbol)) return GZIP_TOO_LARGE;
            continue;
        }
        if (symbol == 256) return 0;
        symbol -= 257;
        if (symbol >= 29) return GZIP_DAMAGED;
        i64 length = lengthBase[symbol] + readBits(in, lengthExtra[symbol]);
        int place = decodeSymbol(in, distances);
        if (place < 0 || place >= 30) return GZIP_DAMAGED;
        i64 distance = distanceBase[place] + readBits(in, distanceExtra[place]);
        if (in->failed || distance > out->size) return GZIP_DAMAGED;
        if (!reserve(out, length)) return GZIP_TOO_LARGE;
        for (i64 i = 0; i < length; ++i) {  // the copy can overlap what it writes
            out->data[out->size] = out->data[out->size - distance];
            out->size++;
        }
    }
}

static int inflateFixed(Input* in, Buffer* out) {
    u8 lengths[288 + 30];
    for (int i = 0; i < 144; ++i) lengths[i] = 8;
    for (int i = 144; i < 256; ++i) lengths[i] = 9;
    for (int i = 256; i < 280; ++i) lengths[i] = 7;
    for (int i = 280; i < 288; ++i) lengths[i] = 8;
    for (int i = 0; i < 30; ++i) lengths[288 + i] = 5;
    Huffman literals;
    Huffman distances;
    buildHuffman(&literals, lengths, 288);
    buildHuffman(&distances, lengths + 288, 30);
    return inflateCodes(in, out, &literals, &distances);
}

static int inflateDynamic(Input* in, Buffer* out) {
    static const u8 order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int literalCount = (int)readBits(in, 5) + 257;
    int distanceCount = (int)readBits(in, 5) + 1;
    int codeCount = (int)readBits(in, 4) + 4;
    if (in->failed || literalCount > 286 || distanceCount > 30) return GZIP_DAMAGED;
    u8 lengths[320];
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < codeCount; ++i) lengths[order[i]] = (u8)readBits(in, 3);
    Huffman lengthCode;
    if (in->failed || !buildHuffman(&lengthCode, lengths, 19)) return GZIP_DAMAGED;
    int total = literalCount + distanceCount;
    for (int index = 0; index < total;) {
        int symbol = decodeSymbol(in, &lengthCode);
        if (symbol < 0) return GZIP_DAMAGED;
        if (symbol < 16) {
            lengths[index++] = (u8)symbol;
            continue;
        }
        u8 repeated = 0;
        int times;
        if (symbol == 16) {
            if (index == 0) return GZIP_DAMAGED;
            repeated = lengths[index - 1];
            times = 3 + (int)readBits(in, 2);
        } else if (symbol == 17) {
            times = 3 + (int)readBits(in, 3);
        } else {
            times = 11 + (int)readBits(in, 7);
        }
        if (in->failed || index + times > total) return GZIP_DAMAGED;
        while (times--) lengths[index++] = repeated;
    }
    if (lengths[256] == 0) return GZIP_DAMAGED;  // no end of the block
    Huffman literals;
    Huffman distances;
    if (!buildHuffman(&literals, lengths, literalCount) || !buildHuffman(&distances, lengths + literalCount, distanceCount)) {
        return GZIP_DAMAGED;
    }
    return inflateCodes(in, out, &literals, &distances);
}

static int inflate(Input* in, Buffer* out) {
    int last;
    do {
        last = (int)readBits(in, 1);
        int type = (int)readBits(in, 2);
        if (in->failed) return GZIP_DAMAGED;
        int problem = 0;
        if (type == 0) {  // stored: from the next byte, its length and the complement
            in->bits = 0;
            in->count = 0;
            if (in->at + 4 > in->size) return GZIP_DAMAGED;
            const u8* head = in->data + in->at;
            u32 length = head[0] | (u32)head[1] << 8;
            u32 complement = head[2] | (u32)head[3] << 8;
            in->at += 4;
            if (length != (~complement & 0xFFFF) || in->at + length > in->size) return GZIP_DAMAGED;
            if (!reserve(out, length)) return GZIP_TOO_LARGE;
            memcpy(out->data + out->size, in->data + in->at, length);
            out->size += length;
            in->at += length;
        } else if (type == 1) {
            problem = inflateFixed(in, out);
        } else if (type == 2) {
            problem = inflateDynamic(in, out);
        } else {
            return GZIP_DAMAGED;
        }
        if (problem) return problem;
    } while (!last);
    return 0;
}

// One member of gzip after the other: the data of each, checked with its CRC.
u8* cromo_gzip_decompress(const u8* data, i64 size, i64 limit, i64* result) {
    Buffer out = {0, 0, 0, limit, 0};
    i64 at = 0;
    int problem = 0;
    do {
        if (size - at < 18 || data[at] != 0x1F || data[at + 1] != 0x8B || data[at + 2] != 8) {
            problem = at == 0 ? GZIP_NOT_GZIP : GZIP_DAMAGED;
            break;
        }
        u8 flags = data[at + 3];
        at += 10;
        if (flags & 4) {  // FEXTRA
            if (at + 2 > size) {
                problem = GZIP_DAMAGED;
                break;
            }
            at += 2 + (data[at] | (i64)data[at + 1] << 8);
        }
        for (int field = 8; field <= 16; field *= 2) {  // FNAME, FCOMMENT: texts ended by a zero
            if (!(flags & field)) continue;
            while (at < size && data[at]) ++at;
            ++at;
        }
        if (flags & 2) at += 2;  // FHCRC
        if (at >= size) {
            problem = GZIP_DAMAGED;
            break;
        }
        i64 start = out.size;
        Input in = {data, size, at, 0, 0, 0};
        problem = inflate(&in, &out);
        if (problem) break;
        at = in.at;
        if (at + 8 > size) {
            problem = GZIP_DAMAGED;
            break;
        }
        const u8* tail = data + at;
        u32 crc = tail[0] | (u32)tail[1] << 8 | (u32)tail[2] << 16 | (u32)tail[3] << 24;
        u32 length = tail[4] | (u32)tail[5] << 8 | (u32)tail[6] << 16 | (u32)tail[7] << 24;
        at += 8;
        if (crc != crc32(out.data + start, out.size - start) || length != (u32)(out.size - start)) {
            problem = GZIP_CHECK;
            break;
        }
        while (at < size && data[at] == 0) ++at;  // the zeros that some writers add at the end
    } while (at < size);
    if (out.full) problem = GZIP_TOO_LARGE;
    if (problem) {
        cromo_free(out.data);
        *result = problem;
        return 0;
    }
    *result = out.size;
    return out.data ? out.data : cromo_alloc(1);
}

// ---- Deflate ----

typedef struct {
    Buffer* out;
    u32 bits;
    int count;
} Bits;

static void putBits(Bits* w, u32 value, int n) {
    w->bits |= value << w->count;
    w->count += n;
    while (w->count >= 8) {
        putByte(w->out, (u8)w->bits);
        w->bits >>= 8;
        w->count -= 8;
    }
}

// A code of Huffman goes out with its first bit the highest.
static void putCode(Bits* w, u32 code, int length) {
    u32 reversed = 0;
    for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1) << (length - 1 - i);
    putBits(w, reversed, length);
}

static void putLiteral(Bits* w, int symbol) {
    if (symbol < 144) {
        putCode(w, 0x30 + symbol, 8);
    } else if (symbol < 256) {
        putCode(w, 0x190 + symbol - 144, 9);
    } else if (symbol < 280) {
        putCode(w, symbol - 256, 7);
    } else {
        putCode(w, 0xC0 + symbol - 280, 8);
    }
}

static void putMatch(Bits* w, int length, int distance) {
    int index = 28;
    while (lengthBase[index] > length) --index;
    putLiteral(w, 257 + index);
    putBits(w, (u32)(length - lengthBase[index]), lengthExtra[index]);
    int place = 29;
    while (distanceBase[place] > distance) --place;
    putCode(w, (u32)place, 5);
    putBits(w, (u32)(distance - distanceBase[place]), distanceExtra[place]);
}

#define WINDOW 32768
#define HASHES 65536
#define CHAIN 128

static u32 hash3(const u8* p) { return ((u32)p[0] << 16 ^ (u32)p[1] << 8 ^ p[2]) * 2654435761u >> 16; }

u8* cromo_gzip_compress(const u8* data, i64 size, i64* result) {
    Buffer out = {0, 0, 0, (i64)1 << 62, 0};
    reserve(&out, size / 2 + 64);
    static const u8 header[10] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 255};
    for (int i = 0; i < 10; ++i) putByte(&out, header[i]);

    Bits w = {&out, 0, 0};
    putBits(&w, 1, 1);  // the last block
    putBits(&w, 1, 2);  // with the fixed codes
    i64* heads = cromo_alloc(HASHES * (i64)sizeof(i64));
    i64* previous = cromo_alloc(WINDOW * (i64)sizeof(i64));
    for (i64 i = 0; i < HASHES; ++i) heads[i] = -1;
    i64 at = 0;
    while (at < size) {
        int bestLength = 0;
        i64 bestDistance = 0;
        if (at + 3 <= size) {
            u32 h = hash3(data + at);
            i64 candidate = heads[h];
            i64 longest = size - at < 258 ? size - at : 258;
            for (int steps = 0; candidate >= 0 && at - candidate <= WINDOW && steps < CHAIN; ++steps) {
                if (data[candidate + bestLength] == data[at + bestLength]) {
                    int length = 0;
                    while (length < longest && data[candidate + length] == data[at + length]) ++length;
                    if (length > bestLength) {
                        bestLength = length;
                        bestDistance = at - candidate;
                        if (length == longest) break;
                    }
                }
                i64 next = previous[candidate % WINDOW];
                if (next >= candidate) break;
                candidate = next;
            }
        }
        i64 step = bestLength >= 3 ? bestLength : 1;
        if (bestLength >= 3) {
            putMatch(&w, bestLength, (int)bestDistance);
        } else {
            putLiteral(&w, data[at]);
        }
        for (i64 i = 0; i < step; ++i, ++at) {  // every position of the match can be found again
            if (at + 3 > size) continue;
            u32 h = hash3(data + at);
            previous[at % WINDOW] = heads[h];
            heads[h] = at;
        }
    }
    cromo_free(heads);
    cromo_free(previous);
    putLiteral(&w, 256);  // the end of the block
    if (w.count) putBits(&w, 0, 8 - w.count);

    u32 crc = crc32(data, size);
    u32 length = (u32)size;
    for (int i = 0; i < 4; ++i) putByte(&out, (u8)(crc >> (8 * i)));
    for (int i = 0; i < 4; ++i) putByte(&out, (u8)(length >> (8 * i)));
    *result = out.size;
    return out.data;
}
