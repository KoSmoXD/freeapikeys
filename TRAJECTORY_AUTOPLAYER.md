# Trajectory AutoPlayer (Android cube beta v0.2.0)

**Geometry Dash Android 2.2081 · Geode 5.5.3**

## What's new in v0.2.0
The mod now makes **actual automated jump inputs** in ordinary forward-moving **cube mode**.

When **Enable cube auto-jump** is on, the code:
1. Measures player speed from game movement each frame.
2. Asks Geometry Dash for damaging objects near the player.
3. Predicts separate jump and no-jump trajectories.
4. Presses jump briefly if the no-jump path is about to hit a hazard and the jump path is predicted to avoid it.
5. Releases the button after a game update.

The path colors are **red = no jump**, **green = jump**.

## Installation
1. Open [GitHub Actions](https://github.com/KoSmoXD/freeapikeys/actions) and find the latest **successful** Android Geode build.
2. Download the **trajectory-autoplayer-Android-Universal** artifact.
3. Extract its ZIP, then import the enclosed `.geode` via Geode.
4. Remove any older installed version, restart the game, open this mod's settings and enable **Enable cube auto-jump**.
5. Test on a simple cube level in Practice Mode first.

## Limits
- This is **experimental**, not a full level-solving AI.
- Simple cube spikes may be recognized; complex levels, moving hazards, speed/gravity portals, orbs, multi-object collisions, ceilings and platforms are not fully simulated.
- Ship, ball, wave, UFO, robot, spider, swing, and platformer modes are not supported.
- Predicted physics uses an approximation; late/early clicks and deaths are possible.
- GitHub Actions compiling successfully does **not** prove it works correctly on your device.
- Use in games at your discretion and consider account/game rules.

## Developer notes
- Code: `src/main.cpp`
- Build: `.github/workflows/android.yml`
- Mod configuration: `mod.json`
