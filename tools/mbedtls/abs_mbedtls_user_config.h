/* abs_mbedtls_user_config.h -- mbedTLS for the video streams (abs_net.c):
 * a TLS 1.2 client over the port's own sockets, nothing else. Applied on top
 * of mbedTLS's default configuration (MBEDTLS_USER_CONFIG_FILE). */
#undef MBEDTLS_NET_C                 /* abs_net.c sends and receives itself */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SSL_PROTO_TLS1_3      /* TLS 1.2 is enough for YouTube */
#undef MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#undef MBEDTLS_SSL_SRV_C             /* a client only */
#undef MBEDTLS_SSL_CACHE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_HAVE_TIME_DATE        /* certificates are not checked (as switch-newpipe) */
#define MBEDTLS_NO_PLATFORM_ENTROPY   /* no /dev/urandom: ... */
#define MBEDTLS_ENTROPY_HARDWARE_ALT  /* ... the console's randomGet (abs_net.c) */
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_AESCE_C               /* software AES: fast enough for a video */
#undef MBEDTLS_HAVE_TIME             /* no clock needed (no tickets, no cert dates) */
