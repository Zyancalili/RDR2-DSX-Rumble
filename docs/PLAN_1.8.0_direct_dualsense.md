# PLAN v1.8.0 (experimental): DualSense without DSX

You are the engineer implementing v1.8.0 of **RDR2 Controller Rumble** (ScriptHookRDR2 ASI, C++17, one translation unit). This file is your full brief. Read `src\RDR2DSXRumble.cpp` (1929 lines) and `src\menu.inl` (743 lines) completely before the first edit. Line numbers below are from commit `bbd69b5` (branch `experimental-1.8.0`); every edit also quotes anchor text, because lines shift as you go.

Work only on branch `experimental-1.8.0`. `main` holds the finished v1.7.1 and stays untouched. Do not push, tag, merge, zip a release, re-render media, or touch `version.txt`, `README.md`, `NEXUS_DESCRIPTION.txt` or `releases\` (`version.txt` on `main` drives the update notice of old versions). The normal release routine does **not** apply to this branch.

Build (PowerShell), after every step:

```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cd /d \"C:\Users\elfge\Desktop\rdr2 rumble\" && cl /nologo /std:c++17 /O2 /EHsc /MT /W3 /D_CRT_SECURE_NO_WARNINGS src\RDR2DSXRumble.cpp /LD /Fe:RDR2DSXRumble.asi /Fo:%TEMP%\ 2>&1"
```

A step is done when this prints no error and no new warning.

---

## 1. Summary and architecture

**Goal.** A second output mode in which the mod drives a real DualSense itself over HID: adaptive triggers, lightbar, rumble and the menu-button clicks, with DSX closed and Steam Input on. DSX stays the default and keeps working exactly as in v1.7.1.

**Setup the new mode must work in (given by the user, do not plan around it):** RDR2 does not read the DualSense natively. Without DSX the game sees the controller only through Steam Input, which shows the game a virtual Xbox pad (XInput). So: Steam Input ON, DSX closed.

**Decisions.**

| Topic | Decision | Why |
|---|---|---|
| Switch | `[DSX] DirectDualSense=0/1`, default 0. `cfg.direct`. F11 row "DualSense without DSX". | A plain 0/1 key works with the existing toggle row (kind 1) with no menu special case. A text key (`Output=DSX/Direct`) would need three special cases in `menu.inl`, and "Direct" already means something else in `[Rumble] Backend`. An old ini without the key gives 0, so v1.7.x users see no change. |
| Master switch | `[DSX] Enabled` (`cfg.dsx`) keeps meaning "DualSense features on" in both modes. `AdaptiveTriggers`, `Lightbar`, `GallopingTriggersOnHorse` apply to both. | No new keys. |
| Who writes to the pad | One writer: `PadThread`. `Tick()`, `ResetDSX()` and the menu only publish the wanted state in three atomics. | The file already passes state between the threads with atomics (`g_gameL`, `g_padButtons`). |
| Triggers, lightbar | The mod writes them to the real DualSense (USB report 0x02, Bluetooth 0x31). | Nothing else can: Steam's virtual Xbox pad has neither. |
| The mod's own rumble (`Backend=Direct`, the default), menu clicks, startup shot | The mod writes the motors of the real DualSense in the same report. | See "Rumble flags" below: a report of ours without the rumble flags probably stops rumble that came in through Steam, so mod rumble must travel in the mod's own reports. It also keeps the menu clicks independent of Steam. |
| `Backend=Game` | Unchanged: `SET_CONTROL_SHAKE` -> game -> XInput -> Steam -> DualSense. Works, but our trigger and lightbar writes may cut short holes into it (see risks). `Backend=Direct` is the recommended partner of the new mode. | No code needed. Not forced, so the user can compare the feel. |
| The game's own vibration | Stays with Steam (XInput -> Steam -> motors). We never set the motor flags while the mod has nothing to say, so it passes, as it does today on DSX's virtual pad. | Same "last writer wins" model as v1.7.1. |
| Buttons (F11 menu, menu clicks, weapon wheel L1) | Parsed from the DualSense input report on the handle we already hold. | Three lines on top of `FromDS4`, and it does not depend on whether Steam's XInput hooks reach our calls. |
| No DualSense found in Direct mode | Fall back to the existing XInput path (Steam's virtual pad or a real Xbox pad) for rumble and buttons; no triggers and no lightbar; one clear log line. | Existing code, `Pad::EnsureOpen` lines 525-529. |
| Player LEDs, mic LED, audio | Never touched (their valid flags stay 0). | Not ours. |

**Rumble flags (why every report states only what the mod owns).** The DualSense applies a field only when its valid flag is set, so we set the trigger flags only for triggers, the lightbar flag only for the lightbar. The two rumble bits behave differently: SDL (which Steam's controller code is built on) writes "Leaving emulated rumble bits off will restore audio haptics" and stops rumble by sending a report without them. So a report from us without the rumble bits most likely ends rumble that Steam started (**unverified on hardware, U1**). Consequences built into the design:

- while the mod rumbles, every report of ours carries the motor values and the rumble bits;
- while the mod is silent, our reports leave the rumble bits off, and we send as few reports as possible (changes, plus one re-assert every 2 s);
- Steam's rumble reports carry no trigger or lightbar flag, so they do not disturb ours.

**Steam Input may overwrite the lightbar or triggers (unverified, U2).** Mitigation: the full wanted state is re-sent every 2 s while the game has focus. Diagnostic: with `Debug=1` the log shows every write of ours and the trigger state the pad itself reports back (section 8), so a trigger that went loose without a write of ours is visible.

**The go/no-go risk (U3).** Steam's overlay DLL inside the game process hooks HID discovery. I read the strings of `C:\Program Files (x86)\Steam\GameOverlayRenderer64.dll`: it names `SetupDiGetClassDevsA/W`, `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`, `HidD_GetAttributes`, `HidD_GetPreparsedData`, `HidP_GetCaps`, `CreateFileA/W`, `NtCreateFile`, `CM_Get_Device_IDA` and the XInput functions of `xinput1_1..1_4.dll`. `ReadFile`, `WriteFile`, `HidD_GetFeature` and `CM_Get_Device_Interface_List` are not named. Whether these hooks hide the physical DualSense from code inside `RDR2.exe` could not be checked offline. Step 3 produces a build that answers it with one game start; the contingencies are in section 11.

**Verified facts** (sources fetched and read on 2026-11-15; the controller facts were read from the user's DualSense on this PC, read-only):

- Linux `drivers/hid/hid-playstation.c`: `struct dualsense_output_report_common` (47 bytes), flag bits, Bluetooth header, CRC, input report struct, PIDs 0x0CE6 / 0x0DF2 (`hid-ids.h`), firmware "update version" at byte 44 of feature report 0x20, vibration v2 from version 2.21.
- SDL `SDL_hidapi_ps5.c`: `DS5EffectsState_t` gives the trigger blocks (right at offset 10, left at 21) and the enable bits (0x04 right trigger, 0x08 left trigger); USB report size 48, Bluetooth 78; CRC over 0xA2 + report; the simple Bluetooth input report.
- Nielk1's gist (raw file, read directly): the encoders for 0x05, 0x21, 0x22, 0x23, 0x25 with their parameter checks.
- This PC, DualSense on USB while Steam runs: `HidP_GetCaps` gives input 64, **output 48**, feature 64 bytes; opening with read/write and shared access works; feature report 0x20 reads, update version 0x0630 (major 6, minor 48 in the driver's reading; the v2 threshold 2.21 is 0x0215, so this pad has vibration v2). The Linux driver's USB size of 63 is the newer descriptor; always use the length from `HidP_GetCaps`.
- Correction to the earlier research: none of the offsets were wrong. New: valid_flag2 at [39] (bit 2 = vibration v2, bit 1 = lightbar setup) and lightbar_setup at [42].

---

## 2. Device discovery

Rework `Pad::OpenDS4` (lines 478-517) into `Pad::OpenHid(bool dualsense)`. Same SetupAPI loop, same open flags (`GENERIC_READ | GENERIC_WRITE`, `FILE_SHARE_READ | FILE_SHARE_WRITE`, `FILE_FLAG_OVERLAPPED`: Steam holds the pad open too, shared access is required and was verified).

New state next to `Pad::h` (lines 455-460):

```cpp
    static bool       isDS = false, isBT = false, rumbleV2 = false;   // real DualSense open / over Bluetooth / firmware 2.21+
    static bool       fresh = false;                                  // just (re)opened: write the whole state once
    static uint8_t    seq = 0;                                        // Bluetooth report sequence, 0..15
    static uint8_t    inBuf[256], outBuf[1024];                       // was outBuf[256]: the Bluetooth output length may be 547 (unverified)
```

`Close()` (line 473): also `isDS = false;`.

Outline of `OpenHid` (keep the existing structure and the existing DS4 log text):

```cpp
    // dualsense = false: DSX's virtual DualShock 4 (054C:05C4 / 09CC). true: the real DualSense (0CE6) or DualSense Edge (0DF2).
    static bool OpenHid(bool dualsense) {
        ...SetupDiGetClassDevsA / SetupDiEnumDeviceInterfaces / SetupDiGetDeviceInterfaceDetailA as now...
        int seen = 0, denied = 0; DWORD deniedErr = 0;
        for (...) {
            ...det filled...
            seen++;
            std::string path = det->DevicePath;                       // USB "...vid_054c&pid_0ce6...", Bluetooth "...vid&0002054c_pid&0ce6..."
            for (char& ch : path) ch = (char)tolower((unsigned char)ch);
            bool looksDS = path.find("054c") != std::string::npos &&
                           (path.find("0ce6") != std::string::npos || path.find("0df2") != std::string::npos);
            HANDLE d = CreateFileA(...as now...);
            if (d == INVALID_HANDLE_VALUE) { if (dualsense && looksDS) { denied++; deniedErr = GetLastError(); } continue; }
            HIDD_ATTRIBUTES a{}; a.Size = sizeof a;
            bool match = HidD_GetAttributes(d, &a) && a.VendorID == 0x054C &&
                         (dualsense ? (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2)
                                    : (a.ProductID == 0x05C4 || a.ProductID == 0x09CC));
            if (match) {
                PHIDP_PREPARSED_DATA pp;
                if (HidD_GetPreparsedData(d, &pp)) {
                    HIDP_CAPS caps;
                    if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                        bool bt = dualsense && caps.InputReportByteLength != 64;   // USB input report = 64 bytes, Bluetooth = 78
                        USHORT need = dualsense ? (bt ? 78 : 48) : 11;
                        if (caps.OutputReportByteLength >= need && caps.OutputReportByteLength <= sizeof outBuf) {
                            inLen = std::min<USHORT>(caps.InputReportByteLength, sizeof inBuf);
                            outLen = caps.OutputReportByteLength;
                            h = d; ok = true; isDS = dualsense; isBT = bt; fresh = true; seq = 0;
                            if (dualsense) {
                                // firmware "update version" (feature report 0x20, bytes 44-45): 2.21+ has the improved rumble
                                // emulation. On Bluetooth, reading a feature report also switches the pad to its full reports.
                                uint8_t fw[256] = {0x20};
                                bool got = HidD_GetFeature(d, fw, std::clamp<USHORT>(caps.FeatureReportByteLength, 64, sizeof fw)) != 0;
                                int ver = fw[44] | (fw[45] << 8);
                                rumbleV2 = a.ProductID == 0x0DF2 || (got && ver >= 0x0215);
                                Log("pad: DualSense%s %04X:%04X over %s (in %u, out %u bytes, firmware %d.%02d%s, rumble %s)",
                                    a.ProductID == 0x0DF2 ? " Edge" : "", a.VendorID, a.ProductID, bt ? "Bluetooth" : "USB",
                                    inLen, outLen, ver >> 8, ver & 255, got ? "" : " (not readable)", rumbleV2 ? "v2" : "v1");
                                if (cfg.debug) Log("pad: path %s", det->DevicePath);
                            } else Log("pad: virtual DualShock 4 ...exactly as now...");
                        } else if (dualsense) Log("pad: DualSense found, but its output report is %u bytes (need %u..%u)", caps.OutputReportByteLength, need, (unsigned)sizeof outBuf);
                    }
                    HidD_FreePreparsedData(pp);
                }
            }
            if (!ok) CloseHandle(d);
        }
        SetupDiDestroyDeviceInfoList(set);
        if (ok) { ...create rov / wov events as now... }
        else if (dualsense) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                if (denied) Log("pad: a DualSense is connected but cannot be opened (error %lu). HidHide or another tool hides it from the game: "
                                "add RDR2.exe to HidHide's allowed applications, or switch 'DualSense without DSX' off in F11.", deniedErr);
                else        Log("pad: no DualSense found for the direct output (%d HID devices seen). If it is connected, something hides it "
                                "from the game (HidHide or Steam Input). Rumble falls back to XInput; adaptive triggers and lightbar are off.", seen);
            }
        }
        return ok;
    }
```

- **USB or Bluetooth:** `InputReportByteLength` 64 = USB, anything else (78) = Bluetooth. The path is logged in debug mode so a wrong guess can be seen.
- **Edge:** PID 0x0DF2, same reports, always vibration v2.
- **Reconnect:** unchanged mechanism. A failed read or write calls `Close()`, `EnsureOpen()` (line 519) retries every 2 s. `fresh = true` makes the first write after a reopen carry everything.
- **`EnsureOpen`** line 524: `if (OpenHid(cfg.direct)) return;`. The XInput fallback below it stays as it is. In Direct mode DSX's virtual DualShock 4 is deliberately not opened.
- **HidHide:** hidden devices either do not enumerate or fail to open; both cases log the message above once.

---

## 3. Report builder

New file `src\dualsense.inl`: pure byte code, no Windows calls, so the test in section 9 can include it. Include it in `RDR2DSXRumble.cpp` right after `AddTrigger` (after line 413), followed by the three atomics:

```cpp
#include "dualsense.inl"
// What the DualSense should show in Direct mode. The script thread publishes, the pad thread writes.
static std::atomic<uint64_t> g_dsTrigL{0}, g_dsTrigR{0};   // L2 / R2 effect, packed by DS5::Pack (0 = loose)
static std::atomic<uint32_t> g_dsRgb{0x000040};            // lightbar 0xRRGGBB (starts at Steam's default blue)
```

`build.bat` needs no change (the file is included).

### Layout (index = byte in the buffer handed to `WriteFile`)

| Field | USB (id 0x02) | Bluetooth (id 0x31) |
|---|---|---|
| report id | [0] = 0x02 | [0] = 0x31, [1] = seq << 4, [2] = 0x10 |
| valid_flag0: bit0 compatible vibration, bit1 haptics select, bit2 R2 effect, bit3 L2 effect | [1] | [3] |
| valid_flag1: bit2 lightbar, bit4 player LEDs (never set) | [2] | [4] |
| right / light motor | [3] | [5] |
| left / heavy motor | [4] | [6] |
| R2 effect, 11 bytes (mode + 10) | [11..21] | [13..23] |
| L2 effect, 11 bytes | [22..32] | [24..34] |
| valid_flag2: bit1 lightbar setup, bit2 compatible vibration v2 | [39] | [41] |
| lightbar_setup (0x02 = end the connect animation) | [42] | [44] |
| player LEDs (never written) | [44] | [46] |
| red, green, blue | [45..47] | [47..49] |
| CRC32, little endian | none | [74..77] |
| report length | 48 | 78 |

Bluetooth = USB + 2 for every shared field. The CRC is the standard CRC-32 (reflected, polynomial 0xEDB88320, start 0xFFFFFFFF, inverted at the end) over the byte 0xA2 followed by report bytes 0..73. The buffer passed to `WriteFile` is always `Pad::outLen` bytes long (`HidP_GetCaps`), zero-padded: Windows wants the length of the longest report.

Rumble flags: firmware 2.21+ (and the Edge): flag0 bit1 plus flag2 bit2. Older firmware: flag0 bit0 and bit1. Motor values are written unscaled in both cases (SDL halves them on old firmware; old firmware is rare and `Rumble strength` covers it).

### `src\dualsense.inl` (complete)

```cpp
// DualSense output reports (USB 0x02, Bluetooth 0x31): pure byte building, no Windows calls, so
// tests\dualsense_test.cpp can include this file on its own.
// Layout: Linux drivers/hid/hid-playstation.c and SDL_hidapi_ps5.c. Trigger effects: Nielk1's TriggerEffectGenerator gist.
namespace DS5 {
    // CRC-32 (reflected, 0xEDB88320). Start with 0xFFFFFFFF, invert the result.
    static uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++) {
            crc ^= p[i];
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return crc;
    }

    // One trigger effect (11 bytes) from a DSX mode and its parameters, the convention the mod already uses:
    //   13 Resistance(start 0-9, force 0-8)                          -> Feedback  0x21
    //   16 SemiAutomaticGun(start 2-7, end <= 8, force 0-8)          -> Weapon    0x25
    //   14 Bow(start, end <= 8, force 0-8, snap 0-8)                 -> Bow       0x22
    //   15 Galloping(start, end <= 9, foot1 0-6, foot2 0-7, freq)    -> Galloping 0x23
    // Every other mode, a force of 0 and parameters out of range give Off (0x05), like the gist.
    static void Trigger(int mode, const int* p, uint8_t* o) {
        memset(o, 0, 11); o[0] = 0x05;
        auto zones = [&](int a, int b) { int z = (1 << a) | (1 << b); o[1] = (uint8_t)z; o[2] = (uint8_t)(z >> 8); };
        switch (mode) {
        case 13: {   // from `start` to the end of the travel: zone bits in [1..2], 3 bits of force per zone in [3..6]
            if (p[0] < 0 || p[0] > 9 || p[1] < 1 || p[1] > 8) return;
            uint32_t force = 0; int active = 0;
            for (int i = p[0]; i < 10; i++) { force |= (uint32_t)(p[1] - 1) << (3 * i); active |= 1 << i; }
            o[0] = 0x21; o[1] = (uint8_t)active; o[2] = (uint8_t)(active >> 8);
            o[3] = (uint8_t)force; o[4] = (uint8_t)(force >> 8); o[5] = (uint8_t)(force >> 16); o[6] = (uint8_t)(force >> 24);
            return; }
        case 16:
            if (p[0] < 2 || p[0] > 7 || p[1] > 8 || p[1] <= p[0] || p[2] < 1 || p[2] > 8) return;
            o[0] = 0x25; zones(p[0], p[1]); o[3] = (uint8_t)(p[2] - 1);
            return;
        case 14:
            if (p[0] < 0 || p[1] > 8 || p[0] >= p[1] || p[2] < 1 || p[2] > 8 || p[3] < 1 || p[3] > 8) return;
            o[0] = 0x22; zones(p[0], p[1]); o[3] = (uint8_t)((p[2] - 1) | ((p[3] - 1) << 3));
            return;
        case 15:
            if (p[0] < 0 || p[1] > 9 || p[0] >= p[1] || p[2] < 0 || p[3] > 7 || p[2] >= p[3] || p[4] < 1 || p[4] > 255) return;
            o[0] = 0x23; zones(p[0], p[1]); o[3] = (uint8_t)(p[3] | (p[2] << 3)); o[4] = (uint8_t)p[4];
            return;
        }
    }

    // Mode + five parameters in one integer, so a trigger effect fits in an atomic. 0 = Normal = loose.
    static uint64_t Pack(int mode, const int* p) {
        uint64_t v = (uint8_t)mode;
        for (int i = 0; i < 5; i++) v |= (uint64_t)(uint8_t)p[i] << (8 * (i + 1));
        return v;
    }
    static void Unpack(uint64_t v, uint8_t* o) {
        int p[5];
        for (int i = 0; i < 5; i++) p[i] = (int)((v >> (8 * (i + 1))) & 255);
        Trigger((int)(v & 255), p, o);
    }

    // What one report says. Bytes only, so two of them compare with memcmp. A part is written, and its
    // valid flag set, only when its switch is on: everything else on the pad is left as it is.
    struct Out {
        uint8_t motors = 0, strong = 0, weak = 0;     // strong = left / heavy motor, weak = right / light motor
        uint8_t trig = 0, l2[11] = {0x05}, r2[11] = {0x05};
        uint8_t light = 0, rgb[3] = {0, 0, 0};
        uint8_t ledSetup = 0;                         // Bluetooth, first report after connecting: end the connect animation
    };

    // Fills buf (zeroed by the caller, 48 / 78 bytes or more) and returns the length of the report itself.
    static int Build(uint8_t* buf, bool bt, uint8_t seq, bool rumbleV2, const Out& o) {
        uint8_t* c = buf + (bt ? 3 : 1);              // the 47 bytes both transports share
        if (bt) { buf[0] = 0x31; buf[1] = (uint8_t)(seq << 4); buf[2] = 0x10; } else buf[0] = 0x02;
        if (o.motors) {
            c[0] |= rumbleV2 ? 0x02 : 0x03;           // haptics select (+ compatible vibration on firmware before 2.21)
            if (rumbleV2) c[38] |= 0x04;              // compatible vibration v2
            c[2] = o.weak; c[3] = o.strong;
        }
        if (o.trig)     { c[0] |= 0x0C; memcpy(c + 10, o.r2, 11); memcpy(c + 21, o.l2, 11); }
        if (o.light)    { c[1] |= 0x04; memcpy(c + 44, o.rgb, 3); }
        if (o.ledSetup) { c[38] |= 0x02; c[41] = 0x02; }
        if (!bt) return 48;
        uint8_t seed = 0xA2;                          // the Bluetooth HID header byte is part of the checksum
        uint32_t crc = ~Crc32(Crc32(0xFFFFFFFFu, &seed, 1), buf, 74);
        buf[74] = (uint8_t)crc; buf[75] = (uint8_t)(crc >> 8); buf[76] = (uint8_t)(crc >> 16); buf[77] = (uint8_t)(crc >> 24);
        return 78;
    }
}
```

A literal Python port of this code was run against an independent zlib reference: all vectors of section 9 match.

### Every `TriggerFx` the code produces, and its bytes

Producers: `WeaponTriggers` (lines 936-958), the gallop line 1823, the `TFx(DSX::Normal)` defaults (lines 1813, 1818), and the raw `DSX::Normal` packets in `PadThread` (694-699) and `MenuOpen` (`menu.inl` 363-368). `menu.inl` builds no other trigger effect. `TriggerFx.p[5]` is never used, so five parameters are enough.

| DSX call in the code | Used for | 11 bytes (hex) |
|---|---|---|
| `Normal` | default, reloading, weapon wheel, menu, alt-tab | `05 00 00 00 00 00 00 00 00 00 00` |
| `Resistance{3,0}` | L2: Varmint, light pistols (force 0 = Off) | `05 00 00 00 00 00 00 00 00 00 00` |
| `Resistance{3,1}` | L2: pistols | `21 F8 03 00 00 00 00 00 00 00 00` |
| `Resistance{4,1}` | L2: Cattleman, Double-Action | `21 F0 03 00 00 00 00 00 00 00 00` |
| `Resistance{2,1}` | L2: heavy revolvers, repeaters | `21 FC 03 00 00 00 00 00 00 00 00` |
| `Resistance{2,2}` | L2: rifles, snipers, shotguns | `21 FC 03 40 92 24 09 00 00 00 00` |
| `Resistance{2,3}` | L2: elephant rifle; R2: lasso | `21 FC 03 80 24 49 12 00 00 00 00` |
| `Resistance{3,2}` | R2: thrown weapons | `21 F8 03 00 92 24 09 00 00 00 00` |
| `SemiAutomaticGun{3,5,2}` | R2: Varmint | `25 28 00 01 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{3,5,3}` | R2: light pistols | `25 28 00 02 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{3,5,4}` | R2: pistols | `25 28 00 03 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{4,6,5}` | R2: Cattleman | `25 50 00 04 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{4,6,6}` | R2: heavy revolvers | `25 50 00 05 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{2,7,6}` | R2: Double-Action | `25 84 00 05 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{3,6,5}` | R2: repeaters | `25 48 00 04 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{3,6,6}` | R2: rifles, shotguns | `25 48 00 05 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{4,6,7}` | R2: snipers | `25 50 00 06 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{3,6,8}` | R2: double barrel, sawed-off | `25 48 00 07 00 00 00 00 00 00 00` |
| `SemiAutomaticGun{4,7,8}` | R2: elephant rifle | `25 90 00 07 00 00 00 00 00 00 00` |
| `Bow{1,7,5,7}` | R2: bow | `22 82 00 34 00 00 00 00 00 00 00` |
| `Bow{1,7,7,8}` | R2: improved bow | `22 82 00 3E 00 00 00 00 00 00 00` |
| `Galloping{0,9,2,5,f}`, f = 2..9 | both triggers at canter and gallop | `23 01 02 15 0f 00 00 00 00 00 00` (f=2: `..15 02`, f=9: `..15 09`) |

The DSX modes map onto the gist functions by name and signature (the gist's changelog: Resistance is now Feedback, SemiAutomaticGun is now Weapon). DSX is closed source, so identical bytes are **not verified (U5)**; the feel is checked in section 10.

---

## 4. Threading, change detection, rate limits, reset

**Publishing (script thread).** `Tick`, `ResetDSX` and `MenuOpen` store into `g_dsTrigL`, `g_dsTrigR`, `g_dsRgb`. A reader may see the new L2 with the old R2 for one 3 ms loop; the next loop corrects it.

**Writing (`PadThread` only).** Each loop it builds a `DS5::Out` from the atomics and its own knowledge (`scriptRunning`, `focused`, `cfg`), compares it with the last one written, and writes when something changed, when the re-assert is due, or when the motors need their refresh.

Rules:

- Triggers hold an effect only while `cfg.dsx && cfg.triggers && cfg.enabled && scriptRunning && focused`. Otherwise they are Off. This covers alt-tab, the start screen, a disabled mod, and quitting: the game stops ticking the script before the process dies, and 0.5 s later (`scriptRunning`, line 685) the pad thread lets go.
- Lightbar: red `230,10,10` until the script runs (the start screen, as now), then `g_dsRgb`.
- Motor bits only while the mod has something to say: `active`, or the last write was not zero (so the stop gets written).
- Trigger bits while triggers are switched on, on the first write after opening (clears a leftover effect from a crashed session), and once more to release after they were switched off.
- Minimum gap between writes: 10 ms on USB, 20 ms on Bluetooth. The comparison is against the last state that was really written, so a skipped change (including the final zero of a pulse) goes out as soon as the gap allows.
- Re-assert: every 2000 ms while focused and enabled, if we own triggers or lightbar. Never while alt-tabbed (another program may be using the pad).
- Motor refresh: the existing "every 60 ms while active" stays.
- A report with no part switched on is never sent.

**Reset.**

| Event | What happens |
|---|---|
| Alt-tab | Next loop: triggers Off, motors 0 (existing `if (!focused)` at line 772). Lightbar stays. No more writes until focus returns. |
| F11 menu opens | `MenuOpen` stores 0 in both trigger atomics. `MenuClose` needs nothing: `Tick` publishes every frame. |
| Mod disabled (`Enabled=0`) | `ScriptMain` line 1901 calls `ResetDSX()`: atomics = loose triggers, colour `0,0,64` (Steam's and SDL's default blue for player 1). Written once. |
| Output switched in F11 | `PadThread` sees `cfg.direct` change: `Pad::FinalReset()`, `Pad::Close()`, look again. |
| Game quits | (a) script stops ticking -> triggers Off after 0.5 s. (b) `DllMain` detach calls `Pad::FinalReset()` (the pad thread is already dead then). If the process is killed without either, the last state stays on the pad until it reconnects (risk R6). |

---

## 5. Rumble under Steam Input

- `Backend=Direct` (default), menu clicks, startup shot: `PadThread` mixes as now (lines 763-776) and, when `Pad::isDS`, writes the result into the motor bytes of the DualSense report. `S` goes to the left / heavy motor, `W` to the right / light motor, the same assignment as on the DS4 (`outBuf[5]` / `outBuf[4]`).
- `Backend=Game`: `OutputRumble` (line 816) is unchanged; the rumble reaches the pad through Steam.
- The game's own vibration: through Steam, untouched by this plan.
- No DualSense open in Direct mode (not connected, hidden, or an Xbox pad): `EnsureOpen` falls through to its XInput block (lines 525-529) and everything behaves as for an Xbox pad in v1.7.1: `XInputSetState` for rumble, `XInputGetState` for buttons. Under Steam Input that pad is Steam's virtual one. Steam's overlay DLL names hooks for `XInputGetState`/`XInputSetState` in `xinput1_4.dll`, which is the first DLL `LoadXInput` tries, so this probably works; **not verified (U8)**. The log line `pad: Xbox controller (XInput user N)` shows it.
- `Pad::OpenDS4`'s target, DSX's virtual DualShock 4, does not exist without DSX and is not looked for in Direct mode.
- Feel: there is no DSX between mod and pad any more, so "Rumble to Haptics" and DSX's "Enhanced Rumble" do not apply. The pad's own rumble emulation (v2 on current firmware) plays the values. `Rumble strength` and `Response curve` were tuned for DSX and will probably need new values; that is tuning for the user, not code (U6).

---

## 6. Buttons without DSX

Decision: parse the DualSense input report from the handle we hold. The d-pad / face-button byte and the shoulder byte have the same bit layout as on the DualShock 4, only at other positions, so `FromDS4` is reused with a shifted pointer.

| Report | d-pad + face buttons | L1 R1 L2 R2 Create Options | call |
|---|---|---|---|
| DS4 / DSX virtual pad, id 0x01 | [5] | [6] | `FromDS4(inBuf)` (as now) |
| DualSense USB, id 0x01 (64 bytes) | [8] | [9] | `FromDS4(inBuf + 3)` |
| DualSense Bluetooth, id 0x31 (78 bytes) | [9] | [10] | `FromDS4(inBuf + 4)` |
| DualSense Bluetooth, simple report id 0x01 (before any feature read) | [5] | [6] | `FromDS4(inBuf)` |

Why not XInput: it would show the buttons as remapped by the user's Steam layout, which matches the game better in theory, but it depends on Steam's hooks reaching our `GetProcAddress` pointer and needs a second device path next to the HID handle. The F11 menu and the click rumble work the same with physical buttons.

Replace lines 591-592 (`if (n >= 8 && inBuf[0] == 0x01) { out = FromDS4(inBuf); return true; }` and the `return false;` after it) with:

```cpp
            const uint8_t* r = nullptr;                                           // 5 bytes before the d-pad byte (DS4 layout)
            if (!isDS)                 { if (n >= 8 && inBuf[0] == 0x01) r = inBuf; }
            else if (!isBT)            { if (n >= 11 && inBuf[0] == 0x01) r = inBuf + 3; }
            else if (inBuf[0] == 0x31) { if (n >= 12) r = inBuf + 4; }
            else if (inBuf[0] == 0x01 && n >= 8) r = inBuf;                       // simple Bluetooth report
            if (!r) return false;
            // (debug read-back of the trigger state goes here, section 8)
            out = FromDS4(r);
            return true;
```

Two readers on one HID device (Steam and the mod) each get every report; that is how DSX and Steam coexist today.

---

## 7. Code changes outside `Pad` and the builder

### 7.1 Config (`RDR2DSXRumble.cpp`)

- `struct Config` line 247, after `bool dsx = true, ...;`: `bool  direct = false;          // DualSense without DSX: triggers, lightbar and rumble written straight to the pad`
- `LoadConfig` after line 321 (`c.lightbar = ...`): `c.direct          = IniI("DSX", "DirectDualSense", 0) != 0;`

### 7.2 `Pad` additions (after `Rumble`, line 558)

`Pad::Rumble` first line: `if (isDS) return;   // DS4 report format: never to a real DualSense` (it is still called at thread exit, line 786).

```cpp
    // One DualSense report, asynchronously. false = the last write is still on its way, or it failed (the pad was closed).
    static bool WriteDS(const DS5::Out& o) {
        if (writing) {
            if (WaitForSingleObject(wov.hEvent, 0) != WAIT_OBJECT_0) return false;
            DWORD n; writing = false;
            if (!GetOverlappedResult(h, &wov, &n, FALSE)) { Log("pad: DualSense write failed (%lu), reopening", GetLastError()); Close(); return false; }
        }
        memset(outBuf, 0, outLen);
        DS5::Build(outBuf, isBT, seq, rumbleV2, o);
        seq = (seq + 1) & 15;
        ResetEvent(wov.hEvent);
        if (WriteFile(h, outBuf, outLen, nullptr, &wov)) return true;
        if (GetLastError() == ERROR_IO_PENDING) { writing = true; return true; }
        Log("pad: DualSense write failed (%lu, %u bytes), reopening", GetLastError(), outLen);
        Close();
        return false;
    }

    // Triggers loose, motors off, lightbar back to Steam's default blue. Synchronous: at process exit the pad thread
    // is already gone, and on an output switch the handle closes right after.
    static void FinalReset() {
        if (!isDS || h == INVALID_HANDLE_VALUE) return;
        uint8_t buf[sizeof outBuf] = {};
        DS5::Out o; o.motors = o.trig = 1; o.light = cfg.lightbar; o.rgb[2] = 0x40;
        DS5::Build(buf, isBT, seq, rumbleV2, o);
        seq = (seq + 1) & 15;
        OVERLAPPED ov{}; ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!WriteFile(h, buf, outLen, nullptr, &ov) && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 100) != WAIT_OBJECT_0) CancelIo(h);
            DWORD n; GetOverlappedResult(h, &ov, &n, TRUE);   // ov lives on this stack: never leave while the write is pending
        }
        CloseHandle(ov.hEvent);
    }
```

### 7.3 `PadThread` (lines 658-791)

1. Before `Pad::EnsureOpen();` (line 688):

```cpp
        static bool wasDirect = cfg.direct;
        if (cfg.direct != wasDirect) {            // output switched in F11 or the ini: let go of the pad and look again
            wasDirect = cfg.direct;
            Pad::FinalReset();
            Pad::Close(); Pad::xuser = -1; Pad::lastOpenTry = 0;
            Log("output: %s", cfg.direct ? "Direct (DualSense over HID, no DSX)" : "DSX (UDP)");
        }
```

2. Line 694: `if (!f && cfg.dsx && cfg.triggers) {` -> add `&& !cfg.direct` (Direct loosens through `focused` in the block below).
3. Line 708: `if (cfg.dsx && cfg.lightbar && !scriptRunning && ...` -> add `&& !cfg.direct` (Direct shows the red through the block below).
4. Lines 777-783. Keep the `bool owned = ...` line, then put the DualSense branch in front of the existing `if`:

```cpp
        bool owned = active || cfg.backend == 1 || pulses.size();   // only touch the pad when we have something to say
        if (Pad::isDS) {
            // The real DualSense: one report carries motors, triggers and lightbar. A part gets its valid flag only
            // while the mod owns it, so the reports Steam Input sends for the game's own rumble are left alone.
            static DS5::Out sent;
            static uint64_t lastDs = 0;
            DS5::Out o;
            if (active || lastS > 0 || lastW > 0) { o.motors = 1; o.strong = (uint8_t)S; o.weak = (uint8_t)W; }
            // an effect only while the game runs in front; loose on the start screen, when alt-tabbed and while quitting
            bool live = cfg.dsx && cfg.triggers && cfg.enabled && scriptRunning && focused;
            DS5::Unpack(live ? g_dsTrigL.load() : 0, o.l2);
            DS5::Unpack(live ? g_dsTrigR.load() : 0, o.r2);
            o.trig = (cfg.dsx && cfg.triggers) || Pad::fresh || (sent.trig && (sent.l2[0] != 0x05 || sent.r2[0] != 0x05));
            if (cfg.dsx && cfg.lightbar) {
                uint32_t c = scriptRunning ? g_dsRgb.load() : 0xE60A0A;   // start screen: red until the game's scripts run
                o.light = 1; o.rgb[0] = (uint8_t)(c >> 16); o.rgb[1] = (uint8_t)(c >> 8); o.rgb[2] = (uint8_t)c;
            }
            o.ledSetup = Pad::fresh && Pad::isBT;
            bool changed = Pad::fresh || memcmp(&o, &sent, sizeof o) != 0;
            bool keep = focused && cfg.enabled && (o.trig || o.light) && now - lastDs > 2000;   // Steam Input may have written its own state
            bool refresh = active && now - lastWrite > 60;
            if (!(o.motors || o.trig || o.light || o.ledSetup)) sent = o;                       // nothing of ours to say
            else if ((changed || keep || refresh) && now - lastDs >= (Pad::isBT ? 20u : 10u) && Pad::WriteDS(o)) {
                sent = o; lastDs = lastWrite = now; lastS = S; lastW = W; Pad::fresh = false;
                // (debug log of the write goes here, section 8)
            }
        } else if (Pad::Ready() && (owned || lastS > 0 || lastW > 0)) {
            ...the existing three lines, unchanged...
        }
```

`now`, `scriptRunning`, `focused`, `active`, `S`, `W`, `lastS`, `lastW`, `lastWrite` all exist in the loop already.

### 7.4 Script side

- `ResetDSX` (lines 1044-1052):

```cpp
static void ResetDSX() {
    if (!cfg.dsx) return;
    if (cfg.direct) { g_dsTrigL = 0; g_dsTrigR = 0; g_dsRgb = 0x000040; }   // loose triggers, Steam's default blue: the pad thread writes it
    else {
        DSX::Packet pk;
        pk.Add(DSX::TriggerThreshold, {cfg.controllerIndex, DSX::Right, 0});
        pk.Add(DSX::ResetToUserSettings, {cfg.controllerIndex});
        pk.Send();
    }
    S.sentValid = false;
    S.rgb[0] = S.rgb[1] = S.rgb[2] = -1;
}
```

- `Tick` line 1809: `bool keepAlive = !cfg.direct && now - S.lastDsx > 3000;      // resend in case DSX was restarted (Direct: the pad thread re-asserts)`
- `Tick` lines 1826-1827:

```cpp
        if (cfg.direct) { g_dsTrigL = DS5::Pack(lt.mode, lt.p); g_dsTrigR = DS5::Pack(rt.mode, rt.p); }
        else {
            if (!S.sentValid || !(lt == S.sentL) || keepAlive) AddTrigger(pk, DSX::Left, lt);
            if (!S.sentValid || !(rt == S.sentR) || keepAlive) AddTrigger(pk, DSX::Right, rt);
        }
```

- `Tick` line 1853 (inside the existing `if ((diff && now - lastRgb > 50) || keepAlive)`, so the 50 ms / 12-step throttle applies to both modes):

```cpp
            if (cfg.direct) g_dsRgb = (uint32_t)(std::clamp(c[0], 0, 255) << 16 | std::clamp(c[1], 0, 255) << 8 | std::clamp(c[2], 0, 255));
            else pk.Add(DSX::RGBUpdate, {cfg.controllerIndex, c[0], c[1], c[2]});
```

  In Direct mode `pk` stays empty, so line 1858 sends nothing.
- `DllMain` detach, line 1926 (`if (DSX::sock != INVALID_SOCKET) { ResetDSX(); DSX::Close(); }`):

```cpp
        if (cfg.direct) Pad::FinalReset();
        else if (DSX::sock != INVALID_SOCKET) ResetDSX();
        if (DSX::sock != INVALID_SOCKET) DSX::Close();
```

- `ScriptMain` line 1868 (`DSX::Open()`) stays: the UDP socket costs nothing and is ready when the user switches back to DSX.
- The fire-at-break block (line 1658) only reads `cfg.triggers && cfg.dsx`; no change.

### 7.5 `menu.inl`

- `g_items`, after line 248 (`{"Lightbar", "DSX", "Lightbar", 1, 1},` on the settings page, the first of the two): `    {"DualSense without DSX",  "DSX",     "DirectDualSense",    1, 0},`
  Kind 1 (toggle), default 0. It sits after both dropdown groups (rows 2-6 and 7-12), so `GroupOf` and the dropdown `step` counts are not disturbed; `kItems`, `PageOf` and `Layout` adapt by themselves. Not added to the Advanced tabs (they need pixel art and are at 8 cards).
- `MenuOpen` lines 363-368:

```cpp
    if (cfg.direct) { g_dsTrigL = 0; g_dsTrigR = 0; }   // loose triggers while the menu is open
    else if (cfg.dsx) {
        ...existing packet...
    }
```

- Toggling the row calls `MenuSave` -> `LoadConfig`; the pad thread notices `cfg.direct` within one loop (7.3.1). No other menu code.
- Rect budget: by my count from `MenuDraw` the settings page draws roughly 330 of RDR2's roughly 400 rects (not measured); one more row adds 1 to 4. Check the `menu: N rects in one frame` debug line (line 561) in the first in-game test.

### 7.6 `RDR2DSXRumble.ini`

In `[DSX]`, change the two header comment lines and add the key after `ControllerIndex=0`:

```ini
[DSX]
; DualSense only (adaptive triggers + lightbar). Xbox controllers need nothing here.
; With DSX (default): DSX v3 > Settings > Networking > enable "Incoming UDP" (default 127.0.0.1:6969)
Enabled=1
; 0 = auto (C:\Temp\DualSenseX\DualSenseX_PortNumber.txt, else 6969)
Port=0
ControllerIndex=0
; EXPERIMENTAL. 0 = through DSX (default). 1 = without DSX: the mod writes rumble, adaptive triggers and lightbar
; straight to the DualSense (USB or Bluetooth). For 1: close DSX and turn Steam Input ON for the game.
; Works best with Backend=Direct. Rumble strength differs from DSX: adjust Intensity / ResponseCurve.
DirectDualSense=0
```

In `[Rumble]`, extend the `Direct =` comment: `; Direct = drive the left/right motors directly: DSX's virtual DualShock 4, or the DualSense itself with DirectDualSense=1.`

---

## 8. Version and logging

- Lines 72-76: `MOD_VERSION "v1.8.0"`, `MOD_VERSION_W L"1.8.0"`, `MOD_VER_MAJOR 1`, `MOD_VER_MINOR 8`, `MOD_VER_PATCH 0`. The update check then reports "up to date" against 1.7.1.
- File header comment (lines 1-13): one added line, e.g. `//  * Direct       : optional, without DSX: the same triggers, lightbar and rumble written to the DualSense over HID.`
- `DllMain` attach, after line 1919: `Log("output: %s", cfg.direct ? "Direct (DualSense over HID, no DSX)" : "DSX (UDP)");`
- `ScriptMain` line 1866: add `, output=%s` with `cfg.direct ? "Direct" : "DSX"`.
- Device line: from `OpenHid` (section 2), e.g. `pad: DualSense 054C:0CE6 over USB (in 64, out 48 bytes, firmware 6.48, rumble v2)` (that is what the user's pad should print).
- Debug log of each write, at most 10 lines a second, in the success branch of 7.3.4:

```cpp
                if (cfg.debug) {
                    static uint64_t lastLog = 0; static int unlogged = 0;
                    if (now - lastLog < 100) unlogged++;
                    else {
                        Log("DS <- motors %s%d/%d | L2 %s%02X %02X %02X %02X %02X | R2 %02X %02X %02X %02X %02X | rgb %s%d,%d,%d%s (+%d writes not logged)",
                            o.motors ? "" : "(not sent) ", o.strong, o.weak, o.trig ? "" : "(not sent) ", o.l2[0], o.l2[1], o.l2[2], o.l2[3], o.l2[4],
                            o.r2[0], o.r2[1], o.r2[2], o.r2[3], o.r2[4], o.light ? "" : "(not sent) ", o.rgb[0], o.rgb[1], o.rgb[2],
                            keep && !changed ? " [re-assert]" : "", unlogged);
                        lastLog = now; unlogged = 0;
                    }
                }
```

- Debug read-back (the diagnostic for "Steam overwrote our triggers"), in `ReadButtons` where section 6 marks it. The pad reports the state of its trigger effects in its input report. The offsets are from memory of community documentation and are **not verified (U7)**; this only logs, no logic depends on it. Drop it if the values make no sense in the first test.

```cpp
            if (cfg.debug && isDS && n >= 50 && !(isBT && inBuf[0] != 0x31)) {
                const uint8_t* s = inBuf + (isBT ? 1 : 0);          // UNVERIFIED offsets: R2 state [42], L2 state [43], active effects [48]
                static uint8_t last[3]; static uint64_t lastLog = 0;
                uint8_t cur[3] = {s[42], s[43], s[48]};
                uint64_t t = GetTickCount64();
                if (memcmp(cur, last, 3) && t - lastLog > 250) { memcpy(last, cur, 3); lastLog = t; Log("pad: DualSense reports trigger state R2 %02X L2 %02X effects %02X", cur[0], cur[1], cur[2]); }
            }
```

---

## 9. Offline test (no game, no controller)

Create `tests\dualsense_test.cpp` in the repo (kept, committed on this branch; it is not part of the ASI). It includes only `src\dualsense.inl`. The expected bytes come from an independent Python implementation (zlib's CRC-32 and a direct port of the gist), so builder and CRC are checked against a second implementation, and the CRC also against the standard check value.

```cpp
// Offline check of src/dualsense.inl. Expected bytes: independent Python reference (zlib.crc32 + Nielk1's gist).
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../src/dualsense.inl"

static int fails = 0;
static void Check(const char* name, const uint8_t* got, int n, const char* hex) {
    uint8_t want[128]; int w = 0, used = 0; unsigned v;
    for (const char* s = hex; w < 128 && sscanf(s, " %2x%n", &v, &used) == 1; s += used) want[w++] = (uint8_t)v;
    if (w == n && !memcmp(got, want, n)) { printf("ok    %s\n", name); return; }
    fails++;
    printf("FAIL  %s\n  got :", name);
    for (int i = 0; i < n; i++) printf(" %02X", got[i]);
    printf("\n  want: %s\n", hex);
}

int main() {
    const uint8_t check[] = "123456789";
    uint32_t crc = ~DS5::Crc32(0xFFFFFFFFu, check, 9);
    if (crc != 0xCBF43926u) { fails++; printf("FAIL  crc32 check value: %08X\n", crc); } else printf("ok    crc32 check value\n");

    struct { const char* name; int mode; int p[5]; const char* hex; } t[] = {
        {"Normal",               0,  {0, 0, 0, 0, 0}, "05 00 00 00 00 00 00 00 00 00 00"},
        {"Resistance 3,0",       13, {3, 0, 0, 0, 0}, "05 00 00 00 00 00 00 00 00 00 00"},
        {"Resistance 3,1",       13, {3, 1, 0, 0, 0}, "21 F8 03 00 00 00 00 00 00 00 00"},
        {"Resistance 4,1",       13, {4, 1, 0, 0, 0}, "21 F0 03 00 00 00 00 00 00 00 00"},
        {"Resistance 2,1",       13, {2, 1, 0, 0, 0}, "21 FC 03 00 00 00 00 00 00 00 00"},
        {"Resistance 2,2",       13, {2, 2, 0, 0, 0}, "21 FC 03 40 92 24 09 00 00 00 00"},
        {"Resistance 2,3",       13, {2, 3, 0, 0, 0}, "21 FC 03 80 24 49 12 00 00 00 00"},
        {"Resistance 3,2",       13, {3, 2, 0, 0, 0}, "21 F8 03 00 92 24 09 00 00 00 00"},
        {"Resistance 9,8",       13, {9, 8, 0, 0, 0}, "21 00 02 00 00 00 38 00 00 00 00"},
        {"Resistance 0,8",       13, {0, 8, 0, 0, 0}, "21 FF 03 FF FF FF 3F 00 00 00 00"},
        {"SemiAuto 3,5,2",       16, {3, 5, 2, 0, 0}, "25 28 00 01 00 00 00 00 00 00 00"},
        {"SemiAuto 3,5,3",       16, {3, 5, 3, 0, 0}, "25 28 00 02 00 00 00 00 00 00 00"},
        {"SemiAuto 3,5,4",       16, {3, 5, 4, 0, 0}, "25 28 00 03 00 00 00 00 00 00 00"},
        {"SemiAuto 4,6,5",       16, {4, 6, 5, 0, 0}, "25 50 00 04 00 00 00 00 00 00 00"},
        {"SemiAuto 4,6,6",       16, {4, 6, 6, 0, 0}, "25 50 00 05 00 00 00 00 00 00 00"},
        {"SemiAuto 2,7,6",       16, {2, 7, 6, 0, 0}, "25 84 00 05 00 00 00 00 00 00 00"},
        {"SemiAuto 3,6,5",       16, {3, 6, 5, 0, 0}, "25 48 00 04 00 00 00 00 00 00 00"},
        {"SemiAuto 3,6,6",       16, {3, 6, 6, 0, 0}, "25 48 00 05 00 00 00 00 00 00 00"},
        {"SemiAuto 4,6,7",       16, {4, 6, 7, 0, 0}, "25 50 00 06 00 00 00 00 00 00 00"},
        {"SemiAuto 3,6,8",       16, {3, 6, 8, 0, 0}, "25 48 00 07 00 00 00 00 00 00 00"},
        {"SemiAuto 4,7,8",       16, {4, 7, 8, 0, 0}, "25 90 00 07 00 00 00 00 00 00 00"},
        {"Bow 1,7,5,7",          14, {1, 7, 5, 7, 0}, "22 82 00 34 00 00 00 00 00 00 00"},
        {"Bow 1,7,7,8",          14, {1, 7, 7, 8, 0}, "22 82 00 3E 00 00 00 00 00 00 00"},
        {"Galloping 0,9,2,5,2",  15, {0, 9, 2, 5, 2}, "23 01 02 15 02 00 00 00 00 00 00"},
        {"Galloping 0,9,2,5,9",  15, {0, 9, 2, 5, 9}, "23 01 02 15 09 00 00 00 00 00 00"},
        {"SemiAuto 1,5,4 (bad)", 16, {1, 5, 4, 0, 0}, "05 00 00 00 00 00 00 00 00 00 00"},
        {"Bow 7,7,5,5 (bad)",    14, {7, 7, 5, 5, 0}, "05 00 00 00 00 00 00 00 00 00 00"},
        {"Galloping 0,9,5,2,4 (bad)", 15, {0, 9, 5, 2, 4}, "05 00 00 00 00 00 00 00 00 00 00"},
        {"mode 99",              99, {1, 2, 3, 0, 0}, "05 00 00 00 00 00 00 00 00 00 00"},
    };
    for (auto& c : t) {
        uint8_t o[11], o2[11];
        DS5::Trigger(c.mode, c.p, o);
        Check(c.name, o, 11, c.hex);
        DS5::Unpack(DS5::Pack(c.mode, c.p), o2);                       // through the atomic and back
        if (memcmp(o, o2, 11)) { fails++; printf("FAIL  %s: Pack/Unpack changes the effect\n", c.name); }
    }

    uint8_t buf[128];
    DS5::Out full; full.motors = 1; full.strong = 200; full.weak = 100; full.trig = 1; full.light = 1;
    full.rgb[0] = 0; full.rgb[1] = 150; full.rgb[2] = 25;
    { int r[5] = {4, 6, 5, 0, 0}, l[5] = {4, 1, 0, 0, 0}; DS5::Trigger(16, r, full.r2); DS5::Trigger(13, l, full.l2); }

    memset(buf, 0, sizeof buf);
    Check("A  USB, rumble v2, all parts", buf, DS5::Build(buf, false, 0, true, full),
          "02 0E 04 64 C8 00 00 00 00 00 00 25 50 00 04 00 00 00 00 00 00 00 21 F0 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 96 19");
    memset(buf, 0, sizeof buf);
    Check("B  Bluetooth, seq 3, same", buf, DS5::Build(buf, true, 3, true, full),
          "31 30 10 0E 04 64 C8 00 00 00 00 00 00 25 50 00 04 00 00 00 00 00 00 00 21 F0 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 96 19 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 E7 B9 F3 06");
    DS5::Out m; m.motors = 1; m.strong = 255; m.weak = 1;
    memset(buf, 0, sizeof buf);
    Check("C  USB, rumble v1, motors only", buf, DS5::Build(buf, false, 0, false, m),
          "02 03 00 01 FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
    DS5::Out none;
    memset(buf, 0, sizeof buf);
    Check("D  Bluetooth, seq 0, nothing set", buf, DS5::Build(buf, true, 0, true, none),
          "31 00 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 B5 01 15 23");
    DS5::Out led; led.ledSetup = 1;
    memset(buf, 0, sizeof buf);
    Check("E  Bluetooth, seq 15, LED setup", buf, DS5::Build(buf, true, 15, true, led),
          "31 F0 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 00 00 02 00 00 00 00 00 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 97 86 F3 CE");
    DS5::Out rel; rel.motors = rel.trig = rel.light = 1; rel.rgb[2] = 0x40;
    memset(buf, 0, sizeof buf);
    Check("F  USB, release (FinalReset)", buf, DS5::Build(buf, false, 0, true, rel),
          "02 0E 04 00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 40");

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
```

Build and run:

```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cd /d \"C:\Users\elfge\Desktop\rdr2 rumble\" && cl /nologo /std:c++17 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS tests\dualsense_test.cpp /Fe:%TEMP%\dualsense_test.exe /Fo:%TEMP%\ && %TEMP%\dualsense_test.exe"
```

Expected last line: `all passed`, exit code 0. Optional cross-check of vector D with a third tool (Python 3 is at `C:\Python315`): `python -I -c "import zlib;print('%08X'%zlib.crc32(bytes([0xA2,0x31,0x00,0x10])+bytes(71)))"` prints `231501B5`, which is bytes [74..77] `B5 01 15 23`.

If a vector fails, the code is wrong, not the vector: the vectors were produced by the zlib reference and matched by a line-by-line port of the code in section 3.

---

## 10. In-game checklist for the user

Give this list to the user with the build. Setup: copy `RDR2DSXRumble.asi` to `E:\SteamLibrary\steamapps\common\Red Dead Redemption 2`, set `Debug=1` and `DirectDualSense=1` in the ini there (add the key under `[DSX]`), **close DSX**, Steam Input **on** for RDR2, start through Steam. Going back at any time: `powershell -ExecutionPolicy Bypass -File install_version.ps1 -Version 1.7.1`.

After each run, keep `RDR2DSXRumble.log` from the game folder.

1. **Go / no-go (USB).** Start the game. The log must contain `output: Direct ...` and `pad: DualSense 054C:0CE6 over USB (in 64, out 48 bytes, ...)`. If it says `no DualSense found` or `cannot be opened`, stop and send the log (section 11, R1).
2. **Start screen.** Lightbar red. D-pad and Cross in the start menu give a click.
3. **Startup shot.** The splash gunshot rumbles.
4. **In game, unarmed.** Lightbar follows the place: green in nature, light orange in Saint Denis, deep orange in the desert, white in snow. Both triggers loose.
5. **Triggers per weapon** (R2 = wall and break, L2 = weight while aiming): Cattleman, Double-Action (long pull), a repeater, a rifle, a shotgun (stiff), the Varmint rifle (light, L2 loose). While reloading R2 is loose. Holding L1 (weapon wheel) loosens both.
6. **Bow.** R2 builds up and snaps back on release. Improved bow is stronger.
7. **Lasso and thrown weapons.** R2 has a steady resistance; L2 loose.
8. **Horse.** At canter and gallop, unarmed, both triggers pulse; faster horse = faster pulse.
9. **Fight.** Lightbar turns red, flashes on a hit, pulses when health is low; dark red when dead.
10. **Rumble.** Footsteps (run / sprint), hoofbeats, gunshots, a punch, a fall, a wagon. Compare strength with DSX; note whether weak effects are too strong or too weak (tuning with `Rumble strength` and `Response curve`).
11. **Game's own vibration.** Once with Settings > Controls > Vibration on, once off. Note any stutter or holes in the rumble.
12. **`Backend=Game`** in the ini: rumble still works; note holes when the lightbar changes or a weapon is drawn.
13. **F11 menu.** Opens, D-pad / Cross / Circle / L1 / R1 work, every press clicks, triggers are loose while it is open and return after closing. "Test rumble" rumbles. Toggle "DualSense without DSX" off and on: triggers go loose, then come back within about 2 s.
14. **Pause menu and map.** Clicks on button presses; triggers loose.
15. **Alt-tab.** Triggers go loose at once, rumble stops. Back in the game the weapon's trigger returns.
16. **Steam overlay** (Shift+Tab or the PS button) open and close: do triggers and lightbar survive? If they change, how long until they are back (should be 2 s at most)?
17. **Lightbar stability.** Any flicker or a wrong colour that keeps coming back means Steam writes its own colour.
18. **Unplug and replug** the cable in game: within about 2 s triggers, lightbar and rumble are back (log: a second `pad: DualSense` line).
19. **Quit the game** with a gun drawn: triggers must be loose afterwards. Note the lightbar colour left behind.
20. **Kill test** (optional): end `RDR2.exe` in Task Manager with a gun drawn. Are the triggers stuck? Does replugging free them?
21. **Bluetooth.** Repeat 1-9, 13, 15, 18 (switch the pad off and on), 19. The log line must say `over Bluetooth` and show the output length. Watch for a blue lightbar that never changes, for lag, and for `write failed` lines.
22. **DualSense Edge**, if available: 1, 5, 10.
23. **Back to DSX.** `DirectDualSense=0`, start DSX, Steam Input as before: everything behaves like v1.7.1 (triggers, lightbar, rumble, menu clicks, start-screen red, alt-tab, quitting resets DSX).
24. **Xbox pad or no pad** with `DirectDualSense=1`: rumble as in v1.7.1, log says no DualSense found, nothing crashes.

---

## 11. Risks, unknowns, rollback

**Could not be verified offline** (each has a test in section 10):

| # | Unknown | If it goes wrong |
|---|---|---|
| U1 | Does a report without the rumble bits stop rumble started by Steam? SDL's source says so. | Holes in the game's own vibration and in `Backend=Game` when we write. Advice then: `Backend=Direct`, in-game vibration off. The design already keeps such writes rare. |
| U2 | Does Steam Input rewrite lightbar or triggers on its own? | The 2 s re-assert repairs it. If Steam does it constantly: flicker. Then raise the re-assert rate, or tell the user to turn the Steam LED option off, or accept "no lightbar in Direct mode" behind `Lightbar=0`. |
| U3 | Do Steam's in-process hooks hide the physical DualSense from HID enumeration inside `RDR2.exe`? | See R1. |
| U4 | Bluetooth on Windows: output length from `HidP_GetCaps` (expected 547), whether the zero-padded write is accepted, whether the LED setup report is needed, the exact path text. | See R3. |
| U5 | DSX modes = the gist's functions byte for byte. | A trigger feels different from DSX: adjust the numbers in `WeaponTriggers` for Direct only after the user describes the difference. |
| U6 | Rumble strength and character compared with DSX. | Tuning with existing settings. A separate Direct strength factor only if the user asks. |
| U7 | Offsets of the trigger state in the input report (debug log only). | Remove the debug read-back. |
| U8 | In-process XInput calls reach Steam's virtual pad. | Only the fallback is affected (no DualSense open). |
| U9 | Re-sending an identical trigger effect every 2 s is not felt. DSX streams the same effect continuously, so probably fine. | Re-assert the lightbar only, and triggers only when the read-back shows them gone. |
| U10 | The script stops ticking at least 0.5 s before the process ends, and `DLL_PROCESS_DETACH` runs. | R6. |
| U11 | `HidD_GetFeature` and a second `WriteFile` on the overlapped handle behave as in DS4Windows (which does the same). | Firmware reads as "not readable" -> rumble v1; visible in the log. |

**Risks and contingencies.**

- **R1 (go/no-go): the DualSense is invisible or locked inside the game process.** Evidence it may be: the hook names in Steam's overlay DLL (section 1). Evidence it may not be: the hooks also exist to add Steam's virtual pad, and opening the pad from a normal process next to Steam works. Read the log of test 1:
  - `no DualSense found (N HID devices seen)`: enumeration is filtered. Contingency C1: list the interfaces with `CM_Get_Device_Interface_ListW` (`<cfgmgr32.h>`, `cfgmgr32.lib`, and `-lcfgmgr32` in `build.bat`), which Steam's DLL does not name, then `CreateFileW` each path and run the same checks:

    ```cpp
    GUID g; HidD_GetHidGuid(&g); ULONG len = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, &g, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS && len > 1) {
        std::vector<wchar_t> list(len);
        if (CM_Get_Device_Interface_ListW(&g, nullptr, list.data(), len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS)
            for (wchar_t* p = list.data(); *p; p += wcslen(p) + 1) { /* CreateFileW(p, ...) and the checks of OpenHid */ }
    }
    ```
  - `cannot be opened (error N)`: the open itself is blocked (`CreateFile` / `NtCreateFile` hook, or HidHide). No in-process fix worth doing. Contingency C2 is a separate small program outside the game that owns the pad; that is a new plan. Stop and report.
  - Do not write C1 or C2 before the log asks for it.
- **R2: two writers on the motors.** Steam and the mod both write motor values; the last one wins, as today on DSX's virtual pad.
- **R3: Bluetooth write fails** (`pad: DualSense write failed (87, 547 bytes)`). Contingency: write exactly 78 bytes instead of `outLen`; if that fails too, `HidD_SetOutputReport(h, buf, 78)`. If the lightbar stays blue on Bluetooth, send the LED setup as its own report 10 s after opening (SDL waits for the connect animation to finish).
- **R4: Bluetooth lag or dropouts** at 50 writes a second. Raise the Bluetooth gap from 20 to 33 ms.
- **R5: HidHide** hides the pad from the game: logged with instructions.
- **R6: stuck triggers after a crash or kill.** Nothing runs then. The pad frees itself when it reconnects, and the next game start writes Off on its first report. The lightbar may keep the last colour after a normal quit when `DllMain` does not run.
- **R7: DSX and Direct at once.** DSX writes the pad continuously and would fight the mod. The ini comment and the checklist say "close DSX"; no detection is built.
- **R8: regression in DSX mode.** Every changed DSX path is behind `cfg.direct`; review the diff for this before the final commit (step 6).
- **R9: settings-page rect budget** (section 7.5).

**Rollback.** Everything lives on `experimental-1.8.0`. `main` and tag v1.7.1 are untouched, nothing is pushed. If the experiment fails, the branch is deleted (`git branch -D experimental-1.8.0`) and the user reinstalls 1.7.1 with `install_version.ps1 -Version 1.7.1`. Users are never affected: the new mode is off unless they set `DirectDualSense=1`.

---

## 12. Step order, checkpoints, estimate

Commit locally on `experimental-1.8.0` at each checkpoint unless the person running you says otherwise (sources only; the rebuilt `RDR2DSXRumble.asi` is tracked, commit it once in step 6). Never push.

| Step | Work | Checkpoint |
|---|---|---|
| 0 | `git status` clean, branch `experimental-1.8.0`. Read both source files fully. Build once to confirm the toolchain. | Build ok, no diff. |
| 1 | `src\dualsense.inl` (section 3), `tests\dualsense_test.cpp` (section 9). Add the `#include` and the three atomics after line 413. | Test prints `all passed`. ASI builds. |
| 2 | Version 1.8.0, header comment, `Config.direct`, `LoadConfig`, the two `output:` log lines, ini key and comments, the menu row (sections 7.1, 7.5 row only, 7.6, 8). | Build ok. With `DirectDualSense=0` nothing else changed. |
| 3 | `Pad`: new state, `outBuf[1024]`, `Close`, `OpenHid`, `EnsureOpen`, `Rumble` guard, `WriteDS`, `FinalReset`, `ReadButtons` with the debug read-back (sections 2, 6, 7.2, 8). | Build ok. **Optional early probe:** this build already opens the pad, logs it and reads its buttons, but writes nothing. If the user is available, have them run test 1 and 13 (menu navigation) now: it answers U3 before the rest is written. |
| 4 | `PadThread`: output switch, the two `!cfg.direct` guards, the DualSense output block with its debug log (sections 7.3, 8). | Build ok. |
| 5 | Script side: `ResetDSX`, `Tick` (three edits), `DllMain`, `MenuOpen` (sections 7.4, 7.5). | Build ok. |
| 6 | Review `git diff main -- src RDR2DSXRumble.ini`: with `cfg.direct == false` every DSX packet, the DS4 rumble path and the XInput path are byte-for-byte what v1.7.1 did. Run the test again. Final build. Commit including the `.asi`. | Test `all passed`, build clean, diff reviewed. |
| 7 | Hand over: the built `.asi`, the ini lines to add, the checklist of section 10, and the list of unknowns U1-U11. Do not install into the game folder unless the user asks. | User tests. |

After the user's test, fix what the log shows, using the contingencies of section 11. Tuning of trigger numbers or rumble strength comes only from the user's description.

**Size:** about 110 lines `dualsense.inl`, 110 lines test, 150 new or changed lines in `RDR2DSXRumble.cpp`, 3 in `menu.inl`, 8 in the ini.
**Time:** steps 0-6 about 2 to 3 hours of focused work. The open end is the user's hardware test: plan one round for USB and one for Bluetooth, and one more if R1 or R3 needs its contingency.

**Deliberately left out:** README / Nexus text / media / release zip (after the experiment succeeds); forcing `Backend=Direct` in the new mode; a separate rumble strength for Direct; detecting a running DSX; player LEDs, mic LED, touchpad, gyro, speaker haptics; more than one DualSense (the first one found is used, `ControllerIndex` stays DSX-only).
