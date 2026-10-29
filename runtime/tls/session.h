// The sessions and the servers of TLS (D233-D235), shared by calls.c, which the
// Connection of std.net uses also without TLS, and cromo_tls.c, which has the
// code of mbedTLS. A Connection reaches mbedTLS only through the calls of its
// session: a program that never calls connectTls or listenTls links no part of it
// (D241).
#pragma once
#include <stdint.h>

#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

typedef struct Calls Calls;

// At the start of a session and of a server.
typedef struct {
    const Calls* calls;
    int error;   // positive for an error of Cromo, negative for one of mbedTLS
    int detail;  // the code of mbedTLS behind an error of Cromo
    int isSession;
} Status;

// The certificate and the key of a Listener, kept alive by its sessions.
typedef struct {
    Status status;
    mbedtls_ssl_config config;
    mbedtls_x509_crt chain;
    mbedtls_pk_context key;
    int64_t references;  // the listener and its sessions
} Server;

// The TLS of a Connection. mbedTLS reads and writes its records in memory, and
// std.net moves them to and from the socket, with its timeouts.
typedef struct {
    Status status;
    mbedtls_ssl_context ssl;
    Server* server;  // null for a client
    char* host;
    uint8_t* input;  // received from the socket, not yet read by mbedTLS
    int64_t inputStart;
    int64_t inputEnd;
    int64_t inputCapacity;
    int ended;        // the socket has nothing more
    uint8_t* output;  // written by mbedTLS, not yet sent
    int64_t outputStart;
    int64_t outputEnd;
    int64_t outputCapacity;
} Session;

struct Calls {
    int64_t (*handshake)(Session* session);
    int64_t (*read)(Session* session, uint8_t* out, int64_t most);
    int64_t (*write)(Session* session, const uint8_t* data, int64_t size);
    void (*close)(Session* session);
    void (*end)(Session* session);
    Session* (*accept)(Server* server);
    void (*serverEnd)(Server* server);
    int64_t (*error)(Status* status, uint8_t* out, int64_t most);
};
