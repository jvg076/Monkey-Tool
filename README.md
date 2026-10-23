# Monkey Tool

**A modding tool for EA's mobile racing games: browse the game files, convert models, textures and sounds, and put your own work back in.**

by **GM25** · Windows (GUI + command line) · the command-line core also builds on Linux and macOS

Monkey Tool reads the game files of:

| Series | Games |
|--------|-------|
| Need for Speed | Undercover, Shift, Hot Pursuit, Shift 2 Unleashed, Most Wanted (2012, mobile), No Limits (including the China build), No Limits VR, Edge |
| Real Racing | Real Racing Next, Real Racing 3, Real Racing 2, Real Racing, Real Racing GTI |

It opens on a **game profile**: you pick the game and point it at that game's files, and the archives are indexed where they are. **Nothing is unpacked to disk.** An asset's bytes are read the moment you click it.

## Features

* **3D viewer** with textures, LOD levels, body kits, paint colours (NFS Shift and Shift 2), wheels placed the way each game places them (each NFS Undercover / Shift / Shift 2 car on its own rims), and animations. Cars and whole tracks, including the iPhone games' city maps.
* **Export**: models to **FBX / OBJ** (with textures) or **glTF 2.0 .glb** (textures inside the file), textures to **PNG / JPG / BMP / TGA / DDS**, sounds to **WAV**, data files to **text**.
* **Import**:
  * your own pictures back into the game's textures (`.sba`, `.pvr`, `.dds`, `.z`);
  * OBJ / FBX models into Real Racing 3 cars, part by part, or as a whole car built on an existing one.
* **Car colours** for NFS No Limits: cars open in the colours the game gives them (body, rims, brake calipers, from the car's own setups), and any other setup of the car can be picked. Exports ask whether rims, calipers and body come out painted or in the textures' default colours.
* **Rim paint** for NFS No Limits: paint a wheel texture in any of the game's own rim colours, or a colour of your own.
* **Limited-time layers** on NFS No Limits tracks: the Halloween, Christmas and event decorations a region carries, switched on in the viewer and taken along on export.
* **UI texture packs** (NFS No Limits `texturepacks/ui`): every picture of a pack saved under the game's own name.
* **Save Editor** for NFS No Limits and Real Racing 3.
* **Car creator** for Real Racing 3, in four simple steps: **add a new car** (its own id, beside
  the game's cars), or change one of the game's cars and give it more liveries. You set:
  * year, brand, class, drivetrain, performance, engine and price;
  * the upgrade package, the liveries (names, colours, pictures) and the engine sound;
  * the body and cockpit models, as OBJ / FBX built into .m3g, and the textures.
* **Racing series** for Real Racing 3: edit the career's series (title, allowed cars, events
  and tracks) or add new ones, in a career group or a new group with your own card picture.
* **New track** for Real Racing 3: a variant of one of the game's tracks, with its own id, name
  and place in the career, and optionally its own models.
* **Car mod installer** for Real Racing 3: a mod's cars merged into the game's lists as they are,
  so mods do not wipe each other out. A car made for a slot another car already has can go
  into the next free slot.
* **Points editor** for Real Racing 3: a car's lights, exhausts, wheels and hinges on its model.
  Click one, move it, save.
* **Real Racing 3 data decoded in full.** The game's RC4 is built in: every `.nct` / `.gui` file,
  the career (`events.dat`) and the language files read and write exactly.
* **Data editor**: Real Racing 3 data files (`.sounddef`, `.evt`, tables) as editable text.
* **Video player** for the games' films (NFS Undercover's `.m4v`, Shift's `.mov`).
* **Several assets at once**: Ctrl+click and Shift+click select rows as in Windows Explorer,
  then export or extract them together.
* A **command-line tool** (`monkeytool_cli`) for batch work.

Changes are kept as *pending* until you press **Save**. The original file is kept once as `.bak`.

```
models/cars/jaguar_xe_sv/jaguar_xe_sv.sb3d        ->  .fbx / .obj
textures/cars/bmw_m3_e30_1992/..._alpha.sba       ->  .png / .jpg / .bmp / .tga / .dds
audio/<bank>.bnk                                  ->  the sounds it holds, as .wav
cars/1979_porsche_935/1979_porsche_935_a.m3g      ->  .fbx / .obj / .glb (Real Racing 3)
cars/1979_porsche_935/..._misc.etc.dds.z          ->  .png (Real Racing 3)
vehicles/<car>/<car>.sb3d                         ->  .fbx / .obj (Real Racing Next)
```

## Download

Ready-made builds are on the **[Releases](../../releases)** page: unzip and run `MonkeyTool.exe`.

Every push is also built by GitHub Actions. To get one of those builds, open the **Actions** tab, pick a run, and download the artifact.

## Building it yourself

### Windows: double-click `BUILD.bat`

You need nothing installed. If no C++ compiler is found, the script downloads a portable one (w64devkit, about 90 MB) into this folder. It uses it from there and installs nothing on your system. It also fetches `libzstd.dll`.

```
dist\MonkeyTool.exe        the program
dist\monkeytool_cli.exe    the command-line version
dist\libzstd.dll           needed for 3D models
```

The **LZHAM** decoder (No Limits' `.m3g` models, No Limits VR's packed files) is compiled into the program, so no extra DLL is needed.

> Don't click inside the build window while it runs. Selecting text makes Windows pause the console, and the build looks frozen until you press **Esc**.

### CMake (Visual Studio, MinGW, or Linux/macOS for the command-line tool)

```
cmake -B build
cmake --build build --config Release
```

On Linux and macOS only `monkeytool_cli` is built; the GUI is Windows-only. `tests/check_windows_syntax.sh` checks that the Windows sources compile against stub headers, so you can catch errors without a Windows machine.

### Runtime libraries

| File | Needed for | Where it comes from |
|------|------------|---------------------|
| `libzstd.dll` | 3D models and some texture cabinets | [zstd releases](https://github.com/facebook/zstd/releases), fetched by `BUILD.bat` and CI |
| `tools/vgmstream/` | playing and converting Wwise `.wem` sounds | [vgmstream](https://github.com/vgmstream/vgmstream), included |

The tool has its own code for DEFLATE, PNG (including Apple's iPhone PNGs), JPEG, ETC1/ETC2, DXT, ASTC, PVRTC, Brotli and LZHAM, so it needs no other libraries.

## Using it

1. **Choose a game.** The tool starts on the profile picker.
   **File → Change game...** brings the picker back.

2. **Point it at the game's files.** For No Limits, pick the app folder or its
   `files\packs` subfolder; for Real Racing 3, the app folder or its
   `files\.depot` — it finds the right one either way and searches subfolders.
   Only the manifests are read, a few kilobytes per archive, so a full install
   is ready in seconds and costs **no disk space at all**.

3. The tree shows every asset under its real in-game path. A **progress bar**
   in the status strip fills as the files are read, so a big install shows how
   far along it is rather than just sitting there. **Type in the
   Search box above it** to narrow the list: `bmw` leaves only the BMW assets,
   `porsche dds` only their textures. It matches the file's own name, not the
   whole path, and the results are expanded for you. Clear the box to get the
   whole tree back.

4. **Double-click any asset (or right-click > Export...) to save it.** Models, textures and
   sounds open the **Export settings** window first, the way Frosty's export does, then the file
   or folder is chosen. The choices are remembered. Folders and selections (right-click) go
   through the same window.

   | Section  | Settings |
   |----------|----------|
   | Models   | FBX, OBJ, glTF 2.0 (.glb) or the original file; scale (game units, metres, centimetres, millimetres, inches); every LOD or the most detailed only; helpers (collision hulls, decal shell) in or out; textures beside the model; No Limits body kits and setup colours |
   | Textures | PNG (zlib level 0-9), JPEG (quality), BMP, TGA, DDS (BC1, BC2, BC3, BC7 or uncompressed R8G8B8A8, with or without mipmaps), a Real Racing 3 game texture for a chosen GPU (ETC1 for every Android phone - Mali, Xclipse, PowerVR, Adreno; ATC for the Adreno build; RGBA4444 uncompressed), or the original file |
   | Sounds   | WAV (PCM 16-bit), Ogg Vorbis (quality -1..10), MP3 / AAC / FLAC through ffmpeg.exe if you have it, or the original file |

   A tick at the top writes everything exactly as the game stores it (what "Extract" did).
   Other files (data, text, `.nct`, `.gui`, `.points` ...) keep the Save As dialog's list of
   formats: text, decoded, or the original.

5. **Select a model** and it appears in the 3D viewer:

   | Action | What it does |
   |--------|--------------|
   | drag | turn the model |
   | right-drag | slide the view |
   | wheel | zoom |
   | **Ctrl** + drag | move the model, X and Y |
   | X / Y / Z boxes | move the model by an exact amount |
   | LOD dropdown | show one LOD, or all of them at once |
   | Textures | switch between textured and plain shading |
   | Vertex colours | multiply in the mesh colours (baked occlusion) |
   | Reset view | back to the starting camera and position 0,0,0 |

   The viewer draws with the model's own textures and LOD levels. Panels are drawn
   two-sided: the game's shells are single-sided and their winding is not
   consistent, so culling them left holes and the car looked see-through.
   The additive light-glow cards are left out of the viewer, because drawn
   solid they sit over the lights as grey slabs.

6. **Select a sound or a bank** and the pane shows it as text, with **Play**
   and **Stop**. A bank lists every sound it holds in the dropdown - pick one
   and play it. Saving the bank writes them all as `.wav` (see [Audio](docs/FORMATS.md#audio-bnk-and-wem) in the format notes).
   Data files are shown as text too.

7. **The funnel icon → Convert everything shown...** does the whole list at once.
   To write only some assets, **Ctrl+click** rows to add or remove them and **Shift+click** to
   take a range, then right-click the selection or press the Export icon.

   A film (`.m4v`, `.mov`) plays in the viewer's place, with Play, Stop and a seek bar under it.

8. **No menu bar: everything is an icon**, in one row at the top left, over the search box and
   the tree, each with a tooltip: open game, export, import, save, car creator, racing series,
   new track, install a car mod, edit car points, Save Editor, encode .nct / .gui, **Show** (the
   funnel: all assets, textures, 3D models or sounds) and **Options** (the gear: **Theme** Light /
   Dark, vgmstream, LZHAM, About). The theme is remembered between runs; on first launch it
   follows your Windows theme. Shortcuts: Ctrl+O open, Ctrl+S save, Ctrl+I import a picture,
   Ctrl+E Save Editor, Ctrl+L show the file an asset is in (also on the tree's right-click menu).

9. **Save Editor.** Its icon (the person with a pencil) or Ctrl+E opens the save editor in its own
   window, which you can keep open beside the tool. Pick the save folder (No
   Limits: `saves-encrypted`; Real Racing 3: `doc`; a parent folder works
   too) or drag it onto the window.
   * **Quick edit** has the usual fields: cash, gold, level, fuel and so on
     for No Limits; R$, Gold, M$ and driver level for Real Racing 3.
   * **All fields** lists everything in the save, with a search box.
   * **VIP stage / level** (No Limits): in Quick edit when the save keeps one.
   * **Premium / VIP player**: a tick beside the player's name turns on (or off) every yes/no
     flag in the save whose name says VIP, premium or subscriber. The flags are found by name,
     so the box is greyed out when a save holds none; the list under "All fields" shows them.
   * **Saving**: every save first backs up the original files to a
     `save_editor_backup_<date>` folder beside them. The No Limits manifest
     hashes are updated, and so is Real Racing 3's `TempSaveGame.dat` copy.
   * **Real Racing 3 calibration**: the game hides money and level behind a
     key, so type the values the game shows once and click **Calibrate from
     game**. It is remembered in `%APPDATA%\MonkeyTool\rr3_keys.txt`, and a
     calibration made with the old separate editor is picked up
     automatically.

### Command line

```
monkeytool_cli list    <gameFolder>
monkeytool_cli extract <gameFolder> <outFolder>
monkeytool_cli convert <gameFolder> <outFolder> --tex png --mdl fbx
monkeytool_cli one     <file.sb3d>  <out.obj>   --format obj
monkeytool_cli export  <asset> <outBase> --model fbx --unit 2 --single-lod --tex dds --dds bc7 --audio ogg
```

`--only textures|models|sounds` limits what gets written.

## Documentation

* **[CHANGELOG.md](CHANGELOG.md)**: what changed in each version.
* **[docs/FORMATS.md](docs/FORMATS.md)**: notes on every game format the tool reads and writes (archives, models, textures, audio, saves, the Real Racing 3 car files). They're useful if you want to write your own tools.

## Contributing

Bug reports and pull requests are welcome. See **[CONTRIBUTING.md](CONTRIBUTING.md)**. When something looks wrong, a screenshot from the tool next to one from the game, plus the file involved, is usually all that's needed.

## License

Monkey Tool is released under the **[MIT License](LICENSE)**.

It includes or downloads third-party code under its own licenses (Brotli, vgmstream, zstd, LZHAM). See **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**.

## Disclaimer

Monkey Tool is an unofficial fan project. It is not affiliated with, endorsed by or connected to Electronic Arts, Firemonkeys, Criterion or any other rights holder.

*Need for Speed*, *Real Racing* and the related names and logos are trademarks of their owners. The game files themselves are **not** part of this project, so use the tool on files from games you own.

Changing save files or game files of online games may break the game's terms of service. You do that at your own risk.
