#!/bin/sh
# Build mbedTLS 3.6 (TLS 1.2 client, abs_mbedtls_user_config.h) for the
# AArch32 Switch wrapper into portlibs32/ (lib/libmbedtls.a, libmbedx509.a,
# libmbedcrypto.a, include/mbedtls, include/psa). Run from anywhere; uses the
# toolchain container. Apache-2.0 (mbedTLS).
set -e
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
exec docker run --rm --platform linux/amd64 -v "$HERE:/work" -w /work "$IMAGE" bash -lc '
set -e
B=/work/build-mbedtls
rm -rf $B && mkdir -p $B && cd $B
tar xjf /work/tools/mbedtls/mbedtls-3.6.2.tar.bz2
cd mbedtls-3.6.2
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec"
CFLAGS="$ARCH -O2 -D__SWITCH__ -ffunction-sections -fdata-sections -I/work/tools/mbedtls -DMBEDTLS_USER_CONFIG_FILE=<abs_mbedtls_user_config.h> -isystem $DEVKITPRO/libnx32/include"
CC=$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc
AR=$DEVKITPRO/devkitARM/bin/arm-none-eabi-ar
cd library
for f in *.c; do $CC $CFLAGS -I../include -I. -c $f -o ${f%.c}.o || exit 1; done
X509="x509.o x509_create.o x509_crl.o x509_crt.o x509_csr.o x509write.o x509write_crt.o x509write_csr.o pkcs7.o"
TLS="debug.o mps_reader.o mps_trace.o net_sockets.o ssl_cache.o ssl_ciphersuites.o ssl_client.o ssl_cookie.o ssl_debug_helpers_generated.o ssl_msg.o ssl_ticket.o ssl_tls.o ssl_tls12_client.o ssl_tls12_server.o ssl_tls13_keys.o ssl_tls13_server.o ssl_tls13_client.o ssl_tls13_generic.o"
CRYPTO=""
for o in *.o; do case " $X509 $TLS " in *" $o "*) ;; *) CRYPTO="$CRYPTO $o";; esac; done
rm -f libmbed*.a
$AR rcs libmbedx509.a $X509
$AR rcs libmbedtls.a $(for o in $TLS; do [ -f $o ] && echo $o; done)
$AR rcs libmbedcrypto.a $CRYPTO
mkdir -p /work/portlibs32/lib /work/portlibs32/include
cp libmbedtls.a libmbedx509.a libmbedcrypto.a /work/portlibs32/lib/
rm -rf /work/portlibs32/include/mbedtls /work/portlibs32/include/psa
cp -r ../include/mbedtls ../include/psa /work/portlibs32/include/
cp /work/tools/mbedtls/abs_mbedtls_user_config.h /work/portlibs32/include/
ls -l /work/portlibs32/lib/libmbed*
'
