// NSP/XCI/NSZ package extraction for the game files setup (runtime/ui/launcher.mm): the player
// brings game packages and their key files instead of already-extracted files. Runs the nstool
// helper for the containers (shallow extraction, then per-NCA partition extraction) and rebuilds
// the .ncz entries of NSZ/XCZ packages (runtime/ncz.c) in between. The final files land directly in
// the game dir the setup uses (exefs, romfs, aoc1..): the 15 GB RomFS is never copied twice.
//
// Key rules (AGENTS.md): key files are read, never logged; the log keeps package and NCA content
// ids, content types and partition layouts only.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NSPE_OK = 0,
    NSPE_ARGS,       // no package(s) or no prod.keys given
    NSPE_NSTOOL,     // the nstool helper program wasn't found (nstool_path, SAVAGEWIND_NSTOOL, or
                     // the shipped <exe dir>/nstool/ layout)
    NSPE_KEYS,       // prod.keys can't be read, or has no NCA header key line
    NSPE_TITLEKEYS,  // the titlekeys the packages' NCAs need aren't covered (why lists the rights
                     // ids; give title.keys or packages that bring their tickets)
    NSPE_PACKAGE,    // a package can't be extracted (nstool's log has the details)
    NSPE_NO_GAME,    // the packages hold no game (or an update without its base package)
    NSPE_VERSION,    // exefs/main isn't a supported game version, or romfs disagrees with it
    NSPE_NCA,        // an NCA header can't be parsed (a bad dump?)
    NSPE_IO,         // staging or output folder errors
    NSPE_CANCEL,     // cancelled: the staging and the files written so far are removed again
} NspeError;

// One NCA as judged from its (decrypted) 0xc00-byte header block. Offsets follow the real layout,
// verified against staged NCAs (the header block: two RSA signatures, then the fields at 0x200, the
// partition entries at 0x240 and the four 0x200-byte partition fs headers at 0x400).
typedef struct {
    unsigned content_type;      // 0 program, 1 meta, 2 control, 3 manual, 4 data, 5 public data
    uint64_t program_id;        // the application id (base and update program NCAs carry the same)
    uint8_t rights_id[16];      // all zero: the NCA doesn't need a titlekey
    uint64_t content_size;
    int partition_count;
    struct {
        uint64_t offset, size;              // in the NCA
        unsigned format_type;               // 0 romfs, 1 partitionfs
        unsigned encryption;                // 1 none, 3 aes-ctr, 4 aes-ctr-ex (an update's romfs)
        uint32_t generation, secure_value;  // the partition counter's two words (BE gen/sec pairs)
    } partition[4];
} NspeNcaInfo;

// Parses a decrypted header block. NSPE_NCA unless it says NCA3 at 0x200. The block's encrypted
// range is sectors 1..5 (bytes 0x200..0xc00), decrypted with nspe_xts_decrypt below and the
// header_key line of prod.keys; the two RSA signatures stay plain.
NspeError nspe_parse_nca_header(const uint8_t block[0xc00], NspeNcaInfo *out);

// Decrypts NCA header bytes in place: data holds size bytes (a multiple of the 0x200 sector size)
// copied from file offset sector_start * 0x200, with the XTS convention nstool uses (tweak = 8 zero
// bytes || BE64(sector), data key = header_key[0:16], tweak key = header_key[16:32]).
NspeError nspe_xts_decrypt(void *data, size_t size, uint64_t sector_start, const uint8_t header_key[32]);

typedef struct {
    const char *const *packages;  // .nsp/.xci/.nsz/.xcz files (base + update + DLC in any mix)
    int package_count;
    const char *prod_keys;        // required: NCA header key, and nstool's key file
    const char *title_keys;       // optional: the packages' own tickets cover it then
    const char *nstool_path;      // resolved by the caller (the shipped <exe dir>/nstool/ layout);
                                  // NULL = SAVAGEWIND_NSTOOL, the dev override
    const char *game_dir;         // the setup's extracted game dir: exefs/ romfs/ aoc1.. land here
    const char *staging_dir;      // scratch (short path: NSZ entries grow past 1 GB here); removed
                                  // before and after, whatever the outcome
    void (*remove_game_files)(const char *game_dir);  // paths.c's rt_remove_game_files; called once
                                  // before extraction and once to undo a cancelled or failed run
                                  // (NULL: the driver touches no game files, synthetic tests)
    void (*progress)(void *user, const char *step, double fraction);  // fraction < 0: unknown yet
    int (*cancelled)(void *user);                                     // nonzero stops the run
    void *user;
} NspeOpts;

// Extracts the game (and DLC) of the packages into game_dir. A few minutes with the RomFS merged;
// false (with NSPE_* in why's text, details in the log) leaves the previous game files removed and
// the staging gone.
NspeError nsp_extract_run(const NspeOpts *o, char *why, size_t why_size);

const char *nspe_error_name(NspeError e);   // log tag: "ok", "nstool", "titlekeys", ...

#ifdef __cplusplus
}
#endif
