// NCZ to NCA reconstruction: header copy, zstd body streaming, in-place CTR re-encryption.
// See runtime/ncz.h for the format notes and NSC_BUILDER file this follows.
#include "ncz.h"
#include "aes128.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zstd.h"

#define NCZ_HEADER_SIZE 0x4000
#define NCZ_IOBUF (64 * 1024)

// NCA bodies run past 4 GiB, so all seeks are 64-bit (long is 32-bit on Windows).
#ifdef _WIN32
static int ncz_fseek(FILE *f, int64_t off) { return _fseeki64(f, off, SEEK_SET); }
#else
static int ncz_fseek(FILE *f, int64_t off) { return fseeko(f, (off_t)off, SEEK_SET); }
#endif

typedef struct {
    uint64_t offset;
    uint64_t size;
    uint64_t crypto;      // 1 = plain, 3/4 = AES-128-CTR
    uint8_t key[16];
    uint8_t counter[16];
} NczSectionEntry;

// --- AES-128 (encrypt only, that is all CTR needs): runtime/aes128.c ------------------------

void ncz_aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    aes128_encrypt_block(key, in, out);
}

// The keystream counter block: the section counter's first 8 bytes as prefix, then a big-endian u64
// block index. NCZ decryptors seek with the ABSOLUTE file offset, i.e. block = offset >> 4 (NSCB's
// decompressor does Counter.new(64, prefix=nonce[0:8], initial_value=(offset>>4)) and the nsz tool's
// IndependentNczDecompressor does the same via crypto.seek(i)); classic NSCB tables store their
// counter as nonce||0 and chunked tables as generation/secure-value pairs, and the absolute block
// formula reproduces both.
static void ctr_block(const uint8_t counter[16], uint64_t block_index, uint8_t out[16]) {
    memcpy(out, counter, 8);
    uint64_t v = block_index;
    for (int i = 15; i >= 8; i--) {
        out[i] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
}

static NczError ctr_region(FILE *f, uint64_t offset, uint64_t size, const uint8_t key[16], const uint8_t counter[16]) {
    uint8_t *buf = malloc(NCZ_IOBUF);
    if (!buf) return NCZ_E_IO;
    NczError err = NCZ_OK;
    uint64_t done = 0;
    while (done < size) {
        size_t want = (size - done > NCZ_IOBUF) ? NCZ_IOBUF : (size_t)(size - done);
        // reseek per chunk: the in-place rewrite reads and rewrites the same span
        if (ncz_fseek(f, (int64_t)(offset + done)) != 0) { err = NCZ_E_IO; break; }
        if (fread(buf, 1, want, f) != want) { err = NCZ_E_IO; break; }
        for (size_t p = 0; p < want; p += 16) {
            uint8_t ks[16];
            uint64_t block_index = (offset + done + p) >> 4;
            ctr_block(counter, block_index, ks);
            ncz_aes128_block(key, ks, ks);
            size_t n = (want - p < 16) ? (want - p) : 16;
            for (size_t i = 0; i < n; i++) buf[p + i] ^= ks[i];
        }
        if (ncz_fseek(f, (int64_t)(offset + done)) != 0) { err = NCZ_E_IO; break; }
        if (fwrite(buf, 1, want, f) != want) { err = NCZ_E_IO; break; }
        done += want;
    }
    free(buf);
    return err;
}

// --- section table ----------------------------------------------------------

static NczError read_table(FILE *f, NczSectionEntry **out_table, uint64_t *out_count) {
    uint8_t magic[8];
    if (fread(magic, 1, 8, f) != 8) return NCZ_E_SHORT;
    if (memcmp(magic, "NCZSECTN", 8) != 0) return NCZ_E_MAGIC;
    uint8_t cbuf[8];
    if (fread(cbuf, 1, 8, f) != 8) return NCZ_E_SHORT;
    uint64_t count = 0;
    for (int i = 0; i < 8; i++) count |= (uint64_t)cbuf[i] << (8 * i);
    if (count == 0 || count > 65536) return NCZ_E_TABLE;
    NczSectionEntry *table = calloc((size_t)count, sizeof *table);
    if (!table) return NCZ_E_IO;
    for (uint64_t i = 0; i < count; i++) {
        uint8_t e[64];
        if (fread(e, 1, 64, f) != 64) {
            free(table);
            return NCZ_E_SHORT;
        }
        NczSectionEntry *s = &table[i];
        for (int b = 0; b < 8; b++) s->offset |= (uint64_t)e[b] << (8 * b);
        for (int b = 0; b < 8; b++) s->size |= (uint64_t)e[8 + b] << (8 * b);
        for (int b = 0; b < 8; b++) s->crypto |= (uint64_t)e[16 + b] << (8 * b);
        memcpy(s->key, e + 32, 16);
        memcpy(s->counter, e + 48, 16);
    }
    *out_table = table;
    *out_count = count;
    return NCZ_OK;
}

// --- rebuild ----------------------------------------------------------------

NczError ncz_rebuild(const char *ncz_path, const char *nca_out_path, NczProgress progress, void *user, uint64_t *body_bytes) {
    FILE *in = fopen(ncz_path, "rb");
    if (!in) return NCZ_E_OPEN;
    FILE *out = fopen(nca_out_path, "wb+");   // update mode: the CTR sections are re-encrypted in place
    if (!out) {
        fclose(in);
        return NCZ_E_OPEN;
    }

    uint8_t header[NCZ_HEADER_SIZE];
    NczError err = NCZ_OK;
    uint64_t body = 0;
    NczSectionEntry *table = NULL;
    uint64_t count = 0;
    ZSTD_DCtx *dctx = NULL;
    uint8_t *ibuf = NULL, *obuf = NULL;

    do {
        if (fread(header, 1, NCZ_HEADER_SIZE, in) != NCZ_HEADER_SIZE) { err = NCZ_E_SHORT; break; }
        if (fwrite(header, 1, NCZ_HEADER_SIZE, out) != NCZ_HEADER_SIZE) { err = NCZ_E_IO; break; }
        err = read_table(in, &table, &count);
        if (err != NCZ_OK) break;
        for (uint64_t i = 0; i < count; i++) {
            if (table[i].crypto != 1 && table[i].crypto != 3 && table[i].crypto != 4) { err = NCZ_E_CRYPTO; break; }
            if (table[i].offset < NCZ_HEADER_SIZE) { err = NCZ_E_TABLE; break; }
        }
        if (err != NCZ_OK) break;

        // the block-table variant ("NCZBLOCK", one frame per block) is a different container layout;
        // reject it explicitly instead of letting the zstd stream fail
        {
            uint8_t peek[8];
            if (fread(peek, 1, 8, in) != 8) { err = NCZ_E_SHORT; break; }
            if (memcmp(peek, "NCZBLOCK", 8) == 0) { err = NCZ_E_TABLE; break; }
            if (ncz_fseek(in, (int64_t)(NCZ_HEADER_SIZE + 16 + 64 * count)) != 0) { err = NCZ_E_IO; break; }
        }

        dctx = ZSTD_createDCtx();
        ibuf = malloc(NCZ_IOBUF);
        obuf = malloc(NCZ_IOBUF);
        if (!dctx || !ibuf || !obuf) { err = NCZ_E_IO; break; }
        bool eof = false;
        bool frame_done = false;
        for (;;) {
            ZSTD_inBuffer inb = { ibuf, 0, 0 };
            if (!eof) {
                inb.size = fread(ibuf, 1, NCZ_IOBUF, in);
                if (ferror(in)) { err = NCZ_E_IO; break; }
                if (inb.size == 0) eof = true;
            }
            if (eof && frame_done && inb.size == 0) break;  // clean finish
            size_t last = 1;
            for (;;) {
                ZSTD_outBuffer outb = { obuf, NCZ_IOBUF, 0 };
                last = ZSTD_decompressStream(dctx, &outb, &inb);
                if (ZSTD_isError(last)) { err = NCZ_E_ZSTD; break; }
                if (outb.pos) {
                    if (fwrite(obuf, 1, outb.pos, out) != outb.pos) { err = NCZ_E_IO; break; }
                    body += outb.pos;
                    if (progress && !progress(user, body)) { err = NCZ_E_CANCEL; break; }
                }
                if (last == 0) {
                    frame_done = true;                     // frame complete; more frames may follow
                } else if (inb.size > 0) {
                    frame_done = false;                    // fresh input went into an open frame
                }
                if (inb.pos >= inb.size) {
                    // input exhausted: an open frame with nothing left to decode is truncation
                    if (eof && !frame_done && outb.pos == 0) err = NCZ_E_ZSTD;
                    break;
                }
            }
            if (err != NCZ_OK) break;
        }
        if (err != NCZ_OK) break;
        for (uint64_t i = 0; i < count; i++) {
            if (table[i].offset + table[i].size > NCZ_HEADER_SIZE + body) { err = NCZ_E_TABLE; break; }
        }
        if (err != NCZ_OK) break;

        err = NCZ_OK;
        for (uint64_t i = 0; i < count && err == NCZ_OK; i++) {
            if (table[i].crypto != 3 && table[i].crypto != 4) continue;   // 1 = the body was already plain
            err = ctr_region(out, table[i].offset, table[i].size, table[i].key, table[i].counter);
        }
    } while (0);

    ZSTD_freeDCtx(dctx);
    free(ibuf);
    free(obuf);
    free(table);
    fclose(in);
    if (fclose(out) != 0 && err == NCZ_OK) err = NCZ_E_IO;
    if (err == NCZ_OK && body_bytes) *body_bytes = body;
    return err;
}

const char *ncz_error_name(NczError e) {
    switch (e) {
    case NCZ_OK: return "ok";
    case NCZ_E_OPEN: return "open";
    case NCZ_E_SHORT: return "short";
    case NCZ_E_MAGIC: return "magic";
    case NCZ_E_TABLE: return "table";
    case NCZ_E_CRYPTO: return "crypto";
    case NCZ_E_ZSTD: return "zstd";
    case NCZ_E_IO: return "io";
    case NCZ_E_CANCEL: return "cancel";
    }
    return "?";
}
