// The mutexes of mbedTLS are the locks of the runtime (D214, D233).
typedef struct {
    unsigned long long state;
} mbedtls_threading_mutex_t;
