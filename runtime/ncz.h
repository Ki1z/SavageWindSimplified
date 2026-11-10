// NCZ to NCA reconstruction (runtime/ncz.c). NSZ/XCZ packages keep some .nca entries zstd-compressed
// as .ncz: a verbatim NCA header copy (0x4000 bytes), an "NCZSECTN" table of 64-byte section entries,
// each carrying its own AES-128-CTR key and counter, then one zstd frame holding the NCA body. Real
// files range from a handful of sections to a per-chunk table (a 1.9.0 update package measured 534).
// The rebuild writes the header out verbatim, streams the body through libzstd, then re-encrypts every
// CTR section in place. Keystream counter = the section counter's first 8 bytes as nonce, then a
// big-endian u64 block index of the ABSOLUTE file offset (block = offset >> 4), the seek convention of
// both NSCB's decompressor (Counter.new(64, prefix=nonce[0:8], initial_value=offset>>4)) and the nsz
// tool's IndependentNczDecompressor (crypto.seek(i)). Correctness criterion used by the nsz tool
// itself: the rebuilt NCA's sha256 must start with the .ncz file name (first 32 hex chars), because an
// NCA's content id is the sha256 of the whole file. Classic NSCB tables and chunked per-chunk tables
// (a 1.9.0 update package measured 534) both use this seek. Crypto types 1 (plain) and 3/4 (AES-CTR)
// are supported; the block-table variant ("NCZBLOCK") is rejected.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NCZ_OK = 0,
    NCZ_E_OPEN,      // input or output won't open
    NCZ_E_SHORT,     // the file ends inside the header or the section table
    NCZ_E_MAGIC,     // the section table's magic isn't "NCZSECTN"
    NCZ_E_TABLE,     // section count or ranges make no sense for this file
    NCZ_E_CRYPTO,    // a section needs a cipher other than AES-128-CTR
    NCZ_E_ZSTD,      // the zstd body stream is corrupt
    NCZ_E_IO,        // a read, write, or seek failed
    NCZ_E_CANCEL,    // the progress callback cancelled the run
} NczError;

// Body bytes written to the output so far; return false to cancel the run.
typedef bool (*NczProgress)(void *user, uint64_t body_bytes);

// Rebuild the .ncz at ncz_path into a full .nca at nca_out_path (header copy + decompressed body,
// CTR sections re-encrypted in place). A failed run leaves partial output behind; the caller removes
// it. progress may be NULL. body_bytes receives the decompressed body size when NCZ_OK.
NczError ncz_rebuild(const char *ncz_path, const char *nca_out_path, NczProgress progress, void *user, uint64_t *body_bytes);

// Log tag for an error ("ok", "open", "short", ...).
const char *ncz_error_name(NczError e);

// Test hook: one raw AES-128 block (FIPS-197 vectors checked in tools/test_ncz.c).
void ncz_aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif
