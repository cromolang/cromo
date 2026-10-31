// The keys of RSA and of the curves P-256 and P-384 of std.jwt (D399), with
// mbedTLS: read once, then used to verify and to sign with PKCS#1 v1.5 and with
// ECDSA. The library of Cromo checks the algorithm and the size of the key
// (library/std/jwt.cro). A signature of ECDSA is r and s side by side, as JWS
// has it (RFC 7518 §3.4); mbedTLS wants it in DER. Many threads verify with the
// same key at once, and sign with it, under the mutex of mbedTLS for RSA.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/rsa.h"
#include "glue.h"

// The kinds of key, for the library.
enum { OTHER_KEY, RSA_KEY, P256_KEY, P384_KEY };

int32_t cromo_crypto_key_kind(void* key);

// The tasks of a server verify with the same key at once: after it is read, a key
// is only read. mbedTLS computes the multiples of the generator of a curve the
// first time it needs them and keeps them in the group, so two first
// verifications at once would both write them: they are computed here, before
// the key is shared.
static void prepareCurve(mbedtls_pk_context* key) {
    mbedtls_ecp_group* group = &mbedtls_pk_ec(*key)->MBEDTLS_PRIVATE(grp);
    mbedtls_ecp_point point;
    mbedtls_mpi one;
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&one);
    if (mbedtls_mpi_lset(&one, 1) == 0) {
        (void)mbedtls_ecp_mul(group, &point, &one, &group->G, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    }
    mbedtls_mpi_free(&one);
    mbedtls_ecp_point_free(&point);
}

// A key from PEM, ended by a zero that `size` counts, or from DER; a public key or
// a private one. Null when mbedTLS cannot read it.
void* cromo_crypto_key_read(int32_t isPrivate, const uint8_t* data, int64_t size) {
    if (!cromo_tls_setup()) return NULL;
    mbedtls_pk_context* key = cromo_alloc(sizeof(mbedtls_pk_context));
    mbedtls_pk_init(key);
    int code = isPrivate ? mbedtls_pk_parse_key(key, data, (size_t)size, NULL, 0, mbedtls_psa_get_random,
                                                MBEDTLS_PSA_RANDOM_STATE)
                         : mbedtls_pk_parse_public_key(key, data, (size_t)size);
    if (code != 0) {
        mbedtls_pk_free(key);
        cromo_free(key);
        return NULL;
    }
    int32_t kind = cromo_crypto_key_kind(key);
    if (kind == P256_KEY || kind == P384_KEY) prepareCurve(key);
    return key;
}

// mbedtls_rsa_public holds the mutex of its context for the whole computation,
// and the verifications with the same key would wait for each other: each one
// has its own copy of n and e, which never change, also while the key signs.
static int rsaVerify(mbedtls_pk_context* key, mbedtls_md_type_t type, const uint8_t* digest, size_t digestSize,
                     const uint8_t* signature, size_t signatureSize) {
    const mbedtls_rsa_context* rsa = mbedtls_pk_rsa(*key);
    if (signatureSize != mbedtls_rsa_get_len(rsa)) return 0;
    mbedtls_mpi n;
    mbedtls_mpi e;
    mbedtls_rsa_context copy;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_rsa_init(&copy);
    int valid = mbedtls_rsa_export(rsa, &n, NULL, NULL, NULL, &e) == 0 &&
                mbedtls_rsa_import(&copy, &n, NULL, NULL, NULL, &e) == 0 && mbedtls_rsa_complete(&copy) == 0 &&
                mbedtls_rsa_pkcs1_verify(&copy, type, (unsigned int)digestSize, digest, signature) == 0;
    mbedtls_rsa_free(&copy);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&n);
    return valid;
}

int32_t cromo_crypto_key_kind(void* key) {
    mbedtls_pk_type_t type = mbedtls_pk_get_type(key);
    if (type == MBEDTLS_PK_RSA) return RSA_KEY;
    if (type != MBEDTLS_PK_ECKEY && type != MBEDTLS_PK_ECDSA) return OTHER_KEY;
    mbedtls_ecp_group_id group = mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(*(mbedtls_pk_context*)key));
    if (group == MBEDTLS_ECP_DP_SECP256R1) return P256_KEY;
    if (group == MBEDTLS_ECP_DP_SECP384R1) return P384_KEY;
    return OTHER_KEY;
}

int64_t cromo_crypto_key_bits(void* key) { return (int64_t)mbedtls_pk_get_bitlen(key); }

// 1 when the signature is the one of the data, with the hash of the enum Hash of
// std.crypto.
int32_t cromo_crypto_key_verify(void* key, int32_t hash, const uint8_t* data, int64_t size, const uint8_t* signature,
                                int64_t signatureSize) {
    const mbedtls_md_info_t* md = cromo_crypto_md(hash);
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    if (mbedtls_md(md, data, (size_t)size, digest) != 0) return 0;
    if (mbedtls_pk_get_type(key) == MBEDTLS_PK_RSA) {
        return rsaVerify(key, mbedtls_md_get_type(md), digest, mbedtls_md_get_size(md), signature,
                         (size_t)signatureSize);
    }
    uint8_t der[MBEDTLS_ECDSA_MAX_LEN];
    size_t derSize = 0;
    if (mbedtls_ecdsa_raw_to_der(mbedtls_pk_get_bitlen(key), signature, (size_t)signatureSize, der, sizeof der,
                                 &derSize) != 0) {
        return 0;
    }
    return mbedtls_pk_verify(key, mbedtls_md_get_type(md), digest, mbedtls_md_get_size(md), der, derSize) == 0;
}

// The signature of the data in `signature`, which has room for `capacity` bytes:
// its size, or -1 when the key is not a private one.
int64_t cromo_crypto_key_sign(void* key, int32_t hash, const uint8_t* data, int64_t size, uint8_t* signature,
                              int64_t capacity) {
    const mbedtls_md_info_t* md = cromo_crypto_md(hash);
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    if (mbedtls_md(md, data, (size_t)size, digest) != 0) return -1;
    uint8_t out[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t length = 0;
    if (mbedtls_pk_sign(key, mbedtls_md_get_type(md), digest, mbedtls_md_get_size(md), out, sizeof out, &length,
                        mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE) != 0) {
        return -1;
    }
    if (mbedtls_pk_get_type(key) == MBEDTLS_PK_RSA) {
        if ((int64_t)length > capacity) return -1;
        memcpy(signature, out, length);
        return (int64_t)length;
    }
    size_t raw = 0;
    if (mbedtls_ecdsa_der_to_raw(mbedtls_pk_get_bitlen(key), out, length, signature, (size_t)capacity, &raw) != 0) {
        return -1;
    }
    return (int64_t)raw;
}

void cromo_crypto_key_free(void* key) {
    mbedtls_pk_free(key);
    cromo_free(key);
}
