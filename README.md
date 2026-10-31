# Frame Window Counter (Geode mod)

Pause menu -> **FWC** button (top right) -> pick a `.gdr2` macro -> **Analyze**.

## Build the .geode (pick one)

**A) GitHub Actions (no local setup)**
1. Create a GitHub repo and push this folder.
2. Open the *Actions* tab -> *Build Geode Mod* -> when finished, download the **Build Output** artifact.
3. It contains `yourname.frame-window-counter.geode`.

**B) Locally**
1. Install the Geode CLI and SDK (https://docs.geode-sdk.org/getting-started/), `geode sdk install`.
2. In this folder: `geode build` -> the `.geode` appears in `build/`.
3. Or drop it into `<Geometry Dash>/geode/mods/`.

Before building edit `mod.json`: replace `yourname` in `id`, set `developer`, and make `"geode"` match your installed SDK version (`geode sdk version`).

## Macros
Put `.gdr2` files in the mod's save folder `macros/` (the **Folder** button opens it) or set *Extra macro folder* in the mod settings.

## How the windows are measured
For each click the mod plays the macro in a fast, headless loop, saves a checkpoint a few frames before the click, then re-runs that section with the click moved 1, 2, 3... frames earlier and later (hold length preserved by default). A shift is valid if the player survives until the next click + the "survival margin". Window = 1 + valid earlier shifts + valid later shifts. Results are capped at 2 x *max shift* + 1.
Adjacent inputs on the same player limit shifts (a click can't pass its neighbours).

## Known limits
- Classic mode only (platformer macros are rejected).
- Needs the macro's TPS to be what the game runs at (240 by default; other TPS need a physics bypass).
- Checkpoint-based rewinds are as accurate as the game's practice checkpoints; levels with random/complex trigger state may give slightly off windows.
- Analysis restarts the level from the start when it begins and when it ends.
- Written against the Geode v5 API without being compiled by the author; if the compiler complains, the likeliest spots are `handleButton`, `createCheckpoint`/`loadFromCheckpoint`, and `PlayLayer::destroyPlayer` signatures in `Analyzer.cpp` / `main.cpp`.
