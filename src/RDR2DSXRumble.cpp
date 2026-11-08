// RDR2 DSX Rumble - context-aware rumble + adaptive triggers + lightbar for the DualSense via DSX.
//
//  * Rumble       : footsteps, hoofbeats per gait, gunshots per weapon class, reloads, damage,
//                   landings, wagon/train roughness, low-health heartbeat.
//                   DSX's UDP API has no rumble command, so rumble is sent to the controller the game
//                   sees (DSX's virtual pad), either through the game (SET_CONTROL_SHAKE) or by writing
//                   rumble reports directly to DSX's virtual DualShock 4 (XInput if DSX emulates Xbox).
//  * Menus        : a tick on every button press in the pause menu, map/catalog UI and the start menu.
//  * Startup      : the gunshot of the startup splash, detected from the game's own audio level.
//  * DSX (UDP)    : adaptive triggers per weapon (revolver/rifle/shotgun/bow/lasso), galloping
//                   triggers on horseback, lightbar colour by health with a red flash on damage.
//
// Needs ScriptHookRDR2 (no SDK needed: its exports are resolved at runtime).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#include <timeapi.h>

static std::atomic<bool>     g_quit{false};
static std::atomic<uint64_t> g_lastTick{0};       // last script tick (GetTickCount64)
static std::atomic<bool>     g_scriptMenu{false}; // pause menu / UI app open (reported by the script)
static std::atomic<float>    g_gameL{0.f}, g_gameR{0.f};  // in-game rumble for the Direct backend
static std::atomic<uint32_t> g_padButtons{0};             // live button state of the pad (Btn bits)

// ---------------------------------------------------------------------------------------------
// ScriptHookRDR2 binding
// ---------------------------------------------------------------------------------------------
using fnScriptRegister   = void (*)(HMODULE, void (*)());
using fnScriptUnregister = void (*)(HMODULE);
using fnScriptWait       = void (*)(DWORD);
using fnNativeInit       = void (*)(uint64_t);
using fnNativePush64     = void (*)(uint64_t);
using fnNativeCall       = uint64_t* (*)();

static fnScriptRegister   shRegister;
static fnScriptUnregister shUnregister;
static fnScriptWait       shWait;
static fnNativeInit       shInit;
static fnNativePush64     shPush;
static fnNativeCall       shCall;
static HMODULE            g_module;
#define MOD_VERSION "v1.5"
typedef int (*fnWorldGetAll)(int* arr, int size);
static fnWorldGetAll      shAllPeds, shAllObjects;

// true while the game window has focus (rumble stops when you alt-tab out)
static std::atomic<bool> g_focused{true};
static std::atomic<bool> g_triggersDirty{false};   // set when focus returns: resend the trigger effects
static bool GameFocused() {
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static bool BindScriptHook() {
    HMODULE sh = GetModuleHandleA("ScriptHookRDR2.dll");
    if (!sh) sh = LoadLibraryA("ScriptHookRDR2.dll");
    if (!sh) return false;
    shRegister   = (fnScriptRegister)  GetProcAddress(sh, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
    shUnregister = (fnScriptUnregister)GetProcAddress(sh, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
    shWait       = (fnScriptWait)      GetProcAddress(sh, "?scriptWait@@YAXK@Z");
    shInit       = (fnNativeInit)      GetProcAddress(sh, "?nativeInit@@YAX_K@Z");
    shPush       = (fnNativePush64)    GetProcAddress(sh, "?nativePush64@@YAX_K@Z");
    shCall       = (fnNativeCall)      GetProcAddress(sh, "?nativeCall@@YAPEA_KXZ");
    shAllPeds    = (fnWorldGetAll)     GetProcAddress(sh, "?worldGetAllPeds@@YAHPEAHH@Z");
    shAllObjects = (fnWorldGetAll)     GetProcAddress(sh, "?worldGetAllObjects@@YAHPEAHH@Z");
    return shRegister && shUnregister && shWait && shInit && shPush && shCall;
}

struct Vec3 { float x; uint32_t _a; float y; uint32_t _b; float z; uint32_t _c; };

template <typename T> static inline void PushArg(T v) {
    static_assert(sizeof(T) <= 8, "arg too big");
    uint64_t raw = 0;
    std::memcpy(&raw, &v, sizeof(T));
    shPush(raw);
}
template <typename R, typename... A> static inline R Nat(uint64_t hash, A... args) {
    shInit(hash);
    (PushArg(args), ...);
    return *reinterpret_cast<R*>(shCall());
}
template <typename... A> static inline void NatV(uint64_t hash, A... args) {
    shInit(hash);
    (PushArg(args), ...);
    shCall();
}

namespace N {
    constexpr uint64_t PLAYER_ID                     = 0x217E9DC48139933D;
    constexpr uint64_t PLAYER_PED_ID                 = 0x096275889B8E0EE0;
    constexpr uint64_t IS_PLAYER_CONTROL_ON          = 0x7964097FCE4C244B;
    constexpr uint64_t IS_PLAYER_FREE_AIMING         = 0x936F967D4BE1CE9D;
    constexpr uint64_t DOES_ENTITY_EXIST             = 0xD42BD6EB2E0F1677;
    constexpr uint64_t IS_ENTITY_DEAD                = 0x7D5B1F88E7504BBA;
    constexpr uint64_t GET_ENTITY_HEALTH             = 0x82368787EA73C0F7;
    constexpr uint64_t GET_ENTITY_MAX_HEALTH         = 0x15D757606D170C3C;
    constexpr uint64_t GET_ENTITY_SPEED              = 0xFB6BA510A533DF81;
    constexpr uint64_t GET_ENTITY_COORDS             = 0xA86D5F069399F44D;
    constexpr uint64_t GET_ENTITY_VELOCITY           = 0x4805D2B1D8CF94A9;
    constexpr uint64_t GET_ENTITY_BONE_INDEX_BY_NAME = 0xBACA8FE9C76C124E;
    constexpr uint64_t GET_WORLD_POS_OF_ENTITY_BONE  = 0x82CFA50E34681CA5;
    constexpr uint64_t IS_PED_ON_MOUNT               = 0x460BC76A0E10655E;
    constexpr uint64_t GET_MOUNT                     = 0xE7E11B8DCBED1058;
    constexpr uint64_t IS_PED_IN_ANY_VEHICLE         = 0x997ABD671D25CA0B;
    constexpr uint64_t GET_VEHICLE_PED_IS_IN         = 0x9A9112A0FE9A4713;
    constexpr uint64_t IS_PED_SHOOTING               = 0x34616828CD07F1A1;
    constexpr uint64_t IS_PED_RELOADING              = 0x24B100C68C645951;
    constexpr uint64_t IS_PED_RAGDOLL                = 0x47E4E977581C5B55;
    constexpr uint64_t IS_PED_FALLING                = 0xFB92A102F1C4DFA3;
    constexpr uint64_t IS_PED_SWIMMING               = 0x9DE327631295B4C2;
    constexpr uint64_t IS_PED_JUMPING                = 0xCEDABC5900A0BF97;
    constexpr uint64_t GET_CURRENT_PED_WEAPON        = 0x3A87E44BB9A01D54;
    constexpr uint64_t GET_WEAPONTYPE_GROUP          = 0xEDCA14CA5199FF25;
    constexpr uint64_t SET_CONTROL_SHAKE             = 0x48B3886C1358D0D5;
    constexpr uint64_t STOP_CONTROL_SHAKE            = 0x38C16A305E8CDC8D;
    constexpr uint64_t IS_CONTROL_JUST_PRESSED       = 0x91AEF906BCA88877; // IS_DISABLED_CONTROL_JUST_PRESSED (also fires when disabled)
    constexpr uint64_t IS_USING_KEYBOARD             = 0xA571D46727E2B718;
    constexpr uint64_t IS_PAUSE_MENU_ACTIVE          = 0x535384D6067BA42E;
    constexpr uint64_t DISABLE_CONTROL_ACTION        = 0xFE99B66D079CF6BC;
    constexpr uint64_t IS_ENTITY_IN_WATER            = 0xDDE5C125AC446723;
    constexpr uint64_t GET_ENTITY_SUBMERGED_LEVEL    = 0x4A77C3F73FD9E831;
    constexpr uint64_t IS_PED_SWIMMING_UNDER_WATER   = 0xC024869A53992F34;
    constexpr uint64_t IS_PED_IN_MELEE_COMBAT        = 0x4E209B2C1EAD5159;
    constexpr uint64_t GET_MELEE_TARGET_FOR_PED      = 0x18A3E9EE1297FD39;
    constexpr uint64_t GET_WATER_MAP_ZONE_AT_COORDS  = 0x5BA7A68A346A5A91;
    constexpr uint64_t GET_DISABLED_CONTROL_NORMAL   = 0x11E65974A982637C;
    constexpr uint64_t IS_ANY_UIAPP_ACTIVE           = 0xAC959AB99AAF3D9F;
    constexpr uint64_t GET_LAST_ENTITY_TO_DAMAGE     = 0xAF72EC7E1B54539B; // _GET_LAST_ENTITY_TO_DAMAGE_ENTITY
    constexpr uint64_t IS_ENTITY_A_PED               = 0xCF8176912DDA4EA5;
    constexpr uint64_t IS_PED_HUMAN                  = 0xB980061DA992779D;
    constexpr uint64_t GET_ENTITY_MODEL              = 0xDA76A9F39210D365;
    constexpr uint64_t IS_PED_IN_COMBAT              = 0x4859F1FC66A6278E;
    constexpr uint64_t IS_PED_CARRYING_SOMETHING     = 0xA911EE21EDF69DAF;
    constexpr uint64_t GET_FIRST_ENTITY_PED_CARRYING = 0xD806CD2A4F2C2996;
    constexpr uint64_t HAS_ENTITY_COLLIDED_WITH_ANY  = 0xDF18751EC74F90FF;
    constexpr uint64_t GET_ENTITY_HEADING            = 0xC230DD956E2F5507;
    constexpr uint64_t GET_ENTITY_HEIGHT_ABOVE_GROUND= 0x0D3B5BAEA08F63E9;
    constexpr uint64_t IS_ENTITY_ATTACHED            = 0xEE6AD63ABF59C0B7;
}
constexpr uint32_t INPUT_HORSE_SPRINT = 0x5AA007D7;

static uint32_t Joaat(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        h += (uint8_t)c; h += h << 10; h ^= h >> 6;
    }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}

// ---------------------------------------------------------------------------------------------
// Logging + config
// ---------------------------------------------------------------------------------------------
static char g_dir[MAX_PATH];
static char g_iniPath[MAX_PATH];
static char g_logPath[MAX_PATH];

static void Log(const char* fmt, ...) {
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

struct Config {
    bool  enabled = true;
    bool  onlyWithController = true;
    bool  debug = false;
    int   backend = 0;            // 0 = game (SET_CONTROL_SHAKE), 1 = direct to the virtual pad
    bool  menu = true;
    float menuIntensity = 1.0f;
    bool  startupShot = true;
    float shotThreshold = 0.45f;
    float shotWindow = 30.f;
    float shotIntensity = 1.0f;
    float intensity = 1.0f;
    float curve = 2.0f;
    float footsteps = 1.0f, horse = 1.0f, weapons = 1.0f, reload = 1.0f, damage = 1.0f, weaponSwitch = 1.0f;
    float melee = 1.0f, water = 1.0f, swimming = 1.0f;
    float animals = 1.0f, doors = 1.0f, carry = 1.0f, sliding = 1.0f;
    bool  muteUnfocused = true;
    float landing = 1.0f, vehicle = 1.0f, heartbeat = 1.0f;
    float lowHealth = 0.20f;
    bool  dsx = true, triggers = true, lightbar = true, gallopTriggers = true;
    bool  fireAtBreak = true;
    float breakAdjust = 0.f;      // fraction of trigger travel added to the break point
    int   dsxPort = 0;            // 0 = auto
    int   controllerIndex = 0;
} cfg;

static float IniF(const char* sec, const char* key, float def) {
    char buf[64], d[32];
    snprintf(d, sizeof d, "%g", def);
    GetPrivateProfileStringA(sec, key, d, buf, sizeof buf, g_iniPath);
    return (float)atof(buf);
}
static int IniI(const char* sec, const char* key, int def) {
    return (int)GetPrivateProfileIntA(sec, key, def, g_iniPath);
}

static void LoadConfig() {
    Config c;
    c.enabled            = IniI("General", "Enabled", 1) != 0;
    c.onlyWithController = IniI("General", "OnlyWithController", 1) != 0;
    c.debug              = IniI("General", "Debug", 0) != 0;
    char b[32];
    GetPrivateProfileStringA("Rumble", "Backend", "Direct", b, sizeof b, g_iniPath);
    c.backend    = (_stricmp(b, "Game") == 0) ? 0 : 1;
    c.menu          = IniI("Menu", "ButtonRumble", 1) != 0;
    c.menuIntensity = IniF("Menu", "Intensity", 100) / 100.f;
    c.startupShot   = IniI("Startup", "SplashShotRumble", 1) != 0;
    c.shotThreshold = IniF("Startup", "ShotThreshold", 10) / 100.f;
    c.shotWindow    = IniF("Startup", "WindowSeconds", 30);
    c.shotIntensity = IniF("Startup", "Intensity", 100) / 100.f;
    c.intensity  = IniF("Rumble", "Intensity", 100) / 100.f;
    c.curve      = std::clamp(IniF("Rumble", "ResponseCurve", 2.0f), 0.5f, 4.f);
    c.footsteps  = IniF("Rumble", "Footsteps", 100) / 100.f;
    c.horse      = IniF("Rumble", "Horse", 100) / 100.f;
    c.weapons    = IniF("Rumble", "Weapons", 100) / 100.f;
    c.reload     = IniF("Rumble", "Reload", 100) / 100.f;
    c.weaponSwitch = IniF("Rumble", "WeaponSwitch", 100) / 100.f;
    c.melee      = IniF("Rumble", "Melee", 100) / 100.f;
    c.water      = IniF("Rumble", "Water", 100) / 100.f;
    c.swimming   = IniF("Rumble", "Swimming", 100) / 100.f;
    c.animals    = IniF("Rumble", "Animals", 100) / 100.f;
    c.doors      = IniF("Rumble", "Doors", 100) / 100.f;
    c.carry      = IniF("Rumble", "Carry", 100) / 100.f;
    c.sliding    = IniF("Rumble", "Sliding", 100) / 100.f;
    c.muteUnfocused = IniI("General", "MuteWhenUnfocused", 1) != 0;
    c.damage     = IniF("Rumble", "Damage", 100) / 100.f;
    c.landing    = IniF("Rumble", "Landing", 100) / 100.f;
    c.vehicle    = IniF("Rumble", "Vehicle", 100) / 100.f;
    c.heartbeat  = IniF("Rumble", "Heartbeat", 100) / 100.f;
    c.lowHealth  = IniF("Rumble", "HeartbeatBelowHealth", 20) / 100.f;
    c.dsx             = IniI("DSX", "Enabled", 1) != 0;
    c.dsxPort         = IniI("DSX", "Port", 0);
    c.controllerIndex = IniI("DSX", "ControllerIndex", 0);
    c.triggers        = IniI("DSX", "AdaptiveTriggers", 1) != 0;
    c.gallopTriggers  = IniI("DSX", "GallopingTriggersOnHorse", 1) != 0;
    // Experimental and off by default: the game only fires on the press edge, which happens while the
    // action is blocked, so with this on the gun never fires.
    c.fireAtBreak     = IniI("DSX", "FireAtBreakPoint", 0) != 0;
    c.breakAdjust     = std::clamp(IniF("DSX", "BreakPointAdjust", 0), -30.f, 30.f) / 100.f;
    c.lightbar        = IniI("DSX", "Lightbar", 1) != 0;
    cfg = c;
}

static FILETIME IniTime() {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &d)) return FILETIME{};
    return d.ftLastWriteTime;
}

// ---------------------------------------------------------------------------------------------
// DSX UDP client
// ---------------------------------------------------------------------------------------------
namespace DSX {
    enum Instr { TriggerUpdate = 1, RGBUpdate = 2, PlayerLED = 3, TriggerThreshold = 4, ResetToUserSettings = 7 };
    enum Mode  { Normal = 0, GameCube = 1, VerySoft = 2, Soft = 3, Hard = 4, VeryHard = 5, Hardest = 6, Rigid = 7,
                 VibrateTrigger = 8, Choppy = 9, Medium = 10, VibrateTriggerPulse = 11, CustomTriggerValue = 12,
                 Resistance = 13, Bow = 14, Galloping = 15, SemiAutomaticGun = 16, AutomaticGun = 17, Machine = 18 };
    enum Side  { Left = 1, Right = 2 };

    static SOCKET      sock = INVALID_SOCKET;
    static sockaddr_in addr{};

    static int AutoPort() {
        // DSX writes its port here when "write port to file" is used; otherwise the default is 6969.
        FILE* f = fopen("C:\\Temp\\DualSenseX\\DualSenseX_PortNumber.txt", "r");
        int p = 0;
        if (f) { if (fscanf(f, "%d", &p) != 1) p = 0; fclose(f); }
        return (p > 0 && p < 65536) ? p : 6969;
    }

    static void Open() {
        WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        u_long nb = 1; ioctlsocket(sock, FIONBIO, &nb);
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)(cfg.dsxPort > 0 ? cfg.dsxPort : AutoPort()));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        Log("DSX UDP target 127.0.0.1:%d", ntohs(addr.sin_port));
    }
    static void Close() { if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; } WSACleanup(); }

    struct Packet {
        std::string json = "{\"instructions\":[";
        bool first = true;
        void Add(int type, std::initializer_list<int> params) {
            char b[32];
            json += first ? "" : ",";
            first = false;
            snprintf(b, sizeof b, "{\"type\":%d,\"parameters\":[", type);
            json += b;
            bool f = true;
            for (int p : params) { snprintf(b, sizeof b, f ? "%d" : ",%d", p); json += b; f = false; }
            json += "]}";
        }
        bool Empty() const { return first; }
        void Send() {
            if (first || sock == INVALID_SOCKET) return;
            std::string s = json + "]}";
            sendto(sock, s.data(), (int)s.size(), 0, (sockaddr*)&addr, sizeof addr);
            if (cfg.debug) Log("DSX <- %s", s.c_str());
        }
    };
}

// A trigger state, compared to avoid spamming DSX.
struct TriggerFx {
    int mode = DSX::Normal;
    int p[6] = {0, 0, 0, 0, 0, 0};
    int n = 0;
    bool operator==(const TriggerFx& o) const {
        if (mode != o.mode || n != o.n) return false;
        for (int i = 0; i < n; i++) if (p[i] != o.p[i]) return false;
        return true;
    }
};
static TriggerFx TFx(int mode, std::initializer_list<int> ps = {}) {
    TriggerFx t; t.mode = mode;
    for (int v : ps) if (t.n < 6) t.p[t.n++] = v;
    return t;
}
static void AddTrigger(DSX::Packet& pk, int side, const TriggerFx& t) {
    int c = cfg.controllerIndex;
    switch (t.n) {
        case 0: pk.Add(DSX::TriggerUpdate, {c, side, t.mode}); break;
        case 1: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0]}); break;
        case 2: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0], t.p[1]}); break;
        case 3: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0], t.p[1], t.p[2]}); break;
        case 4: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0], t.p[1], t.p[2], t.p[3]}); break;
        case 5: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0], t.p[1], t.p[2], t.p[3], t.p[4]}); break;
        default: pk.Add(DSX::TriggerUpdate, {c, side, t.mode, t.p[0], t.p[1], t.p[2], t.p[3], t.p[4], t.p[5]}); break;
    }
}

// ---------------------------------------------------------------------------------------------
// Rumble mixer
// ---------------------------------------------------------------------------------------------
// sharp = decay exponent of the envelope: 1 = soft linear fade, 2 = thud, 4-5 = crisp snap
struct Pulse { float l, r, dur, t, sharp; };
static inline float Envelope(const Pulse& p) { if (p.t < 0.f) return 0.f; return powf(std::max(0.f, 1.f - p.t / p.dur), p.sharp); }
// DSX's rumble emulation makes small values feel much stronger than they are. The curve pushes weak
// effects down hard and leaves strong ones almost untouched, so a footstep stays well below a gunshot.
static inline float Curve(float a, float gamma) { return a <= 0.f ? 0.f : powf(std::min(a, 1.f), gamma); }
static std::vector<Pulse> g_pulses;
static float g_contL = 0, g_contR = 0;     // continuous layer (set each frame)

static void AddPulse(float l, float r, float durSec, float scale, float sharp = 2.f) {
    float s = scale * cfg.intensity;
    if (s <= 0.f) return;
    g_pulses.push_back({l * s, r * s, std::max(durSec, 0.015f), 0.f, sharp});
}

static void AddPulseDelayed(float l, float r, float durSec, float scale, float sharp, float delay) {
    float s = scale * cfg.intensity;
    if (s <= 0.f) return;
    g_pulses.push_back({l * s, r * s, std::max(durSec, 0.015f), -delay, sharp});
}

// ---------------------------------------------------------------------------------------------
// Direct pad access: DSX's virtual DualShock 4 over HID (input + rumble), XInput as fallback
// ---------------------------------------------------------------------------------------------
enum Btn : uint32_t {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_CROSS = 16, B_CIRCLE = 32, B_SQUARE = 64, B_TRIANGLE = 128,
    B_L1 = 256, B_R1 = 512, B_OPTIONS = 1024, B_L2 = 2048, B_R2 = 4096, B_SHARE = 8192
};

namespace Pad {
    struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
    struct XVib   { WORD l, r; };
    using fnXGet = DWORD(WINAPI*)(DWORD, XState*);
    using fnXSet = DWORD(WINAPI*)(DWORD, XVib*);
    static fnXGet xget; static fnXSet xset;
    static int xuser = -1;

    static HANDLE     h = INVALID_HANDLE_VALUE;
    static USHORT     inLen = 0, outLen = 0;
    static OVERLAPPED rov{}, wov{};
    static bool       reading = false, writing = false;
    static uint8_t    inBuf[256], outBuf[256];
    static uint64_t   lastOpenTry = 0;

    static void LoadXInput() {
        const char* names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (auto n : names) {
            HMODULE m = LoadLibraryA(n);
            if (!m) continue;
            xget = (fnXGet)GetProcAddress(m, "XInputGetState");
            xset = (fnXSet)GetProcAddress(m, "XInputSetState");
            if (xget && xset) return;
        }
    }

    static void Close() {
        if (h != INVALID_HANDLE_VALUE) { CancelIo(h); CloseHandle(h); h = INVALID_HANDLE_VALUE; }
        reading = writing = false;
    }

    static bool OpenDS4() {
        GUID hidGuid; HidD_GetHidGuid(&hidGuid);
        HDEVINFO set = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return false;
        SP_DEVICE_INTERFACE_DATA ifd{}; ifd.cbSize = sizeof ifd;
        bool ok = false;
        for (DWORD i = 0; !ok && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailA(set, &ifd, nullptr, 0, &need, nullptr);
            std::vector<uint8_t> mem(need + 8);
            auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)mem.data();
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, nullptr, nullptr)) continue;
            HANDLE d = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (d == INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES a{}; a.Size = sizeof a;
            // DualShock 4 (v1 0x05C4 / v2 0x09CC) - this is what DSX emulates. The real DualSense (0x0CE6) is left alone.
            if (HidD_GetAttributes(d, &a) && a.VendorID == 0x054C && (a.ProductID == 0x05C4 || a.ProductID == 0x09CC)) {
                PHIDP_PREPARSED_DATA pp;
                if (HidD_GetPreparsedData(d, &pp)) {
                    HIDP_CAPS caps;
                    if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength >= 11) {
                        inLen = std::min<USHORT>(caps.InputReportByteLength, sizeof inBuf);
                        outLen = std::min<USHORT>(caps.OutputReportByteLength, sizeof outBuf);
                        h = d; ok = true;
                        Log("pad: virtual DualShock 4 %04X:%04X (in %u, out %u bytes)", a.VendorID, a.ProductID, inLen, outLen);
                    }
                    HidD_FreePreparsedData(pp);
                }
            }
            if (!ok) CloseHandle(d);
        }
        SetupDiDestroyDeviceInfoList(set);
        if (ok) {
            if (!rov.hEvent) rov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!wov.hEvent) wov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        }
        return ok;
    }

    static void EnsureOpen() {
        if (h != INVALID_HANDLE_VALUE) return;
        uint64_t now = GetTickCount64();
        if (now - lastOpenTry < 2000) return;
        lastOpenTry = now;
        if (OpenDS4()) return;
        if (xget) {   // DSX in Xbox mode
            XState s;
            for (int i = 0; i < 4; i++) if (xget(i, &s) == 0) { if (xuser != i) Log("pad: XInput user %d", i); xuser = i; return; }
        }
        xuser = -1;
    }

    static bool Ready() { return h != INVALID_HANDLE_VALUE || xuser >= 0; }

    // strong = left/low-frequency motor, weak = right/high-frequency motor (0..255)
    static void Rumble(uint8_t strong, uint8_t weak) {
        if (h != INVALID_HANDLE_VALUE) {
            if (writing) {
                if (WaitForSingleObject(wov.hEvent, 0) != WAIT_OBJECT_0) return;   // previous write still pending
                DWORD n; GetOverlappedResult(h, &wov, &n, FALSE);
                writing = false;
            }
            memset(outBuf, 0, outLen);
            outBuf[0] = 0x05;      // DS4 USB output report
            outBuf[1] = 0x01;      // flags: rumble only (leave the lightbar to DSX)
            outBuf[4] = weak;
            outBuf[5] = strong;
            ResetEvent(wov.hEvent);
            if (WriteFile(h, outBuf, outLen, nullptr, &wov)) return;
            if (GetLastError() == ERROR_IO_PENDING) { writing = true; return; }
            if (!HidD_SetOutputReport(h, outBuf, outLen)) { Log("pad: write failed (%lu), reopening", GetLastError()); Close(); }
        } else if (xuser >= 0 && xset) {
            XVib v{(WORD)(strong * 257), (WORD)(weak * 257)};
            if (xset(xuser, &v) != 0) xuser = -1;
        }
    }

    static uint32_t FromDS4(const uint8_t* r) {
        uint32_t b = 0;
        static const uint32_t dpad[9] = {B_UP, B_UP | B_RIGHT, B_RIGHT, B_DOWN | B_RIGHT, B_DOWN, B_DOWN | B_LEFT, B_LEFT, B_UP | B_LEFT, 0};
        uint8_t d = r[5] & 0x0F;
        b |= dpad[d > 8 ? 8 : d];
        if (r[5] & 0x10) b |= B_SQUARE;
        if (r[5] & 0x20) b |= B_CROSS;
        if (r[5] & 0x40) b |= B_CIRCLE;
        if (r[5] & 0x80) b |= B_TRIANGLE;
        if (r[6] & 0x01) b |= B_L1;
        if (r[6] & 0x02) b |= B_R1;
        if (r[6] & 0x04) b |= B_L2;
        if (r[6] & 0x08) b |= B_R2;
        if (r[6] & 0x10) b |= B_SHARE;
        if (r[6] & 0x20) b |= B_OPTIONS;
        return b;
    }

    // Returns true when a new button state was read.
    static bool ReadButtons(uint32_t& out) {
        if (h != INVALID_HANDLE_VALUE) {
            if (!reading) {
                ResetEvent(rov.hEvent);
                if (ReadFile(h, inBuf, inLen, nullptr, &rov)) { reading = true; }
                else if (GetLastError() == ERROR_IO_PENDING) reading = true;
                else { Log("pad: read failed (%lu), reopening", GetLastError()); Close(); return false; }
            }
            if (WaitForSingleObject(rov.hEvent, 0) != WAIT_OBJECT_0) return false;
            DWORD n = 0;
            reading = false;
            if (!GetOverlappedResult(h, &rov, &n, FALSE)) { Close(); return false; }
            if (n >= 8 && inBuf[0] == 0x01) { out = FromDS4(inBuf); return true; }
            return false;
        }
        if (xuser >= 0 && xget) {
            static DWORD lastPacket = 0xFFFFFFFF;
            XState s;
            if (xget(xuser, &s) != 0) { xuser = -1; return false; }
            if (s.packet == lastPacket) return false;
            lastPacket = s.packet;
            WORD w = s.buttons; uint32_t b = 0;
            if (w & 0x0001) b |= B_UP;    if (w & 0x0002) b |= B_DOWN;
            if (w & 0x0004) b |= B_LEFT;  if (w & 0x0008) b |= B_RIGHT;
            if (w & 0x0010) b |= B_OPTIONS; if (w & 0x0020) b |= B_SHARE;
            if (w & 0x0100) b |= B_L1;    if (w & 0x0200) b |= B_R1;
            if (w & 0x1000) b |= B_CROSS; if (w & 0x2000) b |= B_CIRCLE;
            if (w & 0x4000) b |= B_SQUARE; if (w & 0x8000) b |= B_TRIANGLE;
            if (s.lt > 120) b |= B_L2;    if (s.rt > 120) b |= B_R2;
            out = b; return true;
        }
        return false;
    }
}

// Peak level of this process's own audio session (the game's sound), via the Windows audio mixer.
struct AudioMeter {
    IAudioMeterInformation* meter = nullptr;
    uint64_t lastTry = 0;
    void Release() { if (meter) { meter->Release(); meter = nullptr; } }
    bool Find() {
        lastTry = GetTickCount64();
        IMMDeviceEnumerator* en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return false;
        IMMDevice* dev = nullptr;
        IAudioSessionManager2* mgr = nullptr;
        IAudioSessionEnumerator* sessions = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
            SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr)) &&
            SUCCEEDED(mgr->GetSessionEnumerator(&sessions))) {
            int count = 0; sessions->GetCount(&count);
            for (int i = 0; i < count && !meter; i++) {
                IAudioSessionControl* ctl = nullptr;
                if (FAILED(sessions->GetSession(i, &ctl))) continue;
                IAudioSessionControl2* ctl2 = nullptr;
                DWORD pid = 0;
                if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&ctl2))) {
                    ctl2->GetProcessId(&pid);
                    ctl2->Release();
                }
                if (pid == GetCurrentProcessId()) ctl->QueryInterface(__uuidof(IAudioMeterInformation), (void**)&meter);
                ctl->Release();
            }
        }
        if (sessions) sessions->Release();
        if (mgr) mgr->Release();
        if (dev) dev->Release();
        en->Release();
        if (meter) Log("startup: found the game's audio session");
        return meter != nullptr;
    }
    float Peak() {
        float p = 0.f;
        if (meter && FAILED(meter->GetPeakValue(&p))) { Release(); return 0.f; }
        return p;
    }
};

// Background thread: menu button rumble, the startup splash shot, and output for the Direct backend.
static DWORD WINAPI PadThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Pad::LoadXInput();
    AudioMeter audio;
    uint64_t audioStart = 0;
    bool shotPhase = cfg.startupShot;
    float avg = 0.f, shotCooldown = 0.f;
    float maxPeak = 0.f, maxJumpPeak = 0.f, maxJumpAvg = 0.f;
    uint32_t prevButtons = 0;
    bool havePrev = false;
    std::vector<Pulse> pulses;
    int lastS = -1, lastW = -1;
    uint64_t lastWrite = 0;
    LARGE_INTEGER f, prev, cur;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&prev);
    timeBeginPeriod(1);

    auto pulse = [&](float l, float r, float dur, float scale, float sharp) {
        if (scale > 0.f) pulses.push_back({l * scale, r * scale, dur, 0.f, sharp});
    };

    while (!g_quit) {
        QueryPerformanceCounter(&cur);
        float dt = std::clamp((float)(cur.QuadPart - prev.QuadPart) / (float)f.QuadPart, 0.f, 0.1f);
        prev = cur;
        uint64_t now = GetTickCount64();
        bool scriptRunning = g_lastTick != 0 && now - g_lastTick < 500;
        bool menu = !scriptRunning || g_scriptMenu;

        Pad::EnsureOpen();
        static uint64_t lastFocus = 0;
        if (now - lastFocus > 100) {
            lastFocus = now;
            bool f = !cfg.muteUnfocused || GameFocused();
            if (f != g_focused.load()) {
                if (!f && cfg.dsx && cfg.triggers) {   // left the game: let both triggers go loose right away
                    DSX::Packet pk;
                    pk.Add(DSX::TriggerUpdate, {cfg.controllerIndex, DSX::Left, DSX::Normal});
                    pk.Add(DSX::TriggerUpdate, {cfg.controllerIndex, DSX::Right, DSX::Normal});
                    pk.Send();
                }
                if (f) g_triggersDirty = true;
                g_focused = f;
            }
        }
        bool focused = g_focused;

        // --- menu button presses ---
        uint32_t b;
        while (Pad::ReadButtons(b)) {
            uint32_t pressed = havePrev ? (b & ~prevButtons) : 0;
            prevButtons = b; havePrev = true;
            g_padButtons = b;
            if (!pressed || !cfg.enabled || !cfg.menu || !menu || !focused) continue;
            float s = cfg.menuIntensity * cfg.intensity;
            // short crisp clicks on the light (right) motor, like a UI detent - not a thump
            if (pressed & B_CROSS)                                  pulse(0.05f, 0.30f, 0.030f, s, 4.f);
            else if (pressed & (B_CIRCLE | B_OPTIONS))              pulse(0.03f, 0.25f, 0.028f, s, 4.f);
            else if (pressed & (B_TRIANGLE | B_SQUARE))             pulse(0.00f, 0.22f, 0.025f, s, 4.f);
            else if (pressed & (B_L1 | B_R1))                       pulse(0.00f, 0.22f, 0.022f, s, 5.f);
            else if (pressed & (B_UP | B_DOWN | B_LEFT | B_RIGHT))  pulse(0.00f, 0.17f, 0.018f, s, 5.f);
        }

        // --- startup splash shot (only before the game's scripts are running) ---
        if (shotPhase) {
            if (!cfg.startupShot || scriptRunning || (audioStart && now - audioStart > (uint64_t)(cfg.shotWindow * 1000.f))) {
                shotPhase = false; audio.Release();
                Log("startup: shot detection finished (loudest peak %.2f, biggest jump %.2f over avg %.2f; ShotThreshold=%.0f)",
                    maxPeak, maxJumpPeak, maxJumpAvg, cfg.shotThreshold * 100.f);
            } else {
                if (!audio.meter && now - audio.lastTry > 500) audio.Find();
                if (audio.meter) {
                    float p = audio.Peak();
                    if (!audioStart && p > 0.001f) audioStart = now;
                    shotCooldown -= dt;
                    maxPeak = std::max(maxPeak, p);
                    if (p - avg > maxJumpPeak - maxJumpAvg) { maxJumpPeak = p; maxJumpAvg = avg; }
                    // a sudden jump well above the recent level (the shot comes out of near-silence)
                    if (p > cfg.shotThreshold && p - avg > cfg.shotThreshold && p > avg * 2.5f && shotCooldown <= 0.f) {
                        float a = std::clamp(0.8f + (p - cfg.shotThreshold) * 2.0f, 0.8f, 1.f);
                        float s = cfg.shotIntensity * cfg.intensity;
                        pulse(0.f, a, 0.06f, s, 4.f);          // muzzle crack
                        pulse(a, a * 0.3f, 0.40f, s, 1.8f);    // heavy boom
                        shotCooldown = 1.0f;
                        Log("startup: shot (peak %.2f, avg %.2f)", p, avg);
                    }
                    if (cfg.debug && p > 0.2f) Log("startup: peak %.2f avg %.2f", p, avg);
                    avg += (p - avg) * std::min(1.f, dt / 0.35f);
                }
            }
        }

        // --- mix + output ---
        float L = 0, R = 0;
        for (auto& p : pulses) {
            float k = Envelope(p);
            L = std::max(L, p.l * k); R = std::max(R, p.r * k);
            p.t += dt;
        }
        pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [](const Pulse& p) { return p.t >= p.dur; }), pulses.end());
        if (cfg.backend == 1 && !menu) { L = std::max(L, g_gameL.load()); R = std::max(R, g_gameR.load()); }
        if (!focused) { L = R = 0.f; pulses.clear(); }
        int S = (int)(Curve(L, cfg.curve) * 255.f + 0.5f);
        int W = (int)(Curve(R, cfg.curve) * 255.f + 0.5f);
        bool active = S > 2 || W > 2;
        if (!active) { S = W = 0; }
        bool owned = active || cfg.backend == 1 || pulses.size();   // only touch the pad when we have something to say
        if (Pad::Ready() && (owned || lastS > 0 || lastW > 0)) {
            if (S != lastS || W != lastW || (active && now - lastWrite > 60)) {
                Pad::Rumble((uint8_t)S, (uint8_t)W);
                lastS = S; lastW = W; lastWrite = now;
            }
        }
        Sleep(3);
    }
    if (Pad::Ready() && (lastS > 0 || lastW > 0)) Pad::Rumble(0, 0);
    timeEndPeriod(1);
    audio.Release();
    CoUninitialize();
    return 0;
}

static int   g_lastShake = 0;
static DWORD g_lastShakeTick = 0;

static void OutputRumble(float dt, bool allowed) {
    float L = 0, R = 0;
    for (auto& p : g_pulses) {
        float k = Envelope(p);
        L = std::max(L, p.l * k);
        R = std::max(R, p.r * k);
        p.t += dt;
    }
    g_pulses.erase(std::remove_if(g_pulses.begin(), g_pulses.end(), [](const Pulse& p) { return p.t >= p.dur; }), g_pulses.end());
    L = std::max(L, g_contL * cfg.intensity);
    R = std::max(R, g_contR * cfg.intensity);
    if (!allowed) { L = R = 0; g_pulses.clear(); }
    L = std::clamp(L, 0.f, 1.f);
    R = std::clamp(R, 0.f, 1.f);

    DWORD now = GetTickCount();
    if (cfg.backend == 1) {
        g_gameL = L; g_gameR = R;            // written to the pad by PadThread
        static int logged = 0;
        if ((L > 0.05f || R > 0.05f) && logged < 5) { logged++; Log("in-game rumble L=%.2f R=%.2f", L, R); }
    } else {
        // The game exposes a single strength. Weight the heavy motor slightly more.
        float a = Curve(std::max(L, R * 0.85f), cfg.curve);
        int v = (int)(a * 255.f + 0.5f);
        if (v > 2) {
            if (std::abs(v - g_lastShake) >= 4 || now - g_lastShakeTick > 40) {
                NatV(N::SET_CONTROL_SHAKE, 0, 80, v);
                g_lastShake = v; g_lastShakeTick = now;
            }
        } else if (g_lastShake) {
            NatV(N::STOP_CONTROL_SHAKE, 0);
            g_lastShake = 0;
        }
    }
}

static void StopRumble() {
    g_pulses.clear(); g_contL = g_contR = 0;
    g_gameL = 0.f; g_gameR = 0.f;
    if (g_lastShake) { NatV(N::STOP_CONTROL_SHAKE, 0); g_lastShake = 0; }
}

// ---------------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------------
enum WClass { W_NONE, W_REVOLVER, W_PISTOL, W_REPEATER, W_RIFLE, W_SNIPER, W_SHOTGUN, W_BOW, W_THROWN, W_LASSO, W_MELEE, W_OTHER,
              W_LIGHTPISTOL, W_LIGHTRIFLE, W_HEAVYREVOLVER, W_ELEPHANT, W_SAWEDOFF, W_DAREVOLVER, W_DOUBLEBARREL,
              W_IMPROVEDBOW };

struct WeaponEntry { uint32_t hash; WClass cls; };
static std::vector<WeaponEntry> g_weapons;
static uint32_t g_groups[12];

static void InitWeapons() {
    struct { const char* n; WClass c; } names[] = {
        {"WEAPON_REVOLVER_CATTLEMAN", W_REVOLVER}, {"WEAPON_REVOLVER_CATTLEMAN_JOHN", W_REVOLVER},
        {"WEAPON_REVOLVER_CATTLEMAN_MEXICAN", W_REVOLVER}, {"WEAPON_REVOLVER_CATTLEMAN_PIG", W_REVOLVER},
        {"WEAPON_REVOLVER_DOUBLEACTION", W_DAREVOLVER}, {"WEAPON_REVOLVER_DOUBLEACTION_EXOTIC", W_DAREVOLVER},
        {"WEAPON_REVOLVER_DOUBLEACTION_GAMBLER", W_DAREVOLVER}, {"WEAPON_REVOLVER_DOUBLEACTION_MICAH", W_DAREVOLVER},
        // .45 Schofield and the LeMat (with its shot barrel) kick harder; the .36 Navy is light
        {"WEAPON_REVOLVER_LEMAT", W_HEAVYREVOLVER}, {"WEAPON_REVOLVER_SCHOFIELD", W_HEAVYREVOLVER},
        {"WEAPON_REVOLVER_SCHOFIELD_GOLDEN", W_HEAVYREVOLVER}, {"WEAPON_REVOLVER_SCHOFIELD_CALLOWAY", W_HEAVYREVOLVER},
        {"WEAPON_REVOLVER_NAVY", W_LIGHTPISTOL}, {"WEAPON_REVOLVER_NAVY_CROSSOVER", W_LIGHTPISTOL},
        // the Volcanic is a weak lever pistol; the Mauser/M1899/semi-auto are snappy, light-recoil autos
        {"WEAPON_PISTOL_VOLCANIC", W_LIGHTPISTOL}, {"WEAPON_PISTOL_M1899", W_PISTOL}, {"WEAPON_PISTOL_MAUSER", W_PISTOL},
        {"WEAPON_PISTOL_MAUSER_DRUNK", W_PISTOL}, {"WEAPON_PISTOL_SEMIAUTO", W_PISTOL},
        {"WEAPON_REPEATER_CARBINE", W_REPEATER}, {"WEAPON_REPEATER_WINCHESTER", W_REPEATER},
        {"WEAPON_REPEATER_HENRY", W_REPEATER}, {"WEAPON_REPEATER_EVANS", W_REPEATER},
        {"WEAPON_RIFLE_VARMINT", W_LIGHTRIFLE}, {"WEAPON_RIFLE_SPRINGFIELD", W_RIFLE},
        {"WEAPON_RIFLE_BOLTACTION", W_RIFLE}, {"WEAPON_RIFLE_ELEPHANT", W_ELEPHANT},
        {"WEAPON_SNIPERRIFLE_ROLLINGBLOCK", W_SNIPER}, {"WEAPON_SNIPERRIFLE_ROLLINGBLOCK_EXOTIC", W_SNIPER},
        {"WEAPON_SNIPERRIFLE_CARCANO", W_SNIPER},
        {"WEAPON_SHOTGUN_DOUBLEBARREL", W_DOUBLEBARREL}, {"WEAPON_SHOTGUN_DOUBLEBARREL_EXOTIC", W_DOUBLEBARREL},
        {"WEAPON_SHOTGUN_SAWEDOFF", W_SAWEDOFF}, {"WEAPON_SHOTGUN_PUMP", W_SHOTGUN},
        {"WEAPON_SHOTGUN_REPEATING", W_SHOTGUN}, {"WEAPON_SHOTGUN_SEMIAUTO", W_SHOTGUN},
        {"WEAPON_BOW", W_BOW}, {"WEAPON_BOW_IMPROVED", W_IMPROVEDBOW},
        {"WEAPON_LASSO", W_LASSO}, {"WEAPON_LASSO_REINFORCED", W_LASSO},
        {"WEAPON_THROWN_DYNAMITE", W_THROWN}, {"WEAPON_THROWN_MOLOTOV", W_THROWN},
        {"WEAPON_THROWN_TOMAHAWK", W_THROWN}, {"WEAPON_THROWN_TOMAHAWK_ANCIENT", W_THROWN},
        {"WEAPON_THROWN_THROWING_KNIVES", W_THROWN}, {"WEAPON_THROWN_POISONBOTTLE", W_THROWN},
        {"WEAPON_THROWN_BOLAS", W_THROWN},
        {"WEAPON_UNARMED", W_NONE}, {"WEAPON_FISHINGROD", W_OTHER},
    };
    for (auto& n : names) g_weapons.push_back({Joaat(n.n), n.c});
    const char* groups[] = {"", "GROUP_REVOLVER", "GROUP_PISTOL", "GROUP_REPEATER", "GROUP_RIFLE", "GROUP_SNIPER",
                            "GROUP_SHOTGUN", "GROUP_BOW", "GROUP_THROWN", "GROUP_LASSO", "GROUP_MELEE", ""};
    for (int i = 0; i < 12; i++) g_groups[i] = groups[i][0] ? Joaat(groups[i]) : 0;
}

static WClass Classify(uint32_t weapon) {
    if (!weapon) return W_NONE;
    for (auto& w : g_weapons) if (w.hash == weapon) return w.cls;
    uint32_t grp = Nat<uint32_t>(N::GET_WEAPONTYPE_GROUP, weapon);
    for (int i = 1; i < 11; i++) if (g_groups[i] && g_groups[i] == grp) return (WClass)i;
    return W_OTHER;
}
static bool IsGun(WClass c) {
    return (c >= W_REVOLVER && c <= W_SHOTGUN) || (c >= W_LIGHTPISTOL && c <= W_DOUBLEBARREL);
}

// A shot = a short crisp crack on the light (right) motor + a recoil kick on the heavy (left) motor.
// Heavier calibres get a stronger, longer, rounder kick; light guns are almost all crack.
struct ShotFx { float crack, crackDur, kick, kickDur, kickSharp; };
static ShotFx ShotFor(WClass c) {
    switch (c) {
        case W_LIGHTRIFLE:    return {0.42f, 0.035f, 0.10f, 0.05f, 3.5f};   // .22 Varmint: a tap
        case W_LIGHTPISTOL:   return {0.52f, 0.040f, 0.22f, 0.06f, 3.5f};   // Volcanic, .36 Navy
        case W_PISTOL:        return {0.58f, 0.040f, 0.30f, 0.06f, 3.5f};   // Mauser, M1899, semi-auto
        case W_DAREVOLVER:
        case W_REVOLVER:      return {0.66f, 0.045f, 0.46f, 0.09f, 3.0f};   // Cattleman, Double-Action
        case W_HEAVYREVOLVER: return {0.72f, 0.050f, 0.56f, 0.10f, 2.8f};   // Schofield, LeMat
        case W_REPEATER:      return {0.66f, 0.045f, 0.50f, 0.10f, 3.0f};   // .44 lever actions
        case W_RIFLE:         return {0.78f, 0.050f, 0.74f, 0.14f, 2.5f};   // Springfield, Bolt-Action
        case W_SNIPER:        return {0.84f, 0.055f, 0.84f, 0.17f, 2.3f};   // Rolling Block, Carcano
        case W_DOUBLEBARREL:
        case W_SHOTGUN:       return {0.82f, 0.055f, 0.95f, 0.20f, 2.2f};   // 12 gauge
        case W_SAWEDOFF:      return {0.90f, 0.055f, 1.00f, 0.20f, 2.2f};   // short barrel, all blast
        case W_ELEPHANT:      return {1.00f, 0.060f, 1.00f, 0.28f, 1.8f};   // the big one
        case W_IMPROVEDBOW:
        case W_BOW:           return {0.32f, 0.030f, 0.08f, 0.05f, 4.0f};   // string release
        case W_THROWN:        return {0.16f, 0.030f, 0.00f, 0.00f, 1.0f};   // throw
        default:              return {0.62f, 0.045f, 0.40f, 0.08f, 3.0f};
    }
}

static void ShotRumble(WClass c) {
    ShotFx f = ShotFor(c);
    float s = cfg.weapons;
    AddPulse(f.crack * 0.15f, f.crack, f.crackDur, s, 4.5f);
    if (f.kick > 0.f) AddPulse(f.kick, f.kick * 0.25f, f.kickDur, s, f.kickSharp);
}

// Adaptive triggers, modelled on the real guns.
//  RT = SemiAutomaticGun(start, end, force): free travel up to `start`, a wall that gets harder with
//       `force` (0-8) and breaks at `end` - like the sear letting go. Short start->end = crisp break,
//       long = long double-action pull. Real pull weights: .22 / light pistols ~2-3 lb, single-action
//       revolvers ~3-4 lb short and crisp, double-action ~10 lb and long, lever actions ~5 lb, military
//       rifles ~6-7 lb, double-barrel shotguns and big-bore rifles the heaviest.
//  LT = Resistance(start, force): aiming = shouldering the weight of the gun - a pistol is light,
//       a rifle or shotgun noticeably heavier.
static void WeaponTriggers(WClass c, TriggerFx& lt, TriggerFx& rt) {
    switch (c) {
        case W_LIGHTRIFLE:    lt = TFx(DSX::Resistance, {3, 0}); rt = TFx(DSX::SemiAutomaticGun, {3, 5, 2}); break; // .22 Varmint
        case W_LIGHTPISTOL:   lt = TFx(DSX::Resistance, {3, 0}); rt = TFx(DSX::SemiAutomaticGun, {3, 5, 3}); break; // Volcanic, Navy
        case W_PISTOL:        lt = TFx(DSX::Resistance, {3, 1}); rt = TFx(DSX::SemiAutomaticGun, {3, 5, 4}); break; // Mauser, M1899, semi-auto
        case W_REVOLVER:      lt = TFx(DSX::Resistance, {4, 1}); rt = TFx(DSX::SemiAutomaticGun, {4, 6, 5}); break; // Cattleman (single action)
        case W_HEAVYREVOLVER: lt = TFx(DSX::Resistance, {2, 1}); rt = TFx(DSX::SemiAutomaticGun, {4, 6, 6}); break; // Schofield, LeMat
        case W_DAREVOLVER:    lt = TFx(DSX::Resistance, {4, 1}); rt = TFx(DSX::SemiAutomaticGun, {2, 7, 6}); break; // Double-Action: long pull
        case W_REPEATER:      lt = TFx(DSX::Resistance, {2, 1}); rt = TFx(DSX::SemiAutomaticGun, {3, 6, 5}); break; // lever actions
        case W_RIFLE:         lt = TFx(DSX::Resistance, {2, 2}); rt = TFx(DSX::SemiAutomaticGun, {3, 6, 6}); break; // Springfield, Bolt-Action
        case W_SNIPER:        lt = TFx(DSX::Resistance, {2, 2}); rt = TFx(DSX::SemiAutomaticGun, {4, 6, 7}); break; // Rolling Block, Carcano
        case W_SHOTGUN:       lt = TFx(DSX::Resistance, {2, 2}); rt = TFx(DSX::SemiAutomaticGun, {3, 6, 6}); break; // pump, repeating, semi-auto
        case W_DOUBLEBARREL:
        case W_SAWEDOFF:      lt = TFx(DSX::Resistance, {2, 2}); rt = TFx(DSX::SemiAutomaticGun, {3, 6, 8}); break; // double triggers are stiff
        case W_ELEPHANT:      lt = TFx(DSX::Resistance, {2, 3}); rt = TFx(DSX::SemiAutomaticGun, {4, 7, 8}); break;
        // bows: RT draws the string - resistance builds with the draw, then snaps back on release
        case W_BOW:           lt = TFx(DSX::Normal);             rt = TFx(DSX::Bow, {1, 7, 5, 7}); break;
        case W_IMPROVEDBOW:   lt = TFx(DSX::Normal);             rt = TFx(DSX::Bow, {1, 7, 7, 8}); break;
        case W_LASSO:         lt = TFx(DSX::Normal);             rt = TFx(DSX::Resistance, {2, 3}); break; // rope tension
        case W_THROWN:        lt = TFx(DSX::Normal);             rt = TFx(DSX::Resistance, {3, 2}); break; // wind-up
        default:              lt = TFx(DSX::Normal);             rt = TFx(DSX::Normal); break;
    }
}

// ---------------------------------------------------------------------------------------------
// Game state tracking
// ---------------------------------------------------------------------------------------------
struct FootTracker {
    int   bone = -1;
    float prevRel = 0, vel = 0, minRel = 0;
    bool  descending = false, init = false;
    float sinceStep = 1.f;
    bool Update(float rel, float dt) {
        if (!init) { prevRel = minRel = rel; init = true; return false; }
        float v = (rel - prevRel) / std::max(dt, 0.001f);
        prevRel = rel;
        vel = vel * 0.4f + v * 0.6f;
        minRel = std::min(rel, minRel + 0.08f * dt);      // slowly rising floor
        sinceStep += dt;
        if (vel < -0.30f) descending = true;
        if (descending && vel > -0.06f && rel < minRel + 0.09f && sinceStep > 0.18f) {
            descending = false; sinceStep = 0.f;
            return true;
        }
        return false;
    }
};

struct State {
    int   ped = 0;
    FootTracker lf, rf;
    float noBoneStep = 0.f;        // time moving without a bone-detected step
    float cadence = 0.f;           // fallback step timer
    bool  leftNext = true;
    int   health = -1, mountHealth = -1, mountHandle = 0;
    bool  wasShooting = false, wasReloading = false, wasDead = false;
    float shotCooldown = 0.f;
    float minVz = 0.f, mountMinVz = 0.f;
    float hoofPhase = 0.f;
    int   hoofIdx = 0;
    float heart = 0.f;
    float bumpT = 0.5f;
    float flashT = 0.f;
    // DSX
    TriggerFx sentL, sentR;
    bool  fireArmed = false;
    float l1Held = 0.f;
    // water / swimming / melee
    float footWater = 0.f, mountWater = 0.f;   // submerged level 0..1 of player / horse
    bool  wasInWater = false;
    float waterPhase = 0.f, swimPhase = 0.f;
    float waterZoneT = 0.f; bool river = false;
    int   meleeTarget = 0, meleeTargetHp = -1;
    std::vector<std::pair<int, int>> closePeds;   // peds within arm's reach + their last health
    float punchScanT = 0.f, sinceShot = 10.f;
    // animals / carrying / doors / horse impacts / sliding
    float biteCooldown = 0.f, bearScanT = 0.f;
    std::vector<int> roaredBears;
    bool  wasCarrying = false;
    float doorScanT = 0.f;
    struct Door { int e; float heading, peak, x, y; };
    std::vector<Door> doors;
    float prevHorseSpeed = 0.f, crashCooldown = 0.f;
    float slideT = 0.f, slidePhase = 0.f;
    int   stepCount[2] = {0, 0};
    int   fallbackSteps = 0;
    float stepLogT = 0.f;
    int   stepLogs = 0;
    uint32_t lastWeapon = 0;
    bool  sentValid = false;
    int   rgb[3] = {-1, -1, -1};
    DWORD lastDsx = 0;
} S;

static float Rand01() { return (float)(rand() & 0x7FFF) / 32767.f; }

static void ResetDSX() {
    if (!cfg.dsx) return;
    DSX::Packet pk;
    pk.Add(DSX::TriggerThreshold, {cfg.controllerIndex, DSX::Right, 0});
    pk.Add(DSX::ResetToUserSettings, {cfg.controllerIndex});
    pk.Send();
    S.sentValid = false;
    S.rgb[0] = S.rgb[1] = S.rgb[2] = -1;
}

// Hoofbeat pattern per gait. Offsets are fractions of one stride.
static void HorseRumble(float speed, float dt) {
    if (speed < 0.6f) { S.hoofPhase = 0; S.hoofIdx = 0; return; }
    static const float walk[]   = {0.f, 0.25f, 0.5f, 0.75f};
    static const float trot[]   = {0.f, 0.5f};
    static const float canter[] = {0.f, 0.16f, 0.30f};
    static const float gallop[] = {0.f, 0.09f, 0.18f, 0.27f};
    const float* pat; int n; float stride, amp, dur;
    // A 500 kg animal: hoofbeats are dull thuds through the saddle - above footsteps, below gunfire.
    if (speed < 2.6f)      { pat = walk;   n = 4; stride = 1.15f - speed * 0.08f; amp = 0.20f; dur = 0.040f; }
    else if (speed < 5.2f) { pat = trot;   n = 2; stride = 0.80f - (speed - 2.6f) * 0.03f; amp = 0.28f; dur = 0.045f; }
    else if (speed < 8.5f) { pat = canter; n = 3; stride = 0.66f - (speed - 5.2f) * 0.02f; amp = 0.34f; dur = 0.048f; }
    else                   { pat = gallop; n = 4; stride = std::max(0.42f, 0.56f - (speed - 8.5f) * 0.02f); amp = 0.40f; dur = 0.045f; }

    float prev = S.hoofPhase;
    S.hoofPhase += dt / stride;
    for (int i = 0; i < n; i++) {
        float o = pat[i];
        bool crossed = (prev <= o && S.hoofPhase > o) || (S.hoofPhase >= 1.f && o < S.hoofPhase - 1.f);
        if (crossed) {
            bool hind = (i % 2) == 0;     // alternate heavy (hind) and light (fore) feel
            float a = amp * (hind ? 1.f : 0.8f);
            if (S.mountWater > 0.08f)   // hooves in water: duller, longer, splashy - no crisp edge
                AddPulse(a * 0.85f, a * 0.2f, dur * 1.9f, cfg.horse, 1.8f);
            else
                AddPulse(a, a * 0.45f, dur, cfg.horse, 4.f);   // hoof strike: thud with a crisp edge
        }
    }
    if (S.hoofPhase >= 1.f) S.hoofPhase -= 1.f;
}

static void FootstepPulse(bool left, float speed) {
    // Boots on the ground: a faint, short heel strike. Walking is barely there, sprinting is a clear tap.
    // Shorter, sharper and with more of the light motor so it reads as a tap, not a smear.
    // (a bit stronger than the first crisp version, to make up for the energy the shorter pulse lost)
    // stronger peak but a quicker drop-off and more of the crisp light motor = noticeable and snappy
    // Both feet hit the heavy motor equally (the light motor is much weaker on the DualSense, so a
    // right-foot-on-light-motor split made every other step vanish). Only a slight side bias.
    float a = speed < 2.2f ? 0.23f : (speed < 5.0f ? 0.285f : 0.36f);
    float d = speed < 2.2f ? 0.040f : (speed < 5.0f ? 0.044f : 0.048f);
    if (S.footWater > 0.08f)   // wading: a slow, soft sloshing step instead of a heel strike
        AddPulse(a * (left ? 0.85f : 0.8f), a * 0.15f, d * 1.8f, cfg.footsteps, 1.8f);
    else
        AddPulse(a * (left ? 1.f : 0.9f), a * (left ? 0.4f : 0.5f), d, cfg.footsteps, 3.f);
    S.stepCount[left ? 0 : 1]++;
}

// ---------------------------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------------------------
// RDR2 has no native for the current itself, so flowing water is told apart by its water zone.
static bool IsRiverZone(uint32_t zone) {
    static std::vector<uint32_t> rivers;
    if (rivers.empty()) {
        const char* names[] = {
            "WATER_KAMASSA_RIVER", "WATER_LANNAHECHEE_RIVER", "WATER_DAKOTA_RIVER", "WATER_SAN_LUIS_RIVER",
            "WATER_LOWER_MONTANA_RIVER", "WATER_UPPER_MONTANA_RIVER", "WATER_LITTLE_CREEK_RIVER",
            "WATER_ARROYO_DE_LA_VIBORA", "WATER_DEWBERRY_CREEK", "WATER_HAWKS_EYE_CREEK", "WATER_RINGNECK_CREEK",
            "WATER_BEARTOOTH_BECK", "WATER_SPIDER_GORGE", "WATER_STILLWATER_CREEK", "WATER_CALLOW_CREEK",
            "WATER_WHINYARD_STRAIT", "WATER_DEADBOOT_CREEK", "WATER_COTORRA_SPRINGS",
        };
        for (auto n : names) rivers.push_back(Joaat(n));
    }
    return zone && std::find(rivers.begin(), rivers.end(), zone) != rivers.end();
}

// Standing or moving in water: a soft swell, never snappy. Rivers push even when you stand still,
// still water only stirs when you move through it. `level` = submerged level 0..1, `heavy` for a horse.
static void WaterRumble(float level, float speed, bool river, bool heavy, float dt) {
    if (level < 0.05f) return;
    float depth = std::clamp(level / 0.6f, 0.f, 1.f);
    float move  = std::clamp(speed / 4.f, 0.f, 1.f);
    float drag  = (0.10f + 0.18f * depth) * move;                    // pushing water aside
    float flow  = river ? (0.10f + 0.16f * depth) : 0.f;             // the current tugging at your legs
    // the current comes in slow, irregular surges
    S.waterPhase += dt * (river ? 1.6f : 1.0f);
    float swell = 0.65f + 0.35f * sinf(S.waterPhase * 2.1f) * sinf(S.waterPhase * 0.77f + 1.3f);
    float a = (drag + flow * swell) * (heavy ? 1.25f : 1.f) * cfg.water;
    g_contL = std::max(g_contL, a);
    g_contR = std::max(g_contR, a * 0.25f);
}

static void SwimRumble(int ped, float dt) {
    bool under = Nat<int>(N::IS_PED_SWIMMING_UNDER_WATER, ped) != 0;
    float sp = Nat<float>(N::GET_ENTITY_SPEED, ped);
    if (under) {
        // diving: everything muffled - a faint, steady hum
        g_contL = std::max(g_contL, 0.09f * cfg.swimming);
        return;
    }
    if (sp < 0.3f) { S.swimPhase = 0.f; return; }    // treading water: calm
    // strokes: ~1.1 s apart at a normal swim, ~0.7 s when sprint-swimming. Rounded, soft swells.
    float period = std::clamp(1.3f - sp * 0.25f, 0.65f, 1.2f);
    S.swimPhase += dt;
    if (S.swimPhase >= period) {
        S.swimPhase = 0.f;
        float a = 0.20f + 0.10f * std::clamp((sp - 1.f) / 2.f, 0.f, 1.f);
        AddPulse(a, a * 0.3f, 0.22f, cfg.swimming, 1.3f);
    }
}

static void OnFoot(int ped, float speed, float dt) {
    if (S.lf.bone < 0) S.lf.bone = Nat<int>(N::GET_ENTITY_BONE_INDEX_BY_NAME, ped, "SKEL_L_Foot");
    if (S.rf.bone < 0) S.rf.bone = Nat<int>(N::GET_ENTITY_BONE_INDEX_BY_NAME, ped, "SKEL_R_Foot");
    if (speed < 0.35f) { S.noBoneStep = 0; S.cadence = 0; return; }

    bool stepped = false;
    if (S.lf.bone >= 0 && S.rf.bone >= 0) {
        Vec3 root = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true);
        Vec3 l = Nat<Vec3>(N::GET_WORLD_POS_OF_ENTITY_BONE, ped, S.lf.bone);
        Vec3 r = Nat<Vec3>(N::GET_WORLD_POS_OF_ENTITY_BONE, ped, S.rf.bone);
        if (S.lf.Update(l.z - root.z, dt)) { FootstepPulse(true, speed);  stepped = true; }
        if (S.rf.Update(r.z - root.z, dt)) { FootstepPulse(false, speed); stepped = true; }
    }
    // Fallback: if the bones give nothing while clearly moving, pace steps from speed.
    S.noBoneStep = stepped ? 0.f : S.noBoneStep + dt;
    if (S.noBoneStep > 1.2f && speed > 0.8f) {
        float interval = std::clamp(0.62f - speed * 0.045f, 0.26f, 0.62f);
        S.cadence += dt;
        if (S.cadence >= interval) { S.cadence = 0; FootstepPulse(S.leftNext, speed); S.leftNext = !S.leftNext; S.fallbackSteps++; }
    }
    // step diagnostics: every 10 s of walking, log how many steps each foot produced
    S.stepLogT += dt;
    if (S.stepLogT > 10.f && S.stepLogs < 6) {
        Log("steps (10 s): left=%d right=%d (of which paced fallback=%d), bones L=%d R=%d, speed %.1f",
            S.stepCount[0], S.stepCount[1], S.fallbackSteps, S.lf.bone, S.rf.bone, speed);
        S.stepCount[0] = S.stepCount[1] = S.fallbackSteps = 0; S.stepLogT = 0.f; S.stepLogs++;
    }
}

// Animal attacks. Dogs / wolves / coyotes bite: a sharp clamp, then shaking tugs as they worry at you.
// Bears, cougars, gators maul: a crushing blow. Bulls, bison, moose, rams: a blunt, deep ram.
static int AnimalKind(int animal) {
    static std::vector<uint32_t> maulers, rammers;
    if (maulers.empty()) {
        for (auto n : {"A_C_BEAR_01", "A_C_BEARBLACK_01", "A_C_COUGAR_01", "A_C_PANTHER_01", "A_C_ALLIGATOR_01",
                       "A_C_ALLIGATOR_02", "A_C_ALLIGATOR_03", "MP_A_C_BEAR_01", "MP_A_C_COUGAR_01", "MP_A_C_PANTHER_01"})
            maulers.push_back(Joaat(n));
        for (auto n : {"A_C_BULL_01", "A_C_BISON_01", "A_C_BISONALBINO_01", "A_C_MOOSE_01", "A_C_ELK_01",
                       "A_C_BIGHORNRAM_01", "A_C_RAM_01", "A_C_BUCK_01", "A_C_COW", "A_C_OX_01", "A_C_BOAR_01",
                       "A_C_BOARLEGENDARY_01", "MP_A_C_BISON_01", "MP_A_C_MOOSE_01", "MP_A_C_ELK_01",
                       "MP_A_C_BUCK_01", "MP_A_C_BIGHORNRAM_01", "MP_A_C_BOAR_01"})
            rammers.push_back(Joaat(n));
    }
    uint32_t m = Nat<uint32_t>(N::GET_ENTITY_MODEL, animal);
    if (std::find(maulers.begin(), maulers.end(), m) != maulers.end()) return 1;
    if (std::find(rammers.begin(), rammers.end(), m) != rammers.end()) return 2;
    return 0;
}
static void AnimalAttack(int animal, int drop, int maxHp) {
    float sev = (float)drop / maxHp;
    switch (AnimalKind(animal)) {
    case 1: {   // bear / cougar: a crushing swipe, deep and long
        float a = std::clamp(0.75f + sev * 2.f, 0.75f, 1.f);
        AddPulse(a * 0.5f, a, 0.05f, cfg.animals, 4.f);
        AddPulse(a, a * 0.4f, 0.45f, cfg.animals, 1.8f);
        break; }
    case 2: {   // bull / bison: a blunt, heavy ram - no crack, all body
        float a = std::clamp(0.7f + sev * 2.f, 0.7f, 1.f);
        AddPulse(a, a * 0.5f, 0.35f, cfg.animals, 2.2f);
        break; }
    default: {  // dog / wolf bite: the clamp, then head-shake tugs
        float a = std::clamp(0.5f + sev * 2.5f, 0.5f, 0.9f);
        AddPulse(a * 0.4f, a * 0.9f, 0.04f, cfg.animals, 5.f);
        for (int i = 1; i <= 3; i++) AddPulseDelayed(a * 0.6f, a * 0.3f, 0.07f, cfg.animals, 3.f, 0.06f + i * 0.09f);
        break; }
    }
}

// A bear turning on you and roaring: a low growl that swells and rolls off (once per bear).
static void BearRoars(int ped, float dt) {
    S.bearScanT -= dt;
    if (S.bearScanT > 0.f || !shAllPeds || cfg.animals <= 0.f) return;
    S.bearScanT = 0.25f;
    static const uint32_t bears[] = { Joaat("A_C_BEAR_01"), Joaat("A_C_BEARBLACK_01"), Joaat("MP_A_C_BEAR_01") };
    static int peds[512];
    int n = shAllPeds(peds, 512);
    Vec3 me = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true);
    for (int i = 0; i < n; i++) {
        int b = peds[i];
        if (b == ped) continue;
        uint32_t m = Nat<uint32_t>(N::GET_ENTITY_MODEL, b);
        if (m != bears[0] && m != bears[1] && m != bears[2]) continue;
        if (std::find(S.roaredBears.begin(), S.roaredBears.end(), b) != S.roaredBears.end()) continue;
        if (Nat<int>(N::IS_ENTITY_DEAD, b)) continue;
        Vec3 p = Nat<Vec3>(N::GET_ENTITY_COORDS, b, true, true);
        float d2 = (p.x - me.x) * (p.x - me.x) + (p.y - me.y) * (p.y - me.y) + (p.z - me.z) * (p.z - me.z);
        if (d2 > 30.f * 30.f || !Nat<int>(N::IS_PED_IN_COMBAT, b, ped)) continue;
        S.roaredBears.push_back(b);
        if (S.roaredBears.size() > 16) S.roaredBears.erase(S.roaredBears.begin());
        float a = std::clamp(1.f - sqrtf(d2) / 40.f, 0.4f, 0.9f);
        AddPulse(a * 0.5f, 0.f, 0.35f, cfg.animals, 0.6f);                      // the growl building
        AddPulseDelayed(a, a * 0.25f, 1.1f, cfg.animals, 1.2f, 0.3f);          // the roar rolling off
    }
}

// Picking up a body: straining to lift, then the dead weight on the shoulder. Setting it down: a dull thud.
static void Carrying(int ped) {
    bool c = Nat<int>(N::IS_PED_CARRYING_SOMETHING, ped) != 0;
    if (c == S.wasCarrying) return;
    S.wasCarrying = c;
    if (c) {
        int e = Nat<int>(N::GET_FIRST_ENTITY_PED_CARRYING, ped);
        if (!e || !Nat<int>(N::IS_ENTITY_A_PED, e)) return;          // pelts and items: nothing
        AddPulse(0.35f, 0.f, 0.45f, cfg.carry, 0.7f);
        AddPulseDelayed(0.55f, 0.15f, 0.12f, cfg.carry, 2.5f, 0.55f);
    } else {
        AddPulse(0.42f, 0.10f, 0.12f, cfg.carry, 2.5f);
    }
}

// Doors: watch objects right next to the player that rotate in place. When a fast swing stops dead,
// the door has slammed into its frame or the wall.
static void Doors(int ped, float dt) {
    if (!shAllObjects || cfg.doors <= 0.f) return;
    S.doorScanT -= dt;
    if (S.doorScanT <= 0.f) {
        S.doorScanT = 0.25f;
        Vec3 me = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true);
        static int objs[2048];
        int n = shAllObjects(objs, 2048);
        std::vector<State::Door> keep;
        for (int i = 0; i < n && keep.size() < 24; i++) {
            Vec3 p = Nat<Vec3>(N::GET_ENTITY_COORDS, objs[i], true, true);
            float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z;
            if (dx * dx + dy * dy > 3.5f * 3.5f || fabsf(dz) > 2.5f) continue;
            // guns, satchel, lantern, hat etc. are attached to the player and swing with every stride - not doors
            if (Nat<int>(N::IS_ENTITY_ATTACHED, objs[i])) continue;
            auto it = std::find_if(S.doors.begin(), S.doors.end(), [&](const State::Door& d) { return d.e == objs[i]; });
            if (it != S.doors.end()) {
                // a door turns on its hinge and stays put; anything that moved is a loose prop, drop it
                if (fabsf(p.x - it->x) > 0.05f || fabsf(p.y - it->y) > 0.05f) continue;
                keep.push_back(*it);
            } else keep.push_back(State::Door{objs[i], Nat<float>(N::GET_ENTITY_HEADING, objs[i]), 0.f, p.x, p.y});
        }
        S.doors.swap(keep);
    }
    for (auto& d : S.doors) {
        float h = Nat<float>(N::GET_ENTITY_HEADING, d.e);
        float dh = h - d.heading;
        if (dh > 180.f) dh -= 360.f; else if (dh < -180.f) dh += 360.f;
        d.heading = h;
        float w = fabsf(dh) / std::max(dt, 0.001f);       // degrees per second
        if (w > d.peak) d.peak = w;
        if (w < 15.f) {
            if (d.peak > 110.f) {                           // a fast swing came to a sudden stop
                float a = std::clamp(0.35f + (d.peak - 110.f) / 400.f, 0.35f, 0.8f);
                AddPulse(a * 0.4f, a * 0.8f, 0.03f, cfg.doors, 5.f);   // wood on wood
                AddPulse(a, a * 0.2f, 0.10f, cfg.doors, 2.8f);         // the frame shuddering
            }
            d.peak = 0.f;
        }
    }
}

// Horse dropping down a ledge or landing a jump: from about knee height up, heavier the further it drops.
static void HorseDrop(float& minVz, float vz) {
    if (vz < minVz) minVz = vz;
    if (vz > -0.6f) {
        if (minVz < -2.5f) {
            float a = std::clamp(0.40f + (-minVz - 2.5f) / 6.f, 0.40f, 1.0f);
            AddPulse(a * 0.3f, a * 0.6f, 0.04f, cfg.horse * cfg.landing, 4.f);
            AddPulse(a, a * 0.35f, 0.12f + a * 0.2f, cfg.horse * cfg.landing, 2.2f);
        }
        minVz = 0.f;
    }
}

// Sliding down a slope: feet on the ground but dropping fast. A rough, gravelly scrape that grows with speed.
static void Sliding(int ped, const Vec3& v, bool jumping, float dt) {
    float hag = Nat<float>(N::GET_ENTITY_HEIGHT_ABOVE_GROUND, ped);
    float hs = sqrtf(v.x * v.x + v.y * v.y);
    // only really steep descents count (running down a normal hill is not a slide)
    bool sliding = !jumping && hag < 0.7f && v.z < -2.0f && hs > 1.0f && -v.z > hs * 0.75f;
    S.slideT = sliding ? S.slideT + dt : 0.f;
    if (S.slideT < 0.3f) { S.slidePhase = 0.f; return; }
    float a = std::clamp(0.12f + (-v.z - 2.0f) / 10.f + hs / 40.f, 0.12f, 0.38f);
    S.slidePhase += dt;
    float grit = 0.75f + 0.25f * sinf(S.slidePhase * 37.f) * sinf(S.slidePhase * 13.f);
    g_contL = std::max(g_contL, a * grit * cfg.sliding);
    g_contR = std::max(g_contR, a * 0.6f * grit * cfg.sliding);
}

static bool AttackerClose(int ped, int other) {
    Vec3 a = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true), b = Nat<Vec3>(N::GET_ENTITY_COORDS, other, true, true);
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz < 3.0f * 3.0f;
}

static void Landing(float& minVz, float vz, float scale) {
    if (vz < minVz) minVz = vz;
    if (vz > -0.6f) {
        if (minVz < -4.0f) {
            // a hop off a porch is a bump, a fall from a roof is a slam
            float a = std::clamp(0.30f + (-minVz - 4.0f) / 8.0f, 0.30f, 1.0f);
            AddPulse(a, a * 0.35f, 0.07f + a * 0.15f, scale, 2.5f);
        }
        minVz = 0.f;
    }
}

static void HealthColor(float ratio, int out[3]) {
    ratio = std::clamp(ratio, 0.f, 1.f);
    if (ratio > 0.5f) { float t = (ratio - 0.5f) * 2.f; out[0] = (int)(255 * (1 - t)); out[1] = 200; out[2] = (int)(40 * t); }
    else              { float t = ratio * 2.f;          out[0] = 255; out[1] = (int)(200 * t); out[2] = 0; }
}

// ---------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------
static void Tick(float dt) {
    int player = Nat<int>(N::PLAYER_ID);
    int ped    = Nat<int>(N::PLAYER_PED_ID);
    if (!ped || !Nat<int>(N::DOES_ENTITY_EXIST, ped)) { g_contL = g_contR = 0; OutputRumble(dt, false); return; }

    if (ped != S.ped) {   // new player ped (load, model change)
        S.ped = ped; S.lf = FootTracker{}; S.rf = FootTracker{}; S.health = -1;
    }

    // HUD elements are UI apps too, so a UI app only counts as a menu when it also takes player control
    // (map, journal, catalogs, shops). The pause menu always counts.
    bool pause    = Nat<int>(N::IS_PAUSE_MENU_ACTIVE) != 0;
    bool uiapp    = Nat<int>(N::IS_ANY_UIAPP_ACTIVE) != 0;
    bool ctrlOn   = Nat<int>(N::IS_PLAYER_CONTROL_ON, player) != 0;
    bool menu     = pause || (uiapp && !ctrlOn);
    g_scriptMenu  = menu;
    bool control  = ctrlOn && !menu;
    bool keyboard = cfg.onlyWithController && Nat<int>(N::IS_USING_KEYBOARD, 0) != 0;
    {
        static int lastState = -1;
        int st = (pause ? 1 : 0) | (uiapp ? 2 : 0) | (ctrlOn ? 4 : 0) | (keyboard ? 8 : 0);
        if (st != lastState) {
            Log("state: pause=%d uiapp=%d control=%d keyboard=%d -> menu=%d", (int)pause, (int)uiapp, (int)ctrlOn, (int)keyboard, (int)menu);
            lastState = st;
        }
    }
    bool dead     = Nat<int>(N::IS_ENTITY_DEAD, ped) != 0;
    bool allowed  = control && !keyboard && g_focused;

    g_contL = g_contR = 0;
    S.shotCooldown -= dt;
    S.biteCooldown -= dt; S.crashCooldown -= dt;
    S.flashT -= dt;

    // --- health / damage / death -------------------------------------------------------------
    bool inMelee = Nat<int>(N::IS_PED_IN_MELEE_COMBAT, ped) != 0;
    int hp = Nat<int>(N::GET_ENTITY_HEALTH, ped);
    int maxHp = std::max(1, Nat<int>(N::GET_ENTITY_MAX_HEALTH, ped, false));
    if (S.health >= 0 && hp < S.health && !S.wasDead) {
        int drop = S.health - hp;
        int attacker = Nat<int>(N::GET_LAST_ENTITY_TO_DAMAGE, ped);
        bool animal = attacker && attacker != ped && Nat<int>(N::IS_ENTITY_A_PED, attacker)
                      && !Nat<int>(N::IS_PED_HUMAN, attacker);
        if (animal) {
            if (S.biteCooldown <= 0.f) { AnimalAttack(attacker, drop, maxHp); S.biteCooldown = 0.25f; S.flashT = 0.35f; }
        } else if ((inMelee || (attacker && Nat<int>(N::IS_ENTITY_A_PED, attacker) && !Nat<int>(N::IS_PED_SHOOTING, attacker)
                    && AttackerClose(ped, attacker))) && drop >= 1) {
            // taking a punch: a hard, short smack and a brief jolt - snappier and shorter than a wound
            float a = std::clamp(0.50f + (float)drop / maxHp * 3.f, 0.50f, 0.95f);
            AddPulse(a * 0.45f, a * 0.85f, 0.035f, cfg.melee, 4.5f);          // smack
            AddPulse(a * 0.75f, a * 0.2f, 0.09f, cfg.melee, 2.8f);            // jolt
            S.flashT = 0.25f;
        } else if (drop >= 3 || drop * 50 >= maxHp) {
            // a wound: a short impact, then a deeper body hit that lasts longer the worse it is
            float a = std::clamp(0.45f + (float)drop / maxHp * 2.5f, 0.45f, 1.0f);
            AddPulse(a * 0.3f, a * 0.8f, 0.04f, cfg.damage, 4.f);              // impact
            AddPulse(a, a * 0.3f, 0.10f + a * 0.18f, cfg.damage, 2.2f);       // body

            S.flashT = 0.35f;
        }
    }
    S.health = hp;
    if (dead && !S.wasDead) { AddPulse(1.0f, 0.4f, 0.7f, cfg.damage, 1.3f); allowed = true; }
    S.wasDead = dead;

    float ratio = (float)hp / maxHp;
    if (!dead && allowed && ratio < cfg.lowHealth && cfg.heartbeat > 0) {
        S.heart += dt;
        float period = 0.75f + ratio * 2.0f;
        // lub-dub: a rounded thump followed by a softer one
        if (S.heart >= period) { S.heart = 0; AddPulse(0.36f, 0.0f, 0.08f, cfg.heartbeat, 1.6f); }
        else if (S.heart >= 0.17f && S.heart - dt < 0.17f) AddPulse(0.26f, 0.0f, 0.07f, cfg.heartbeat, 1.6f);
    } else S.heart = 0;

    // --- context --------------------------------------------------------------------------------
    bool onMount   = Nat<int>(N::IS_PED_ON_MOUNT, ped) != 0;
    bool inVehicle = !onMount && Nat<int>(N::IS_PED_IN_ANY_VEHICLE, ped, false) != 0;
    bool ragdoll   = Nat<int>(N::IS_PED_RAGDOLL, ped) != 0;
    bool swimming  = Nat<int>(N::IS_PED_SWIMMING, ped) != 0;
    bool falling   = Nat<int>(N::IS_PED_FALLING, ped) != 0;
    bool jumping   = Nat<int>(N::IS_PED_JUMPING, ped) != 0;

    uint32_t weapon = 0;
    Nat<int>(N::GET_CURRENT_PED_WEAPON, ped, &weapon, false, 0, false);
    WClass wc = Classify(weapon);

    // --- weapon wheel + weapon switching -----------------------------------------------------------
    // The wheel opens while L1 is held. Treat a hold longer than a tap as "wheel open".
    bool l1 = (g_padButtons.load() & B_L1) != 0;
    S.l1Held = l1 ? S.l1Held + dt : 0.f;
    bool wheel = control && S.l1Held > 0.15f;
    if (S.lastWeapon != 0 && weapon != S.lastWeapon && !dead) {
        // a crisp detent click; holstering (back to unarmed) is softer than drawing a gun
        if (wc == W_NONE) AddPulse(0.04f, 0.20f, 0.022f, cfg.weaponSwitch, 5.f);
        else              AddPulse(0.08f, 0.30f, 0.026f, cfg.weaponSwitch, 5.f);
    }
    S.lastWeapon = weapon;

    // --- weapons -------------------------------------------------------------------------------
    bool shooting = Nat<int>(N::IS_PED_SHOOTING, ped) != 0;
    if (shooting && !S.wasShooting && S.shotCooldown <= 0.f && !dead) { ShotRumble(wc); S.shotCooldown = 0.06f; }
    S.wasShooting = shooting;
    bool reloading = Nat<int>(N::IS_PED_RELOADING, ped) != 0;
    if (reloading && !S.wasReloading) AddPulse(0.10f, 0.34f, 0.032f, cfg.reload, 4.5f);   // mechanical click
    S.wasReloading = reloading;

    // --- fire at the trigger break --------------------------------------------------------------
    // DSX can't hold R2 back per weapon, so the game's fire action is blocked until R2 passes the point
    // where the adaptive-trigger wall breaks. The shot then goes off as you snap over it.
    if (cfg.fireAtBreak && cfg.triggers && cfg.dsx && control && !dead && IsGun(wc)) {
        TriggerFx l, r;
        WeaponTriggers(wc, l, r);
        if (r.mode == DSX::SemiAutomaticGun) {
            static const uint32_t inAttack = Joaat("INPUT_ATTACK"), inHorseAttack = Joaat("INPUT_HORSE_ATTACK"),
                                  inVehAttack = Joaat("INPUT_VEH_PASSENGER_ATTACK");
            float brk = std::clamp(r.p[1] / 9.f + cfg.breakAdjust, 0.05f, 0.98f);
            float v = std::max(Nat<float>(N::GET_DISABLED_CONTROL_NORMAL, 0, inAttack),
                               Nat<float>(N::GET_DISABLED_CONTROL_NORMAL, 0, inHorseAttack));
            static bool loggedAnalog = false;
            if (!loggedAnalog && v > 0.1f && v < 0.9f) { loggedAnalog = true; Log("fire gate: R2 is analog (%.2f), break at %.2f", v, brk); }
            if (!S.fireArmed && v >= brk) S.fireArmed = true;
            else if (S.fireArmed && v < brk - 0.10f) S.fireArmed = false;
            if (!S.fireArmed) {
                NatV(N::DISABLE_CONTROL_ACTION, 0, inAttack, false);
                NatV(N::DISABLE_CONTROL_ACTION, 0, inHorseAttack, false);
                NatV(N::DISABLE_CONTROL_ACTION, 0, inVehAttack, false);
            }
        } else S.fireArmed = false;
    } else S.fireArmed = false;

    // --- landing a punch ------------------------------------------------------------------------
    // The game's melee target is often empty, so watch everyone within arm's reach instead: when one of
    // them loses health, the player did it, and no shot was just fired, our blow landed.
    S.sinceShot = shooting ? 0.f : S.sinceShot + dt;
    S.punchScanT -= dt;
    if (!dead && !onMount && !inVehicle && shAllPeds && cfg.melee > 0.f) {
        if (S.punchScanT <= 0.f) {      // refresh who is close enough 5x a second
            S.punchScanT = 0.2f;
            static int peds[512];
            int n = shAllPeds(peds, 512);
            Vec3 me = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true);
            std::vector<std::pair<int, int>> keep;
            for (int i = 0; i < n && keep.size() < 12; i++) {
                int t = peds[i];
                if (t == ped || Nat<int>(N::IS_ENTITY_DEAD, t)) continue;
                Vec3 p = Nat<Vec3>(N::GET_ENTITY_COORDS, t, true, true);
                float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z;
                if (dx * dx + dy * dy + dz * dz > 3.0f * 3.0f) continue;
                auto it = std::find_if(S.closePeds.begin(), S.closePeds.end(), [&](auto& c) { return c.first == t; });
                keep.push_back({t, it != S.closePeds.end() ? it->second : Nat<int>(N::GET_ENTITY_HEALTH, t)});
            }
            S.closePeds.swap(keep);
        }
        for (auto& c : S.closePeds) {
            int thp = Nat<int>(N::GET_ENTITY_HEALTH, c.first);
            if (thp < c.second && S.sinceShot > 0.4f && Nat<int>(N::GET_LAST_ENTITY_TO_DAMAGE, c.first) == ped) {
                int tmax = std::max(1, Nat<int>(N::GET_ENTITY_MAX_HEALTH, c.first, false));
                bool knockout = thp <= 0 || Nat<int>(N::IS_ENTITY_DEAD, c.first) != 0;
                float a = knockout ? 0.9f : std::clamp(0.45f + (float)(c.second - thp) / tmax * 2.f, 0.45f, 0.8f);
                // knuckles on a jaw: a firm thud through the heavy motor with a little crack on top
                AddPulse(a * 0.35f, a * 0.7f, 0.03f, cfg.melee, 5.f);
                AddPulse(a, a * 0.4f, knockout ? 0.12f : 0.07f, cfg.melee, knockout ? 2.5f : 3.2f);
            }
            c.second = thp;
        }
    } else S.closePeds.clear();

    // --- water ---------------------------------------------------------------------------------
    bool inWater = Nat<int>(N::IS_ENTITY_IN_WATER, ped) != 0;
    S.footWater = inWater && !onMount ? Nat<float>(N::GET_ENTITY_SUBMERGED_LEVEL, ped) : 0.f;
    S.waterZoneT -= dt;
    if (inWater && S.waterZoneT <= 0.f) {    // look up the water zone twice a second
        S.waterZoneT = 0.5f;
        Vec3 p = Nat<Vec3>(N::GET_ENTITY_COORDS, ped, true, true);
        uint32_t zone = Nat<uint32_t>(N::GET_WATER_MAP_ZONE_AT_COORDS, p.x, p.y, p.z);
        bool river = IsRiverZone(zone);
        if (cfg.debug && river != S.river) Log("water zone 0x%08X -> %s", zone, river ? "flowing" : "still");
        S.river = river;
    }
    if (inWater && !S.wasInWater && !onMount && !inVehicle) {
        // jumping in: a splash that depends on how fast you hit the water
        Vec3 v = Nat<Vec3>(N::GET_ENTITY_VELOCITY, ped, 0);
        float a = std::clamp(0.25f + (-v.z - 1.f) / 8.f, 0.25f, 0.85f);
        if (-v.z > 1.5f || swimming) AddPulse(a, a * 0.4f, 0.12f + a * 0.15f, cfg.water, 2.f);
    }
    S.wasInWater = inWater;
    if (swimming && !dead) SwimRumble(ped, dt);
    if (!dead) { Carrying(ped); BearRoars(ped, dt); if (!onMount && !inVehicle) Doors(ped, dt); }

    // --- movement ------------------------------------------------------------------------------
    float horseSpeed = 0.f;
    if (onMount) {
        int mount = Nat<int>(N::GET_MOUNT, ped);
        if (mount && Nat<int>(N::DOES_ENTITY_EXIST, mount)) {
            horseSpeed = Nat<float>(N::GET_ENTITY_SPEED, mount);
            S.mountWater = Nat<int>(N::IS_ENTITY_IN_WATER, mount) ? Nat<float>(N::GET_ENTITY_SUBMERGED_LEVEL, mount) : 0.f;
            if (S.mountWater > 0.05f && S.waterZoneT <= 0.f) {
                S.waterZoneT = 0.5f;
                Vec3 p = Nat<Vec3>(N::GET_ENTITY_COORDS, mount, true, true);
                S.river = IsRiverZone(Nat<uint32_t>(N::GET_WATER_MAP_ZONE_AT_COORDS, p.x, p.y, p.z));
            }
            S.waterZoneT -= dt;
            WaterRumble(S.mountWater, horseSpeed, S.river, true, dt);
            HorseRumble(horseSpeed, dt);
            Vec3 v = Nat<Vec3>(N::GET_ENTITY_VELOCITY, mount, 0);
            HorseDrop(S.mountMinVz, v.z);
            // running into a tree / rock / wall: the speed collapses in a fraction of a second
            float decel = (S.prevHorseSpeed - horseSpeed) / std::max(dt, 0.001f);
            if (S.prevHorseSpeed > 4.f && decel > 22.f && S.crashCooldown <= 0.f
                && Nat<int>(N::HAS_ENTITY_COLLIDED_WITH_ANY, mount)) {
                float a = std::clamp(0.55f + (S.prevHorseSpeed - 4.f) / 10.f, 0.55f, 1.f);
                AddPulse(a * 0.4f, a * 0.9f, 0.04f, cfg.horse * cfg.landing, 4.5f);    // crack of the impact
                AddPulse(a, a * 0.35f, 0.25f + a * 0.2f, cfg.horse * cfg.landing, 2.f); // the horse stopping dead
                S.crashCooldown = 1.0f;
            }
            S.prevHorseSpeed = horseSpeed;
            int mhp = Nat<int>(N::GET_ENTITY_HEALTH, mount);
            if (mount == S.mountHandle && S.mountHealth >= 0 && S.mountHealth - mhp >= 5)
                AddPulse(0.65f, 0.25f, 0.20f, cfg.horse * cfg.damage, 2.2f);
            S.mountHandle = mount; S.mountHealth = mhp;
            if (Nat<int>(N::IS_CONTROL_JUST_PRESSED, 0, INPUT_HORSE_SPRINT)) AddPulse(0.20f, 0.14f, 0.04f, cfg.horse, 3.5f);   // spur kick
        }
        S.noBoneStep = 0;
    } else {
        S.mountHandle = 0; S.mountHealth = -1; S.hoofPhase = 0; S.prevHorseSpeed = 0.f;
        if (inVehicle) {
            int veh = Nat<int>(N::GET_VEHICLE_PED_IS_IN, ped, false);
            float sp = veh ? Nat<float>(N::GET_ENTITY_SPEED, veh) : 0.f;
            if (sp > 0.5f) {
                // Wooden wheels on dirt. t = 0 at a crawl, 1 at full gallop (~14 m/s). The ramp is steep:
                // rolling along slowly is barely felt, a wagon at full speed shakes hard.
                float t = std::clamp((sp - 0.5f) / 13.5f, 0.f, 1.f);
                float base = 0.42f * powf(t, 1.7f) * cfg.vehicle;
                g_contL = base * (0.75f + 0.5f * Rand01());
                g_contR = base * (0.25f + 0.25f * Rand01());
                // potholes and rocks: rare and soft when slow, frequent and hard when fast
                S.bumpT -= dt;
                if (S.bumpT <= 0.f) {
                    S.bumpT = 0.2f + Rand01() * (1.8f - 1.4f * t);
                    float a = 0.12f + 0.40f * t;
                    AddPulse(a, a * 0.35f, 0.05f + 0.03f * t, cfg.vehicle * (0.3f + 0.7f * t), 3.f);
                }
            }
        } else if (!swimming && !ragdoll && !dead) {
            Vec3 v = Nat<Vec3>(N::GET_ENTITY_VELOCITY, ped, 0);
            Landing(S.minVz, v.z, cfg.landing);
            float speed = Nat<float>(N::GET_ENTITY_SPEED, ped);
            Sliding(ped, v, jumping, dt);
            WaterRumble(S.footWater, speed, S.river, false, dt);
            if (!falling && !jumping) OnFoot(ped, speed, dt);
        }
    }
    OutputRumble(dt, allowed);

    // --- DSX: triggers + lightbar --------------------------------------------------------------
    if (!cfg.dsx) return;
    DWORD now = GetTickCount();
    bool keepAlive = now - S.lastDsx > 3000;      // resend in case DSX was restarted
    DSX::Packet pk;

    if (cfg.triggers) {
        TriggerFx lt = TFx(DSX::Normal), rt = TFx(DSX::Normal);
        if (g_triggersDirty.exchange(false)) S.sentValid = false;
        if (!dead && control && !wheel && g_focused) {      // loose in the weapon wheel and outside the game
            if (IsGun(wc) || wc == W_BOW || wc == W_IMPROVEDBOW || wc == W_LASSO || wc == W_THROWN) {
                WeaponTriggers(wc, lt, rt);
                if (reloading) rt = TFx(DSX::Normal);    // nothing to pull against while reloading
            }
            else if (onMount && cfg.gallopTriggers && horseSpeed > 5.2f) {
                // galloping hooves under the fingertips; frequency follows the horse
                int freq = std::clamp((int)(horseSpeed * 0.45f), 2, 9);
                lt = rt = TFx(DSX::Galloping, {0, 9, 2, 5, freq});
            }
        }
        if (!S.sentValid || !(lt == S.sentL) || keepAlive) AddTrigger(pk, DSX::Left, lt);
        if (!S.sentValid || !(rt == S.sentR) || keepAlive) AddTrigger(pk, DSX::Right, rt);
        S.sentL = lt; S.sentR = rt; S.sentValid = true;
    }

    if (cfg.lightbar) {
        int c[3];
        if (dead) { c[0] = 90; c[1] = 0; c[2] = 0; }
        else if (S.flashT > 0.f) { c[0] = 255; c[1] = 0; c[2] = 0; }
        else {
            HealthColor(ratio, c);
            if (ratio < cfg.lowHealth) {          // pulse while low
                float k = 0.45f + 0.55f * (0.5f + 0.5f * sinf(now * 0.008f));
                for (int& x : c) x = (int)(x * k);
            }
        }
        bool diff = std::abs(c[0] - S.rgb[0]) + std::abs(c[1] - S.rgb[1]) + std::abs(c[2] - S.rgb[2]) > 12;
        static DWORD lastRgb = 0;
        if ((diff && now - lastRgb > 50) || keepAlive) {
            pk.Add(DSX::RGBUpdate, {cfg.controllerIndex, c[0], c[1], c[2]});
            S.rgb[0] = c[0]; S.rgb[1] = c[1]; S.rgb[2] = c[2];
            lastRgb = now;
        }
    }
    if (!pk.Empty()) { pk.Send(); S.lastDsx = now; }
}

static void ScriptMain() {
    srand(GetTickCount());
    LoadConfig();
    Log("RDR2 DSX Rumble " MOD_VERSION " - script started (backend=%s, dsx=%d)", cfg.backend ? "Direct" : "Game", (int)cfg.dsx);
    InitWeapons();
    DSX::Open();
    FILETIME iniT = IniTime();
    DWORD lastCheck = GetTickCount();
    LARGE_INTEGER f, prev, cur;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&prev);
    bool wasEnabled = cfg.enabled;

    while (true) {
        QueryPerformanceCounter(&cur);
        float dt = std::clamp((float)(cur.QuadPart - prev.QuadPart) / (float)f.QuadPart, 0.0f, 0.1f);
        prev = cur;

        if (GetTickCount() - lastCheck > 2000) {
            lastCheck = GetTickCount();
            FILETIME t = IniTime();
            if (CompareFileTime(&t, &iniT) != 0) {
                iniT = t;
                int oldPort = cfg.dsxPort;
                LoadConfig();
                if (cfg.dsxPort != oldPort) { DSX::Close(); DSX::Open(); }
                S.sentValid = false; S.rgb[0] = -1;
                Log("config reloaded");
            }
        }

        g_lastTick = GetTickCount64();
        if (cfg.enabled) Tick(dt);
        else if (wasEnabled) { StopRumble(); ResetDSX(); }
        wasEnabled = cfg.enabled;
        shWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = mod;
        GetModuleFileNameA(mod, g_dir, MAX_PATH);
        char* slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
        snprintf(g_iniPath, MAX_PATH, "%sRDR2DSXRumble.ini", g_dir);
        snprintf(g_logPath, MAX_PATH, "%sRDR2DSXRumble.log", g_dir);
        DeleteFileA(g_logPath);
        LoadConfig();
        Log("RDR2 DSX Rumble loaded");
        if (HANDLE t = CreateThread(nullptr, 0, PadThread, nullptr, 0, nullptr)) CloseHandle(t);
        if (!BindScriptHook()) { Log("ScriptHookRDR2.dll not found or incompatible - in-game features inactive"); return TRUE; }
        shRegister(mod, ScriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_quit = true;
        if (shUnregister) shUnregister(mod);
        if (DSX::sock != INVALID_SOCKET) { ResetDSX(); DSX::Close(); }
    }
    return TRUE;
}
