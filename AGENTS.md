# Codex Pet Project — Agent Guide

## Project Overview

This repository contains two related parts:

1. **`hatch-pet/`** — A deterministic pipeline and AI-agent skill for creating, validating, and packaging Codex-compatible animated desktop pets. It is governed by the `hatch-pet` skill specification and uses Python scripts with Pillow for raster processing.

2. **`my-pet/`** — A collection of pre-built custom pets (each is a `pet.json` manifest plus a `spritesheet.webp` atlas) that conform to the Codex Pet Contract.

The repository also includes `plan.txt`, which outlines a downstream goal: building a lightweight, standalone Windows executable (< 30 KB) that loads the pets from `my-pet/`, previews them, and spawns them on the desktop.

## Directory Structure

```
.
├── hatch-pet/                 # Pet creation skill and tooling
│   ├── SKILL.md               # Full skill spec (539 lines)
│   ├── agents/
│   │   └── openai.yaml        # Agent interface metadata
│   ├── references/
│   │   ├── animation-rows.md  # Frame counts and durations per state
│   │   ├── codex-pet-contract.md   # Atlas format and manifest shape
│   │   └── qa-rubric.md       # Visual QA acceptance criteria
│   ├── scripts/               # Deterministic Python processing scripts
│   │   ├── compose_atlas.py
│   │   ├── derive_running_left_from_running_right.py
│   │   ├── extract_strip_frames.py
│   │   ├── inspect_frames.py
│   │   ├── make_contact_sheet.py
│   │   ├── prepare_pet_run.py
│   │   ├── render_animation_previews.py
│   │   └── validate_atlas.py
│   └── LICENSE.txt            # Apache License 2.0
├── my-pet/                    # Packaged custom pets
│   ├── ace/
│   ├── agumon/
│   ├── alexander/
│   ├── anya/
│   └── apupepe/
│       └── (each contains pet.json + spritesheet.webp)
├── plan.txt                   # User's exe-build plan (Chinese)
└── AGENTS.md                  # This file
```

## Technology Stack

- **Language:** Python 3 (scripts use `from __future__ import annotations`)
- **Primary Dependency:** Pillow (PIL) for image I/O, compositing, alpha handling, WebP export, and drawing
- **Shell Utilities:** `jq` for JSON manifest manipulation in shell snippets documented in `SKILL.md`
- **No formal package manager files** (no `pyproject.toml`, `setup.py`, `requirements.txt`, `package.json`, or `Cargo.toml` are present)
- **License:** Apache License 2.0

## Codex Pet Contract (Data Format)

Every pet under `my-pet/<pet-id>/` must provide exactly two files:

- **`pet.json`** — Manifest with keys:
  - `id` — machine name (kebab-case)
  - `displayName` — human-readable name
  - `description` — one short sentence
  - `spritesheetPath` — always `"spritesheet.webp"` (local reference)

- **`spritesheet.webp`** — Sprite atlas with strict geometry:
  - Dimensions: `1536 x 1872` pixels
  - Grid: `8 columns x 9 rows`
  - Cell size: `192 x 208` pixels
  - Background: transparent
  - Unused cells: fully transparent (no RGB residue)

### Animation States

| Row | State         | Frames | Duration notes (from contract)                |
|-----|---------------|--------|-----------------------------------------------|
| 0   | idle          | 6      | 280, 110, 110, 140, 140, 320 ms               |
| 1   | running-right | 8      | 120 ms each, final 220 ms                     |
| 2   | running-left  | 8      | 120 ms each, final 220 ms                     |
| 3   | waving        | 4      | 140 ms each, final 280 ms                     |
| 4   | jumping       | 5      | 140 ms each, final 280 ms                     |
| 5   | failed        | 8      | 140 ms each, final 240 ms                     |
| 6   | waiting       | 6      | 150 ms each, final 260 ms                     |
| 7   | running       | 6      | 120 ms each, final 220 ms                     |
| 8   | review        | 6      | 150 ms each, final 280 ms                     |

## Key Scripts

All scripts live in `hatch-pet/scripts/` and are self-contained CLI tools.

| Script                                    | Purpose                                                        |
|-------------------------------------------|----------------------------------------------------------------|
| `prepare_pet_run.py`                      | Creates a run folder, prompts, layout guides, and `imagegen-jobs.json` |
| `extract_strip_frames.py`                 | Splits generated horizontal row strips into `192x208` frames   |
| `inspect_frames.py`                       | Validates extracted frames and writes `qa/review.json`          |
| `compose_atlas.py`                        | Assembles frames into the final `1536x1872` atlas               |
| `validate_atlas.py`                       | Checks atlas geometry, transparency, and RGB residue            |
| `make_contact_sheet.py`                   | Generates a contact-sheet PNG for human/visual QA               |
| `render_animation_previews.py`            | Renders per-row preview GIFs from extracted frames              |
| `derive_running_left_from_running_right.py` | Mirrors `running-right` frames into `running-left` slot-by-slot |

## Build and Test Commands

- **No formal test suite is present.** Validation is done by running the scripts above against a pet run folder and inspecting the resulting `qa/` artifacts.
- **Typical validation flow** (after a run is prepared and decoded images exist):
  ```bash
  python hatch-pet/scripts/extract_strip_frames.py --decoded-dir ./run/decoded --output-dir ./run/frames --states all --method auto
  python hatch-pet/scripts/inspect_frames.py --frames-root ./run/frames --json-out ./run/qa/review.json --require-components
  python hatch-pet/scripts/compose_atlas.py --frames-root ./run/frames --output ./run/final/spritesheet.png --webp-output ./run/final/spritesheet.webp
  python hatch-pet/scripts/validate_atlas.py ./run/final/spritesheet.webp --json-out ./run/final/validation.json
  python hatch-pet/scripts/make_contact_sheet.py ./run/final/spritesheet.webp --output ./run/qa/contact-sheet.png
  python hatch-pet/scripts/render_animation_previews.py --frames-root ./run/frames --output-dir ./run/qa/previews
  ```

## Code Style Guidelines

- Python 3 with `from __future__ import annotations`
- Use `pathlib.Path` for filesystem paths
- Use `argparse` for CLI interfaces
- Use type hints where practical
- Constants for geometry are defined at module level (e.g., `CELL_WIDTH = 192`)
- Scripts are idempotent and deterministic; they do not invoke image-generation APIs directly

## Security Considerations

- The Python scripts only read/write image and JSON files via explicit CLI arguments; they do not execute shell commands or evaluate arbitrary code.
- `plan.txt` describes a future executable that will spawn desktop pets. Any implementation of that executable should:
  - Load pet data only from the known `my-pet/` tree
  - Avoid executing untrusted code or shelling out to system commands
  - Keep the spritesheet parser strict (reject malformed atlases that violate the `1536x1872` contract)

## Notes for Agents

- The authoritative source for the pet contract is `hatch-pet/references/codex-pet-contract.md`.
- The authoritative QA criteria are in `hatch-pet/references/qa-rubric.md`.
- The full skill workflow is documented in `hatch-pet/SKILL.md`.
- There is no CI/CD configuration, no dependency lockfile, and no package manifest in this repo. If you add one, document it here.
