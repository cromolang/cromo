// The ciphers with authentication of std.crypto (D236), with mbedTLS. The library
// of Cromo checks the sizes of the keys and of the data before calling them
// (library/std/crypto.cro).

#include <stddef.h>
#include <stdint.h>

#include "mbedtls/chachapoly.h"
#include "mbedtls/gcm.h"

// ---- Ciphers with authentication (AEAD), in the order of the enum Cipher ----
//
// The key is of 16, 24 or 32 bytes for AES-GCM and of 32 for ChaCha20-Poly1305,
// the nonce of 12. The encrypted data are followed by the tag, of 16 bytes.

enum { AES_GCM, CHACHA20_POLY1305 };

void cromo_crypto_seal(int32_t cipher, const uint8_t* key, int64_t keySize, const uint8_t* nonce, const uint8_t* extra,
                       int64_t extraSize, const uint8_t* data, int64_t size, uint8_t* out) {
    if (cipher == AES_GCM) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, (unsigned)keySize * 8);
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, (size_t)size, nonce, 12, extra, (size_t)extraSize, data,
                                  out, 16, out + size);
        mbedtls_gcm_free(&gcm);
    } else {
        mbedtls_chachapoly_context chacha;
        mbedtls_chachapoly_init(&chacha);
        mbedtls_chachapoly_setkey(&chacha, key);
        mbedtls_chachapoly_encrypt_and_tag(&chacha, (size_t)size, nonce, extra, (size_t)extraSize, data, out, out + size);
        mbedtls_chachapoly_free(&chacha);
    }
}

// 1 if the data, with their tag at the end, are authentic: then `out` has them
// decrypted.
int32_t cromo_crypto_open(int32_t cipher, const uint8_t* key, int64_t keySize, const uint8_t* nonce,
                          const uint8_t* extra, int64_t extraSize, const uint8_t* data, int64_t size, uint8_t* out) {
    size_t length = (size_t)size - 16;
    int code;
    if (cipher == AES_GCM) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, (unsigned)keySize * 8);
        code = mbedtls_gcm_auth_decrypt(&gcm, length, nonce, 12, extra, (size_t)extraSize, data + length, 16, data, out);
        mbedtls_gcm_free(&gcm);
    } else {
        mbedtls_chachapoly_context chacha;
        mbedtls_chachapoly_init(&chacha);
        mbedtls_chachapoly_setkey(&chacha, key);
        code = mbedtls_chachapoly_auth_decrypt(&chacha, length, nonce, extra, (size_t)extraSize, data + length, data, out);
        mbedtls_chachapoly_free(&chacha);
    }
    return code == 0;
}
