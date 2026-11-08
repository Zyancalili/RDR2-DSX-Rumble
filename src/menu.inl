// In-game settings menu (F11) and the startup update notice.
// Included into RDR2DSXRumble.cpp right before ScriptMain, so it can use everything defined above.
//
// Text is drawn the way RDR2 menu bases do it: _BG_SET_TEXT_COLOR + _BG_DISPLAY_TEXT with a VAR_STRING
// "LITERAL_STRING" that carries scaleform-style markup for font, size and alignment.

namespace UI {
    constexpr uint64_t DRAW_RECT                   = 0x405224591DF02025;
    constexpr uint64_t BG_SET_TEXT_COLOR           = 0x16FA5CE47F184F1E;
    constexpr uint64_t BG_DISPLAY_TEXT             = 0x16794E044C9EFB58;
    constexpr uint64_t VAR_STRING                  = 0xFA925AC00EB830B9;
    constexpr uint64_t SET_GAME_PAUSED             = 0xFAEC088D28B1DE4A;
    constexpr uint64_t DISABLE_ALL_CONTROL_ACTIONS = 0x5F4B6931816E599B;
    constexpr uint64_t SET_MOUSE_CURSOR_THIS_FRAME = 0xF12E4CCAF249DC10;

    static float screenW = 1920.f, screenH = 1080.f, aspect = 16.f / 9.f;

    static void UpdateScreen() {
        screenW = (float)GetSystemMetrics(SM_CXSCREEN);
        screenH = (float)GetSystemMetrics(SM_CYSCREEN);
        if (screenW < 100.f || screenH < 100.f) { screenW = 1920.f; screenH = 1080.f; }
        aspect = screenW / screenH;
    }

    // x, y, w, h in 0..1 screen units, (x, y) = top-left
    static void Rect(float x, float y, float w, float h, int r, int g, int b, int a) {
        NatV(DRAW_RECT, x + w * 0.5f, y + h * 0.5f, w, h, r, g, b, a, 1, 1);
    }

    enum Align { Left, Center, Right };
    // (x, y) = anchor in 0..1, y = top of the text. size = font size in 1080p pixels.
    static void Text(const char* text, float x, float y, int size, const char* font, Align al, int r, int g, int b, int a = 255) {
        static char buf[512];
        int px = (int)(size * screenH / 1080.f + 0.5f);
        int rightMargin = al == Right ? (int)(screenW - x * screenW) : 0;
        const char* aln = al == Left ? "Left" : al == Right ? "Right" : "Center";
        snprintf(buf, sizeof buf,
                 "<TEXTFORMAT RIGHTMARGIN='%d'><P ALIGN='%s'><FONT FACE='$%s' LETTERSPACING='0' SIZE='%d'>~s~%s</FONT></P><TEXTFORMAT>",
                 rightMargin, aln, font, px, text);
        float tx = al == Right ? 0.f : al == Center ? -1.f + x * 2.f : x;
        NatV(BG_SET_TEXT_COLOR, r, g, b, a);
        const char* s = Nat<const char*>(VAR_STRING, 10, "LITERAL_STRING", (const char*)buf);
        NatV(BG_DISPLAY_TEXT, s, tx, y);
    }

    // a filled circle from horizontal strips; (cx, cy) centre, r = radius in screen-height units
    static void Circle(float cx, float cy, float r, int cr, int cg, int cb, int a, int strips = 22) {
        float h = 2.f * r / strips;
        for (int i = 0; i < strips; i++) {
            float yy = -r + (i + 0.5f) * h;
            float half = sqrtf(std::max(0.f, r * r - yy * yy)) / aspect;
            Rect(cx - half, cy + yy - h * 0.5f, half * 2.f, h * 1.02f, cr, cg, cb, a);
        }
    }

    // The dusk backdrop used by the Nexus art: red-to-amber sky, a low sun with rumble rings, mesa silhouette.
    static void Backdrop(float x, float y, float w, float h, float sunX, float sunR, bool rings) {
        struct Stop { float t; int r, g, b; };
        static const Stop stops[] = { {0.f, 30, 6, 6}, {0.50f, 120, 22, 12}, {0.82f, 215, 95, 25}, {1.f, 120, 45, 15} };
        const int bands = 28;
        for (int i = 0; i < bands; i++) {
            float t = (i + 0.5f) / bands;
            int k = 0; while (k < 2 && t > stops[k + 1].t) k++;
            float u = (t - stops[k].t) / (stops[k + 1].t - stops[k].t);
            int r = (int)(stops[k].r + (stops[k + 1].r - stops[k].r) * u);
            int g = (int)(stops[k].g + (stops[k + 1].g - stops[k].g) * u);
            int b = (int)(stops[k].b + (stops[k + 1].b - stops[k].b) * u);
            Rect(x, y + h * i / bands, w, h / bands * 1.05f, r, g, b, 245);
        }
        float horizon = y + h * 0.90f;
        float cx = x + w * sunX, cy = horizon;
        for (int i = 3; i >= 1; i--) Circle(cx, cy, sunR * (1.f + i * 0.45f), 255, 190, 90, 22);   // glow
        Circle(cx, cy, sunR, 255, 200, 120, 255);
        if (rings) {   // rumble rings: dotted arcs above the sun
            for (int ring = 1; ring <= 3; ring++) {
                float rr = sunR * (1.f + ring * 0.55f);
                int dots = 18 + ring * 8;
                for (int d = 0; d <= dots; d++) {
                    float ang = 3.14159f * (0.15f + 0.70f * d / dots);
                    float dx = cosf(ang) * rr / aspect, dy = -sinf(ang) * rr;
                    float s = 0.0028f;
                    if (cx + dx < x || cx + dx > x + w || cy + dy < y) continue;
                    Rect(cx + dx - s / aspect * 0.5f, cy + dy - s * 0.5f, s / aspect, s, 255, 220, 160, 150 - ring * 30);
                }
            }
        }
        // mesa silhouette: (x fraction, height above horizon in panel heights; negative = below)
        static const float pts[][2] = { {0.f, -0.02f}, {0.08f, -0.015f}, {0.14f, 0.025f}, {0.22f, 0.027f}, {0.25f, 0.f},
            {0.34f, -0.005f}, {0.37f, 0.012f}, {0.41f, 0.006f}, {0.60f, 0.f}, {0.64f, 0.04f}, {0.74f, 0.043f},
            {0.77f, 0.f}, {0.84f, -0.008f}, {0.92f, 0.008f}, {1.f, 0.004f} };
        const int cols = 72;
        for (int c = 0; c < cols; c++) {
            float fx = (c + 0.5f) / cols;
            int k = 0; while (k < 13 && fx > pts[k + 1][0]) k++;
            float u = (fx - pts[k][0]) / (pts[k + 1][0] - pts[k][0]);
            float hgt = pts[k][1] + (pts[k + 1][1] - pts[k][1]) * u;
            float top = horizon - hgt * h;
            Rect(x + w * c / cols, top, w / cols * 1.04f, y + h - top, 18, 7, 5, 255);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Settings menu
// ---------------------------------------------------------------------------------------------
struct MenuItem {
    const char* label;
    const char* sec; const char* key;
    int kind;                  // 0 = percent slider, 1 = toggle, 2 = curve (x10), 3 = action
    int def, min, max, step;
    int val;
};

static MenuItem g_items[] = {
    {"Rumble strength",        "Rumble",  "Intensity",          0, 100, 0, 200, 10},
    {"Response curve",         "Rumble",  "ResponseCurve",      2, 15,  5, 40,  1},
    {"Footsteps",              "Rumble",  "Footsteps",          0, 100, 0, 200, 10},
    {"Horse",                  "Rumble",  "Horse",              0, 100, 0, 200, 10},
    {"Guns",                   "Rumble",  "Weapons",            0, 100, 0, 200, 10},
    {"Fistfights",             "Rumble",  "Melee",              0, 100, 0, 200, 10},
    {"Rumble in cutscenes",    "General", "CutsceneRumble",     1, 1},
    {"Adaptive triggers",      "DSX",     "AdaptiveTriggers",   1, 1},
    {"Lightbar",               "DSX",     "Lightbar",           1, 1},
    {"Menu clicks",            "Menu",    "ButtonRumble",       1, 1},
    {"Mute when alt-tabbed",   "General", "MuteWhenUnfocused",  1, 1},
    {"Check for updates",      "General", "CheckForUpdates",    1, 1},
    {"Pause game in this menu","General", "MenuPausesGame",     1, 1},
    {"Test rumble",            nullptr,   nullptr,              3},
    {"Close",                  nullptr,   nullptr,              3},
};
constexpr int kItems = sizeof g_items / sizeof g_items[0];

static struct {
    bool  open = false, paused = false;
    int   sel = 0, hover = -1;
    float anim = 0.f;
    uint32_t prevPad = 0;
    bool  keys[256] = {};
    bool  prevLmb = false;
    DWORD lastNativeClick = 0;
} M;

static bool KeyEdge(int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !M.keys[vk];
    M.keys[vk] = down;
    return edge;
}

static void MenuLoadValues() {
    for (auto& it : g_items) {
        if (!it.sec) continue;
        if (it.kind == 2) it.val = (int)(IniF(it.sec, it.key, it.def / 10.f) * 10.f + 0.5f);
        else              it.val = it.kind == 1 ? (IniI(it.sec, it.key, it.def) != 0) : (int)(IniF(it.sec, it.key, (float)it.def) + 0.5f);
    }
}

static void MenuSave(MenuItem& it) {
    char v[32];
    if (it.kind == 2) snprintf(v, sizeof v, "%.1f", it.val / 10.f);
    else              snprintf(v, sizeof v, "%d", it.val);
    WritePrivateProfileStringA(it.sec, it.key, v, g_iniPath);
    LoadConfig();
}

static void MenuClose() {
    if (!M.open) return;
    M.open = false;
    if (M.paused) { NatV(UI::SET_GAME_PAUSED, 0); M.paused = false; }
    S.sentValid = false;              // re-send the weapon's trigger effect
    g_scriptMenu = false; g_settingsMenu = false;
    Log("menu closed");
}

static void MenuOpen() {
    M.open = true; M.sel = 0; M.anim = 0.f;
    MenuLoadValues();
    UI::UpdateScreen();
    M.paused = IniI("General", "MenuPausesGame", 1) != 0;
    if (M.paused) NatV(UI::SET_GAME_PAUSED, 1);
    if (cfg.dsx) {                    // loose triggers while the menu is open
        DSX::Packet pk;
        pk.Add(DSX::TriggerUpdate, {cfg.controllerIndex, DSX::Left, DSX::Normal});
        pk.Add(DSX::TriggerUpdate, {cfg.controllerIndex, DSX::Right, DSX::Normal});
        pk.Send();
    }
    g_pulses.clear();
    g_settingsMenu = true;
    Log("menu opened");
}

// dir = -1 / +1 to change a value, 0 = activate (click / cross / enter)
static void MenuChange(int i, int dir) {
    MenuItem& it = g_items[i];
    switch (it.kind) {
        case 1: it.val = !it.val; MenuSave(it);
                if (!strcmp(it.key, "MenuPausesGame")) {
                    if (it.val && !M.paused) { NatV(UI::SET_GAME_PAUSED, 1); M.paused = true; }
                    if (!it.val && M.paused) { NatV(UI::SET_GAME_PAUSED, 0); M.paused = false; }
                }
                break;
        case 0: case 2: {
            if (dir == 0) return;
            int nv = std::clamp(it.val + dir * it.step, it.min, it.max);
            if (nv != it.val) { it.val = nv; MenuSave(it); }
            break; }
        case 3:
            if (dir != 0) return;
            if (!strcmp(it.label, "Close")) MenuClose();
            else {   // a revolver shot, so the strength settings can be felt right away
                AddPulse(0.15f * 0.72f, 0.72f, 0.045f, cfg.weapons, 4.5f);
                AddPulse(0.56f, 0.14f, 0.09f, cfg.weapons, 3.f);
            }
            break;
    }
}

static void MenuValueText(const MenuItem& it, char* out, size_t n) {
    switch (it.kind) {
        case 0: snprintf(out, n, "%d%%", it.val); break;
        case 1: snprintf(out, n, "%s", it.val ? "On" : "Off"); break;
        case 2: snprintf(out, n, "%.1f", it.val / 10.f); break;
        default: out[0] = 0;
    }
}

// Layout (0..1 screen units)
static constexpr float kPanelH = 0.86f, kPanelY = 0.07f, kRowH = 0.040f, kRowGap = 0.005f, kRowsY = 0.215f;

static void MenuDraw(float cursorX, float cursorY) {
    using namespace UI;
    float pw = 0.52f / aspect * (16.f / 9.f) * 0.75f;      // ~0.39 of the width on 16:9
    float px = 0.5f - pw * 0.5f, py = kPanelY;
    Rect(0, 0, 1, 1, 0, 0, 0, (int)(110 * M.anim));         // dim the game behind
    Backdrop(px, py, pw, kPanelH, 0.74f, 0.075f, true);
    Rect(px, py, pw, 0.003f, 245, 230, 200, 120);
    Rect(px, py + kPanelH - 0.003f, pw, 0.003f, 245, 230, 200, 120);

    Text("CONTROLLER RUMBLE", 0.5f, py + 0.034f, 36, "title", Center, 245, 230, 200);
    Rect(0.5f - pw * 0.30f, py + 0.105f, pw * 0.60f, 0.002f, 245, 230, 200, 170);
    Text("Settings  -  " MOD_VERSION, 0.5f, py + 0.112f, 22, "body", Center, 215, 185, 150);

    float rx = px + pw * 0.06f, rw = pw * 0.88f;
    for (int i = 0; i < kItems; i++) {
        const MenuItem& it = g_items[i];
        float y = kRowsY + i * (kRowH + kRowGap);
        bool sel = i == M.sel;
        Rect(rx, y, rw, kRowH, 15, 5, 3, sel ? 215 : 150);
        if (sel) {
            Rect(rx, y, 0.004f, kRowH, 250, 175, 80, 255);
            Rect(rx, y, rw, 0.002f, 250, 175, 80, 160);
            Rect(rx, y + kRowH - 0.002f, rw, 0.002f, 250, 175, 80, 160);
        }
        int tr = sel ? 250 : 245, tg = sel ? 175 : 230, tb = sel ? 80 : 200;
        float ty = y + kRowH * 0.5f - 0.011f;
        if (it.kind == 3) { Text(it.label, 0.5f, ty, 22, "body", Center, tr, tg, tb); continue; }
        Text(it.label, rx + 0.012f, ty, 22, "body", Left, tr, tg, tb);
        char v[32]; MenuValueText(it, v, sizeof v);
        float vr = rx + rw - 0.012f;
        if (it.kind == 1) {
            Text(v, vr, ty, 22, "body", Right, it.val ? 250 : 160, it.val ? 175 : 140, it.val ? 80 : 120);
        } else {   // -  value  +
            float vc = rx + rw * 0.82f;
            Text("-", rx + rw * 0.68f, ty, 24, "body", Center, 245, 230, 200, 200);
            Text(v, vc, ty, 22, "body", Center, 250, 175, 80);
            Text("+", rx + rw * 0.96f, ty, 24, "body", Center, 245, 230, 200, 200);
        }
    }
    float fy = kRowsY + kItems * (kRowH + kRowGap) + 0.006f;
    Text("Mouse: click, wheel   -   Pad: D-pad, Cross, Circle   -   F11 / Esc: close", 0.5f, fy, 16, "body", Center, 215, 185, 150, 220);
    (void)cursorX; (void)cursorY;
}

// Row + zone under the cursor. zone: -1 = left (decrease) / label, +1 = right (increase), 0 = label
static int MenuHit(float cx, float cy, int& zone) {
    using namespace UI;
    float pw = 0.52f / aspect * (16.f / 9.f) * 0.75f;
    float px = 0.5f - pw * 0.5f, rx = px + pw * 0.06f, rw = pw * 0.88f;
    zone = 0;
    if (cx < rx || cx > rx + rw) return -1;
    for (int i = 0; i < kItems; i++) {
        float y = kRowsY + i * (kRowH + kRowGap);
        if (cy >= y && cy <= y + kRowH) {
            float f = (cx - rx) / rw;
            if (g_items[i].kind == 0 || g_items[i].kind == 2) zone = f > 0.82f ? 1 : f > 0.62f ? -1 : 0;
            return i;
        }
    }
    return -1;
}

// Returns true while the menu is open (the caller then skips the normal rumble logic).
static bool MenuFrame(float dt) {
    bool focused = GameFocused();
    if (focused && KeyEdge(VK_F11)) {
        if (M.open) MenuClose();
        else if (!Nat<int>(N::IS_PAUSE_MENU_ACTIVE)) MenuOpen();
    }
    if (!M.open) { M.prevPad = g_padButtons.load(); return false; }

    g_scriptMenu = true;              // lets the pad thread click on button presses
    M.anim = std::min(1.f, M.anim + dt * 6.f);
    NatV(UI::DISABLE_ALL_CONTROL_ACTIONS, 0);
    NatV(UI::DISABLE_ALL_CONTROL_ACTIONS, 2);
    NatV(UI::SET_MOUSE_CURSOR_THIS_FRAME);

    // --- keyboard ---
    if (focused) {
        if (KeyEdge(VK_ESCAPE) || KeyEdge(VK_BACK)) { MenuClose(); return false; }
        if (KeyEdge(VK_UP))     M.sel = (M.sel + kItems - 1) % kItems;
        if (KeyEdge(VK_DOWN))   M.sel = (M.sel + 1) % kItems;
        if (KeyEdge(VK_LEFT))   MenuChange(M.sel, -1);
        if (KeyEdge(VK_RIGHT))  MenuChange(M.sel, +1);
        if (KeyEdge(VK_RETURN)) MenuChange(M.sel, 0);
        if (!M.open) return false;
    }
    // --- controller ---
    uint32_t pad = g_padButtons.load(), pressed = pad & ~M.prevPad;
    M.prevPad = pad;
    if (pressed & B_CIRCLE) { MenuClose(); return false; }
    if (pressed & B_UP)     M.sel = (M.sel + kItems - 1) % kItems;
    if (pressed & B_DOWN)   M.sel = (M.sel + 1) % kItems;
    if (pressed & B_LEFT)   MenuChange(M.sel, -1);
    if (pressed & B_RIGHT)  MenuChange(M.sel, +1);
    if (pressed & B_CROSS)  MenuChange(M.sel, 0);
    if (!M.open) return false;

    // --- mouse: the game's cursor, with the Windows cursor as a fallback ---
    static const uint32_t inX = Joaat("INPUT_CURSOR_X"), inY = Joaat("INPUT_CURSOR_Y"),
                          inAccept = Joaat("INPUT_CURSOR_ACCEPT"),
                          inUp = Joaat("INPUT_CURSOR_SCROLL_UP"), inDown = Joaat("INPUT_CURSOR_SCROLL_DOWN");
    float cx = Nat<float>(N::GET_DISABLED_CONTROL_NORMAL, 0, inX);
    float cy = Nat<float>(N::GET_DISABLED_CONTROL_NORMAL, 0, inY);
    if (focused && cx == 0.f && cy == 0.f) {
        POINT p; RECT rc; HWND w = GetForegroundWindow();
        if (w && GetCursorPos(&p) && ScreenToClient(w, &p) && GetClientRect(w, &rc) && rc.right > 0 && rc.bottom > 0) {
            cx = (float)p.x / rc.right; cy = (float)p.y / rc.bottom;
        }
    }
    bool click = Nat<int>(N::IS_CONTROL_JUST_PRESSED, 0, inAccept) != 0;
    if (click) M.lastNativeClick = GetTickCount();
    bool lmb = focused && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lmb && !M.prevLmb && GetTickCount() - M.lastNativeClick > 200) click = true;
    M.prevLmb = lmb;
    int zone, hit = MenuHit(cx, cy, zone);
    if (hit >= 0) {
        if (hit != M.hover) M.sel = hit;
        if (click) MenuChange(hit, zone);
        if (Nat<int>(N::IS_CONTROL_JUST_PRESSED, 0, inUp))   MenuChange(hit, +1);
        if (Nat<int>(N::IS_CONTROL_JUST_PRESSED, 0, inDown)) MenuChange(hit, -1);
    }
    M.hover = hit;
    if (!M.open) return false;

    MenuDraw(cx, cy);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Update check (GitHub) + startup notice
// ---------------------------------------------------------------------------------------------
// 0 = checking, 1 = up to date, 2 = newer version available, 3 = could not check / disabled
static std::atomic<int> g_updState{0};
static char g_latest[16] = "";

static DWORD WINAPI UpdateThread(LPVOID) {
    int state = 3;
    HINTERNET s = WinHttpOpen(L"RDR2DSXRumble/" MOD_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        WinHttpSetTimeouts(s, 4000, 4000, 4000, 4000);
        // Latest GitHub release: {"...","tag_name":"v1.6.1",...}. The API wants a User-Agent (set by WinHttpOpen).
        HINTERNET c = WinHttpConnect(s, UPDATE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", UPDATE_PATH, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            std::string body; char buf[4096]; DWORD got = 0;
            while (code == 200 && body.size() < 256 * 1024 && WinHttpReadData(r, buf, sizeof buf, &got) && got) body.append(buf, got);
            size_t at = body.find("\"tag_name\"");
            if (at != std::string::npos) at = body.find('"', body.find(':', at));   // opening quote of the value
            if (at != std::string::npos) {
                int maj = 0, min = 0, pat = 0;
                const char* b = body.c_str() + at + 1; while (*b == 'v' || *b == 'V' || *b == ' ') b++;
                if (sscanf(b, "%d.%d.%d", &maj, &min, &pat) >= 1) {
                    if (pat > 0) snprintf(g_latest, sizeof g_latest, "%d.%d.%d", maj, min, pat);
                    else         snprintf(g_latest, sizeof g_latest, "%d.%d", maj, min);
                    long latest = maj * 1000000L + min * 1000L + pat;
                    long mine   = MOD_VER_MAJOR * 1000000L + MOD_VER_MINOR * 1000L + MOD_VER_PATCH;
                    state = latest > mine ? 2 : 1;
                }
            }
            Log("update check: HTTP %lu, latest '%s' -> %s", code, g_latest, state == 1 ? "up to date" : state == 2 ? "update available" : "unknown");
        } else Log("update check failed (%lu)", GetLastError());
        if (r) WinHttpCloseHandle(r);
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
    }
    g_updState = state;
    return 0;
}

static void StartUpdateCheck() {
    if (IniI("General", "CheckForUpdates", 1) == 0) { g_updState = 3; return; }
    if (HANDLE t = CreateThread(nullptr, 0, UpdateThread, nullptr, 0, nullptr)) CloseHandle(t);
    else g_updState = 3;
}

// A small card at the top of the screen, once per game start, as soon as you have control.
static void UpdateNotice(float dt) {
    static float t = -1.f, wait = 0.f;
    if (t >= 9.f) return;
    if (t < 0.f) {
        if (g_updState.load() == 0) { wait += dt; if (wait < 8.f) return; }   // give the check a few seconds
        int player = Nat<int>(N::PLAYER_ID);
        if (!Nat<int>(N::IS_PLAYER_CONTROL_ON, player) || Nat<int>(N::IS_PAUSE_MENU_ACTIVE)) return;
        t = 0.f; UI::UpdateScreen();
    }
    t += dt;
    float a = std::clamp(std::min(t / 0.4f, (9.f - t) / 0.8f), 0.f, 1.f);
    using namespace UI;
    float w = 0.30f * (16.f / 9.f) / aspect, h = 0.085f, x = 0.5f - w * 0.5f, y = 0.035f;
    if (a < 0.98f) {   // simple fade: a dark card that the backdrop grows into
        Rect(x, y, w, h, 18, 7, 5, (int)(230 * a));
        if (a < 0.6f) return;
    }
    Backdrop(x, y, w, h, 0.86f, 0.022f, false);
    Rect(x, y, w, 0.002f, 245, 230, 200, 140);
    int st = g_updState.load();
    char line[128];
    Text("CONTROLLER RUMBLE " MOD_VERSION, x + 0.012f, y + 0.010f, 26, "title", Left, 245, 230, 200);
    if (st == 2) snprintf(line, sizeof line, "Version %s is out - get it on Nexus Mods.   F11: settings", g_latest);
    else if (st == 1) snprintf(line, sizeof line, "The mod is up to date.   F11: settings");
    else snprintf(line, sizeof line, "Loaded.   F11: settings");
    Text(line, x + 0.012f, y + 0.050f, 18, "body", Left, st == 2 ? 250 : 245, st == 2 ? 175 : 230, st == 2 ? 80 : 200);
}
