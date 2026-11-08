# CR3-U: HD textures, HD models, smooth models, FSR 4 and add-ons

The full reference for the port's optional extras. The README has the short version.

## HD textures

Upscaled textures you make yourself, from your own copy - waifu2x or Real-ESRGAN, drawn in place of
the originals:

1. `cr3view --export-textures` writes every picture the game draws to `<game folder>/textures/export/`
   (`<hash>.png`, upright; the profile folder's `textures/export/` if the game folder can't be written;
   `--export-textures <folder>` elsewhere). Each release exports its own pictures.
2. `python3 tools/hd_textures.py <game folder>` upscales them into `<game folder>/textures/hd/`
   (`<hash>.png`): waifu2x (`waifu2x-ncnn-vulkan` on the PATH, or `--waifu2x <it>`) for the
   lossless and block-compressed pictures, Real-ESRGAN (`--esrgan <the folder with
   realesrgan-ncnn-vulkan and models/>`) for the 16-bit ones - or `--engine waifu2x` for all. It
   needs Python 3 with NumPy and Pillow; `--help` lists the rest (`--scale`, `--noise`, `--only` ...).
3. Start the game: with `textures/hd/` there, HD Textures is on by itself (Options, Graphics, HD Textures switches
   it; `--no-hd-textures` for a run without). One `textures/hd/` can hold every release's.

The exports are ordinary PNGs, so any upscaler or image editor works as well: save the result
under the same name in `textures/hd/`. Keep each picture's shape - one scale across and down, at
least the original's size, up to 8192 a side - and its rows the right way up: a picture that
doesn't fit is left out (the log says which and why) rather than drawn wrong.

`CR3_HD_TEXTURES=<folder>` reads them from elsewhere; the older `hd-textures/` beside the program or in
the profile folder is still read while there is no `textures/hd/`. The `tools/hd_textures.py` is the
same in all four ports. Personal use: the outputs are made from the game's art - keep them out of
anything shared.

## HD models

Models you make or improve yourself, drawn in place of the game's - more detailed, smoother,
retextured - moving with the game's own skeleton and animations:

1. `cr3view --export-models` writes every figure to `<game folder>/models/export/` as glTF 2.0
   (`<name>.glb`: its skeleton, meshes, materials, pictures and animations; `--export-models <folder>`
   elsewhere; `--no-animations` leaves the animations out - with them a character's file runs to
   hundreds of MB, all of them to about 6 GB). Blender (File, Import, glTF 2.0) opens them as they are.
2. Edit it - sculpt, subdivide, rebuild, re-weight, paint - keeping the armature: its bones are named
   `node_<n>...` and the game finds them by that (a new bone goes with its nearest original parent).
   A material keeps the game's render state by its name `cr_mat_<n>...`; give it a picture of your own
   (PNG or JPEG, any size, packed in the `.glb`) and that picture replaces the figure's.
3. Export it from Blender as glTF 2.0 (`.glb`, the defaults) to `models/hd/<figure>.glb` (`j001b01.glb`
   for the figure `j001b01`). With HD Models on (Options, Graphics; off until you switch it on) it is drawn
   instead of the original from the next scene on.

What it reads: triangles, the first UV set and the second and colour set, four weights a vertex; meshes
on more than 85 joints are split, up to 65535 vertices a mesh; normals, and tangents for normal-mapped
materials, are worked out when missing. The animations in the file are for previewing - the game plays
its own. `cr3tool gltfcheck <release> <figure|all> [file.glb]` checks a round trip (the export loaded
back: every figure lands exactly on the original in every animation). `CR3_HD_MODELS=<folder>` reads
them from elsewhere. Personal use: the exports are the game's models - keep them, and what you make
from them, out of anything shared.

## Smooth models

Options, Graphics, **Smooth Models**: Off, Smooth (4x triangles) or Smoother (9x) - every model in
the world (characters, monsters, stages and props; not the menus) rounded out at load time with PN
triangles, as CR1-U's: the original vertices stay put, edges sharper than 60 degrees stay hard, and
skinning and animation work as before. From the next scene on; kept in settings.ini
(`smooth_models` 0, 2 or 3), `--smooth-models n` for one run.

## FSR 4

The game's own upscaler is AMD's FSR 3.1.6, built from AMD's open source, on every system. On
Linux and Windows with an AMD GPU (RX 6000 series or later) **FSR 4** - AMD's machine-learning
upscaler, FSR Upscaling 4.1.2 - is an Upscaler choice too (Options > Graphics, or `--upscaler fsr4`),
once its files are in `fsr4/` in the game folder. AMD ships FSR 4 for Vulkan only as its own signed
Windows DLLs, whose licence allows them to be passed on in that form under AMD's terms alone, so
they aren't included:

- `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` from AMD's FSR SDK 2.4
  (`Kits/FidelityFX/signedbin/` in its [release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0));
- `amdvkffx64.dll`, FSR 4 itself, from AMD Software: Adrenalin Edition 26.10.1 or later (the Windows
  driver package: `Packages/Drivers/Display/WT6A_INF/B*/` inside the installer). Without it AMD's SDK
  offers only FSR 3.1, and the game says so;
- on Linux, `libffx_vk_linux.so` from [ffx-vk-linux](https://github.com/DuIslingr/ffx-vk-linux) (MIT),
  which maps AMD's DLLs natively - no Wine. Windows loads them as they are.

The log says which FSR AMD's SDK picked (`FSR 4.1.2: AMD's upscaler from ...`). FSR 4 costs about
2 ms a frame at 1440p on an RX 7900 XTX. `CR3_FSR4_DIR` points at the files elsewhere.

## Add-ons: outfits and weapons of your own

New outfits and weapons - beside the game's, not in place of them - sold in the premium shop (the
field's Shop button, or the VS mode's Sp Shop) next to its special ones, worn like any other:

1. Make the model from an export (`cr3view --export-models --no-animations`): an outfit over a
   character's body - `j001b02.glb`, the hero's Worn Hoodie -, a weapon over a weapon -
   `w001_01.glb`, the Chain Sword. Keep the armature (bones `node_<n>...`) and the material names
   (`cr_mat_<n>...`); new pictures, more detail, another shape are all yours. Save it as `.glb`.
2. Beside it in `<game folder>/models/addons/`, a manifest `<anything>.ini`:

   ```ini
   id = 1               # 1 .. 999 - keep it: saves know what you own by it
   kind = outfit        # or weapon
   character = 1        # who wears it (1 the hero)
   name = Night Coat    # (name_ja = for Japanese)
   model = coat.glb
   like = 200083        # the item it's made from - its numbers, icon and body or weapon (default: the character's own)
   price = 30           # in the premium shop...
   currency = coin      # ...in coin, GP or gold (shop = none: not sold)
   hd_models = no       # an outfit's look with HD Models - see below
   ```

3. Start the game: it is in the shop (`cr3tool addons <release> <game folder>/models/addons`
   checks them first - each one's item, name, shop goods and model, as the game takes them).

Add-ons are always sold and worn, their numbers their own. A weapon is always drawn as its own
model; an outfit is made for the character as the game draws it, so while HD Models replaces any
of that character's figures (`models/hd/j001*.glb`) it looks like the item it's made from - as the
game's own armour does, its numbers still the add-on's - unless it says it fits: `hd_models = yes`
(any), or the replaced figures it is made for (`hd_models = j001b01 j001h01`). (`like` an armour
whose body is your replaced one - the hero's b01, the Black Jacket - keeps your model on show.) `CR3_ADDONS=<folder>` reads them from
elsewhere. Personal use, as HD Models: they're made from the game's models.

