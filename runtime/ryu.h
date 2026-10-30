// The shortest digits of an f64 with Ryu (Ulf Adams, PLDI 2018): two products of
// 64 by 128 bits with the powers of 5 of ryu_tables.h give the bounds of the
// interval of the numbers that read back to the same value, and the digits are
// removed while the bounds keep them apart. The same digits as the algorithm of
// Burger & Dybvig with big integers (shortestDigits), much faster. Included by
// cromo_rt.c, where u64, i32, Decimal and FloatParts are defined.

#include "ryu_tables.h"

// The high 64 bits, and the low ones, of a * b, without __int128: on Windows its
// shifts would call compiler-rt.
static u64 ryuMultiply(u64 a, u64 b, u64* high) {
    u64 aLow = (u32)a;
    u64 aHigh = a >> 32;
    u64 bLow = (u32)b;
    u64 bHigh = b >> 32;
    u64 low = aLow * bLow;
    u64 middle1 = aHigh * bLow;
    u64 middle2 = aLow * bHigh;
    u64 top = aHigh * bHigh;
    u64 carry = (low >> 32) + (u32)middle1 + (u32)middle2;
    *high = top + (middle1 >> 32) + (middle2 >> 32) + (carry >> 32);
    return (carry << 32) | (u32)low;
}

// (m * multiplier) >> shift, the multiplier of 128 bits, with shift >= 64.
static u64 ryuMultiplyShift(u64 m, const u64* multiplier, i32 shift) {
    u64 high1;
    u64 low1 = ryuMultiply(m, multiplier[1], &high1);
    u64 high0;
    ryuMultiply(m, multiplier[0], &high0);
    u64 sum = high0 + low1;
    if (sum < high0) ++high1;
    i32 rest = shift - 64;
    if (rest == 0) return sum;
    return (high1 << (64 - rest)) | (sum >> rest);
}

static i32 ryuPow5Bits(i32 e) { return (i32)(((u32)e * 1217359) >> 19) + 1; }    // the bits of 5^e
static u32 ryuLog10Pow2(i32 e) { return ((u32)e * 78913) >> 18; }               // floor(e * log10(2))
static u32 ryuLog10Pow5(i32 e) { return ((u32)e * 732923) >> 20; }              // floor(e * log10(5))

static int ryuMultipleOfPow5(u64 value, u32 p) {
    u32 count = 0;
    while (value % 5 == 0) {
        value /= 5;
        ++count;
    }
    return count >= p;
}

static int ryuMultipleOfPow2(u64 value, u32 p) { return p >= 64 ? value == 0 : (value & ((1ull << p) - 1)) == 0; }

static i32 ryuLength(u64 v) {
    i32 length = 1;
    while (v >= 10) {
        v /= 10;
        ++length;
    }
    return length;
}

// D63: the shortest digits that read back to the same f64, and among them the
// nearest, the even one on a tie.
static void ryuDigits(const FloatParts* f, Decimal* out) {
    i32 e2 = f->exponent - 2;
    u64 m2 = f->mantissa;
    int even = (m2 & 1) == 0;
    u64 mv = 4 * m2;
    // At a power of two the next lower number is twice as close as the next higher one.
    u32 mmShift = !(f->mantissa == (1ull << 52) && f->exponent > f->minExponent);

    u64 vr, vp, vm;
    i32 e10;
    int vmTrailingZeros = 0;
    int vrTrailingZeros = 0;
    if (e2 >= 0) {
        u32 q = ryuLog10Pow2(e2) - (e2 > 3);
        e10 = (i32)q;
        i32 k = 125 + ryuPow5Bits((i32)q) - 1;
        i32 i = -e2 + (i32)q + k;
        vr = ryuMultiplyShift(4 * m2, ryuPow5Inverse[q], i);
        vp = ryuMultiplyShift(4 * m2 + 2, ryuPow5Inverse[q], i);
        vm = ryuMultiplyShift(4 * m2 - 1 - mmShift, ryuPow5Inverse[q], i);
        if (q <= 21) {
            if (mv % 5 == 0) {
                vrTrailingZeros = ryuMultipleOfPow5(mv, q);
            } else if (even) {
                vmTrailingZeros = ryuMultipleOfPow5(mv - 1 - mmShift, q);
            } else {
                vp -= ryuMultipleOfPow5(mv + 2, q);
            }
        }
    } else {
        u32 q = ryuLog10Pow5(-e2) - (-e2 > 1);
        e10 = (i32)q + e2;
        i32 i = -e2 - (i32)q;
        i32 k = ryuPow5Bits(i) - 125;
        i32 j = (i32)q - k;
        vr = ryuMultiplyShift(4 * m2, ryuPow5[i], j);
        vp = ryuMultiplyShift(4 * m2 + 2, ryuPow5[i], j);
        vm = ryuMultiplyShift(4 * m2 - 1 - mmShift, ryuPow5[i], j);
        if (q <= 1) {
            vrTrailingZeros = 1;
            if (even) {
                vmTrailingZeros = mmShift == 1;
            } else {
                --vp;
            }
        } else if (q < 63) {
            vrTrailingZeros = ryuMultipleOfPow2(mv, q);
        }
    }

    i32 removed = 0;
    u32 lastRemoved = 0;
    u64 output;
    if (vmTrailingZeros || vrTrailingZeros) {
        for (; vp / 10 > vm / 10; ++removed) {
            vmTrailingZeros &= vm % 10 == 0;
            vrTrailingZeros &= lastRemoved == 0;
            lastRemoved = (u32)(vr % 10);
            vr /= 10;
            vp /= 10;
            vm /= 10;
        }
        if (vmTrailingZeros) {
            for (; vm % 10 == 0; ++removed) {
                vrTrailingZeros &= lastRemoved == 0;
                lastRemoved = (u32)(vr % 10);
                vr /= 10;
                vp /= 10;
                vm /= 10;
            }
        }
        if (vrTrailingZeros && lastRemoved == 5 && vr % 2 == 0) lastRemoved = 4;  // the tie goes to the even
        output = vr + ((vr == vm && (!even || !vmTrailingZeros)) || lastRemoved >= 5);
    } else {
        int roundUp = 0;
        for (; vp / 10 > vm / 10; ++removed) {
            roundUp = vr % 10 >= 5;
            vr /= 10;
            vp /= 10;
            vm /= 10;
        }
        output = vr + (vr == vm || roundUp);
    }

    i32 length = ryuLength(output);
    out->count = length;
    out->exponent = e10 + removed + length;
    for (i32 i = length - 1; i >= 0; --i) {
        out->digit[i] = (u8)(output % 10);
        output /= 10;
    }
}
