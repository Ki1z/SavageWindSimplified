# Savage Wind Simplified

A fork of [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp) — a static recompilation of
*The Legend of Zelda: Breath of the Wild* (Nintendo Switch, version 1.9.0) into a native application for
Windows — that adds what the upstream start screen doesn't have yet: **setting the game up straight from your
own dump packages**. This fork's changes live on the `simplified` branch and stay here: they are this fork's
local convenience, not proposals against upstream.

> [!NOTE]
> **No game files are included.** You need your own legally obtained copy of the game, dumped from **your own**
> Nintendo Switch. This project does not include, and will not provide, game files, keys, firmware, dumping or
> decryption tools, or instructions on how to obtain any of them. The packages are read, never redistributed,
> and the game code is recompiled locally, on your machine, from your files.

## What this fork adds

The upstream setup takes a folder of already-extracted game files (exefs and romfs). This fork adds a
**"Set up from game packages"** flow on the same start screen:

- Point it at your packages — **NSP, XCI or NSZ** — instead of a folder. The base game and its update can be
  one integrated dump or two separate ones; the update's RomFS is merged over the base automatically.
- **DLC** can be set up on its own, later, against an already-set-up game (up to two DLC packages; they add
  `aoc1`/`aoc2` to the game files without touching the ExeFS).
- The packages are unpacked by a bundled [nstool](https://github.com/jakcron/nstool) (this project's fork with
  the `--titlekeyset` option re-enabled), then everything else — the version checks, the game code build with
  the bundled Zig, the DLC check — runs exactly as the folder flow's does.

Everything else is upstream Savage Wind, unchanged: the launcher, the graphics options, the controls, the
whole game. See the [upstream README](https://github.com/Revivedx/SavageWindRecomp#readme) for the full
picture, the [upstream `test` branch](https://github.com/Revivedx/SavageWindRecomp/tree/test) for
contributing, and [docs/PROGRESS-SIMPLIFIED.md](docs/PROGRESS-SIMPLIFIED.md) for this fork's own roadmap and
test results.

## Setting up from packages

1. **Keys** — two files from your own console:
   - `prod.keys`: must hold an NCA header key.
   - `title.keys`: must hold an entry for every package that is title-key encrypted (updates and DLC usually
     are). A dump's own ticket does **not** stand in for an entry: a ticket is locked to the console that
     made it, and the unpacker is given the keys file alone.
2. **Packages** — your dumps. One integrated base+update NSP, or a base NSP and an update NSP, plus up to two
   DLC NSPs (a DLC-only run adds to the game files already set up).
3. Press **Set up**. The start screen unpacks the packages, checks the game version, and builds the game code
   with the bundled Zig 0.17.0 — no compiler installation needed. The first build takes a few minutes;
   after that the game starts like any other setup.

Report this fork's problems on
[its issue tracker](https://github.com/Ki1z/SavageWindSimplified/issues) — but gameplay bugs that also happen
upstream belong on the upstream tracker.

## Building from source (Windows)

The upstream [build guide](https://github.com/Revivedx/SavageWindRecomp#building-from-source) applies; the
package flow adds these pieces:

- MSYS2 UCRT64 with the upstream's package list, plus a local `deps/` prefix or `make deps-windows` (the
  LGPL FFmpeg the release ships).
- `tools/fetch_zig.sh` puts the pinned Zig 0.17.0 into `build/zig/` — the game code build needs it (or
  `SAVAGEWIND_CC`, a developer override).
- `make gen-helper` freezes the generator (`tools/` + its Python packages) into `build/helper/swgen`.
- `make dist` then assembles `build/dist/Savage Wind`: the release exe, the DLLs, `nstool/`, `zig/`,
  `swgen/`, the gamelib headers, the fonts and the licence files. That folder is what a release zip holds.

## Credits and License

This fork is built entirely on [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp) — see its
README for the full credits: [LAS Recomp](https://github.com/peiikai/LASRecomp) by peiikai (MIT) underneath
everything, and the long list of libraries and references the project stands on. This fork's own additions
keep the same [MIT License](LICENSE); the note there covers this project's code only: it grants nothing for
Nintendo's game, which you must supply yourself.

The Windows release zip bundles, unchanged unless noted, under their own licenses:

- FFmpeg 9.0.2 (libavcodec, libavformat, libavutil, libswscale), LGPL 2.1 or later: built from the official
  [source](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz) as LGPL only, without GPL or nonfree parts, by
  [tools/build_deps_windows.sh](tools/build_deps_windows.sh). They are separate DLLs: you can replace them with your
  own build of the same version.
- From [MSYS2](https://packages.msys2.org) (UCRT64, sources there): SDL 3.4.18 (zlib), shaderc 2026.4 (Apache 2.0),
  libiconv 1.19 (LGPL 2.1 or later), the GCC 16.2 runtime (libgcc, libstdc++: GPL 3 with the GCC Runtime Library
  Exception) and winpthreads (MIT and BSD).
- Zig 0.17.0 (MIT; its bundled LLVM, Clang and LLD: Apache 2.0 with LLVM Exceptions; the MinGW-w64 headers and
  runtime: their own permissive licenses), trimmed to what the game code build uses.
- `swgen/`, this project's generator frozen with PyInstaller (GPL 2.0 with the bootloader exception): Python
  (PSF License), Capstone (BSD), [Unicorn](https://github.com/unicorn-engine/unicorn) (GPL 2.0, source at that
  link) and python-lz4 (BSD).
- [nstool](https://github.com/jakcron/nstool) (MIT, `nstool/LICENSE`), the package unpacker behind the
  game-files setup's "from game packages" flow, built from this project's fork of it (the `--titlekeyset`
  option re-enabled).
- The Fira Sans font, Medium and SemiBold (SIL OFL 1.1, `FiraSans-OFL.txt`), and the Noto Sans Thai and Noto
  Sans Hebrew fonts, Medium, for the menus in Thai and Hebrew (SIL OFL 1.1, `NotoSansThai-OFL.txt`,
  `NotoSansHebrew-OFL.txt`).
- Three colour LUTs in `colour-luts/` (CC0 1.0, from [FreshLUTs](https://freshluts.com): see
  `colour-luts/README.md`).
