# Third-party notices

Monkey Tool's own code is under the [MIT License](LICENSE). That includes the LZMA decoder (`src/nfsnl_lzma.cpp`), written for the tool from the format's description. The pieces below belong to other projects and stay under their own licenses.

## Included in this repository

### LZHAM decompressor: `src/lzham/`

Rich Geldreich's LZHAM decoder, compiled into the tool through `src/nfsnl_lzham.cpp`.
Public domain (see [`src/lzham/LICENSE`](src/lzham/LICENSE)), with a two-line portability fix.
<https://github.com/richgel999/lzham_codec>

### Brotli decoder: `src/brotli/`

Google's reference Brotli decoder, compiled into the tool through `src/nfsnl_brotli.cpp`.
MIT License, Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors. Full text: [`src/brotli/LICENSE`](src/brotli/LICENSE).
<https://github.com/google/brotli>

### miniz: `src/thirdparty/miniz/`

Rich Geldreich's zlib replacement: the PNG writer's compression levels.
MIT License, Copyright 2013-2014 RAD Game Tools and Valve Software, Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC. Full text: [`src/thirdparty/miniz/LICENSE`](src/thirdparty/miniz/LICENSE).
<https://github.com/richgel999/miniz>

### bc7enc: `src/thirdparty/bc7enc/`

Rich Geldreich's BC7 encoder, for DDS textures written as BC7.
MIT License or public domain, at your choice (see [`src/thirdparty/bc7enc/LICENSE`](src/thirdparty/bc7enc/LICENSE)).
<https://github.com/richgel999/bc7enc>

### libogg and libvorbis: `src/thirdparty/ogg/`, `src/thirdparty/vorbis/`

Xiph.Org's Ogg container and Vorbis encoder, for sounds exported as Ogg Vorbis. Only the files
the encoder needs are included; `ogg/os_types.h` takes `stdint.h` on every compiler, and
`ogg/config_types.h` is written by hand in place of the generated one.
BSD-style licenses: libogg Copyright (c) 2002 Xiph.org Foundation, libvorbis Copyright (c) 2002-2020 Xiph.org Foundation. Full texts:
[`src/thirdparty/ogg/COPYING`](src/thirdparty/ogg/COPYING), [`src/thirdparty/vorbis/COPYING`](src/thirdparty/vorbis/COPYING).
<https://github.com/xiph/ogg>, <https://github.com/xiph/vorbis>

### Not included: ffmpeg

MP3, AAC and FLAC are written by `ffmpeg.exe` when you have it (beside the program, in an
`ffmpeg` folder there, on the PATH, or where you point the tool once). It is not shipped with
Monkey Tool: the MP3 and AAC encoders carry their own licences.

### vgmstream: `tools/vgmstream/`

Unmodified Windows binaries of vgmstream-cli and the DLLs it ships with. The tool runs it to play and convert Wwise sounds.

* **vgmstream**: ISC-style license, Copyright (c) 2008-2025 Adam Gashlin, Fastelbja, Ronny Elfert, bnnm and others. Full text: [`tools/vgmstream/COPYING`](tools/vgmstream/COPYING). <https://github.com/vgmstream/vgmstream>
* The DLLs next to it are other projects' libraries, as distributed by vgmstream:
  * FFmpeg (`avcodec`, `avformat`, `avutil`, `swresample`): LGPL 2.1 or later
  * mpg123 (`libmpg123-0.dll`): LGPL 2.1
  * libvorbis: BSD-style
  * Speex: BSD-style
  * CELT: BSD-style
  * LibAtrac9: MIT
  * G.719 decoder: ITU-T reference code

  Their sources are linked from the vgmstream project. The LGPL libraries are unmodified dynamic libraries and can be replaced with other builds.

## Downloaded at build time (not in this repository)

| Project | Used for | License |
|---------|----------|---------|
| [zstd](https://github.com/facebook/zstd) (`libzstd.dll`) | 3D models, texture cabinets | BSD 3-Clause (or GPLv2) |
| [w64devkit](https://github.com/skeeto/w64devkit) | the compiler `BUILD.bat` fetches when none is installed; build only, not shipped | Unlicense / public domain, with GCC under the GPL with the runtime exception |

When you publish a release that includes `libzstd.dll`, include its license text too. The GitHub workflow does this for you.

## Game content

No game files are included. The splash pictures, game logos and icons under `src/` (`splash_*.jpg`, `logo_*.png`, `icon_*.png`) show the games the tool supports. They are the property of their owners (Electronic Arts and its studios) and are **not** covered by the MIT License.
