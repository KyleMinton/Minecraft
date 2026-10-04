# RaylibApp

A Minecraft Beta 1.8 clone in C++ with [raylib](https://www.raylib.com/) 5.5.

The game is split in two:

- **RaylibGame.dll**: all of the game code, with raylib built in.
- **RaylibApp.exe**: a small launcher that loads the DLL and starts the game.

## Requirements

- **Windows 10 or 11**
- **Visual Studio 2022** (or Build Tools for Visual Studio 2022) with the
  **Desktop development with C++** workload (the MSVC compiler and Windows SDK)
- **CMake 3.16 or newer** (`cmake --version`)
- **Git**, which CMake uses to download raylib
- An internet connection the first time you configure the project
- **Minecraft Java Edition** (1.21, and Beta 1.8's sounds): the textures and
  sounds are Mojang's and aren't in this repository; see [Game assets](#game-assets)

## Building

Run all commands from the project folder (the one with `CMakeLists.txt`).

### 1. Configure

```bash
cmake -S . -B build
```

The first time, this downloads raylib 5.5 from GitHub into `build/_deps`.
That takes a minute or two.

### 2. Build

For a fast, optimized build:

```bash
cmake --build build --config Release
```

For a build you can step through in a debugger (slower in game):

```bash
cmake --build build --config Debug
```

The outputs go to `build/Release/` or `build/Debug/`:

```
RaylibApp.exe
RaylibGame.dll
```

The exe needs the DLL next to it.

### 3. Run

Start the game from the project folder, so it finds `resources/`:

```bash
./build/Release/RaylibApp.exe
```

If `resources/` is next to the exe (as it is when installed), the game uses
that copy wherever you start it from.

In Visual Studio, open `build/RaylibApp.sln`, pick the configuration, and
press F5. RaylibApp is the startup project and already runs from the project
folder.

## Installing

This puts the exe, the DLL and `resources/` in `C:\Program Files\RaylibApp`.
Run it from an **administrator** terminal, after a Release build:

```bash
cmake --install build --config Release --component Game
```

Keep `--component Game`. Without it, raylib's own install rules also copy its
library and headers into the folder.

To install somewhere else, add `--prefix "C:/Games/RaylibApp"` (that doesn't
need administrator rights if you can write to the folder).

To update an installed copy, build again and run the install command again.

## Saves

Worlds are stored in `%APPDATA%\RaylibApp\saves`, so the game can run from a
read-only folder like Program Files. The first time it runs, it copies any
worlds from an old `saves/` folder in the directory it was started from.

Settings from the Options menu are saved in `%APPDATA%\RaylibApp\options.txt`.

## Game assets

The textures, sounds and music are Mojang's, so they aren't in this repository
(`resources/textures` and `resources/sounds` are git-ignored). Make them from
your own copy of Minecraft.

### Textures

The game uses Minecraft 1.21's textures, laid out the way it reads them
(Beta-style `terrain.png`, `items.png` and `particles.png` atlases).
`tools/ModernTextures.cpp` builds them:

1. Run Minecraft 1.21 once from the launcher, so its files are downloaded.
2. Unzip `.minecraft/versions/1.21/1.21.jar` (it's a zip file) into a folder
   named `.minecraft/versions/1.21/1.21`, so that
   `.minecraft/versions/1.21/1.21/assets/minecraft/textures` exists.
3. Build and run the tool from the project folder:

```bash
cmake --build build --config Release --target ModernTextures
```

```bash
./build/Release/ModernTextures.exe "C:/Users/<you>/AppData/Roaming/.minecraft/versions/1.21/1.21/assets/minecraft" resources
```

It also copies the title-screen panorama from the launcher's downloads. Run it
again after changing which tile a block uses. A few things 1.21 has no texture
for (the sides of beds, the bed item) come out blank.

### Sounds and music

Sounds go in `resources/sounds`, named as `resources/sounds/README.txt`
describes: Minecraft Beta 1.8's sound files, and its music in
`resources/sounds/music`. The title-screen music (1.21's `menu1.ogg` to
`menu4.ogg`) goes in `resources/sounds/music/menu`. The game runs without any
of them, silently.

## Blocks after Beta

The blocks from later versions (1.21's building and colored blocks) are listed in
`tools/modern_blocks.py`, which writes `src/ModernBlocksData.h`: each block's
shape, textures and hardness, and the order of the Building Blocks and Colored
Blocks creative tabs. To add blocks, edit the script, then run it and rebuild
the textures:

```bash
python tools/modern_blocks.py
```

Block IDs are positions in that list, and saved worlds store them, so the
script only ever appends new blocks to the end.

Water, lava, fire and portal animate from the strips in `resources/textures/animated`.
Menu music plays from `resources/sounds/music/menu`.

## Project layout

```
CMakeLists.txt     build and install rules
tools/             ModernTextures, which rebuilds resources/textures from 1.21
src/               game source (src/Launcher.cpp is the exe; everything else is the DLL)
resources/         textures, sounds and shaders the game loads at runtime
build/             created by CMake; safe to delete
```

## Troubleshooting

- **"cannot open file RaylibApp.exe" or a link error while building:** the game is
  still running and Windows has the exe or DLL locked. Close the game and build
  again.
- **The game starts with missing or pink textures, or no sound:** it can't find
  `resources/`. Start it from the project folder, or install it.
- **"file cannot create directory: C:/Program Files/...":** the install needs an
  administrator terminal.
- **Configure fails while downloading raylib:** check that Git is installed and
  on your PATH, and that you're online. Then delete `build/` and configure again.
