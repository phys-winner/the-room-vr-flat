# The Room VR: A Dark Matter — flat touch port for Android phones

A port of the PICO VR build of *The Room VR: A Dark Matter* (version 1.02, Unity 2019.4 IL2CPP, arm64) to ordinary
Android phones: no headset, full-screen landscape, touch controls for the whole game, optional gyroscope.

**This repository contains no game files.** It holds the port's own work only: a native hook library, a small Java
overlay, Python build and patch tools, and a patch for the decoded APK. To build the port you need your own copy of
the original APK; the tools rebuild everything else from it on your machine.

*Описание на русском — [ниже](#на-русском); полный список возможностей порта — [PORT_FEATURES.md](PORT_FEATURES.md).*

## Features

- Runs as a normal app: the PICO / Oculus VR runtime, the platform entitlement and the headset checks are detached.
- Touch controls for every VR action:
  - swipe on empty space to look around - turning never moves the player, and a teleport puts them at the centre of
    the location; spread or pinch two fingers to step forward or back, drag with two fingers to move sideways or up
    (peek behind things) - always inside the location's play area, as a VR player moves in theirs, never into a wall;
  - a position indicator (top right) shows, for the axis being moved, where the player stands relative to the
    location's centre and how far they can go; at the edge the game's own "return to the play area" marker appears;
    the indicator can be turned off in Options;
  - press an object and drag to take it and move it: levers (they follow the finger along their arc from any
    angle), drawers, handles, ropes, bolts (lift, turn, hang up); turn cranks, winches, keys and knobs with a
    circular finger motion;
  - a picked-up note, clipboard or tool comes up to be read, turned to the eyes with the side the inventory shows,
    upright and wholly in view;
  - while an item is held, a slider on the left under the pause button moves it farther away or nearer (from
    20 cm to arm's reach); the Sun of the constellation puzzles stays where it was taken and is carried through
    the stars in mid-air, its depth set with the slider;
  - a second finger next to a held object turns it in the hand; a second finger elsewhere looks around while holding;
  - pressable panels and buttons are pushed by the fingertip, as in VR.
- On-screen buttons: pause, teleport (then tap a glowing spot; crystals enter the miniature worlds), inventory,
  eyepiece, turn arrows. Android Back closes menus and pauses the game (also on Android 16, where predictive back
  would otherwise close it). The inventory always opens wholly in view; its hint tab is pulled with a finger.
- Menus are tapped directly — no laser pointer beam, no camera turning; every menu screen is framed in view
  (the field of view widens while a menu needs it).
- Tutorial hints and the hero's remarks are pinned flat to the screen (hints at the top, remarks at the bottom) and
  drawn over the scene instead of drifting after the view; their letters have a set size in millimetres, every box
  fits the screen (tablets included), and two hints shown at once are stacked. A new game opens facing the level's
  title.
- Gyroscope options: *eye position* (turn / tilt / push the phone to move the eyes) and *tilt and turn* (the camera
  follows the phone), each with its own sensitivity 25–300 %.
- Options: FPS limit 30 / 45 / 60 / display rate (default 60), field of view 70–110° (hor+, default 90), position
  indicator on / off, a gesture card in all 10 game languages, the game's hidden graphics screen (MSAA, texture quality, render scale, AO, bloom,
  volumetrics), a chapter select that replays chapters without touching progress, and a QA row.
- Fixes for Mali GPUs (glow "disc" artefact, shadow-pass shader link errors); the one DXT5 texture re-encoded to
  ASTC; full screen including the display cutout; Unity's "trial version" mark removed.
- Saves and settings live in `Android/data/com.FireproofStudios.TheRoomVRPlus/files`; all five permissions of the
  original are removed.
- The release build writes no logs: no log file, no developer console, and on Android 11+ the game's process is
  muted in logcat.

Verified on a Redmi Note 8 Pro (Mali-G76, Android 11), a Realme RMX3371 (Adreno, Android 15) and a POCO X3 NFC
(Adreno 618, Android 16; tablet 16:10 emulated by display size); starts and loads chapters on a Pixel 10 (PowerVR,
Android 17). Not verified: devices with 16 KB memory pages; touch play-through of a few late story places (Chapel
organ keys and lockpick; pouring, mirror, bellows and candles in the Witch chapter; the constellation puzzles with
the hold-distance slider) — the controls for them exist.

## Building

### What you need

- The original APK of the PICO build 1.02 (`com.FireproofStudios.TheRoomVRPlus`, versionCode 5011):
  2 061 253 795 bytes, SHA-256 `d6608b54f62f3b2166292655945e8a32e717b18e07b1de010d410a68acfbe65e`.
- Java 17 or newer (verified with OpenJDK 24.0.1), Android SDK platform `android-36`, Android NDK 27.2.12479018,
  build-tools 37.0.0 (aapt2, zipalign, d8), apktool 3.0.3, baksmali 3.0.x, apksigner and a signing key of your own.
- Python 3.12+ with `UnityPy` 1.25, `lz4`, `numpy`, `texture2ddecoder`, `astc-encoder-py` (asset patches);
  `Pillow` for the device test tools; `capstone` and `pyelftools` only to regenerate the hook table.
- About 12 GB of free disk space (the decode and the working tree take 2.7 GB each, the APK 2 GB, packaging needs
  two more temporary copies).

### Steps

```sh
cp work/local.example.json work/local.json     # then fill in the tool paths and your signing key
python work/tools/prepare_app.py /path/to/original.apk --release
python work/tools/build_port.py apk --release
adb install -r out/base.apk
```

`prepare_app.py` checks the APK, decodes it once into `decoded_original/` (and compares the decode file by file with
`work/manifests/decoded_original.json`), copies it to `app/`, applies `patches/app.patch`, removes the VR-only files,
writes the binary asset fixes (each script checks its source bytes first) and builds the port's code into `app/`.
`build_port.py apk` packages `app/` into `out/base.apk` (apktool → zipalign `-P 16` → apksigner).

Without `--release` you get the developer build: it writes a bounded `roomfix.log` next to the saves and, when a
file named `roomfix.dev` exists there, runs a command console (`roomfix.cmd`) used by the test tools.

The build is verified on Windows 11. The tools avoid platform-specific calls (apktool's line endings are handled),
but Linux and macOS builds have not been tried.

An APK built this way contains the game's assets: keep it for your own use and do not distribute it.

## How it works

- `src/roomfix/` → `libroomfix.so`, loaded from `UnityPlayerActivity.onCreate` before Unity starts. It installs
  inline hooks into `libil2cpp.so` (targets listed in `symbols.spec`, resolved into `gen_symbols.h`); the build-id
  and the original bytes of every hooked function are checked at run time, a mismatch disables the hooks.
- The XR runtime is replaced by synthetic devices: the head pose comes from touch and the gyroscope, and a touch
  action moves the synthetic right controller like a VR player's hand, so the game's own grab, teleport and menu
  code decides what happens.
- `src/java/port/` adds the HUD, touch routing, the gesture card, the gyroscope sensors, the data redirect to the
  external files directory and the per-app language.
- `patches/app.patch` changes the decoded APK: manifest (SDK levels, no permissions, application class, theme),
  themes, `boot.config` (no XR pre-init, full screen) and four smali files (the port's entry points in
  `UnityPlayerActivity`, receiver registration for Android 14+). `work/tools/patch_shaders.py`,
  `patch_textures.py` and `patch_buildsettings.py` make the binary asset edits; `prune_app.py` removes the
  VR-only files.

## Repository layout

| Path | Contents |
|---|---|
| `patches/app.patch` | the port's edits of the decoded APK |
| `src/roomfix/` | native library: hooks, touch, camera, menus, flat texts, gyroscope, options |
| `src/java/port/` | Java side: HUD, touch routing, help card, sensors, data redirect |
| `work/tools/` | build, patch and device test tools (device tools talk only to the adb serial in `RF_SERIAL`) |
| `work/manifests/` | reference hashes: original APK, its decode, binary asset patches |
| `work/*.md` | research reports: code / resource / permission cleanup, hidden content, graphics, textures, device effects, touch action map |
| `PORT_FEATURES.md` | feature list of the port (Russian) |
| `STATE.md` | the porting log (state of the porting workflow) |

## License

The port's code and documentation in this repository are free software: you can redistribute them and/or modify
them under the terms of the GNU General Public License as published by the Free Software Foundation, either version
3 of the License, or (at your option) any later version (`GPL-3.0-or-later`, see [LICENSE](LICENSE)). Copyright
(C) 2026 the contributors of this port.

*The Room VR: A Dark Matter* is © Fireproof Studios. This project is not affiliated with or endorsed by Fireproof
Studios or PICO; the game, its code and its assets are not covered by this license and are not part of this
repository.

---

## На русском

Порт PICO-версии игры *The Room VR: A Dark Matter* (1.02, Unity 2019.4 IL2CPP, arm64) на обычные Android-телефоны:
без шлема, в полноэкранном ландшафте, с сенсорным управлением на всю игру и гироскопом по желанию.

**В репозитории нет файлов игры** — только собственная работа порта: нативная библиотека перехватов, Java-оверлей,
Python-инструменты сборки и патч для декодированного APK. Для сборки нужна своя копия оригинального APK, всё
остальное инструменты восстанавливают из неё у вас на компьютере. Возможности порта подробно описаны в
[PORT_FEATURES.md](PORT_FEATURES.md).

### Сборка

Нужны оригинальный APK PICO-версии 1.02 (2 061 253 795 байт, SHA-256
`d6608b54f62f3b2166292655945e8a32e717b18e07b1de010d410a68acfbe65e`), Java 17+, Android SDK `android-36`, NDK
27.2.12479018, build-tools 37.0.0, apktool 3.0.3, baksmali 3.0.x, apksigner со своим ключом подписи и Python 3.12+
с пакетами `UnityPy`, `lz4`, `numpy`, `texture2ddecoder`, `astc-encoder-py`.

```sh
cp work/local.example.json work/local.json     # указать пути к инструментам и ключ подписи
python work/tools/prepare_app.py /путь/к/original.apk --release
python work/tools/build_port.py apk --release
adb install -r out/base.apk
```

Релизная сборка (`--release`) не пишет логов: нет файла журнала и консоли разработчика, а на Android 11+ процесс
игры молчит в logcat. Без `--release` получается сборка разработчика с ограниченным `roomfix.log` и консолью
команд при наличии файла `roomfix.dev`.

Собранный APK содержит ресурсы игры: используйте его сами и не распространяйте.

### Лицензия

Код и документация порта распространяются по GNU GPL версии 3 или (на ваш выбор) любой более поздней версии
(`GPL-3.0-or-later`, файл [LICENSE](LICENSE)). Игра *The Room VR: A Dark Matter* принадлежит Fireproof Studios;
проект не связан с Fireproof Studios и PICO, игра и её ресурсы не входят в репозиторий и не подпадают под эту
лицензию.
