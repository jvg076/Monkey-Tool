# File formats

Notes on the game formats Monkey Tool reads and writes, written while working them out.

## Need for Speed: No Limits and the file types at a glance

| Extension | What it is |
|-----------|------------|
| `.pack`   | `PACK`/`ZBDS` archive: gzip-compressed manifest plus independently compressed *cabinets*. A file is `cabinet[index][offset .. offset+length]`. |
| `.cab`    | A bare `SBIN` blob. |
| `.sba`    | Texture. Payload is usually PNG; can also be raw pixels, ETC1/ETC2 or DXT block data. |
| `.sb3d`   | Model. Zstandard-compressed vertex/index buffers plus a scene description. |
| `.m3g`    | Model. Either JSR-184 M3G, or the game's own `DA BD` compressed wrapper around one. |
| `.wem`    | Wwise audio — convert with vgmstream or ww2ogg to play it. |
| `.z`      | Real Racing 3: `uint32` uncompressed size, then a zlib stream. Taken off transparently. |
| `.z.bin`  | Real Racing 3: a chain of those blocks, each a whole file. Shadows ship one frame per sun angle. |
| `.etc.dds`| Real Racing 3 texture: a plain DDS header over ETC1 blocks, or uncompressed RGBA4444. |
| `.points` | Real Racing 3: where the wheels, the steering wheel and the mirrors go. |

### Game profiles

A profile is what the tool needs to know about one game: its name, the folder
to ask for, the archive extensions to look for, and its logo for the picker.
They live in `src/nfsnl_profiles.cpp`, one struct each, so adding another of
the studio's games is a profile plus whatever format readers that game needs -
not a second program.

Listed now, each with its logo on the card:

| Game | State |
|------|-------|
| Need for Speed: No Limits | working |
| Need for Speed: Most Wanted (mobile) | declared, not supported |
| Need for Speed: Hot Pursuit (mobile) | declared, not supported |
| Real Racing Next | declared, not supported |
| Real Racing 3 | **working** — models with their hardpoints, ETC textures, sounds |
| Real Racing 2 | declared, not supported |
| Real Racing | declared, not supported |

Picking one that is not supported says so; it does not open a file dialog and
fail later. The list scrolls with the wheel when the window is short.

### Reading without unpacking

A `.pack` is a manifest plus a row of independently compressed cabinets.
Indexing an archive reads only the manifest, so every asset the game has is
listed in seconds. Clicking one reads that cabinet's slice off disk,
decompresses it in memory, and cuts the asset out of it. A cabinet usually
holds a car's model *and* its textures, so the decompressed cabinets are kept
in a 192 MB cache and the rest of that car costs nothing.

What this changes, compared to 0.4: no temp folder, nothing written to
`AppData\Local\Temp`, nothing to clean up if the program is closed the hard
way, and no multi-gigabyte copy of a game you already have on disk.

### Car colours: `colours.sb` and the car setups

A car's paint is not in its model or its textures. The paint meshes use
`textures/cars/common/texture_paint.sba` (a stand-in), the brake calipers
`texture_brake_caliper_01.sba` and the rims the wheel's own texture; the game
multiplies its colours in at run time.

* `data/colours/colours.sb` lists every colour, in groups: `BodyColours`
  (`CG_COMMON_BODY`, `CG_FERRARI_BODY_5` ...), `RimColours`, `BrakeColours`,
  `WindowColours`, the neons. Each has an `Id` (`V_BC_GT3_Orange_Gloss`), a
  `HashedID`, a `Colour` (RGB 0..1), its finish (`Gloss`, `Matte`, `Metallic`)
  and the BRDF lookup textures of that finish (`/published/brdf/texture_brdf_*`,
  `texture_sp_*` - 128 x 128 and 2048 x 1 PNGs).
* `data/car_setups/<car>/Bodykits/<X>_STOCK.sb` is the car as it ships (and
  `_STOCK_LTS`, `CUSTOM_AI_*` and `Customs/*.sb` the other versions). Among
  its `_EncryptedHashedId` values are the `HashedID`s of its body, rim, brake
  and window colours. The 2015 GT3 (991): GT3 Orange Gloss, Silver rims,
  Yellow calipers.
* A wheel bought in the shop carries its own `DefaultColour` in
  `data/visualparts/wheels_common.sb`.

The viewer paints the body (`texture_paint`), the calipers
(`texture_brake_caliper_*`, not `_alpha`) and the rims (the wheel's `*_tint`
meshes; `*_notint` keeps its colours) and lists the car's setups under the view.

### UI texture packs

`texturepacks/ui/*.sba` hold a `TexturePack`: `Box`es with a name
(`FrontEnd/LTS/LTS_PROVING_GROUNDS/lts_card_back.png`), a `source_rect` and an
`Image` (format, BULK index, width, height). Several boxes share one page.
Image format 13 is a PNG; 15 is a palette picture packed with LZ4 - one byte
with the palette size (0 = 256), the palette as RGBA, one index byte per pixel;
33 is a vector icon (`.svg`, a packed SVG tree), which the tool does not draw.

### Limited-time layers on tracks

`prefabs/tracks/<region>.scene.sb` places what is not in the static geometry.
Its top-level actors with a `LayerScene` component are the event decorations:
`halloween_2024_hopebridge_lts` (`FileName = "Halloween_2024"`,
`LayerNames = [ layer_halloween_2024 ]`), `LTS_2022_Christmas`,
`lts_2024_paradyne_end` ... The layer's geometry is its own
`<FileName>.scene_static.sba`; the actors' `NFSModel` components place more
models (`/published/models/environments/animations/LTS_HALLOWEEN_2023/...`)
with `TransformComponent`s (translation, yaw / pitch / roll in degrees,
scale), in the same space as the region's static geometry.

### Wheels on No Limits cars

A No Limits car model carries its brake discs (`mesh_rotor_*`) already in
place. The wheel is a separate model, `wheel_<car>.sb3d`, usually in
`models/cars/wheels/`, that the game hangs on those discs.

The discs are found by position. Every vertex of every rotor mesh, at the
finest LOD, is sorted into a corner by its side (x) and its axle: the discs'
z span is split in the middle when they spread further than one disc is wide.
The names are not used, because they are wrong too often. The Nissan Z calls
all four discs `rotor_front_left`, and the Cayenne's "front_left" disc is on
the right.

The tool then does what the game does, from these measurements on the Jaguar
XE SV:

* The wheel is modelled around its own axle with the spokes towards -x, so
  unchanged it is a left-side wheel. The right side is the same wheel turned
  half a turn, not mirrored, so the rim lettering still reads correctly.
* The axle goes through the disc's centre. The front-left disc spans
  y -0.566..-0.163, so the hub is at y -0.364. A 0.360 tyre then reaches
  y -0.725, which is the ground under that car.
* Across the axle, the back of the wheel's centre section sits against the
  outer face of the disc.
* No scaling: the wheel is modelled at the car's size. Every LOD of the wheel
  goes on, and each shows with the car's LOD of the same number.

If there is no wheel under the car's exact name, the closest name is used,
provided at least the make and model match (the log says which wheel was
used). The command line takes it with `--wheel`:

```
monkeytool_cli one jaguar_xe_sv.sb3d jaguar.fbx --format fbx --wheel wheel_jaguar_xe_sv.sb3d
```

### Packs with external cabinets

The manifest's `DATA` chunk holds three counted tables in a row, 0x24 bytes
in: folders (20 bytes each), files (20 bytes each) and cabinets (16 bytes
each: flags, size, offset, packed size). 0.7.3 reads them at those positions,
the same way Bigchillghost's QuickBMS unpacker does. Before that, the cabinet
table was found by looking for a run of records whose packed data chained
right to the end of the `.pack`.

That search only works when every cabinet is inside the `.pack`, and they are
not always. A cabinet with flag `0x40` is a separate file:

```
<folder of the pack>\<pack name>\<cabinet number>.cab
```

It is stored as it is when flag `0x20` is set or `0x04` is not, and it is
Zstandard otherwise. One external cabinet was enough for the old search to
fail on the whole pack, and then every asset in it was lost. That is why the
AE86, the S2000 and the Falcon showed only their `.lua` and `.sb` files: their
models and textures are in packs laid out this way. Checked against the BMS on
26 packs, the file count is identical: 60,367 assets.

Two consequences in the tree:

* A `.cab` file inside a folder named after a `.pack` beside it is read
  through that pack and is not listed a second time on its own.
* An asset whose cabinet is not on disk (an external `.cab` that was never
  downloaded, or a `.pack` cut short) is **not listed at all**. The status log
  says how many were left out, and for which pack.

### LOD levels

A mesh is named `mesh_<material>_<part>_lodNN`, and **that number is the detail
level**. `mesh_interior_interior_a_lod00` is the interior at the car's highest
detail; `mesh_interior_interior_a_lod05` is the same interior five steps down.

Up to 0.7 this reader did not believe the names. It re-derived the level from
vertex counts and bounding boxes, on the theory that the numbers counted across
body kits. That theory cost more than it bought: the re-derivation split one
ladder into several and restarted each at zero, so `LOD00` ended up holding
`_lod00`, `_lod05` *and* `_lod07` of the very same mesh - which is what showed
up in 3ds Max as LOD 2 parts sitting inside LOD 0.

From 0.7.2 the stated number is used, which fixes it by construction: a number
appears once per chain, so one chain can never put two meshes in the same
level. On the Cayenne, `interior_a` now reads

```
LOD00 -> mesh_interior_interior_a_lod00
LOD01 -> mesh_interior_interior_a_lod01
LOD02 -> mesh_interior_interior_a_lod02, mesh_chassis_interior_a_lod02
...
```

A chain whose numbers start above zero simply has no mesh at `LOD00`, and that
is the truth rather than a gap to paper over: `mesh_chassis_interior_a` runs
`_lod02.._lod04` because the chassis material only takes over the interior once
the car is far enough away. Inventing an `LOD00` for it, as the old code did,
is what put low-detail geometry in the high-detail level.

The original mesh names are kept exactly as the game spells them - nothing is
renamed, only grouped.

### Scene tree

Both exporters group the same way, which is the structure the parts have in
the game's own art:

```
LOD00
  bumper_rear_a
    mesh_paint_bumper_rear_a_lod00
    mesh_carbon_bumper_rear_a_lod00
  body_a
    mesh_paint_body_a_lod00
  ...
LOD01
  ...
```


### Vertex layout in `.sb3d`

**There is no fixed vertex layout.** Every mesh carries its own vertex
declaration, and the UV offset differs from mesh to mesh. Each record in the
scene description holds, at byte 18, the number of attributes, followed by
that many 28-byte descriptors:

```
semantic, usageIndex, type, componentCount, setIndex, byteOffset, 0

semantic:  0 position  4 normal  5 tangent  6 bitangent  8 texcoord  9 colour
type:      0 int8      1 uint8   3 uint16
```

A typical mesh looks like this, but only *this* mesh:

```
 0..5   position   3x uint16, normalised into the shape bounding box
 8..10  normal     3x int8
12..14  tangent    3x int8
16..18  bitangent  3x int8
20..23  UV         2x uint16 over [0,1]
```

Measured across one whole car (2,395 meshes): **1,386 meshes keep their UVs at
byte 12 and 1,009 at byte 20**, and the stride does not separate them - stride
24 occurs in both groups. That is what broke `mesh_paint_*` and
`mesh_interior_*` in earlier versions and in the Blender importer circulating
for this game: a hardcoded offset is right for roughly half the mesh list and
wrong for the rest, so paint UVs collapse onto a line and interior UVs vanish.
Reading the declaration fixes both.

The normal at offset 8 was confirmed by comparing against normals computed
from the triangles themselves (mean dot 0.994).

**Both positions and texture coordinates are quantised per mesh.** Right after
the attribute table sit four vec4s:

```
position scale, position bias, texcoord scale, texcoord bias
value * scale + bias
```

The position pair just repeats the bounding box, but the texcoord pair appears
nowhere else. Dividing the stored uint16 by 65535 and stopping there stretches
every mesh's UVs across the whole texture: a chrome trim piece whose unwrap
really occupies 0.06 x 0.04 of the sheet at (0.70, 0.33) ends up covering all
of it. Since each LOD carries its own pair, the LODs then disagree with one
another - which is what made the lowest LOD look wrong while the highest
looked plausible. Reading the pair puts every LOD of a part in the same place.

A UV span wider than 1 is normal, not a bug: tiling meshes such as a radiator
grill legitimately repeat their texture (one measures 14.6 x 3.9).

**Vertex colours (semantic 9)** are four uint8 per vertex, RGBA. On the cars
checked (Audi TT RS, Porsche Cayenne) they are grey: baked ambient occlusion.
The grille, the wheel wells and the interior are dark, and open panels are
white. 92,080 of the Audi's 92,910 vertices carry them. They are exported as
FBX `LayerElementColor` (all four channels) and as extra numbers on the OBJ
`v` lines (RGB), which Blender, MeshLab and ZBrush read. Real Racing 3's
JSR-184 colour arrays are read the same way when a model has them; the cars
on hand do not.

**A second texcoord (usage 1)** appears on the paint meshes. It is the livery
layout: the whole body unwrapped once, with no dequantisation of its own (the
second half of the scale/bias vec4s reads 1 and 0). It is exported as a
second FBX UV set named `LiveryUV`. OBJ has room for only one.

The engine already uses a Y-up, Z-forward, X-right right-handed system, which
matches FBX and OBJ, so no axis conversion is applied. Measured on a car body:
X = 2.06 m wide, Y = 1.30 m tall, Z = 4.72 m long.

### Audio: `.bnk` and `.wem`

A `.bnk` is a Wwise SoundBank. Its `DIDX` section is a flat table of
`(id, offset, size)` into the `DATA` section, so splitting one into the files
it contains is exact - nothing is guessed. Save a bank and every sound inside
is written out, named after the bank plus each sound's own id.

**A bank opens in the tree like a folder.** Click the arrow next to it and its
sounds are listed one per row, each labelled with what it actually is:

```
V8_Ford_Flathead_Coupe.bnk
   463464671.gnsu   (engine, 1335-7254 rpm)
   926765192.wem    (Wwise Vorbis, 16000 Hz)
   1014694220.gnsu  (engine, 1185-7146 rpm)
```

Select one to see it and press **Play**; double-click it to save that one
sound - as `.wav` whenever it can be decoded, which with vgmstream set up
means every `.wem`. The bank is read the moment you open the row, not before,
so having thousands of them in the tree costs nothing.

A `.wem` is a RIFF/WAVE file. Every sound in No Limits uses format **0xFFFF,
Wwise Vorbis**, at 24 kHz mono for effects and 44.1 kHz stereo for music.
Wwise Vorbis strips the Vorbis setup header: instead of the codebooks, the
file holds ten-bit indices into a codebook library that ships with the Wwise
*encoder*. You can see it in the dump - a setup packet of about 215 bytes
where real codebooks would be several kilobytes. That library is not in the
game's files and cannot be derived from them, so this tool will not pretend to
decode those streams.

What it does instead:

* PCM, IEEE float and IMA ADPCM `.wem` files are decoded to WAV here.
* For Wwise Vorbis it uses **vgmstream**. Point at `vgmstream-cli.exe` once -
  through **File → Locate vgmstream-cli.exe...**, or when the tool asks while
  saving - and the path is remembered in the registry. It is also found
  automatically next to `MonkeyTool.exe`, in `vgmstream\`, `vgmstream-win64\`
  or `tools\`. Get it from <https://vgmstream.org>.
* With it set up: **open a bank in the tree, click any sound, press Play**,
  and saving - one sound or the whole bank - writes `.wav`. The dropdown above
  the preview does the same thing for people who prefer it.
* Without it, sounds are still written out as valid `.wem` files, and the
  status line says exactly why.

There is no MP3 export: writing an MP3 needs an encoder this program does not
carry. WAV is lossless and every editor opens it.

The granular engine banks (`Gnsu20`) inside a car's `.bnk` are not streams, so
they are written out whole as `.gnsu` and cannot be played - there is no single
waveform in one. That is why saving a car bank as wav gives you `.wav` files
for the sound effects and `.gnsu` for the engine.

### Engine sounds: `Gnsu20`

Two of the three entries in a car's `.bnk` are not streams at all. They start
with `Gnsu20` and hold a granular synthesis table: a rev range, a sample rate,
and a list of grain offsets the game crossfades as engine speed changes. For
`i6_Nissan_Fairlady_432Z.bnk` that is 1528-6590 rpm at 24 kHz. They are
written out whole and reported in the text dump; there is no single waveform
inside to turn into one wav.

### `.sbfx` - particle effects, not animation

`.sbfx` files are plain SBIN, and their contents say exactly what they are:
`EffectParticleTemplate`, `CloudTemplate`, `EmitterTemplate_Implicit`,
`RendererQuad`, `ParticleMaterial`, blend and stencil render states, and
`texturePaths` pointing at `/published/textures/vfx/*.sba`.

So there is no skeleton and no animation curve in them, and converting one to
FBX would produce an empty scene. They export as **text** instead, which for
an effect is the useful form: every emitter, every parameter ramp and every
texture the effect uses.

### Text export, and what `.sb` files are

Every SBIN file carries its own schema, and the text dump prints it:

```
structures (37)
  EffectParticleTemplate
      float      preRoll                      at 0
      float      totalTime                    at 4
      reference  entities                     at 12
      int        cycleCount                   at 20
      flags      properties                   at 28  EffectParticlePropertyFlags
  ...
enumerations
  ImplicitGeoShape: NoGeo, Point, Sphere, Box, Plane, Disc, Cylinder, Cone
  BlendMode: Zero, One, SrcColor, OneMinusSrcColor, DestColor, ...
```

The field types come from `FIEL`, and which enum a field uses is the last word
of its entry - an index into `ENUM`. That is checkable rather than guessed:
`geoType` carries 1 and `ENUM[1]` is `ImplicitGeoShape`; `deathEvent` carries 5
and `ENUM[5]` is `EventID`. Enum *member* lists are delimited by the next name
the file declares as a field or a structure, since no member count is stored;
where a struct is only referenced and not declared, a member list can run on a
few names too far.

**`.sb` data files are encrypted.** Every one is an exact multiple of 16 bytes
with entropy around 7.9 and no repeated block inside a file; two cars' files
(`AST_MAR_DB5_STOCK.sb` and `..._LTS.sb`) share exactly one identical 16-byte
block, the first, and nothing after it. That is a block cipher in CBC mode.
Without the key - which lives in the game binary - no tool can show their
contents, so they export as a report plus a hex listing rather than as
plausible-looking nonsense.

### `.m3g` models - the codec is LZHAM

Two different things share this extension.

**Compressed, as No Limits ships them.** The files in `models/scenes/`,
`models/environments/` and `models/roadblocks/` start with a ten-byte header
of the game's own:

```
DA BD  <uint32 uncompressed size>  <uint32 compressed size>   then the payload
```

The payload is **LZHAM**. That is not a guess any more - three independent
things say so, all of them checkable:

1. **The game ships LZHAM.** `libapp.so` in the APK names its own vendored
   copy: `core\vendor\lzham\lzham_symbol_codec.cpp`, `lzham_lzcomp_state.cpp`,
   `lzham_mem.cpp`, plus the runtime strings `lzham_malloc: out of memory` and
   `"lzham::vector operator=: Out of memory!"`.
2. **The asset format names it.** The same binary carries the compression
   enumeration: `CompressedDeflate`, `CompressedLZHAM`, `CompressedLZMA`,
   `CompressedZstd`.
3. **The streams match.** The packs inside the APK contain cabinets whose
   manifest flags have bit 2 set - `CompressedLZHAM` - and those begin exactly
   like every compressed `.m3g`:

```
LZHAM cabinet : 16 xx xx xx  xx xx xx xx  40 00 00 00 ...
.m3g payload  : 16 00 00 00  vv vv vv vv  40 03 38 e9 ...
```

   Byte 0 is `16` and byte 8 is `40` in both, every time.

And those cabinets gave up the framing as well. Two of the packs ship the same
payload twice:

```
740fef9c... 16 6f cf 9c | d8 9b db bf | 40 00 00 00 ...  bf db 9b d8
05eec1c3... 16 16 36 cf | d8 9b db bf | 40 00 00 00 ...  bf db 9b d8
```

Same length, same contents, and every byte the same except bytes 1 to 3 - so
those three are a per-pack stamp, not compressed data. Which leaves:

* **byte 0** - always `16`, which is 22: a legal LZHAM dictionary size log2,
  and the one the 4 MB presets use.
* **bytes 4-7** - an Adler-32. It comes back reversed as the last four bytes
  of every payload, cabinet and `.m3g` alike: written at both ends, the tail
  one laid down backwards by LZHAM's bidirectional coder.
* **byte 8 onwards** - the stream itself, which always starts `40`.

A decode is accepted when it produces exactly the promised number of bytes,
and accepted immediately when its Adler-32 matches the one in the header.

So the tool now decompresses `.m3g` through LZHAM, loaded at runtime the same
way Zstandard already is:

* **`BUILD_LZHAM.bat` builds it for you**, and `BUILD.bat` offers to do it at
  the end of a normal build. It fetches the LZHAM source and compiles
  `lzham_x64.dll` into `dist\` with the compiler it already downloaded, one
  file at a time with the file name printed, so you can see it working. LZHAM
  is a 2013 codebase written for Visual C++, so two source trees and two sets
  of defines are tried in turn and the first one that compiles wins. If none
  of them do, everything else is unaffected and it says so.
* **The GitHub build makes one too.** The workflow builds `lzham_x64.dll` with
  Visual C++ and puts it in the artifact next to the exe, so the Actions
  download needs nothing further.
* **Or point the program at one you already have.** **File → Locate
  lzham_x64.dll...** takes a DLL from anywhere on the disk, loads it then and
  there, and remembers it - the same arrangement as vgmstream. It says at once
  whether the file really is an LZHAM decoder instead of failing later, so a
  32-bit or wrong DLL is caught immediately.
* Or put **`lzham_x64.dll`** (or `lzham.dll`) next to `MonkeyTool.exe`
  yourself - source at <https://github.com/richgel999/lzham_codec>. It is also
  picked up from `dist\`, `lzham\` or `tools\` beside the exe.
* The status line says which codecs are loaded once a game is open, the About
  box repeats it, and `MonkeyTool.log` records where the DLL was loaded from.

A LZHAM stream does not carry its own settings, and the game writes none of
them down, so the reader sweeps what it cannot know - stream offset, dictionary
size, table update rate, the unbuffered flag - best guess first. A wrong
setting never yields wrong data: the decoder refuses the stream or stops short,
and both the length and the Adler-32 are checked. The whole sweep costs
milliseconds.

**What is still unverified, plainly:** the identification is solid, and the
decoder is wired into both `.m3g` and LZHAM cabinets, but the decode itself has
never been run - this machine cannot reach the LZHAM source to build the
library, so there was nothing here to run it against. The first real test is
the one on your PC. If a model still refuses after `lzham_x64.dll` is in place,
the text dump (Save as -> Text) now prints the header and the codec status, and
that dump is what to send back.

The same code path also unlocks **LZHAM cabinets** inside `.pack` archives,
which earlier versions skipped.

### The China build (Tencent, `com.tencent.tmgp.NFSNL`)

* Its OBB (`main.<n>.com.tencent.tmgp.NFSNL.obb.png`) is the same FMOBB container as the other
  builds, but most files are packed with **LZMA**: the index marks them with flag 1 and the
  data starts with the usual properties byte (`5D`), the dictionary size (4 bytes) and the
  range-coded stream, with no size field (the index gives it). `src/nfsnl_lzma.cpp` decodes it.
* `.sb` files decrypt with the usual key, then hold `ZBIN` + gzip, inside which is an SBIN
  version 3 file without a `STRU` block. Their text export works; the tool does not yet read the
  colours or car setups from them, so China cars open with the plain textures.
* The base OBB carries only the car's small `_reflection` masks (64 × 64, magenta paint, yellow
  lights); the full colour textures come with the game's in-game download, which is not in the
  APK.
* Its cars are `.m3g` models that name their meshes the `.sb3d` way (`mesh_<part>_<kit>`), so
  body kits are worked out after loading, as for `.sb3d` cars.
* A police livery `.sba` that names a plain texture falls back to `_diffuse_reflection` /
  `_diffuse` versions of the name.

### Body kits in exports

A car with body kits is saved the way the viewer shows it (the kit chosen in the list) or with
every part of every kit visible - the export asks. The command line takes
`one <car> <out> --kit stock|all|<letter>`.

### JSR-184 M3G (No Limits' older builds)

This is the general reader, the one that finds its offsets by trial. Real
Racing 3's `.m3g` has an exact reader of its own - see the Real Racing 3
section below - and is tried first; this one stays as the fallback for the EA
variants and for anything the exact reader declines.

Three container identifiers are
recognised, plus EA's little gzip wrapper (`1F 8C <size> <gzip>`):

```
AB "JSR184" BB 0D 0A 1A 0A      stock JSR-184
AB "IM2M3G" BB 0D 0A 1A 0A      EA / Firemonkeys
AB "IM3M3G" BB 0D 0A 1A 0A      EA / Firemonkeys
```

The object payloads are not laid out identically across those variants - the
`Object3D` header grows with animation tracks and user parameters, and the EA
builds keep the mesh name in a user parameter - so rather than hardcoding one
layout per variant, each record is scanned for the offset at which it parses
*and consumes its payload exactly*. Component sizes, index encodings and
cross-references between objects all have to agree, so a wrong offset
essentially never validates. Mesh names are recovered from the EA user
parameter when present.

Texture coordinates in stock `JSR184` files come out at four times the scale
the field states; the same correction is applied here as in
[M3G2FBX](https://github.com/RaduMC/M3G2FBX), which is the reference for these
files.

### Texture encodings

**Every `.sba` states its own codec - nothing is guessed.** The DATA chunk
holds one record per mip level, five words each:

```
0, ImageFormatType, dataIndex, width, height
```

and the file's string table spells the enum out in full:

```
default RGBA RGB PVRTC_2BPP_RGB PVRTC_2BPP_RGBA PVRTC_4BPP_RGBA PVRTC_4BPP_RGB
DXT1 DXT3 DXT5 ATC_RGB ATC_RGBA_Explicit ATC_RGBA_Interpolated ETC_RGB PNG JPEG
LZ4_Indexed8 ASTC_LDR_4x4 ... ETC2_RGB ETC2_RGBA EAC_R11 ... RGB565
```

That list is **not the same in every build**. The older packs stop at JPEG and
order it `default, RGB, RGBA, ...`; the newer ones add the ASTC / ETC2 / EAC
entries and swap the first two to `default, RGBA, RGB`. So the enum cannot be
hardcoded - each file's own table is read, and because the anchor the values
count from is not stated anywhere, both plausible anchors are tried and the
payload's byte rate settles it: a codec whose size does not match the blob is
not the codec.

| Codec | Bytes |
|-------|-------|
| PVRTC 4bpp (RGB / RGBA) | width x height / 2 |
| PVRTC 2bpp | width x height / 4 |
| ETC_RGB, ETC2_RGB, DXT1 | width x height / 2 |
| DXT3, DXT5, ETC2_RGBA | width x height |
| RGB / RGBA / RGB565 | 3, 4 or 2 bytes per pixel |

PVRTC is not like ETC or DXT. Those store each 4x4 block independently, so a
wrong guess produces obvious garbage. PVRTC stores two endpoint colours per
block and **interpolates them bilinearly across neighbouring blocks**, with the
blocks laid out in Morton order rather than raster order. At 4bpp its block
data is exactly the same size as ETC1, so nothing but the decode distinguishes
them - which is why guessing produced a recognisable but dark, dithered,
blocky image instead of an obviously broken one.

A bare `.pvr` file - one SBAbrute wrote, for instance - can be dropped on the
tool directly and converts like any other texture.

### The ETC1 selector order

ETC1 stores, per 4x4 block, a base colour and a table index; each pixel then
picks one of four modifiers from that table. **The four entries are not in
ascending order.** A selector means:

```
0 = +small    1 = +large    2 = -small    3 = -large
```

so the table row for index 0 is `{2, 8, -2, -8}`, not `{-8, -2, 2, 8}`.
Sorting the rows - which looks like tidying up - swaps the two *bright*
selectors for the two *dark* ones. The image keeps its structure perfectly,
because the per-pixel pattern is untouched; it just comes out far too dark,
with the mid-tones crushed into black and only hard edges surviving. It reads
as a badly compressed texture rather than as a decoder bug, which is exactly
why it survived several passes over the texture code.

### Materials and render states

The `.sb3d` string table carries the material names the artist used and the
texture each points at:

```
paint_shader_opaque        ../../../textures/cars/common/texture_paint.sba
interior_opaque            ../../../textures/cars/<car>/..._interior.sba
glass_taillight_tint_alpha_shatter_cull_nozwrite_layer65
taillight_alphaadd_layer70_nozwrite
```

Those names are not decoration - they spell out **how the engine draws the
material**, and that is the part worth exporting:

| In the name | What it means | Exported as |
|-------------|---------------|-------------|
| `opaque` | no blending | `NFS_BlendMode = opaque` |
| `alpha` | alpha blended | `NFS_BlendMode = alpha`, TransparencyFactor |
| `alphaadd` | added to the frame - glowing lights | `NFS_BlendMode = additive`, EmissiveColor |
| `twosided` | no backface culling | `NFS_TwoSided = 1` |
| `nozwrite` | draws without writing depth | `NFS_DepthWrite = 0` |
| `layerNN` | draw order | `NFS_Layer = NN` |

A material also picks up the sibling maps the model references - `..._normal`
and `..._reflection` next to the diffuse - wired as NormalMap and
SpecularColor, and gets a phong shading model when it has a reflection map
and lambert when it does not.

**On "native shaders":** the advice you were given is right, and this is the
useful half of it. Reproducing the game's shaders would mean writing a
renderer, and a hand-rolled renderer is worse than the one Blender already
has. What a converter owes the renderer is the *inputs*: the full texture
set and the render states above. With those, Blender's own shading gets the
glass transparent, the taillights glowing and the normal maps working -
without this tool containing a single line of shader code.

**How a mesh is matched to its material:** the binary link is somewhere this
reader does not decode yet, so the match is made on names - a mesh is called
`mesh_<tag>_<part>_<lod>` and the tag is the same word the material is named
after. The tag is consumed greedily in chunks, so compounds resolve correctly
(`mesh_glasstail_*` goes to `glass_taillight_...`, not `glass_headlight_...`).
Collision hulls get no material. It is right for nearly every mesh, but it is
a name match, not a decoded pointer.

### LOD grouping

Meshes are parented to one null per LOD, so a model opens as

```
LOD00 -> mesh_paint_hood_a_lod00, mesh_chrome_..., ...
LOD01 -> ...
```

rather than one flat list. Meshes whose name carries no `_lodNN` are grouped
under `MESHES`. OBJ exports get the same grouping through `g` statements, plus
`usemtl` per mesh.

## Real Racing 3

A different game with almost nothing in common with No Limits, so it is a
profile plus four new readers rather than a second program. Point the tool at

```
com.ea.games.r3_row\files\.depot
```

(or at the folder above it - the tool walks down to `.depot` itself).

### No archives, just files

There is no `.pack` here. `.depot` is a folder of loose files, and nearly all
of them are zlib-wrapped:

```
.z        uint32 uncompressed size, then a zlib stream
.z.bin    a chain of those: uint32 packedSize, uint32 plainSize, zlib payload,
          and the next block starts packedSize + 4 bytes on
```

The old route was QuickBMS with `real_racing_3.bms`. Both wrappers come off
here on the way out of the library, so the tree shows
`1979_porsche_935_misc.etc.dds` and clicking it gives you the texture. A newer
variant puts a 16-byte `FF FF FF FF` marker in front of the chain, which the
QuickBMS script does not handle; that is recognised too.

Shadow textures are `.z.bin` with one frame per sun angle - 22 frames for the
Porsche 935's exterior shadow, every one the same 512x512 image. The first
frame is what you see and what gets saved.

### Textures without PVRTexTool

An `.etc.dds` is a standard 128-byte DDS header. The FourCC at offset 84 says
what follows: `ETC ` for ETC1 blocks, or nothing at all for an uncompressed
16-bit format. Both decode here, so the `real_racing_3_dds.bms` -> `.pvr` ->
PVRTexTool detour is not needed. The Adreno build's `.atc.dds` (ATITC, for
Qualcomm GPUs) decodes too: FourCC `ATC ` is RGB, `ATCA` RGB with explicit
4-bit alpha (laid out like DXT3), `ATCI` RGB with interpolated alpha (like DXT5).
Importing a picture into one writes the same format back.

**Two UV sets.** A track's meshes carry a second set of texture coordinates for
the baked lighting (lightmaps). Both are read; FBX exports write the second as
`UVChannel_2` (OBJ has room for one set only).

**The channel order is read, not assumed.** The header carries a mask per
channel, and Real Racing 3 puts red in the *high* nibble:

```
R 0x0000F000   G 0x00000F00   B 0x000000F0   A 0x0000000F
```

0.7 assumed the opposite and so swapped red with alpha and green with blue,
which is why the Koenigsegg badge sheet came out magenta and cyan. 0.7.2 works
the shift and width out from whatever masks the file states, so 16-bit and
32-bit layouts in any order all decode the same way.

**They are stored bottom-up**, like everything else both games ship, and are
turned over on the way out. The proof is in the file itself: decoded as stored,
`2013_koenigsegg_agerar_badges.etc.dds` reads "KOENIGSEGG" upside down.

### Models: a real JSR-184 M3G

Confusingly, Real Racing 3's `.m3g` and No Limits' `.m3g` are unrelated. No
Limits uses the extension for its own `DA BD` wrapper around an LZHAM blob;
Real Racing 3 uses it for a genuine JSR-184 file. The magic decides which
reader runs, so nothing is guessed.

The reader follows the community's
[M3G2FBX](https://github.com/RaduMC/M3G2FBX) layout: sections of
length-prefixed objects, of which `20` (VertexArray), `21` (VertexBuffer),
`11` (TriangleStripArray), `14` (Mesh) and Firemint's `24` (material name
list) are what a model is made of. Positions come out of the vertex buffer's
own bias and scale, so parts land in the car's space; material names come
from the list, so a mesh arrives as `LOD_A_DOOR_LEFT_mm_cab` with
`Vehicle Exterior_mm_cab` on it rather than as `material_7`.

### `.points` - where the wheels and the steering wheel go

This is the part every other importer skips, and it is why imported Real
Racing 3 cars come out with the steering wheel lying in the middle of the
floor and no wheels at all.

A car's exterior model **contains no wheels**. It declares the wheel, tyre,
rotor and wheel-blur materials and uses none of them - the game supplies the
wheels from elsewhere and puts them where the `.points` file says. Several
parts that *do* ship with the model - the steering wheel, the rev-counter
needle, the driver's hands - are modelled at the origin for the same reason.

The file is small and rigid:

```
uint16  version (2)
uint16  count
count x {
    uint16  type      0 = point, 2 = hinge
    uint16  length    name + payload
    char    name[]    NUL-terminated, e.g. POINT_WHEEL_FL
    float   pos[3]
    float   basis[9]  hinges only: the frame the part swings in
}
```

The units were worked out against geometry rather than assumed. A value is
**1/32 of a model unit**, and the axes are the authoring program's rather than
the model's, so

```
model.x =  points.x / 32
model.y =  points.z / 32      (up)
model.z = -points.y / 32      (along the car)
```

Checked on the 1979 Porsche 935: `POINT_BRAKELIGHT_LEFT` converts to
`(-0.459, 0.473, 1.849)` and the `LOD_A_BRAKES_LEFT_mm_lights` mesh actually
sits at `(-0.459, 0.471, 1.841)` - **eight millimetres out on a 4.7 m car**.
The headlight, mirror and exhaust points agree to the same accuracy, which is
what makes this a measurement rather than a guess.

What the tool does with it:

* **Parts modelled at the origin are moved onto the point they belong to.** A
  part only qualifies if its bounding box is actually wrapped around zero and
  small, so the body, the doors and the mirrors - which are already in the
  car's space - are never touched.
* **Wheels are attached** from `<car>_shared.m3g` (see *Wheels from
  `_shared.m3g`* below). Each corner is scaled so the tyre's radius matches
  the height its hardpoint sits at. On the 935 that gives 0.301 m front and
  0.319 m rear: the rear tyres really are taller. Any other wheel model is
  only used if it is disc-shaped, so a wrong guess is never attached four
  times over.
* **Every hardpoint is exported.** In FBX they are empties under a
  `HARDPOINTS` null, at the right positions, with a hinge's axes kept as extra
  properties. In OBJ they are named single vertices. So even where the tool
  cannot find a wheel model, you can drop your own onto `POINT_WHEEL_FL` in
  Blender or Max and it lands exactly where the game puts it.

Select a `.points` file on its own and the text view lists every hardpoint in
model units.

### Wheels from `_shared.m3g`

`<car>_shared.m3g` holds the corner assembly in six LODs: `TYRE`, `WHEEL`
(the rim), `ROTOR` (the disc), `BRAKE_CALIPER_FRONT/REAR_LEFT/RIGHT` and the
`BLUR` stand-ins used while the wheel spins. Some cars add `WHEEL_REAR` and
`ROTOR_REAR` for a different rear size (the C11 does). Measured on the Targa
and the C11, then checked against the 935's and the Agera's bodies:

* Every piece is modelled in one wheel frame. **x = 0 is the outer face of
  the wheel**, where the spokes are, and **+x runs inward** towards the
  brakes: the Targa's hub sits at x 0.00..0.02 and its disc at 0.064..0.104.
* `POINT_WHEEL_xx` is on the outer edge of the body. The 935's front point is
  at x = 0.885, and the body there ends at 0.896. So the x = 0 plane goes
  exactly on the point. Nothing is centred.
* Unchanged, the frame is the **left** side of the car (x < 0, inward is +x).
  That is why the calipers named `LEFT` are the ones at +x. The right side is
  the same wheel **turned half a turn** about the vertical axis. It is not
  mirrored, so the lettering on the rim still reads correctly.
* The calipers do not turn with the wheel. `FRONT_LEFT`, `FRONT_RIGHT`,
  `REAR_LEFT` and `REAR_RIGHT` are each already in place for their own
  corner, at the back of the disc.
* The finest LOD is used; `BLUR` meshes are left out.

On the 935 with the Targa's shared file, the front-left tyre spans
x -0.884..-0.690 and y 0.002..0.601: it touches the ground and sits inside
the arch. Each corner is grouped as `wheel_FL`, `wheel_FR` and so on in the
FBX.

### UV maps and texture orientation

Two separate things had to line up here, and 0.7 got both halves of the Real
Racing 3 case wrong.

**Scale.** A vertex buffer states a texture-coordinate scale of `1/512`, and
that is four times the real one. Measured rather than assumed: on the Agera R's
body the raw `u` runs 283..2028 and the raw `v` -3150..-101, which is
0.14..0.99 and -1.54..-0.05 at `1/2048`, and four times too large at `1/512`.
The 3ds Max script everyone uses divides by 2048 for the same reason. With the
correction, 96.8% of the Agera's 34,115 coordinates land inside 0..1 and the
rest are honest tiling - before it, the unwrap was a quarter of the sheet,
which is what showed up in the Edit UVWs window.

**Direction.** `v` runs the other way in the file, and the texture itself is
turned over when it is decoded, so the two together come to one `1 + v`.

For No Limits the same rule applies through a different route: the game stores
its `.sba` payloads bottom-up whatever the payload is, so every decoded image
is turned over once and the model readers are told not to flip `V`. The pair
stays consistent, so the 3D viewer and any FBX/OBJ you export look exactly as
they did, while the preview and the saved `.png` are now the right way up.
Saving as **raw** still writes the payload byte for byte, upside down and all -
that is what raw means.

### Parts that are modelled at the origin

A Real Racing 3 car does not arrive assembled. Three separate things place it,
and all three now run when you open or export a model:

* **`.points`** puts the steering wheel, the rev-counter needles and the
  driver's arms on their hardpoints, and the wheels on `POINT_WHEEL_*`. Both
  arms (`HAND_STEER` and `HAND_GEAR`) are modelled around the steering wheel.
  On the 935 they are mirror images at x ±0.18, which is the rim's radius, so
  both go on `POINT_STEERING_WHEEL`. `POINT_GEARSTICK_HAND` sits within 0.15
  of the origin: it is the offset the game moves the right arm by for a gear
  change, not a place. A part named for a point is moved when its centre is
  nearer the origin than the point, so the arms, which hang 0.2 to 0.3 off
  their pivot, are caught as well. An interior with no `_int.points` of its
  own uses the car's main `.points`.
* **The outside model's steering wheel** (`LOD_A_STEERING_WHEEL_mm_cab`) has
  no point of its own: the exterior `.points` has only `POINT_DRIVER`. It is
  not modelled around the same centre as the cockpit's wheel.
  * On the 2018 911 GT3 RS the cockpit's wheel spans z +0.04..+0.12 off its
    point, and the outside one spans -0.11..+0.02.
  * So the cockpit's point alone pushes the outside wheel 12.8 cm into the
    dashboard.
  * The tool places the cockpit's wheel (`<car>_int_a.m3g` on its
    `_int.points`) and centres the outside wheel on it, one group per LOD.
  * Shifts seen: 935 0.3 cm, Evija 1.4, SF24 1.4, RX-3 5.9, Megane 7.7,
    Zenvo 8.7, Taycan 11.7, GT3 RS 12.8.
  * A shift over 30 cm is not made.
  * Without the cockpit model the point is used as before.
* **Hinges turn what they carry.**
  * A hinge record (type 2) holds a 3x3 basis after its position. Its rows are the part's
    forward, left and up in the car's axes.
  * The Ferrari SF24's `HINGE_STEERING_WHEEL` is exactly the frame the part is modelled in:
    forward (0,0,-1), left (-1,0,0), up (0,1,0).
  * The Taycan Turbo GT's up is (0, 0.966, -0.259), a 15-degree rake with the top towards the
    dashboard.
  * The part turns by `R = B^T * M0`, where M0 is that neutral frame.
  * The game's hardpoint names (libRealRacing3.so, a table of 79) include `POINT_STEERING_WHEEL`
    (6) and `HINGE_STEERING_WHEEL` (57).
  * A plain point states no angle, and the game has no rake setting of its own.
* **Needles** turn on `HINGE_NEEDLE_SPEED` / `HINGE_NEEDLE_RPM`, the dial's centre and the dial
  face's tilt.
  * `POINT_NEEDLE_*_1`, `_2` and `_3` are marks round the dial's edge. On the Megane, `_1` is
    the zero at the lower left and `_3` the top of the scale at the lower right.
  * A needle at rest points at `_1`.
* **`<car>_shared.m3g`** is where the wheels live (see above).
* **`<part>.banim`** carries the rest pose of a part that moves. The Agera R's
  rear wing is modelled at the origin - x +/-0.918, y -0.019..0.093 - and its
  `_wing.banim` places it at (0.000, 0.843, 1.902), which sits it on the rear
  deck at roof height, where the game has it. A rest position that falls
  outside the model is rejected rather than applied.

Anything still sitting on the origin after all three is named in the preview
rather than quietly left there.

### Real Racing 3's scrambled files: `.nct`, `.gui` and the text files

`106_2011_pagani_huayra.bin.nct` is a car's data file,
`1969_dodge_charger_rt.liveries.bin.nct` a car's livery list, `data/events.dat.nct`
the career and `custom_menu_japan_gp.gui` a menu. All of them are scrambled the same
way: **one keystream XORed over the file from byte 0, the same keystream for every
file**, so encoding and decoding are the same operation.

The keystream is **RC4 with a non-standard start**. The game's loader (in
`libRealRacing3.so`, at `0x12bc330` in the arm64 build of 14.0.1) does this:

1. It fills the 256-byte state from a table in the library instead of the usual
   0..255. The table is in `src/nct_rc4.inc`.
2. It runs the RC4 key schedule over that state with an 8-byte key
   (`a5 35 b3 b1 e8 43 e7 cf`).
3. It runs 133 output steps and throws their bytes away (i = 1..133).
4. It XORs the following output bytes over the file.

The first output bytes are `da d5 18 a8 2c 06 85 ed`, the same bytes earlier versions
of the tool had recovered statistically from FlatBuffers headers.

`gametext.txt` and `text_<language>.txt` use the same starting table with their
own key (`b1 75 a5 23 f3 c2 b6 15`) and drop 211 bytes (loader at `0x109ac28`).
`gametext.txt` holds the keys, one per line (`GAMETEXT_CAR_NAME_HUAYRA`); each
language file holds that key's text on the same line. The loader skips decoding
when `gametext.txt` already starts with `GAMETEXT_`.

**Editing.** Save a `.nct` as **bin** (or a `.gui` as **xml**), edit it, and use
**Tools → Encode an edited file back**: the whole file comes out exactly as the game
reads it. The text files open in the data editor as text and are scrambled again
when saved.

### What the game believes it has: `asset_list_base.txtCache.txt`

| Part | What |
|------|------|
| u32 `0xA4F6F39D`, u32 4 | a fixed magic and the cache version (`AssetManager::LoadCacheList`). The magic is not a checksum |
| a line | the asset list version, e.g. `7469` |
| lines | `/path<TAB>md5` for every file downloaded, md5 of the file as stored |

New files the tool writes are added with their own md5. A file the list already
names keeps the official md5 by default. The game decides what to download by
comparing the server's list with this one, so a changed file stays changed.

### A car's data: `vehicles/data/<id>_<car>.bin.nct`

Decoded, a car's data file is a run of little-endian fields, each a 4-byte number
or a string (u32 length, the characters, no terminator; an empty string is four
zero bytes). The order is the same in all 540 cars, so the fields are found by
their place:

| Fields | What |
|--------|------|
| u32, u32 | 0; the car id (as in the file name) |
| 3 strings | year, make, model |
| u32, string | 100; class (`Race`, `Supercar`, `Prod`) |
| 13 numbers | u32, u32, weight (lb), top speed and top speed fully upgraded (mph), u32, u32, then floats: 0-60 mph (s), braking 60-0 (ft) and cornering grip (g), each stock and fully upgraded |
| string | drivetrain (`RWD`, `AWD`, `FWD`, `RLY`) |
| 13 fields | the engine sound: accel and decel sample paths (`audio/Vehicles/<set>/id_<id>_accel`), idle, three levels, u32, the `bf_` and `_ovrun` families, three numbers, one more string |
| ... | transmission and shift sounds, cameras, turbo sounds |
| 2 strings | aspiration (`Turbo`, `Nat`, `Super`, `None`), engine position (`F`, `M`, `R`, `FM`) |
| 13 + gears numbers | gears, u32, top gear ratio, final drive, power (hp), power rpm, torque (lb-ft), torque rpm, redline, limiter, shift up, shift down, one ratio per gear, reverse |
| string | upgrade / handling package (`PROD_04`, `SUPER_03`, or the car's own); its upgrades are in `carupgrade.upgrades.nct` under that name |
| string | unlock condition (`QUEST:...`, `ROUND:...;LTS:...`), empty for a car sold at the dealer |
| 3 u32, u32 | 0 x 3; the **appearance index**, which picks the car's model folder (below) |
| u32, u32, 3 u32 | 0, a date (unix, some cars), 0 x 3 |
| u32, u32, ... | price (R$), price (gold), ..., 86400, 1 x 5 |
| 2 strings | `CAR_MANUFACTURER_<x>`, `CAR_NAME_<x>`: the keys the menus look the names up by |
| 3 u32, 2 floats | performance rating, stock and fully upgraded |

Checked against public figures: the 2010 BMW M3 GTS says 3538 lb, 190 mph, 4.4 s,
444 hp at 8300 rpm and 325 lb-ft at 3750 rpm; the Pagani Huayra 2976 lb, 730 hp,
738 lb-ft, PR 65.4. `monkeytool_cli rr3car show <file>` lists them; re-written
field by field, 535 of the 540 files come back byte for byte.

Re-written field by field, all 540 files come back byte for byte. The menus show
the names through the `CAR_NAME_` / `CAR_MANUFACTURER_` keys (`GAMETEXT_` + key in
the text files).

### The car list and what a car needs

The data file never names the car's model folder. The appearance index links them,
and that is why `7_2013_chevrolet_camaro_zl1.bin` has its models in
`vehicles/2013_chevrolet_camarozl1/`.

| File (under `vehicles/data/`) | Layout |
|------|--------|
| `cardesc.list.nct` | every car's data file name: u32 length + `<id>_<name>.bin`, sorted by id, no count |
| `carappearances.appearances.nct` | u32 23 (version), u32 count, then per car: u32 appearance index, the model folder name, then its look (exhaust flames, lights...). Records vary in length |
| `cardamagethresholds.damage.nct` | u32 count, then 116 bytes per car, starting with the car id, sorted by id |
| `suspensionbehaviour.suspensiondata.nct` | u32 1, u32 count, then 140 bytes per car, starting with the car id |
| `liveries/<folder>.liveries.bin.nct` | below |

**A liveries list:** the folder name, then:

1. The texture table: u32 count, then each entry as u32 slot and a path such as `livery/<folder>_ext_black.ptc.pvr.z`. The device reads `.etc.dds.z` / `.atc.dds.z` in place of `.ptc.pvr.z`.
2. The mesh-to-material table: u32 count, then (u32, u32) pairs.
3. A third table: u32 count, then (u32, u32, u32) triples.
4. The liveries: u32 count, then each livery as:
   * the file name, `01_<folder>.livery`;
   * the **name the game shows** (often empty: the colour's own name is used);
   * 8 bytes;
   * swatch picture, then team logo;
   * a byte;
   * driver arm texture, then driver suit texture;
   * 4 bytes;
   * the livery button's colour, as 3 bytes R G B;
   * its texture overrides (u32 count, then slot + path);
   * its material overrides (u32 count, then pairs).

Slots and materials are indices into `liveryStringTable.bin.nct` (u32 count, then
strings such as `ext_ext`, `car_paint_metallic_white`). All 540 lists read to their
last byte.

**A new car** (`rr3car add`, the car creator's "Add a new car") is all of these:

* the donor's folder copied under the new name. The model files do not name their
  folder, so a rename is enough;
* a new data file with the next free id and a new appearance index;
* the liveries list renamed and rebuilt;
* one more entry in each list above;
* the name key added to `gametext.txt` and every language file;
* the new files added to the asset list.

### The career: `data/events.dat.nct`

`RRCE`, version 0x82 (loader at `0xc74fb8`). Little-endian; strings are u32 length
plus bytes; lists are an i32 count followed by the elements. Read in this order:

1. 3 x 17 lists of f32: the reward and fame scale per finishing position, indexed
   by event type.
2. Tracks: display name, f32, bool. This is the track list events may use.
3. **Series** (1602): id, title key, type (7 = limited time), description key,
   three (x, y) curves, requirement groups (type, a, b, text: `SERIES`,
   `UNLOCK_IF_OWN_CAR`...), i32, 2 bools, completion reward (`car:219`), reward id,
   i32, bool.
4. **Tiers** (21,767). Each has:
   * name key, display key, index in its series, tier id, series id, unlock type,
     trophies needed, bool;
   * the **allowed cars** (i32 car id + a flag byte);
   * sponsor and two trackside banner sets;
   * the **events**. Each event has its type (0 elimination, 1 head to head,
     2 cup, 4 endurance, 7 speed record, 10 speed snap, 11 drag race, 13 time trial),
     event id, the **track's display name**, laps, three i32, fame, reward,
     currency, a parameter string, then 30-odd tuning fields (one optional block:
     a byte, then u8 + 2 f32 when it is not 0).
5. The career menu. Super groups (Motorsports, Career) each hold a tree of group
   nodes:
   * name key, flavour key, card picture, logo, loading screen, brand logos, asset
     packs, locked message;
   * then children;
   * then, for types 0 and 1, **pages of series ids**. Type 2 nodes (the F1 hubs)
     carry their own lists.
6. AI name pools (count before name), AI grids, AI drivers, rolling starts,
   limited-time series (they point at a series by its **index** in the list, so new
   series go at the end) and reward schedules.

The tool reads and writes it byte for byte. A new series:

* copies a template series, with new ids for the series, its tiers and its events;
* gets its allowed cars on every tier and its events on the chosen tracks;
* is listed on a page of a group, or of a new group in the Career menu.

A new group's card is a page added to `gui_assets/<size>/events/career/sprites.android.atlas`:

* the image is added with `<pos id="n">`;
* the page itself is `sprites_<n>.rgb.pvr.z`: PVR (v2 header), 32-bit RGBA, rows
  bottom-up, `.z`-wrapped.

### Tracks: `Tracks/data/<base>-<layout>-<time>-<id>.bin.nct`

The game lists every file in `Tracks/data/`. Each is a run of (u32 key hash, u32
length, value). All 167 files have the same 123 keys in the same order, among them:

| Key hash | Value |
|----------|-------|
| `0xf8197f89` | base name |
| `0x16471b2a` | layout |
| `0x13ed3c91` | time of day |
| `0x9acfeddd` | id |
| `0x99d35de4` | display name, which career events use |
| `0xd4533915` | name key |
| `0x9103eb0b` | location key |
| `0xb8fb40ed` | folder (`Tracks/Laguna_Seca`) |
| `0xb2172fc9` | country |

Other keys hold the sky, music, cutscene and grid paths.

`Tracks/data/ttk.dat.nct` is u32 1, u32 count, then (u32 id, md5 string): the md5 of
the layout's collision mesh `.fmc` for each track id. Variants of a layout share one.

**A new track** (`rr3track add`, Tools → New track) is a variant of one of the game's
tracks:

* a new id and name;
* its ttk entry;
* an entry in the career's track list;
* its text;
* optionally, its own copy of the donor's folder, whose models an OBJ / FBX can
  replace by mesh name.

The collision mesh, the AI line (`.rr_ai`, `.spline`) and the timing stay the donor's.

### Searching the tree

A full No Limits install indexes about 72,000 assets, and Real Racing 3's
`.depot` is comparable. The **Search** box above the tree filters on the file's
own name rather than the whole path, so `bmw` finds the BMW assets instead of
everything under a folder with those letters in it. Several words all have to
match, in any order: `porsche dds`. Typing only restarts a short timer, and the
tree is rebuilt once you stop, so it does not stutter on a large install.

## NFS Undercover, Shift and Shift 2 Unleashed (iPhone)

* **Models:** standard JSR-184 M3G 1.0. Shift wraps every `.m3g` in gzip.
  * Each mesh hangs from a Group, and the Group's user ID names the part. The numbering is
    shared by all three games:
    * below 100: the car as it ships, except 25 (the wide arches and sills, a body kit part);
    * 100-137: the first body kit's versions (126 its arches), 160-199 the second kit's;
    * 138-159: bonnet scoops and spoilers;
    * 1000 and up: Shift 2's extra parts.
  * Empty Groups with a place of their own are locators. The wheels hang from user IDs 32, 34,
    132 and 134.
* **Textures:** `texture_*.m3g`, each holding one Image2D.
  * Formats 124 (RGB) and 125 (RGBA) are PVRTC 4 bpp with the whole mip chain after the first
    level. Formats 99 and 100 are RGB and RGBA bytes.
* **Car textures.** A car's meshes name `car_<name>_ext_01.pvr` only; the game binds a texture
  per appearance, by what its parts are:
  * the appearance holding parts 25 / 126 (arches, sills): `texture_car_<name>_bodykit.m3g`;
  * holding 131 / 170 / 172 (racing livery shells): `texture_car_<name>_livery.m3g`;
  * only shop parts (138-159), Undercover: `texture_spoilers.m3g`;
  * only 128 / 130 with height (Undercover's vinyl shell): `texture_vinyls_01.m3g`;
  * everything else: `texture_car_<name>.m3g` (or its first livery, `_01`).
  * Traffic and police cars have no texture of their own:
    `texture_civilian_and_cop_mastertexture.m3g`.
  * Livery variants: `_01` to `_07`, plus `_garage` and `_garage_bodykit` for the menu.
* **Wheels:** `wheels.m3g` holds every rim (each in its own Group, radius 4.7). The car's left
  wheels go on locators 32 (front) and 34 (rear); the right ones are their mirror. User IDs 38-60
  draw from `texture_wheels_01`, 61-80 from `_02`, 81-99 from `texture_wheels_generic`, Shift 2's
  1100 and up from `_03`.
  * Which rim a car wears is in `cars.bin`. Shift: per car 8 paint entries (23 bytes: `@GL`, four
    ids, RGB, `000`), the rim's u16 30 bytes after them. Shift 2: 25-byte paint entries, the rim
    49 bytes into the block after the group. Undercover: a u8 count, then 125-byte records, the
    rim in column 28. The records do not name their car; the tool matched them by the paint
    colours (against `texture_car_<car>_01..07`) and the rims' centre-cap logos, and keeps the
    result as a table (`eaPhoneCarWheel`).
* **Shift 2's extra parts:** an appearance holding only options 1001-1019 (wings) draws
  `texture_spoilers.m3g`; one holding 1020-1029 (exhausts) draws
  `texture_car_<car>_exhaust.m3g`, and the untagged (user ID 0) cards in that appearance are the
  flame and heat effects, hidden in the viewer.
* **Cockpit (Shift):** `cockpit_<car>.m3g`: 250 the dashboard card (`texture_cockpit_<car>`),
  251 the steering wheel (`texture_steeringwheel_<car>`), 100-199 windscreen cracks.
  `bonnet_<car>.m3g`: `texture_bonnet_<car>`. `Cockpit_HandL/R`: `texture_cockpit_arm_01`.
* **Tracks are kits.** A location's `.m3g` holds road pieces, all at the origin, each in a Group;
  an event's layout file places them. A piece is named by its Group's place among the file's
  Groups. All numbers are big-endian.
  * Undercover (`<location>_<event>.bin`): u8 tile count, then per tile f32 x, f32 z (times 10
    in model units), u16 turn in degrees, u16 mirror (bit 0 mirrors x, bit 1 z), u8 0, u8 kind,
    u8 n, n × (u16 id, u16 piece). Other data follows the tiles.
  * Shift (`chicago_0N.bin`) and Shift 2 (`locationc_0N.bin`): six bytes (mostly
    `00 FF 00 FF 00 FF`, but some of Shift 2's - `locationb_02`, `locationc_06`, the night tracks -
    have other values there), u8 tile count, then the same head (Shift 2 has two more bytes
    before n), n entries of 8 bytes (Shift 2: 9) with the piece at bytes 2-3, u8 m and m × 28
    bytes, then 20 bytes (`01`, six u16 neighbours, three u16), u8 q and q × 8 bytes.
  * The mirror's bit 1 is not a mirror in Shift 2: its tracks only join up when it is left out
    (bit 0 still mirrors x). The tool lays a track out both ways and keeps the one whose road
    pieces meet; Undercover's layouts come out the same either way.
* **Shift 2 track textures:** `texturelist_<track>.bin` gives each appearance six bytes: u8
  kind, u8, a u16 texture number, two bytes. The number indexes a table the game fills while it
  reads every texture list in name order (lightmap lists included), giving each texture it has
  not seen the next number. Which texture a new number stands for follows from the appearance
  that first used it:
  * the track's first appearance is its road: `texture_tracktex_<location>_alpha`;
  * the cut-out one (blending with an alpha threshold) is the people and trees: `_threshold`;
  * plain see-through ones are `texture_TyreMarks` and `texture_headlights_road`;
  * the rest are `_01`, `_02` ... in turn, and the night versions once the day ones are taken.
* **Skies:** `skydome_<location>.m3g` draws `texture_skyline_<location>` (`loca` is
  `locationa`). Undercover's `skydomes.m3g` has one appearance per location, in name order.
* **PNGs** in the `.app` are Apple's CgBI variant: a CgBI chunk first, raw deflate without the
  zlib header, BGRA with premultiplied alpha.

## If it misbehaves

**If the build window stops with nothing happening:** press **Esc**. Clicking
inside a console window puts it in selection mode and Windows suspends the
program running there; the title shows `Select` (`Wybierz`) while it is stuck.
0.61 disables that for its own window.

**If it stops at "Building LZHAM":** that step compiles a couple of dozen
files and now prints each one. It is also optional - close the window, run
`MonkeyTool.exe` from `dist\`, and everything except `.m3g` scene models
works. `BUILD_LZHAM.bat` retries it on its own.

**Two LZHAM compile errors, fixed in 0.7.2.** LZHAM is a 2013 codebase written
for Visual C++ and glibc, and building it with MinGW hit both:

* `'UINT16_MAX' was not declared in this scope` in `lzham_huffman_codes.cpp` -
  the file uses the limit macros without including `<cstdint>`. The build now
  passes `-include cstdint`.
* `'malloc_usable_size' was not declared in this scope` in `lzham_mem.cpp` -
  the file does `#define _msize malloc_usable_size`, which is right for glibc
  and wrong for MinGW, where `_msize` exists and `malloc_usable_size` does not.
  The build now passes `-Dmalloc_usable_size=_msize`, so the alias resolves
  back to the real function: `_msize` expands to `malloc_usable_size` expands
  to `_msize`, which the preprocessor stops expanding there.

Both were reproduced against g++ and checked before being put in the script.
That is why `lzham_codec` used to stop at file 3 of 18 and
`lzham_codec_devel` at file 6.

**If LZHAM fails to compile:** the full compiler output is written to
`dist\lzham_build.log`. Send that file rather than a screenshot - it names the
file and the line. The DLL from the GitHub Actions artifact works just as
well: drop `lzham_x64.dll` next to `MonkeyTool.exe`, or take **File → Locate
lzham_x64.dll...** to it wherever it is.

**If `.m3g` models still say they cannot convert:** the status line at the
bottom says `LZHAM: missing` when no decoder is loaded, and that is the whole
problem - the DLL is not a part of the exe and does not appear by rebuilding
the exe. `BUILD_LZHAM.bat` is the step that produces it.

The program writes **`MonkeyTool.log`** next to itself, one line per step:
which game was picked, which folder, how many archives were found, how many
assets were indexed, and what failed. A crash adds the fault code and address
before the program closes.

That file is the whole diagnosis - send it rather than a description, and the
fault can be found without guessing.

## Known limits

* **UI texture atlases** (`texturepacks_ui/*.sba`) use a different schema with
  `Box` / `source_rect` nine-patch records. Their dimensions are not resolved
  yet, so they are written as `.bin`.
  [ImageHeat](https://github.com/bartlomiejduda/ImageHeat) can open those by
  entering the size by hand.
* **ASTC / EAC** payloads are written out undecoded. PVRTC (2bpp and
  4bpp), ETC1/ETC2, DXT1/3/5, ATC, PNG, JPEG and raw pixel formats are decoded.
* **The mesh-to-material pointer is not decoded.** Materials and their
  textures come from the file, but which mesh uses which is matched by name
  (see above).
* **Compressed `.m3g` files need LZHAM.** The codec is identified and wired
  in, but no LZHAM decoder ships in the exe: `BUILD.bat` builds `lzham_x64.dll`
  beside it, and without that DLL these models are reported rather than
  converted (see above).
* **Engine sounds (`Gnsu20`) are not decoded.** The rev range, sample rate and
  grain table read correctly. The grain audio is not raw PCM (8-bit or 16-bit,
  either endianness: neighbouring samples barely correlate) and not IMA ADPCM -
  tried continuously and in blocks of 0x10 to 0x200 with and without per-block
  predictors, sixty configurations in all; the best result is the artefact you
  get from re-seeding a predictor, not a decode. At about 4.8 bits per sample
  with uneven nibbles it is a bit-packed codec of the game's own. Written out
  whole as `.gnsu`.
* **Wwise Vorbis sounds need vgmstream** to become wav; see Audio above.
* **Real Racing 3 additions are untested in the game.** New cars, series and
  tracks are written to the formats above and read back byte for byte, but they
  have not been loaded on a device. Unknown fields (most career event tuning,
  the livery flags) are copied from the car, series or track a new one starts from.
* **A new track keeps the donor's collision, AI line and timing.** Only its
  models and textures can be your own.
* **No Limits number plates:** the Cayenne's plate UVs, texture flip and
  handedness were each checked and are consistent. The A45 S plate that
  shows wrong has not been available here. Its `.sb3d` and plate texture are
  what is needed to find the cause.
* **`.banim` is read only for a rest position.** The curves, the frame table
  and the `AE_*` target list are parsed enough to find where a moving part
  sits at rest; the animation itself is not exported.
* **Real Racing 3 sounds and tracks are not specifically handled yet.** The
  containers come off and the files are listed; only cars have been worked
  through end to end.
* **Hinges are exported as data, not as rotations.** `HINGE_MIRROR_LEFT` and
  friends carry a 3x3 frame, which is written onto the empty as extra
  properties rather than baked into a rotation an importer might reinterpret.
* **`.sb` files stay encrypted** without the game's key.
* **Object data inside SBIN is not decoded field by field.** The text dump
  prints the schema, the enums and every name; walking `OHDR` to lay each
  object over its structure is still to do.
* Real Racing 3's readers were checked against a 1979 Porsche 935: the
  exterior (89 meshes, 52,240 vertices) and interior (30 meshes) convert, the
  ETC1 and shadow textures decode, and the `.points` conversion lands within
  8 mm of the geometry it names. The binary readers were additionally run
  under AddressSanitizer and UndefinedBehaviorSanitizer against about 16,000
  damaged inputs - truncations at every header boundary, single-byte flips and
  pure noise - without a single out-of-bounds read or runaway allocation.
* No Limits' `.m3g` support is written from the format specification and the M3G2FBX
  reference and verified against files generated here, in all five container
  shapes (stock, both EA identifiers, zlib sections, gzip wrapper). Nine real
  game files have since been read - garage, modshop, two skydomes, four
  roadblock transforms - and every one of them is a `DA BD` wrapper whose
  header, sizes and payload framing read correctly. The **inner** M3G reader
  has still not met real game data, because it cannot be reached until LZHAM
  unwraps it. That is the next thing to check once the DLL is in place.

---

## On texture quality

Four passes, and the first three conclusions were wrong:

1. *"Decoding matches SBAbrute pixel for pixel; the difference is in the source
   data."* Wrong - only true of the PNG-carrying packs that were on hand.
2. *"The packs ship PVRTC and it was being decoded as ETC."* True for those
   packs, but the fix still guessed the codec from the payload size.
3. *"Read the codec the `.sba` declares."* Right, and necessary - but the ETC
   decoder it then dispatched to was itself wrong.
4. **The ETC1 modifier table was sorted.** See above. This is what made every
   ETC texture look dark and flat, and it is what "quality is still bad" meant
   each time.

Two smaller things from the same work: the blue channel of a PVRTC endpoint in
opaque mode is a 4-bit field that stays shifted when widened to 5 bits
(getting that wrong tints everything yellow-green), and trailing alignment
padding after a PNG's `IEND` is trimmed so written images are byte-exact.

## Source files

```
BUILD.bat             double-click this
BUILD.ps1             the build script it runs
CMakeLists.txt        for building with CMake instead
src/nfsnl.h           public API
src/nfsnl_core.cpp    DEFLATE, zstd loading, SBIN, PACK manifest
src/nfsnl_image.cpp   .sba, PNG, JPEG, BMP, TGA, DDS, ETC + DXT decoding
src/nfsnl_model.cpp   .sb3d decoding, FBX and OBJ writers
src/nfsnl_m3g.cpp     No Limits .m3g: the DA BD wrapper and the general reader
src/nfsnl_rr3.cpp     Real Racing 3: .z / .z.bin, .points, JSR-184 models,
                      _shared.m3g wheels, the .nct / .gui / text RC4
src/nfsnl_jsr.cpp     standard JSR-184 scenes (NFS Undercover, Shift, Shift 2)
src/nfsnl_lzham.cpp   the built-in LZHAM decoder (src/lzham/, public domain)
src/nfsnl_lzma.cpp    LZMA decoder (the China build's FMOBB), the tool's own
src/nfsnl_import.cpp  OBJ / FBX reading and the Real Racing 3 .m3g writer (car mods)
src/nfsnl_data.cpp    data files as editable text and back (.sounddef, .evt, tables)
src/nfsnl_render.cpp  the software renderer behind the 3D viewer
src/nfsnl_audio.cpp   .bnk banks, .wem sounds, Gnsu engine sounds
src/nfsnl_text.cpp    SBIN schema dumps and hex listings
src/nfsnl_library.cpp asset index and on-demand cabinet reads
src/nfsnl_profiles.cpp the game profiles the picker offers
src/logo_nfsnl.png    the logo on the No Limits card
src/logo_rr3.png      the logo on the Real Racing 3 card
src/resource.h        the icon ID, shared by the .rc and the GUI
src/main_win32.cpp    Windows GUI
src/main_cli.cpp      command line tool
src/monkeytool.rc     icon, version info, visual-styles manifest
src/monkeytool.ico    the monkey
src/nct_rc4.inc       Real Racing 3's RC4 start table and keys
src/nfsnl_rr3car.cpp  Real Racing 3 car data: fields, reading and writing
src/nfsnl_rr3add.cpp  Real Racing 3: liveries, the car lists, adding a car
src/nfsnl_rr3career.cpp Real Racing 3 career (events.dat), text files, series and tracks
src/nfsnl_save.cpp    the Save Editor's core: No Limits (AES, SBIN) and
                      Real Racing 3 saves
src/win32_save_editor.cpp  the Save Editor window
tools/SaveEditor/     the original Go source of the Save Editor, for reference
```



## Real Racing 3: more of what the files hold (1.2.10)

* **`suspensionbehaviour.suspensiondata`**: u32 1, u32 count, then 140 bytes a car: its id and 34
  floats.
  * Floats 22 and 23 (21 and 22 counted from 0) are the front and rear tyre diameters in metres.
  * 911 GT3 RS: 0.710 / 0.720 (real tyres 0.694 / 0.728). Ferrari SF24: 0.719 / 0.748. Megane
    Trophy R: 0.670.
  * Floats 1 and 2 look like camber (-0.5 on the GT3 RS).
  * The wheels of `<car>_shared.m3g` are scaled to these.
* **`<car>_int_shadows.etc.dds.z.bin`**: a chain of frames (43 on the GT3 RS), each a 256x256
  ETC1 sun-shadow mask on the cockpit's second UV set: lit white, shadowed black, one per sun
  direction. Their mean is the cockpit's ambient occlusion.
* **`<car>_ext_shadows_high`** (22 frames of 512x512) belongs to no body UVs: it is the shadow
  on the ground.
* **Cockpit trim slots** `_mm_mat00`..`mat07` are one scheme across the game:
  * mat00 carbon fibre, mat01 leather, mat02 stitching (alpha), mat03 plastic leather, mat04
    suede (the steering wheel rim), mat06 rubber or carpet, mat07 aluminium.
  * Their first UVs tile (to +-15) over a detail texture from `common/`.
  * A car's .liveries list names only the slots it recolours (the GT3 RS: `stitching_black_mat02`).
* **The game's hardpoint names** (libRealRacing3.so): a table of 37 POINT_ names and 42 HINGE_
  names.
  * The .points loader reads a type-0 record as a position only (x, then y negated into z, z into
    y). A type-2 record is position plus a 3x3 orientation.
* **Car mods**: a car is the data file `vehicles/data/<id>_<folder>.bin.nct`, plus records keyed
  by its id or folder:
  * `cardesc.list`: `<id>_<folder>.bin`.
  * `carappearances`: the record for the data's appearance index, naming the folder.
  * `cardamagethresholds` (116 bytes) and `suspensionbehaviour` (140 bytes), keyed by id.
  * Its name key's line in `gametext.txt` and the same line in each `text_<lang>.txt`.
  * `vehicles/data/liveries/<folder>.liveries.bin.nct`.
  * `vehicles/<folder>/`.
  * Two mods for one slot need their records merged into the game's lists, one of them
    renumbered; copying the lists over each other keeps only the last car.
