# cpw-rebuild

A clean-room native Win32/WIC rebuild of the original `cpw.exe`.

## Goals

- Smaller executable size
- Lower runtime overhead
- Simpler and safer code structure
- Compatibility with the existing `pet.json` + `spritesheet.webp/png` asset format

## Current features

- Scans pets from `DIGIT_PET_PATH`, `./my`, or `../my`
- Loads metadata from `pet.json`
- Loads spritesheets through WIC
- Shows a selector window
- Renders a transparent desktop pet window
- Supports dragging, reselection, and `Esc` to exit

## Build

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

## Runtime assets

The executable expects a pet asset directory containing folders like:

```text
my/<pet-id>/pet.json
my/<pet-id>/spritesheet.webp
```

or:

```text
my/<pet-id>/spritesheet.png
```
