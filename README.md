# RDR2 Controller Rumble

A Script Hook RDR2 mod that adds rumble that follows what happens in the game: footsteps, hoofbeats, guns, fights, wagons, water, cutscenes and more. It's best with a **DualSense**, which also gets adaptive triggers per weapon and a lightbar. Xbox controllers get the rumble.

Formerly "RDR2 DSX Rumble". The files keep the old `RDR2DSXRumble` name so updates replace the old version.

- Download: [Releases](https://github.com/Zyancalili/RDR2-DSX-Rumble/releases) or [Nexus Mods](https://www.nexusmods.com/reddeadredemption2/mods/11490)
- Story mode only.

## Which version?

| Controller | Version | Where |
|---|---|---|
| Xbox | main | branch `main`, [Releases](https://github.com/Zyancalili/RDR2-DSX-Rumble/releases), Nexus main file |
| DualSense with [DSX](https://store.steampowered.com/app/1812620/DSX/) | main | same as above |
| DualSense without DSX | DualSense Native (experimental) | branch [`dualsense-native`](https://github.com/Zyancalili/RDR2-DSX-Rumble/tree/dualsense-native), Nexus optional file |

## Requirements

- [Script Hook RDR2](https://www.dev-c.com/rdr2/scripthookrdr2/), for both versions
- [DSX](https://store.steampowered.com/app/1812620/DSX/), only for a DualSense with the main version
- [Lenny's Mod Loader](https://www.rdr2mods.com/downloads/rdr2/tools/76-lennys-mod-loader-rdr/), only for PlayStation buttons with the Native version

## Install: main (Xbox, or DualSense with DSX)

1. Install Script Hook RDR2.
2. Copy `RDR2DSXRumble.asi` and `RDR2DSXRumble.ini` into the game folder, next to `RDR2.exe`.
3. DualSense only: in DSX turn on **Incoming UDP** (Settings > Networking, port 6969) and turn **Rumble to Haptics off**. Start DSX before the game.

## Install: DualSense Native (no DSX)

1. Install Script Hook RDR2.
2. Copy `RDR2DSXRumble.asi` and `RDR2DSXRumble.ini` from the Native zip (Nexus optional file) into the game folder.
3. For PlayStation buttons: install Lenny's Mod Loader, then copy the `lml` folder from the zip into the game folder.
4. In Steam: RDR2 > Properties > Controller > Steam Input **on**. Keep DSX closed.

Press **F11** in game for the settings menu. Everything else is in `RDR2DSXRumble.ini`, which reloads while the game runs. The log is `RDR2DSXRumble.log` in the game folder.

## How it works

- **main:** rumble goes to the controller the game sees: the DSX virtual DualShock 4 over HID, or an Xbox pad over XInput when no DSX pad is found. The DSX UDP API has no rumble command. Adaptive triggers and the lightbar go to DSX over UDP (`127.0.0.1:6969`).
- **DualSense Native:** the mod opens the DualSense over HID itself (USB or Bluetooth) and sends rumble, triggers and lightbar in the controller's own output report. With Steam Input on, Steam hides the pad from the normal device list inside the game, so the mod also looks in the config manager list.
- **PlayStation buttons (Native):** `tools/make_ps_prompts.py` on the `dualsense-native` branch copies the PS4 icons over the Xbox ones in your own exported `gamepad_icons_pc.gfx`. That file comes from the game, so it is not in this repo.
- Game state comes from natives via Script Hook RDR2. The exports are resolved at runtime, so no SDK is needed to build. Native hashes are from [alloc8or's RDR3 native database](https://github.com/alloc8or/rdr3-nativedb-data).
- On startup the main version checks the latest [GitHub release](https://github.com/Zyancalili/RDR2-DSX-Rumble/releases/latest) (falling back to `version.txt`) and shows a short notice if there is a newer version (`CheckForUpdates=0` turns this off). The Native version reads `version_native.txt`.

## Build

```
build.bat C:\path\to\llvm-mingw\bin
```

Or run `build.bat` from a Visual Studio Developer prompt. The source is `src/RDR2DSXRumble.cpp` (plus `src/menu.inl` for the F11 menu and update check). The `dualsense-native` branch builds the Native version and adds `src/dualsense.inl` for the DualSense reports.

Other files: `tools_media.ps1` renders the Nexus images in `media/`, `NEXUS_DESCRIPTION.txt` is the Nexus page text, and `install_version.ps1` copies a zip from `releases/` into the game folder.

## License

BSD 2-Clause, see [LICENSE](LICENSE).

This mod is vibecoded: it was written with the help of AI (Claude).
