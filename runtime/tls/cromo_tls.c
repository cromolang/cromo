// TLS for std.net (D233-D235), with mbedTLS and without sockets: mbedTLS reads
// and writes its records in buffers of memory, and the library of Cromo moves
// them to and from the socket, with its own timeouts (library/std/net.cro). Only
// connectTls and listenTls call this file; the Connection reaches it through the
// calls of its session (session.h, calls.c).

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "glue.h"
#include "session.h"

// The errors of Cromo are positive, those of mbedTLS negative.
enum {
    NO_ROOTS = 1,     // the system has no root certificates (D234)
    NO_RANDOM,        // mbedTLS could not start without random numbers
    BAD_CERTIFICATE,  // of a server (D235)
    BAD_KEY,
    KEY_MISMATCH,
};

// ---- The root certificates of the system (D234), loaded once ----
//
// SSL_CERT_FILE, as for OpenSSL and Go, names a file in PEM that replaces them (D247).

static uint64_t rootsLock;
static int rootsState;  // 0 not loaded, -1 loaded, NO_ROOTS
static char rootsProblem[512];
static mbedtls_x509_crt roots;
static mbedtls_ssl_config client;

static int countRoots(void) {
    int count = 0;
    for (const mbedtls_x509_crt* root = &roots; root && root->raw.len; root = root->next) count++;
    return count;
}

#ifdef _WIN32

#define WINAPI __stdcall
__declspec(dllimport) uint32_t WINAPI GetEnvironmentVariableW(const uint16_t* name, uint16_t* value, uint32_t size);
__declspec(dllimport) void* WINAPI CreateFileW(const uint16_t* name, uint32_t access, uint32_t share, void* security,
                                               uint32_t creation, uint32_t flags, void* model);
__declspec(dllimport) int WINAPI GetFileSizeEx(void* file, int64_t* size);
__declspec(dllimport) int WINAPI ReadFile(void* file, void* data, uint32_t size, uint32_t* done, void* overlapped);
__declspec(dllimport) int WINAPI CloseHandle(void* handle);
__declspec(dllimport) int WINAPI WideCharToMultiByte(uint32_t codePage, uint32_t flags, const uint16_t* text, int size,
                                                     char* out, int outSize, const char* fallback, int* used);
__declspec(dllimport) int WINAPI MultiByteToWideChar(uint32_t codePage, uint32_t flags, const char* text, int size,
                                                     uint16_t* out, int outSize);

// A file read whole, with a zero after it for mbedTLS; null if it cannot be read.
static uint8_t* readFile(const uint16_t* path, int64_t* size) {
    void* file = CreateFileW(path, 0x80000000 /* GENERIC_READ */, 1 /* FILE_SHARE_READ */, NULL, 3 /* OPEN_EXISTING */,
                             0x80 /* FILE_ATTRIBUTE_NORMAL */, NULL);
    if (file == (void*)(intptr_t)-1) return NULL;
    int64_t total = 0;
    if (!GetFileSizeEx(file, &total) || total > (1 << 28)) {
        CloseHandle(file);
        return NULL;
    }
    uint8_t* data = cromo_alloc(total + 1);
    int64_t done = 0;
    while (done < total) {
        uint32_t read = 0;
        if (!ReadFile(file, data + done, (uint32_t)(total - done), &read, NULL) || read == 0) break;
        done += read;
    }
    CloseHandle(file);
    data[done] = 0;
    *size = done;
    return data;
}

typedef struct {
    uint32_t encoding;
    const uint8_t* data;
    uint32_t size;
} CertContext;  // the start of CERT_CONTEXT

typedef void*(WINAPI* OpenSystemStore)(uintptr_t provider, const uint16_t* name);
typedef const CertContext*(WINAPI* EnumCertificates)(void* store, const CertContext* previous);
typedef int(WINAPI* CloseStore)(void* store, uint32_t flags);

static void loadSystemRoots(void) {
    const uint16_t* crypt32 = (const uint16_t*)L"crypt32.dll";
    OpenSystemStore open = (OpenSystemStore)cromo_system_function(crypt32, "CertOpenSystemStoreW");
    EnumCertificates enumerate = (EnumCertificates)cromo_system_function(crypt32, "CertEnumCertificatesInStore");
    CloseStore close = (CloseStore)cromo_system_function(crypt32, "CertCloseStore");
    void* store = open && enumerate && close ? open(0, (const uint16_t*)L"ROOT") : NULL;
    if (!store) {
        cromo_snprintf(rootsProblem, sizeof rootsProblem, "cannot open the store ROOT of Windows");
        return;
    }
    for (const CertContext* found = enumerate(store, NULL); found; found = enumerate(store, found)) {
        if (found->encoding & 1) mbedtls_x509_crt_parse_der(&roots, found->data, found->size);  // X509_ASN_ENCODING
    }
    close(store, 0);
    if (!countRoots()) cromo_snprintf(rootsProblem, sizeof rootsProblem, "the store ROOT of Windows has no certificates");
}

static void loadRoots(void) {
    uint16_t path[1024];
    uint32_t length = GetEnvironmentVariableW((const uint16_t*)L"SSL_CERT_FILE", path, 1024);
    if (length == 0 || length >= 1024) {
        loadSystemRoots();
        return;
    }
    int64_t size = 0;
    uint8_t* data = readFile(path, &size);
    if (data) {
        mbedtls_x509_crt_parse(&roots, data, (size_t)size + 1);
        cromo_free(data);
    }
    if (!countRoots()) {
        char name[700];
        if (!WideCharToMultiByte(65001, 0, path, -1, name, sizeof name, NULL, NULL)) name[0] = 0;
        cromo_snprintf(rootsProblem, sizeof rootsProblem, "no certificates in %s, the file of SSL_CERT_FILE", name);
    }
}

// Windows keeps few roots on the disk and downloads the others when a chain needs
// them, so the store ROOT is not all that it trusts. A chain that mbedTLS does not
// accept is checked again by Windows, with the rules of HTTPS for the host, as Go
// does: CertGetCertificateChain and CertVerifyCertificateChainPolicy of crypt32
// (D246). Revocation is not checked (D248).

typedef struct {
    uint32_t count;
    const char* const* identifiers;
} Usage;  // CERT_ENHKEY_USAGE

typedef struct {
    uint32_t size;
    struct {
        uint32_t type;  // USAGE_MATCH_TYPE_AND
        Usage usage;
    } requested;
} ChainPara;  // CERT_CHAIN_PARA, with its CERT_USAGE_MATCH

typedef struct {
    uint32_t size;
    uint32_t authType;  // AUTHTYPE_SERVER
    uint32_t checks;
    const uint16_t* serverName;
} SslPolicyPara;  // SSL_EXTRA_CERT_CHAIN_POLICY_PARA

typedef struct {
    uint32_t size;
    uint32_t flags;
    void* extra;
} PolicyPara;  // CERT_CHAIN_POLICY_PARA

typedef struct {
    uint32_t size;
    uint32_t error;
    int32_t chainIndex;
    int32_t elementIndex;
    void* extra;
} PolicyStatus;  // CERT_CHAIN_POLICY_STATUS

typedef void*(WINAPI* OpenStore)(const char* provider, uint32_t encoding, uintptr_t cryptProvider, uint32_t flags,
                                 const void* para);
typedef int(WINAPI* AddEncoded)(void* store, uint32_t encoding, const uint8_t* data, uint32_t size, uint32_t disposition,
                                const void** added);
typedef int(WINAPI* GetChain)(void* engine, const void* certificate, void* time, void* store, ChainPara* para,
                              uint32_t flags, void* reserved, const void** chain);
typedef int(WINAPI* VerifyPolicy)(const char* policy, const void* chain, PolicyPara* para, PolicyStatus* status);
typedef void(WINAPI* FreeChain)(const void* chain);
typedef int(WINAPI* FreeCertificate)(const void* certificate);

#define CERT_STORE_PROV_MEMORY ((const char*)2)
#define CERT_CHAIN_POLICY_SSL ((const char*)4)
#define ENCODINGS 0x10001  // X509_ASN_ENCODING | PKCS_7_ASN_ENCODING
#define CERT_STORE_ADD_ALWAYS 4
#define AUTHTYPE_SERVER 2

static int windowsTrusts(const Session* session) {
    const uint16_t* crypt32 = (const uint16_t*)L"crypt32.dll";
    OpenStore open = (OpenStore)cromo_system_function(crypt32, "CertOpenStore");
    AddEncoded add = (AddEncoded)cromo_system_function(crypt32, "CertAddEncodedCertificateToStore");
    GetChain getChain = (GetChain)cromo_system_function(crypt32, "CertGetCertificateChain");
    VerifyPolicy verify = (VerifyPolicy)cromo_system_function(crypt32, "CertVerifyCertificateChainPolicy");
    FreeChain freeChain = (FreeChain)cromo_system_function(crypt32, "CertFreeCertificateChain");
    FreeCertificate freeCertificate = (FreeCertificate)cromo_system_function(crypt32, "CertFreeCertificateContext");
    CloseStore close = (CloseStore)cromo_system_function(crypt32, "CertCloseStore");
    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(&session->ssl);
    uint16_t name[512];
    if (!open || !add || !getChain || !verify || !freeChain || !freeCertificate || !close || !peer ||
        MultiByteToWideChar(65001, 0, session->host, -1, name, 512) <= 0) {
        return 0;
    }
    void* store = open(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!store) return 0;
    const void* leaf = NULL;
    for (const mbedtls_x509_crt* certificate = peer; certificate && certificate->raw.len; certificate = certificate->next) {
        add(store, ENCODINGS, certificate->raw.p, (uint32_t)certificate->raw.len, CERT_STORE_ADD_ALWAYS,
            certificate == peer ? &leaf : NULL);
    }
    int trusted = 0;
    static const char* const serverAuthentication[] = {"1.3.6.1.5.5.7.3.1"};
    ChainPara chainPara = {sizeof(ChainPara), {0, {1, serverAuthentication}}};
    const void* chain = NULL;
    if (leaf && getChain(NULL, leaf, NULL, store, &chainPara, 0, NULL, &chain)) {
        SslPolicyPara ssl = {sizeof(SslPolicyPara), AUTHTYPE_SERVER, 0, name};
        PolicyPara policy = {sizeof(PolicyPara), 0, &ssl};
        PolicyStatus status = {sizeof(PolicyStatus), 0, 0, 0, NULL};
        trusted = verify(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) && status.error == 0;
        freeChain(chain);
    }
    if (leaf) freeCertificate(leaf);
    close(store, 0);
    return trusted;
}

#else

#define SYS_read 0
#define SYS_open 2
#define SYS_close 3
#define O_CLOEXEC 0x80000
#define EINTR 4

static uint8_t* readFile(const char* path, int64_t* size) {
    int64_t file = cromo_system_call(SYS_open, (int64_t)path, O_CLOEXEC, 0, 0, 0, 0);
    if (file < 0) return NULL;
    int64_t capacity = 1 << 16;
    int64_t done = 0;
    uint8_t* data = cromo_alloc(capacity);
    for (;;) {
        if (done + 1 == capacity) {
            capacity *= 2;
            data = cromo_realloc(data, capacity);
        }
        int64_t read = cromo_system_call(SYS_read, file, (int64_t)(data + done), capacity - done - 1, 0, 0, 0);
        if (read == -EINTR) continue;
        if (read <= 0) break;
        done += read;
    }
    cromo_system_call(SYS_close, file, 0, 0, 0, 0, 0);
    data[done] = 0;
    *size = done;
    return data;
}

static int loadFile(const char* path) {
    int64_t size = 0;
    uint8_t* data = readFile(path, &size);
    if (!data) return 0;
    mbedtls_x509_crt_parse(&roots, data, (size_t)size + 1);
    cromo_free(data);
    return countRoots();
}

// The files of the distributions, as Go looks for them.
static const char* const bundles[] = {
    "/etc/ssl/certs/ca-certificates.crt",                 // Debian, Ubuntu, Arch
    "/etc/pki/tls/certs/ca-bundle.crt",                   // Fedora, RHEL 6
    "/etc/ssl/ca-bundle.pem",                             // openSUSE
    "/etc/pki/tls/cacert.pem",                            // OpenELEC
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // CentOS, RHEL 7
    "/etc/ssl/cert.pem",                                  // Alpine
};

static void loadRoots(void) {
    static const char variable[] = "SSL_CERT_FILE=";
    for (int64_t i = 0;; ++i) {
        const char* entry = (const char*)cromo_environment(i);
        if (!entry) break;
        if (strncmp(entry, variable, sizeof variable - 1) == 0) {
            const char* path = entry + sizeof variable - 1;
            if (!loadFile(path)) {
                cromo_snprintf(rootsProblem, sizeof rootsProblem, "no certificates in %s, the file of SSL_CERT_FILE", path);
            }
            return;
        }
    }
    for (size_t i = 0; i < sizeof bundles / sizeof bundles[0]; ++i) {
        if (loadFile(bundles[i])) return;
    }
    cromo_snprintf(rootsProblem, sizeof rootsProblem, "the system has no file of root certificates, as %s", bundles[0]);
}

#endif

// The roots and the configuration of every client: 0, or NO_ROOTS.
static int clientReady(void) {
    int state = __atomic_load_n(&rootsState, __ATOMIC_ACQUIRE);
    if (state) return state > 0 ? state : 0;
    cromo_mutex_lock(&rootsLock);
    if (!rootsState) {
        mbedtls_x509_crt_init(&roots);
        mbedtls_ssl_config_init(&client);
        loadRoots();
        state = countRoots() ? -1 : NO_ROOTS;
        if (state < 0 && mbedtls_ssl_config_defaults(&client, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                     MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            cromo_snprintf(rootsProblem, sizeof rootsProblem, "cannot configure the clients of TLS");
            state = NO_ROOTS;
        }
        if (state < 0) {
#ifdef _WIN32
            // Verified always (D235), but after the handshake, by Windows too.
            mbedtls_ssl_conf_authmode(&client, MBEDTLS_SSL_VERIFY_OPTIONAL);
#else
            mbedtls_ssl_conf_authmode(&client, MBEDTLS_SSL_VERIFY_REQUIRED);  // always (D235)
#endif
            mbedtls_ssl_conf_ca_chain(&client, &roots, NULL);
            mbedtls_ssl_conf_rng(&client, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
        }
        __atomic_store_n(&rootsState, state, __ATOMIC_RELEASE);
    }
    cromo_mutex_unlock(&rootsLock);
    state = __atomic_load_n(&rootsState, __ATOMIC_ACQUIRE);
    return state > 0 ? state : 0;
}

// ---- Sessions ----

static const Calls calls;

// A copy that ends with a zero, as mbedTLS wants the texts in PEM and the names.
static uint8_t* zeroEnded(const uint8_t* text, int64_t length) {
    uint8_t* copy = cromo_alloc(length + 1);
    memcpy(copy, text, (size_t)length);
    copy[length] = 0;
    return copy;
}

static int sendRecords(void* context, const unsigned char* data, size_t size) {
    Session* session = context;
    if (session->outputEnd + (int64_t)size > session->outputCapacity) {
        int64_t waiting = session->outputEnd - session->outputStart;
        memmove(session->output, session->output + session->outputStart, (size_t)waiting);
        session->outputStart = 0;
        session->outputEnd = waiting;
        if (waiting + (int64_t)size > session->outputCapacity) {
            int64_t grown = session->outputCapacity * 2;
            if (grown < waiting + (int64_t)size) grown = waiting + (int64_t)size;
            if (grown < 4096) grown = 4096;
            session->output = cromo_realloc(session->output, grown);
            session->outputCapacity = grown;
        }
    }
    memcpy(session->output + session->outputEnd, data, size);
    session->outputEnd += (int64_t)size;
    return (int)size;
}

static int receiveRecords(void* context, unsigned char* out, size_t most) {
    Session* session = context;
    int64_t waiting = session->inputEnd - session->inputStart;
    if (waiting == 0) return session->ended ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    size_t count = (size_t)waiting < most ? (size_t)waiting : most;
    memcpy(out, session->input + session->inputStart, count);
    session->inputStart += (int64_t)count;
    if (session->inputStart == session->inputEnd) session->inputStart = session->inputEnd = 0;
    return (int)count;
}

static Session* newSession(void) {
    Session* session = cromo_alloc(sizeof(Session));
    memset(session, 0, sizeof(Session));
    session->status.calls = &calls;
    session->status.isSession = 1;
    mbedtls_ssl_init(&session->ssl);
    return session;
}

// A client for the host, which the certificate must name. It always gives a
// session: an error stops its first handshake.
void* cromo_tls_connect(const uint8_t* host, int64_t length) {
    Session* session = newSession();
    session->host = (char*)zeroEnded(host, length);
    if (!cromo_tls_setup()) {
        session->status.error = NO_RANDOM;
        return session;
    }
    int problem = clientReady();
    if (problem) {
        session->status.error = problem;
        return session;
    }
    int code = mbedtls_ssl_setup(&session->ssl, &client);
    if (code == 0) code = mbedtls_ssl_set_hostname(&session->ssl, session->host);
    if (code == 0) mbedtls_ssl_set_bio(&session->ssl, session, sendRecords, receiveRecords, NULL);
    session->status.error = code;
    return session;
}

static Session* accept(Server* server) {
    Session* session = newSession();
    __atomic_add_fetch(&server->references, 1, __ATOMIC_RELAXED);
    session->server = server;
    int code = server->status.error ? server->status.error : mbedtls_ssl_setup(&session->ssl, &server->config);
    if (code == 0) mbedtls_ssl_set_bio(&session->ssl, session, sendRecords, receiveRecords, NULL);
    session->status.error = code;
    return session;
}

static void serverEnd(Server* server) {
    if (__atomic_sub_fetch(&server->references, 1, __ATOMIC_ACQ_REL) != 0) return;
    mbedtls_ssl_config_free(&server->config);
    mbedtls_x509_crt_free(&server->chain);
    mbedtls_pk_free(&server->key);
    cromo_free(server);
}

static void end(Session* session) {
    mbedtls_ssl_free(&session->ssl);
    if (session->server) serverEnd(session->server);
    cromo_free(session->host);
    cromo_free(session->input);
    cromo_free(session->output);
    cromo_free(session);
}

static int64_t outcome(Session* session, int code) {
    if (code >= 0) return code;
    if (code == MBEDTLS_ERR_SSL_WANT_READ) return -1;
    if (!session->status.error) session->status.error = code;
    return -2;
}

static int64_t handshake(Session* session) {
    if (session->status.error) return -2;
    int code = mbedtls_ssl_handshake(&session->ssl);
#ifdef _WIN32
    if (code == 0 && !session->server && mbedtls_ssl_get_verify_result(&session->ssl) != 0 && !windowsTrusts(session)) {
        code = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }
#endif
    return outcome(session, code);
}

// The end of the connection, also without the alert close_notify, as Go does (D245).
static int64_t readData(Session* session, uint8_t* out, int64_t most) {
    if (session->status.error) return -2;
    if (most > (1 << 30)) most = 1 << 30;
    for (;;) {
        int code = mbedtls_ssl_read(&session->ssl, out, (size_t)most);
        if (code == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
        if (code == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || code == MBEDTLS_ERR_SSL_CONN_EOF) return 0;
        return outcome(session, code);
    }
}

static int64_t writeData(Session* session, const uint8_t* data, int64_t size) {
    if (session->status.error) return -2;
    if (size > (1 << 30)) size = 1 << 30;
    return outcome(session, mbedtls_ssl_write(&session->ssl, data, (size_t)size));
}

static void closeNotify(Session* session) {
    if (!session->status.error && mbedtls_ssl_is_handshake_over(&session->ssl)) mbedtls_ssl_close_notify(&session->ssl);
}

// ---- Servers ----

// The certificate, with its chain, and the private key of a server, in PEM. It
// always gives a server: cromo_tls_error says what is wrong with it.
void* cromo_tls_server(const uint8_t* certificate, int64_t certificateLength, const uint8_t* key, int64_t keyLength) {
    Server* server = cromo_alloc(sizeof(Server));
    memset(server, 0, sizeof(Server));
    server->status.calls = &calls;
    server->references = 1;
    mbedtls_ssl_config_init(&server->config);
    mbedtls_x509_crt_init(&server->chain);
    mbedtls_pk_init(&server->key);
    if (!cromo_tls_setup()) {
        server->status.error = NO_RANDOM;
        return server;
    }
    uint8_t* text = zeroEnded(certificate, certificateLength);
    int code = mbedtls_x509_crt_parse(&server->chain, text, (size_t)certificateLength + 1);
    cromo_free(text);
    if (code != 0) {  // a positive code counts the certificates that are not valid
        server->status.error = BAD_CERTIFICATE;
        server->status.detail = code;
        return server;
    }
    text = zeroEnded(key, keyLength);
    code = mbedtls_pk_parse_key(&server->key, text, (size_t)keyLength + 1, NULL, 0, mbedtls_psa_get_random,
                                MBEDTLS_PSA_RANDOM_STATE);
    mbedtls_platform_zeroize(text, (size_t)keyLength);
    cromo_free(text);
    if (code != 0) {
        server->status.error = BAD_KEY;
        server->status.detail = code;
        return server;
    }
    if (mbedtls_pk_check_pair(&server->chain.pk, &server->key, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE) != 0) {
        server->status.error = KEY_MISMATCH;
        return server;
    }
    code = mbedtls_ssl_config_defaults(&server->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (code == 0) {
        mbedtls_ssl_conf_rng(&server->config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
        code = mbedtls_ssl_conf_own_cert(&server->config, &server->chain, &server->key);
    }
    server->status.error = code;
    return server;
}

// ---- Errors (D250) ----

static int verificationText(Session* session, char* text, size_t size) {
    uint32_t flags = mbedtls_ssl_get_verify_result(&session->ssl);
    if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
        return cromo_snprintf(text, size, "the certificate is not signed by an authority that the system trusts");
    }
    if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) return cromo_snprintf(text, size, "the certificate is not for %s", session->host);
    if (flags & MBEDTLS_X509_BADCERT_EXPIRED) return cromo_snprintf(text, size, "the certificate has expired");
    if (flags & MBEDTLS_X509_BADCERT_FUTURE) return cromo_snprintf(text, size, "the certificate is not valid yet");
    int length = mbedtls_x509_crt_verify_info(text, size, "", flags);
    while (length > 0 && text[length - 1] == '\n') text[--length] = 0;
    return length;
}

static int libraryText(int code, char* text, size_t size) {
    switch (code) {
    case MBEDTLS_ERR_SSL_CONN_EOF:
        return cromo_snprintf(text, size, "the other side closed the connection");
    case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
        return cromo_snprintf(text, size, "the other side refused the connection with an alert of TLS");
    case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
        return cromo_snprintf(text, size, "no version of TLS in common");
    case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
        return cromo_snprintf(text, size, "the handshake of TLS failed: no algorithms in common");
    case MBEDTLS_ERR_SSL_INVALID_RECORD:
        return cromo_snprintf(text, size, "what arrives is not TLS");
    default:
        mbedtls_strerror(code, text, size);
        return (int)strlen(text);
    }
}

static int64_t errorText(Status* status, uint8_t* out, int64_t most) {
    char* text = (char*)out;
    size_t size = (size_t)most;
    int length = 0;
    char detail[200];
    switch (status->error) {
    case 0:  // empty
        break;
    case NO_ROOTS:
        length = cromo_snprintf(text, size, "%s", rootsProblem);
        break;
    case NO_RANDOM:
        length = cromo_snprintf(text, size, "the system gives no random numbers");
        break;
    case BAD_CERTIFICATE:
        if (status->detail > 0) {
            length = cromo_snprintf(text, size, "the certificate is not valid: %d of its parts cannot be read", status->detail);
        } else {
            libraryText(status->detail, detail, sizeof detail);
            length = cromo_snprintf(text, size, "the certificate is not valid PEM: %s", detail);
        }
        break;
    case BAD_KEY:
        libraryText(status->detail, detail, sizeof detail);
        length = cromo_snprintf(text, size, "the key is not a valid private key in PEM: %s", detail);
        break;
    case KEY_MISMATCH:
        length = cromo_snprintf(text, size, "the key is not the one of the certificate");
        break;
    case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
        if (status->isSession) {
            length = verificationText((Session*)status, text, size);
            break;
        }
        // fall through
    default:
        length = libraryText(status->error, text, size);
        break;
    }
    if (length < 0) length = 0;
    if (length >= most) length = (int)most - 1;
    return length;
}

static const Calls calls = {handshake, readData, writeData, closeNotify, end, accept, serverEnd, errorText};
