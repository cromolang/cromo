// The signatures and the exchange of keys of std.crypto (D236): Ed25519 and
// X25519 of Monocypher. The library of Cromo checks the sizes of the keys before
// calling them (library/std/crypto.cro).

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "monocypher-ed25519.h"
#include "monocypher.h"

// ---- Ed25519 (RFC 8032): the private key is the seed of 32 bytes ----

void cromo_crypto_ed25519_public(const uint8_t* privateKey, uint8_t* publicKey) {
    uint8_t seed[32];
    uint8_t secret[64];
    memcpy(seed, privateKey, 32);
    crypto_ed25519_key_pair(secret, publicKey, seed);  // it wipes the seed
    crypto_wipe(secret, sizeof secret);
}

void cromo_crypto_ed25519_sign(const uint8_t* privateKey, const uint8_t* message, int64_t size, uint8_t* signature) {
    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t publicKey[32];
    memcpy(seed, privateKey, 32);
    crypto_ed25519_key_pair(secret, publicKey, seed);
    crypto_ed25519_sign(signature, secret, message, (size_t)size);
    crypto_wipe(secret, sizeof secret);
}

int32_t cromo_crypto_ed25519_verify(const uint8_t* publicKey, const uint8_t* message, int64_t size,
                                    const uint8_t* signature) {
    return crypto_ed25519_check(signature, publicKey, message, (size_t)size) == 0;
}

// ---- X25519 (RFC 7748) ----

void cromo_crypto_x25519_public(const uint8_t* privateKey, uint8_t* publicKey) {
    crypto_x25519_public_key(publicKey, privateKey);
}

// 1, or 0 if the public key of the other side gives a secret of zeros.
int32_t cromo_crypto_x25519(const uint8_t* privateKey, const uint8_t* peerKey, uint8_t* shared) {
    crypto_x25519(shared, privateKey, peerKey);
    uint8_t any = 0;
    for (int i = 0; i < 32; ++i) any |= shared[i];
    return any != 0;
}
