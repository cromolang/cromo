// Empty for mbedTLS (D233): without AF_INET6, x509_crt.c reads the addresses of
// the certificates with its own code instead of inet_pton of ws2_32.dll.
#pragma once
