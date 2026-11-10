// AES-128 block cipher, both directions (runtime/aes128.c). CTR only needs the encrypt direction
// (runtime/ncz.c); the package driver's NCA header decrypt (runtime/nsp_extract.c) is XTS and needs
// the decrypt direction too. The S box is derived from the GF(2^8) inverse and the affine map rather
// than transcribed; the FIPS-197 and NIST SP 800-38A known-answer vectors in tools/test_ncz.c and
// tools/test_nsp_extract.c pin both directions.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif
