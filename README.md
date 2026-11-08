# RDR2 DSX Rumble

A ScriptHookRDR2 `.asi` that gives the DualSense rumble that follows what happens in the game. It also drives the adaptive triggers and the lightbar through DSX.

## What it does

| Situation | Rumble | DSX (UDP) |
|---|---|---|
| Walking / running / sprinting | A pulse on every footstep, taken from the foot bones and stronger at higher speed. Left foot uses the left motor, right foot the right motor (XInput backend). If the bones don't work, steps are paced from speed instead. | – |
| Horse | Hoofbeats that change with the gait: walk (4-beat), trot (2-beat), canter (3-beat), gallop (rolling 4-beat). Also landings after jumps, a kick when spurring, and a jolt when the horse is hit. | Galloping effect in both triggers while cantering or galloping |
| Shooting | A kick for each shot, set per weapon class: pistol < revolver < repeater < rifle < sniper/elephant < shotgun. Bow and thrown weapons get a light pulse. | RT: trigger break for each class (SemiAutomaticGun), Bow draw for bows, tension for the lasso. LT: light aim resistance |
| Reloading | Short tick | – |
| Taking damage / death | Strength scales with the damage taken | Lightbar flashes red |
| Fall / jump landing | Scales with how fast you were falling | – |
| Wagon, coach, train | Rough base rumble plus random bumps, scaled by speed | – |
| Menu button presses | A tick on each press in the start menu, pause menu, map, journal, catalogs and shops. Cross is firmer, circle softer, the D-pad gives a light tick | – |
| Startup splash | A strong kick on the gunshot in the startup splash, detected from the game's audio level | – |
| Low health | Heartbeat double-pulse that speeds up as health drops | Lightbar pulses. Its colour follows health (green → yellow → red) |

Rumble is off in cutscenes or menus without player control, and with keyboard/mouse (setting `OnlyWithController`).

## Why rumble doesn't go over UDP

The DSX v3 UDP API only accepts trigger, RGB, player-LED, mic-LED and reset instructions. It has no rumble command. So the mod sends rumble to the controller the game sees, which is the DSX virtual pad, and DSX passes it on to the DualSense (rumble, or rumble-to-haptics if you enabled that in DSX):

* `Backend=Game` (default): uses the game's vibration (`SET_CONTROL_SHAKE`). Keep in-game vibration on.
* `Backend=Direct`: writes rumble reports straight to DSX's virtual DualShock 4 (or XInput if DSX emulates an Xbox pad). This is more precise and gives left/right separation.

Menus and the startup splash always use the direct path, because the game's scripts aren't running there. A background thread reads the buttons from the virtual pad and measures the game's audio level.

## Install

1. Copy `RDR2DSXRumble.asi` and `RDR2DSXRumble.ini` into
   `E:\SteamLibrary\steamapps\common\Red Dead Redemption 2`. ScriptHookRDR2 and the ASI loader are already installed there.
2. In DSX: turn on Incoming UDP (port 6969, the default) and use an emulated Xbox controller.
3. **Conflicts:** `NodixDualSense.asi` (DualSense haptics/triggers) is also in the game folder. Running it alongside this mod can cause doubled effects and a fight over the triggers. If that happens, rename it to `.asi.off`.

Settings are in `RDR2DSXRumble.ini` and reload while the game runs. The log is written to `RDR2DSXRumble.log` in the game folder.

## Build

```
build.bat C:\path\to\llvm-mingw\bin
```
You can also run `build.bat` from a Visual Studio Developer prompt. The source is `src/RDR2DSXRumble.cpp` and needs no SDK, because the ScriptHookRDR2 exports are resolved at runtime.
