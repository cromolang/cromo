// The parts of std.crypto that a protocol puts together by itself (D581), with
// mbedTLS: ChaCha20 and Poly1305 one without the other, AES in CTR mode, and the
// signatures of RSA and of ECDSA with the keys given as numbers, as SSH has them.
// The library of Cromo checks the sizes before calling them (library/std/crypto.cro).

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/bignum.h"
#include "mbedtls/chacha20.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/poly1305.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/rsa.h"
#include "glue.h"

// ---- The ciphers without authentication ----

// The data with the stream of ChaCha20 (RFC 8439): a key of 32 bytes, a nonce of
// 12, and the number of the first block.
void cromo_crypto_chacha20(const uint8_t* key, const uint8_t* nonce, uint32_t counter, const uint8_t* data, int64_t size,
                           uint8_t* out) {
    mbedtls_chacha20_crypt(key, nonce, counter, (size_t)size, data, out);
}

// The tag of Poly1305, of 16 bytes, with a key of 32 that is used once.
void cromo_crypto_poly1305(const uint8_t* key, const uint8_t* data, int64_t size, uint8_t* out) {
    mbedtls_poly1305_mac(key, data, (size_t)size, out);
}

// The data with the stream of AES in CTR mode: a key of 16, 24 or 32 bytes and a
// counter of 16, which is the first block and grows by one, big-endian, each 16
// bytes.
void cromo_crypto_aes_ctr(const uint8_t* key, int64_t keySize, const uint8_t* counter, const uint8_t* data, int64_t size,
                          uint8_t* out) {
    mbedtls_aes_context aes;
    uint8_t block[16];
    uint8_t stream[16];
    size_t offset = 0;
    memcpy(block, counter, 16);
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, (unsigned)keySize * 8);
    mbedtls_aes_crypt_ctr(&aes, (size_t)size, &offset, block, stream, data, out);
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(stream, sizeof stream);
}

// ---- RSA with PKCS#1 v1.5: the numbers are big-endian, of any length ----

static int digestOf(int32_t hash, const uint8_t* data, int64_t size, uint8_t* digest, mbedtls_md_type_t* type,
                    size_t* digestSize) {
    const mbedtls_md_info_t* md = cromo_crypto_md(hash);
    if (mbedtls_md(md, data, (size_t)size, digest) != 0) return 0;
    *type = mbedtls_md_get_type(md);
    *digestSize = mbedtls_md_get_size(md);
    return 1;
}

// 1 when the signature is the one of the data, for the public key n and e.
int32_t cromo_crypto_rsa_verify(int32_t hash, const uint8_t* n, int64_t nSize, const uint8_t* e, int64_t eSize,
                                const uint8_t* data, int64_t size, const uint8_t* signature, int64_t signatureSize) {
    if (!cromo_tls_setup()) return 0;  // the context of RSA has a mutex of the runtime
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    mbedtls_md_type_t type;
    size_t digestSize;
    if (!digestOf(hash, data, size, digest, &type, &digestSize)) return 0;
    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa);
    int valid = mbedtls_rsa_import_raw(&rsa, n, (size_t)nSize, NULL, 0, NULL, 0, NULL, 0, e, (size_t)eSize) == 0 &&
                mbedtls_rsa_complete(&rsa) == 0 && (size_t)signatureSize == mbedtls_rsa_get_len(&rsa) &&
                mbedtls_rsa_pkcs1_verify(&rsa, type, (unsigned int)digestSize, digest, signature) == 0;
    mbedtls_rsa_free(&rsa);
    return valid;
}

// The signature of the data in `out`, which has room for `capacity` bytes: its
// size, that of n, or -1 when the numbers are not a private key. p and q can be
// empty: mbedTLS finds them from n, e and d.
int64_t cromo_crypto_rsa_sign(int32_t hash, const uint8_t* n, int64_t nSize, const uint8_t* e, int64_t eSize,
                              const uint8_t* d, int64_t dSize, const uint8_t* p, int64_t pSize, const uint8_t* q,
                              int64_t qSize, const uint8_t* data, int64_t size, uint8_t* out, int64_t capacity) {
    if (!cromo_tls_setup()) return -1;
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    mbedtls_md_type_t type;
    size_t digestSize;
    if (!digestOf(hash, data, size, digest, &type, &digestSize)) return -1;
    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa);
    int64_t length = -1;
    if (mbedtls_rsa_import_raw(&rsa, n, (size_t)nSize, pSize > 0 ? p : NULL, (size_t)pSize, qSize > 0 ? q : NULL,
                               (size_t)qSize, d, (size_t)dSize, e, (size_t)eSize) == 0 &&
        mbedtls_rsa_complete(&rsa) == 0 && mbedtls_rsa_check_privkey(&rsa) == 0 &&
        (int64_t)mbedtls_rsa_get_len(&rsa) <= capacity &&
        mbedtls_rsa_pkcs1_sign(&rsa, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE, type, (unsigned int)digestSize,
                               digest, out) == 0) {
        length = (int64_t)mbedtls_rsa_get_len(&rsa);
    }
    mbedtls_rsa_free(&rsa);
    return length;
}

// ---- ECDSA on the curves of NIST, in the order of the enum Curve ----
//
// A public key is the point as SEC 1 writes it, 4 and then x and y; a private
// key is its number, and a signature is r and s side by side, each as long as
// the numbers of the curve: 32, 48 and 66 bytes.

static mbedtls_ecp_group_id groupOf(int32_t curve) {
    if (curve == 0) return MBEDTLS_ECP_DP_SECP256R1;
    if (curve == 1) return MBEDTLS_ECP_DP_SECP384R1;
    return MBEDTLS_ECP_DP_SECP521R1;
}

int32_t cromo_crypto_ecdsa_verify(int32_t curve, int32_t hash, const uint8_t* publicKey, int64_t keySize,
                                  const uint8_t* data, int64_t size, const uint8_t* signature, int64_t signatureSize) {
    if (!cromo_tls_setup()) return 0;
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    mbedtls_md_type_t type;
    size_t digestSize;
    if (!digestOf(hash, data, size, digest, &type, &digestSize)) return 0;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    size_t half = (size_t)signatureSize / 2;
    int valid = mbedtls_ecp_group_load(&group, groupOf(curve)) == 0 &&
                mbedtls_ecp_point_read_binary(&group, &point, publicKey, (size_t)keySize) == 0 &&
                mbedtls_ecp_check_pubkey(&group, &point) == 0 && mbedtls_mpi_read_binary(&r, signature, half) == 0 &&
                mbedtls_mpi_read_binary(&s, signature + half, half) == 0 &&
                mbedtls_ecdsa_verify(&group, digest, digestSize, &point, &r, &s) == 0;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return valid;
}

// The signature in `out`, r and s of `half` bytes each: 1, or 0 when the number is
// not a private key of the curve.
int32_t cromo_crypto_ecdsa_sign(int32_t curve, int32_t hash, const uint8_t* privateKey, int64_t keySize,
                                const uint8_t* data, int64_t size, uint8_t* out, int64_t half) {
    if (!cromo_tls_setup()) return 0;
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    mbedtls_md_type_t type;
    size_t digestSize;
    if (!digestOf(hash, data, size, digest, &type, &digestSize)) return 0;
    mbedtls_ecp_group group;
    mbedtls_mpi d;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int done = mbedtls_ecp_group_load(&group, groupOf(curve)) == 0 &&
               mbedtls_mpi_read_binary(&d, privateKey, (size_t)keySize) == 0 &&
               mbedtls_ecp_check_privkey(&group, &d) == 0 &&
               mbedtls_ecdsa_sign(&group, &r, &s, &d, digest, digestSize, mbedtls_psa_get_random,
                                  MBEDTLS_PSA_RANDOM_STATE) == 0 &&
               mbedtls_mpi_write_binary(&r, out, (size_t)half) == 0 &&
               mbedtls_mpi_write_binary(&s, out + half, (size_t)half) == 0;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&group);
    return done;
}

// The public key of a private one, in `out`, of 1 + 2 * half bytes: 1, or 0.
int32_t cromo_crypto_ecdsa_public(int32_t curve, const uint8_t* privateKey, int64_t keySize, uint8_t* out,
                                  int64_t capacity) {
    if (!cromo_tls_setup()) return 0;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi d;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&d);
    size_t written = 0;
    int done = mbedtls_ecp_group_load(&group, groupOf(curve)) == 0 &&
               mbedtls_mpi_read_binary(&d, privateKey, (size_t)keySize) == 0 &&
               mbedtls_ecp_check_privkey(&group, &d) == 0 &&
               mbedtls_ecp_mul(&group, &point, &d, &group.G, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE) == 0 &&
               mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, out,
                                              (size_t)capacity) == 0 &&
               (int64_t)written == capacity;
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return done;
}
