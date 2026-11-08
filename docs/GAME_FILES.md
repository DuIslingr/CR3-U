# CR3-U: your copy of the game in detail

The full reference for where CR3-U looks for the game, what each release needs, the fan
translations, the movies and the original card artwork. The README has the short version.

## The command-line tools

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr3view --model c101.geom
build/cr3tool release "<your copy>"
```

`cr3view` finds your copy by itself in the game folder - for this build `engine/build/`, or the
folder you start it from - as `vita/`, `android/` or `ios/` ([Where the port looks](#where-the-port-looks)); `cr3tool`
(and `cr3view --data`) take one wherever it is:

| Your copy | `<your copy>` |
|---|---|
| iPhone / iPad 1.2.2 | the unpacked `.ipa` (or its `Payload/ChaosRings3_FINAL_ENG.app`) |
| Android 1.1.3 (`com.square_enix.chaosrings3gp`) | the `.apk` / `.apks`, or a folder with the APK unpacked; the asset packs (`CRDB{bgm,voice,se,mov}.android.mvgl`) anywhere under it. An `.apks` that holds them compressed (a repacked one) has them unpacked once into `assetpacks/` beside it: about 1.3 GB; one that holds its APKs themselves compressed (as some tools pack them - APKPure's `.xapk` too) has those unpacked once into `unpacked/` beside it: about 2.3 GB more |
| PS Vita PCSG00500 / PCSG00457 | the installed (decrypted) title's folder, or its `CR3/resources` |
| + a fan translation | `cr3tool --patch <its rePatch folder> ...` over an unpatched Vita copy (`cr3view`: `translation/` in the game folder): the Translation Project's English, or Joseas's Spanish (`CR3_ES`, with its own font for á, ñ, ¿, ¡); either one patched into the Vita copy itself is read as it is too |

An encrypted Vita dump (NoNpDrm, still PFS-encrypted) has to be installed first for `cr3tool` (Vita3K does it);
`cr3view` decrypts one put in `vita/` itself (below).

## Where the port looks

The easiest way: just start the program. With no copy found, it asks where yours is - pick it in
the window it opens, and the files the port needs are **copied into the game folder** (the profile
folder's if that one can't be written), the same layout as putting them there by hand (below):

- **iOS** - the `.ipa` (or any file in its unpacked `.app`): unpacked into `ios/`;
- **Android** - the `.apks` (or the `base.apk` with its asset packs, all at once): copied into `android/`
  (an APK beside them that carries the game's package goes along; another game's is refused);
- **Vita** - Vita3K's folder with the game installed (its own, the folder its `config.yml` names as
  `pref-path`, or `ux0`), or the game's own `PCSG00500` folder: copied into `vita/PCSG00500/` as Vita3K installs
  it (`eboot.bin`, `sce_sys/`, the game's folder); the Prequel Trilogy's `PCSG00457` too (into `vita/PCSG00457/`,
  without the other games) - then its fan translation (the `rePatch` folder: Vita3K's
  `ux0/rePatch/PCSG00500` by itself, else asked for, or left out) into `translation/en/` or `translation/es/` (its language's).
  A **NoNpDrm dump** (its files encrypted, `sce_sys/package/work.bin` its licence) - its folder, or packed as
  `.rar`, `.7z`, `.zip` or `.vpk` - is decrypted as Vita3K's install does, this game's parts alone.

One copy is the game: the first given starts it. Nothing is read where it was chosen, so the folder it came from can go
afterwards. (Windows, Linux and macOS; on Android, copy the files over USB as below.)

Or, as before, nothing given on the command line: put your copy in the **game folder** and start
the program - it finds it by itself. The game folder is:

| Platform | The game folder |
|---|---|
| Windows, Linux | the folder holding `cr3view.exe` / `cr3view` |
| Linux AppImage | the folder holding the `.AppImage` |
| macOS | the folder holding `cr3view.app` (its `Contents/Resources/` works too) |
| Android | `Android/data/local.preservation.chaosrings3/files/` on the shared storage (copy your files there over USB) |

(then the working directory too). In it, a folder for each release you have - any one is enough:

```
<game folder>/
├── cr3view                          (or cr3view.exe, cr3view.app, the .AppImage)
├── ios/
│   └── ChaosRings3_FINAL_ENG.app/      the iPhone / iPad 1.2.2's .app: its ~170 CRDB*.ios.mvgl and movies
│                                       - or Payload/ChaosRings3_FINAL_ENG.app/ (the .ipa unpacked as it is)
├── android/
│   └── <the 1.1.3 .apks>               (or its base.apk with the asset packs' CRDB{bgm,voice,se,mov}.android.mvgl
│                                        anywhere under android/)
├── vita/
│   └── PCSG00500/                      the installed title: CR3/resources/**.psp2.mvgl (or the Prequel Trilogy's PCSG00457)
├── translation/
│   ├── en/                             a fan translation, a folder each (its rePatch/PCSG00500/CR3/resources/*.psp2.mvgl - or
│   └── es/                             the .rar / .zip / .7z, unpacked into en/ or es/): over an unpatched Vita copy,
│                                       "English (fan translation)" and "Spanish (fan translation)" in the Options' Language
├── movies/                          cNN.mp4 your release lacks, from another (the iPad lacks c04..c11): "Movies"
├── fsr4/                            AMD FSR 4 (optional; Linux and Windows, an AMD GPU): AMD's amd_fidelityfx_loader_vk.dll
│                                    and amd_fidelityfx_upscaler_vk.dll (FSR SDK 2.4), the driver's amdvkffx64.dll
│                                    (AMD Software 26.10.1+), and on Linux ffx-vk-linux's libffx_vk_linux.so: "FSR 4"
└── textures/
    ├── export/                         written by --export-textures: the pictures to upscale (<hash>.png)
    └── hd/                             read by the game: the upscaled pictures (<hash>.png or .crhd)
```

A release's folder may hold the release itself or have it one or two folders down, as above
(`ios/Payload/....app`, `vita/PCSG00xxx/`), so a copy can be dropped in as it was unpacked or
installed; the names of the folders below `ios/`, `android/` and `vita/` don't matter. A Vita copy
is the installed, decrypted title - or a NoNpDrm dump's folder as it was dumped, still encrypted (its
`app/`, and `addcont/` beside it for the Prequel Trilogy's add-on): the next start decrypts it into
`vita/` (on a phone too; it needs the space for the decrypted copy), and the dump can be deleted then.
A dump packed in an archive (.zip, .rar, .7z, .vpk) is chosen in the first start's setup instead - a
phone's too, which shows when there's no copy yet and picks files (folders are copied in over USB).
The release downloads come with these folders made, each with a `README.txt` saying what
goes in it.

CR3-U plays one release at a time: with several, **the first of `vita/`, `android/`, `ios/`** (the
same order as the other ports take one alone); `--data` picks another. `translation/` is used only
with the Vita release (the iOS and Android ones have English of their own); a Vita copy with the
patch already installed shows that English without it.

If nothing is found the program says where it looked. The older places still work: `ipad/`, `PCSG00500/`, `PCSG00457/`, `ChaosRings3_FINAL_ENG.app/`, `Payload/` or `data/` straight in the game folder.
To use a copy elsewhere: `--data <a release: its folder, .apks, archive>` (or `CR3_DATA`), `--patch <the rePatch folder>`. Settings and saves go to the profile folder
(`~/.local/share/DuIslingr/CR3-U/` on Linux; the same folders under `%APPDATA%\` on Windows and
`~/Library/Application Support/` on macOS), not the game folder. All four ports (CR1:U, CRO-U, CR2-U, CR3-U) look for their copies the same way.
With Vulkan the profile folder also keeps `vulkan-pipelines.bin`: the graphics driver's compiled
pipelines, so later starts and new screens don't compile them again (safe to delete; another GPU
or a driver update starts it anew).

## Original card artwork

**Options → Graphics → Original Card Art** switches the Gene Index entries **80, 81 and 86**
to their original artwork in menus, gene icons and full-screen card views, with matching
names and N/R/SR profiles: **Little Red Riding Hood, Aphrodite and Adam**. English and
fan-English modes use English translations of the Japanese-only originals; Japanese mode
keeps their original Japanese text. Only cards whose original artwork is available receive
the matching text. Turning the setting Off restores the selected release's art and text.
The setting is Off by default, takes effect immediately, and is remembered between sessions.
Gene IDs, stats, rarities and save data stay the same.

Vita already uses the originals. iOS retains all three originals in its files. Android retains
the originals for 80 and 81; with Android alone the option shows **On (2/3)** and keeps card 86's
release artwork until its original is available.

For the missing artwork, copy **both** card archives from your decrypted Vita or iOS copy into
`artwork/` beside the game's `ios/`, `android/` and `vita/` folders:

- Vita: `CRDBcard01.psp2.mvgl` and `CRDBcardhd.psp2.mvgl`.
- iOS: `CRDBcard01.ios.mvgl` and `CRDBcardhd.ios.mvgl`.

Subfolders are accepted. Existing Vita/iOS copies in the normal game folders are checked too.
These files supply pictures only; putting them in `artwork/` keeps the selected gameplay release.
If the game is already running when you copy the files, turn the option Off and On to refresh it.
No artwork is included with the port. On Android, `artwork/` belongs under
`Android/data/local.preservation.chaosrings3/files/`.

For files elsewhere, `--original-art-source <folder>` or `CR3_ORIGINAL_ART` points to the card
archives or a complete Vita/iOS release. `--original-art` / `--release-art` override the toggle
for a run (also useful for captures).

## Movies

The story's movies play as in the game: full screen, with their sound at the music's volume and
their subtitles in the game's language (`cNN_en.srt` / `cNN_jp.srt`; the fan translation's English
is in its `_jp` files). Tap once for **Skip**, again within three seconds to skip. Each release
has its own: the iPad's `.app` holds c01..c03 and c50..c58 but not the story's c04..c11 (the game
downloaded those), the Vita's `CR3/resources/movie/` and Android's asset pack hold them all. Put
the ones yours lacks in `movies/` in the game folder (or point `CR3_MOVIES` at a folder of them,
such as a Vita copy's `CR3/resources/movie/`); a movie found nowhere is skipped. `--movie cNN`
plays one at start-up. The video is H.264, decoded with OpenH264 (the system's, else the copy
built in), the sound with FAAD2.

