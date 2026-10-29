// The hashes and HMAC of std.crypto (D236), with mbedTLS. The library of Cromo
// checks the arguments before calling them (library/std/crypto.cro).

#include <stddef.h>
#include <stdint.h>

#include "mbedtls/md.h"
#include "glue.h"

// ---- Hashes, in the order of the enum Hash of std.crypto ----

static const mbedtls_md_info_t* hashInfo(int32_t kind) {
    static const mbedtls_md_type_t types[] = {MBEDTLS_MD_MD5, MBEDTLS_MD_SHA1, MBEDTLS_MD_SHA256, MBEDTLS_MD_SHA512};
    return mbedtls_md_info_from_type(types[kind]);
}

void cromo_crypto_hash(int32_t kind, const uint8_t* data, int64_t size, uint8_t* out) {
    mbedtls_md(hashInfo(kind), data, (size_t)size, out);
}

void cromo_crypto_hmac(int32_t kind, const uint8_t* key, int64_t keySize, const uint8_t* data, int64_t size,
                       uint8_t* out) {
    mbedtls_md_hmac(hashInfo(kind), key, (size_t)keySize, data, (size_t)size, out);
}

// A hash computed in parts.
void* cromo_crypto_hasher(int32_t kind) {
    mbedtls_md_context_t* context = cromo_alloc(sizeof(mbedtls_md_context_t));
    mbedtls_md_init(context);
    mbedtls_md_setup(context, hashInfo(kind), 0);
    mbedtls_md_starts(context);
    return context;
}

void cromo_crypto_hasher_add(void* context, const uint8_t* data, int64_t size) {
    mbedtls_md_update(context, data, (size_t)size);
}

// The hash of what was added; the hasher starts again, empty.
void cromo_crypto_hasher_finish(void* context, uint8_t* out) {
    mbedtls_md_finish(context, out);
    mbedtls_md_starts(context);
}

void cromo_crypto_hasher_end(void* context) {
    mbedtls_md_free(context);
    cromo_free(context);
}
