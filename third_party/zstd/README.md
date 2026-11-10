zstd (decoder only), vendored from https://github.com/facebook/zstd at v1.5.7.

- zstddeclib.c: the generated single-file decompressor (build/single_file_libs/zstddeclib-in.c
  run through combine.py), used under the BSD license (see zstd's LICENSE; dual BSD/GPLv2, we
  take BSD). Regenerate with:
      python combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c
  from the zstd tree's build/single_file_libs directory.
- zstd.h: the matching public header (lib/zstd.h), unchanged.

Used by runtime/ncz.c to stream-decompress the body of NCZ entries (NSZ/XCZ packages).
Decoder only: nothing here compresses.
