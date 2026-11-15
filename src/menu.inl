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
    int kind;                  // 0 = percent slider, 1 = toggle, 2 = curve (x10), 3 = action, 4 = toggle + dropdown, 5 = next page starts
    int def, min, max, step;   // kind 4: step = how many rows below belong to its dropdown
    int val;
};

// 10x10 pixel icons for the advanced page, in the pixel style of the Nexus art.
// '#' = main colour, '+' = amber accent, 'r' = red, '.' = empty
static const struct { const char* key; const char* px; } g_icons[] = {
    {"Footsteps",       "...###...." "...###...." "...###...." "...###...." "...####..." "...######." "..#######." "..########" "..++++++++" ".........."},
    {"HorseRumble",     ".##....##." ".#+....+#." "##......##" "#+......+#" "##......##" ".##....##." ".###..###." "..######.." "...####..." ".........."},
    {"Weapons",         ".........." "..#######+" ".#######.." ".##++##..." ".#####...." ".##..#...." ".##.##...." "###......." "###......." ".........."},
    {"Reload",          ".........." ".++.++.++." ".++.++.++." ".##.##.##." ".##.##.##." ".##.##.##." ".##.##.##." ".##.##.##." ".##.##.##." ".........."},
    {"WeaponSwitch",    "...####..." ".##.##.##." ".#..##..#." "#...##...#" "##########" "#+++##...#" ".#++##..#." ".##+##.##." "...####..." ".........."},
    {"Melee",           ".........." "..######.." ".#+#+#+##." ".########." ".#+#+#+##." ".########." ".#####++#." "..######.." "...####..." "...####..."},
    {"Damage",          "....r....." "....r....." "...rrr...." "...rrr...." "..rrrrr..." ".rr#rrrr.." ".r##rrrr.." ".rrrrrrr.." "..rrrrr..." "...rrr...."},
    {"Heartbeat",       ".........." ".rr...rr.." "rrrr.rrrr." "r#rrrrrrr." "rrrrrrrrr." ".rrrrrrr.." "..rrrrr..." "...rrr...." "....r....." ".........."},
    {"Animals",         ".........." "..##..##.." "..##..##.." "##......##" "##.####.##" "..######.." ".########." ".########." "..######.." ".........."},
    {"Water",           ".........." "..##...##." ".#..#.#..#" "#....#...." ".........." "..++...++." ".+..+.+..+" "+....+...." ".........." ".........."},
    {"Swimming",        "......##.." ".....####." "......##.." "##........" "..###....." "....##...." "..++...++." ".+..+.+..+" "+....+...." ".........."},
    {"Doors",           ".########." ".#......#." ".#.####.#." ".#.#..#.#." ".#.####.#." ".#.....+#." ".#.####.#." ".#.#..#.#." ".#.####.#." ".########."},
    {"Carry",           "....++...." "...#..#..." "...####..." "..######.." ".########." ".###++###." ".########." ".########." "..######.." ".........."},
    {"Sliding",         "#........." "#+........" "##+......." "###+......" "####+....." "#####+...." "######+..." "#######+.." "##########" ".........."},
    {"Landing",         "....##...." "....##...." "....##...." "..######.." "...####..." "....##...." ".........." "+.+.++.+.+" "##########" ".........."},
    {"Vehicle",         "...####..." ".##....##." ".#+.##.+#." "#..+##+..#" "#.##++##.#" "#.##++##.#" "#..+##+..#" ".#+.##.+#." ".##....##." "...####..."},
    {"CutsceneRumble",  ".........." "#.#.#.#.#." "##########" "#++++++++#" "#++++++++#" "#++++++++#" "##########" "#.#.#.#.#." ".........." ".........."},
    {"ButtonRumble",    ".#........" ".##......." ".#+#......" ".#++#....." ".#+++#...." ".#++++#..." ".#++###..." ".###.##..." ".#....##.." "......##.."},
    {"SplashShotRumble","....#....." ".#..#..#.." "..#.#.#..." "...+++...." "####+####." "...+++...." "..#.#.#..." ".#..#..#.." "....#....." ".........."},
    {"AdaptiveTriggers",".........." "...####..." "..#++++#.." ".#++++++#." ".#++++++#." ".#++++++#." ".#++++++#." ".########." ".........." ".........."},
    {"GallopingTriggersOnHorse", ".........." "...####..." "+.#++++#.+" ".+#++++#+." "+.#++++#.+" "..#++++#.." "..######.." ".##....##." ".#+....+#." ".........."},
    {"Lightbar",        ".........." "+...+...+." ".+..+..+.." "..+.+.+..." ".........." "##########" "#++++++++#" "##########" ".........." ".........."},
};
static const char* IconFor(const char* key) {
    for (auto& ic : g_icons) if (key && !strcmp(ic.key, key)) return ic.px;
    return nullptr;
}

static MenuItem g_items[] = {
    {"Rumble strength",        "Rumble",  "Intensity",          0, 100, 0, 200, 10},
    {"Response curve",         "Rumble",  "ResponseCurve",      2, 15,  5, 40,  1},
    {"Footsteps",              "Rumble",  "Footsteps",          0, 100, 0, 200, 10},
    {"Horse",                  "Rumble",  "HorseRumble",        4, 1,   0, 0,   5},
    {"Strength",               "Rumble",  "Horse",              0, 100, 0, 200, 10},
    {"At a walk",              "Rumble",  "HorseWalk",          1, 1},
    {"At a trot",              "Rumble",  "HorseTrot",          1, 1},
    {"At a canter",            "Rumble",  "HorseCanter",        1, 1},
    {"At a gallop",            "Rumble",  "HorseGallop",        1, 1},
    {"Guns",                   "Rumble",  "Weapons",            0, 100, 0, 200, 10},
    {"Fistfights",             "Rumble",  "Melee",              0, 100, 0, 200, 10},
    {"Rumble in cutscenes",    "General", "CutsceneRumble",     1, 1},
    {"Adaptive triggers",      "DSX",     "AdaptiveTriggers",   1, 1},
    {"Lightbar",               "DSX",     "Lightbar",           1, 1},
    {"Menu clicks",            "Menu",    "ButtonRumble",       1, 1},
    {"Mute when alt-tabbed",   "General", "MuteWhenUnfocused",  1, 1},
    {"Check for updates",      "General", "CheckForUpdates",    1, 1},
    {"Pause game in this menu","General", "MenuPausesGame",     1, 1},
    {"Advanced settings",      nullptr,   nullptr,              3},
    {"Test rumble",            nullptr,   nullptr,              3},
    {"Close",                  nullptr,   nullptr,              3},
    {"",                       nullptr,   nullptr,              5},
    // advanced page: every effect on its own switch ([Effects] in the ini, the strengths stay in [Rumble])
    {"Footsteps",              "Effects", "Footsteps",          1, 1},
    {"Horse",                  "Rumble",  "HorseRumble",        1, 1},
    {"Gunshots",               "Effects", "Weapons",            1, 1},
    {"Reloading",              "Effects", "Reload",             1, 1},
    {"Weapon switch",          "Effects", "WeaponSwitch",       1, 1},
    {"Fistfights",             "Effects", "Melee",              1, 1},
    {"Getting hurt",           "Effects", "Damage",             1, 1},
    {"Heartbeat",              "Effects", "Heartbeat",          1, 1},
    {"Animal attacks",         "Effects", "Animals",            1, 1},
    {"Water",                  "Effects", "Water",              1, 1},
    {"Swimming",               "Effects", "Swimming",           1, 1},
    {"Doors",                  "Effects", "Doors",              1, 1},
    {"Carrying",               "Effects", "Carry",              1, 1},
    {"Sliding",                "Effects", "Sliding",            1, 1},
    {"Falls and jumps",        "Effects", "Landing",            1, 1},
    {"Wagons",                 "Effects", "Vehicle",            1, 1},
    {"Cutscenes",              "General", "CutsceneRumble",     1, 1},
    {"Menu clicks",            "Menu",    "ButtonRumble",       1, 1},
    {"Startup shot",           "Startup", "SplashShotRumble",   1, 1},
    {"Adaptive triggers",      "DSX",     "AdaptiveTriggers",   1, 1},
    {"Gallop triggers",        "DSX",     "GallopingTriggersOnHorse", 1, 1},
    {"Lightbar",               "DSX",     "Lightbar",           1, 1},
    {"Back",                   nullptr,   nullptr,              3},
};
constexpr int kItems = sizeof g_items / sizeof g_items[0];

static struct {
    bool  open = false, paused = false;
    bool  expanded = false;           // shortcut: one flag for all dropdowns, fine while "Horse" is the only one
    int   page = 0;                   // 0 = settings, 1 = advanced
    int   sel = 0, hover = -1;
    float anim = 0.f;
    uint32_t prevPad = 0;
    bool  keys[256] = {};
    bool  prevLmb = false;
    DWORD lastNativeClick = 0;
} M;

// Dropdown row that row i sits in, or -1
static int GroupOf(int i) {
    for (int j = 0; j < i; j++)
        if (g_items[j].kind == 4 && i <= j + g_items[j].step) return j;
    return -1;
}
static int PageOf(int i) { int p = 0; for (int j = 0; j < i; j++) p += g_items[j].kind == 5; return p; }
static bool Hidden(int i) { return g_items[i].kind == 5 || PageOf(i) != M.page || (!M.expanded && GroupOf(i) >= 0); }

// A short tick for mouse and keyboard use too (the pad thread only clicks on controller buttons).
static void MenuClick(float a = 0.26f) { if (cfg.menu) AddPulse(a, a * 0.9f, 0.035f, cfg.menuIntensity, 4.f); }

// d visible rows down (negative = up), wrapping around
static void MenuMove(int d) {
    int vis[kItems], n = 0, pos = 0;
    for (int i = 0; i < kItems; i++) if (!Hidden(i)) { if (i <= M.sel) pos = n; vis[n++] = i; }
    if (!n) return;
    M.sel = vis[((pos + d) % n + n) % n];
    MenuClick(0.22f);
}

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
        else              it.val = it.kind == 1 || it.kind == 4 ? (IniI(it.sec, it.key, it.def) != 0) : (int)(IniF(it.sec, it.key, (float)it.def) + 0.5f);
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
    M.open = true; M.sel = 0; M.page = 0; M.anim = 0.f;
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

static void MenuPage(int p) {
    M.page = p;
    MenuLoadValues();                 // both pages show some of the same keys
    for (int i = 0; i < kItems; i++)  // advanced page: its first tile, back on page 1: the "Advanced settings" row
        if (!Hidden(i) && (p == 1 || !strcmp(g_items[i].label, "Advanced settings"))) { M.sel = i; break; }
}

// dir = -1 / +1 to change a value, 0 = activate (click / cross / enter)
static void MenuChange(int i, int dir) {
    MenuItem& it = g_items[i];
    if (!((it.kind == 0 || it.kind == 2) && dir == 0) && !(it.kind == 3 && dir != 0)) MenuClick();
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
        case 4:   // activate = open / close the dropdown, left / right = on / off
            if (dir == 0) M.expanded = !M.expanded;
            else { it.val = !it.val; MenuSave(it); }
            break;
        case 3:
            if (dir != 0) return;
            if (!strcmp(it.label, "Close")) MenuClose();
            else if (!strcmp(it.label, "Advanced settings")) MenuPage(1);
            else if (!strcmp(it.label, "Back")) MenuPage(0);
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
        case 1: case 4: snprintf(out, n, "%s", it.val ? "On" : "Off"); break;
        case 2: snprintf(out, n, "%.1f", it.val / 10.f); break;
        default: out[0] = 0;
    }
}

// Layout (0..1 screen units). The settings page is a list, the advanced page a two-column grid of tiles.
static constexpr float kPanelY = 0.04f, kRowsY = 0.185f;
struct Cell { float x, y, w, h; };   // h = 0: not on screen
static Cell  g_cell[kItems];
static float g_rowsEnd;              // y just below the last row

static float PanelW() { return 0.52f / UI::aspect * (16.f / 9.f) * 0.75f; }   // ~0.39 of the width on 16:9

static void Layout() {
    float pw = PanelW(), rx = 0.5f - pw * 0.5f + pw * 0.06f, rw = pw * 0.88f, gap = 0.006f;
    auto tile = [](int i) { return M.page == 1 && g_items[i].kind != 3; };
    int rows = 0, col = 0;
    for (int i = 0; i < kItems; i++) {
        if (Hidden(i)) continue;
        if (!tile(i) || col == 0) rows++;
        col = tile(i) ? col ^ 1 : 0;
    }
    // rows shrink when the dropdown is open, so the panel always ends above 0.95
    float step = std::min(M.page == 1 ? 0.060f : 0.045f, 0.725f / std::max(rows, 1)), h = step * 0.89f;
    float cw = (rw - gap) * 0.5f;
    int r = -1; col = 0;
    for (int i = 0; i < kItems; i++) {
        g_cell[i] = {0, 0, 0, 0};
        if (Hidden(i)) continue;
        if (!tile(i) || col == 0) r++;
        g_cell[i] = tile(i) ? Cell{rx + col * (cw + gap), kRowsY + r * step, cw, h} : Cell{rx, kRowsY + r * step, rw, h};
        col = tile(i) ? col ^ 1 : 0;
    }
    g_rowsEnd = kRowsY + rows * step;
}

// Draws a 10x10 icon from g_icons, ph = height of one pixel. Runs of one colour are one rect.
static void Icon(const char* px, float x, float y, float ph, bool on, bool sel) {
    if (!px) return;
    float pxw = ph / UI::aspect;      // square pixels
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 10; ) {
            char ch = px[r * 10 + c];
            int n = 1; while (c + n < 10 && px[r * 10 + c + n] == ch) n++;
            int R, G, B;
            if (ch == '#')      { R = sel ? 255 : 245; G = sel ? 215 : 230; B = sel ? 160 : 200; }
            else if (ch == '+') { R = 250; G = 175; B = 80; }
            else if (ch == 'r') { R = 205; G = 55;  B = 45; }
            else { c += n; continue; }
            if (!on) { R = R * 2 / 5; G = G * 2 / 5; B = B * 2 / 5; }   // switched off: the icon goes dark
            UI::Rect(x + c * pxw, y + r * ph, n * pxw, ph, R, G, B, 255);
            c += n;
        }
}

static void MenuDraw(float cursorX, float cursorY) {
    using namespace UI;
    Layout();
    float pw = PanelW();
    float px = 0.5f - pw * 0.5f, py = kPanelY;
    const float kPanelH = g_rowsEnd - kPanelY + 0.04f;
    Rect(0, 0, 1, 1, 0, 0, 0, (int)(110 * M.anim));         // dim the game behind
    Backdrop(px, py, pw, kPanelH, 0.74f, 0.075f, true);
    Rect(px, py, pw, 0.003f, 245, 230, 200, 120);
    Rect(px, py + kPanelH - 0.003f, pw, 0.003f, 245, 230, 200, 120);

    Text("CONTROLLER RUMBLE", 0.5f, py + 0.034f, 36, "title", Center, 245, 230, 200);
    Rect(0.5f - pw * 0.30f, py + 0.105f, pw * 0.60f, 0.002f, 245, 230, 200, 170);
    Text(M.page ? "Advanced settings  -  " MOD_VERSION : "Settings  -  " MOD_VERSION, 0.5f, py + 0.112f, 22, "body", Center, 215, 185, 150);

    for (int i = 0; i < kItems; i++) {
        const Cell& cell = g_cell[i];
        if (cell.h <= 0.f) continue;
        const MenuItem& it = g_items[i];
        const float rx = cell.x, rw = cell.w, y = cell.y, kRowH = cell.h;
        bool sel = i == M.sel, child = GroupOf(i) >= 0;
        Rect(rx, y, rw, kRowH, 15, 5, 3, sel ? 215 : child ? 110 : 150);
        if (sel) {
            Rect(rx, y, 0.004f, kRowH, 250, 175, 80, 255);
            Rect(rx, y, rw, 0.002f, 250, 175, 80, 160);
            Rect(rx, y + kRowH - 0.002f, rw, 0.002f, 250, 175, 80, 160);
        }
        int tr = sel ? 250 : 245, tg = sel ? 175 : 230, tb = sel ? 80 : 200;
        float ty = y + kRowH * 0.5f - 0.011f;
        if (it.kind == 3) { Text(it.label, 0.5f, ty, 22, "body", Center, tr, tg, tb); continue; }
        if (M.page == 1) {   // tile: icon, name, On / Off
            float ih = kRowH * 0.72f;
            Icon(IconFor(it.key), rx + 0.008f, y + (kRowH - ih) * 0.5f, ih / 10.f, it.val != 0, sel);
            float lx = rx + 0.008f + ih / aspect + 0.008f;
            int dim = it.val ? 255 : 150;
            Text(it.label, lx, ty, 20, "body", Left, tr * dim / 255, tg * dim / 255, tb * dim / 255);
            Text(it.val ? "On" : "Off", rx + rw - 0.008f, ty, 20, "body", Right, it.val ? 250 : 160, it.val ? 175 : 140, it.val ? 80 : 120);
            continue;
        }
        float lx = rx + (child ? 0.030f : 0.012f);
        if (it.kind == 4) {   // pixel arrow: > closed, v open
            float ax = rx + 0.012f, ay = y + kRowH * 0.5f, u = 0.0028f;
            for (int k = 0; k < 4; k++) {
                if (M.expanded) Rect(ax + k * u * 0.6f, ay - u * 1.5f + k * u, (7 - 2 * k) * u * 0.6f, u, tr, tg, tb, 255);
                else            Rect(ax + k * u * 0.6f, ay - (3.5f - k) * u, u * 0.6f, (7 - 2 * k) * u, tr, tg, tb, 255);
            }
            lx = rx + 0.030f;
        }
        Text(it.label, lx, ty, 22, "body", Left, tr, tg, tb);
        char v[32]; MenuValueText(it, v, sizeof v);
        float vr = rx + rw - 0.012f;
        if (it.kind == 1 || it.kind == 4) {
            Text(v, vr, ty, 22, "body", Right, it.val ? 250 : 160, it.val ? 175 : 140, it.val ? 80 : 120);
        } else {   // -  value  +
            float vc = rx + rw * 0.82f;
            Text("-", rx + rw * 0.68f, ty, 24, "body", Center, 245, 230, 200, 200);
            Text(v, vc, ty, 22, "body", Center, 250, 175, 80);
            Text("+", rx + rw * 0.96f, ty, 24, "body", Center, 245, 230, 200, 200);
        }
    }
    Text(M.page ? "Click / Cross: on - off   -   Circle / Esc: back   -   F11: close"
                : "Mouse: click, wheel   -   Pad: D-pad, Cross, Circle   -   F11 / Esc: close",
         0.5f, g_rowsEnd + 0.006f, 16, "body", Center, 215, 185, 150, 220);
    (void)cursorX; (void)cursorY;
}

// Row + zone under the cursor. zone: -1 = left (decrease) / label, +1 = right (increase), 0 = label
static int MenuHit(float cx, float cy, int& zone) {
    Layout();
    zone = 0;
    for (int i = 0; i < kItems; i++) {
        const Cell& c = g_cell[i];
        if (c.h > 0.f && cx >= c.x && cx <= c.x + c.w && cy >= c.y && cy <= c.y + c.h) {
            float f = (cx - c.x) / c.w;
            if (g_items[i].kind == 0 || g_items[i].kind == 2) zone = f > 0.82f ? 1 : f > 0.62f ? -1 : 0;
            if (g_items[i].kind == 4) zone = f > 0.62f ? 1 : 0;   // label opens the dropdown, the value switches on / off
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
        if (KeyEdge(VK_ESCAPE) || KeyEdge(VK_BACK)) { if (M.page) MenuPage(0); else { MenuClose(); return false; } }
        if (KeyEdge(VK_UP))     MenuMove(M.page ? -2 : -1);   // the grid: up / down a row, left / right a column
        if (KeyEdge(VK_DOWN))   MenuMove(M.page ? +2 : +1);
        if (KeyEdge(VK_LEFT))   { if (M.page) MenuMove(-1); else MenuChange(M.sel, -1); }
        if (KeyEdge(VK_RIGHT))  { if (M.page) MenuMove(+1); else MenuChange(M.sel, +1); }
        if (KeyEdge(VK_RETURN)) MenuChange(M.sel, 0);
        if (!M.open) return false;
    }
    // --- controller ---
    uint32_t pad = g_padButtons.load(), pressed = pad & ~M.prevPad;
    M.prevPad = pad;
    if (pressed & B_CIRCLE) { if (M.page) MenuPage(0); else { MenuClose(); return false; } }
    if (pressed & B_UP)     MenuMove(M.page ? -2 : -1);
    if (pressed & B_DOWN)   MenuMove(M.page ? +2 : +1);
    if (pressed & B_LEFT)   { if (M.page) MenuMove(-1); else MenuChange(M.sel, -1); }
    if (pressed & B_RIGHT)  { if (M.page) MenuMove(+1); else MenuChange(M.sel, +1); }
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

// GET https://host/path: the body, or "" when it fails (HTTP status in code, 0 = no answer).
static std::string HttpGet(HINTERNET s, const wchar_t* host, const wchar_t* path, DWORD& code) {
    std::string body; code = 0;
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD sz = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        char buf[4096]; DWORD got = 0;
        while (code == 200 && body.size() < 256 * 1024 && WinHttpReadData(r, buf, sizeof buf, &got) && got) body.append(buf, got);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    return body;
}

static DWORD WINAPI UpdateThread(LPVOID) {
    int state = 3;
    HINTERNET s = WinHttpOpen(L"RDR2DSXRumble/" MOD_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        WinHttpSetTimeouts(s, 4000, 4000, 4000, 4000);
        // Latest GitHub release: {"...","tag_name":"v1.6.1",...}. The API wants a User-Agent (set by WinHttpOpen).
        DWORD code = 0;
        const char* from = "release";
        std::string body = HttpGet(s, UPDATE_HOST, UPDATE_PATH, code);
        size_t at = body.find("\"tag_name\"");
        if (at != std::string::npos) at = body.find('"', body.find(':', at));   // opening quote of the value
        const char* b = at != std::string::npos ? body.c_str() + at + 1 : nullptr;
        if (!b) {   // API limit hit or GitHub down: version.txt holds the same number ("1.6.1")
            from = "version.txt";
            body = HttpGet(s, UPDATE_TXT_HOST, UPDATE_TXT_PATH, code);
            if (!body.empty()) b = body.c_str();
        }
        if (b) {
            int maj = 0, min = 0, pat = 0;
            while (*b == 'v' || *b == 'V' || *b == ' ') b++;
            if (sscanf(b, "%d.%d.%d", &maj, &min, &pat) >= 1) {
                if (pat > 0) snprintf(g_latest, sizeof g_latest, "%d.%d.%d", maj, min, pat);
                else         snprintf(g_latest, sizeof g_latest, "%d.%d", maj, min);
                long latest = maj * 1000000L + min * 1000L + pat;
                long mine   = MOD_VER_MAJOR * 1000000L + MOD_VER_MINOR * 1000L + MOD_VER_PATCH;
                state = latest > mine ? 2 : 1;
            }
        }
        Log("update check (%s): HTTP %lu, latest '%s' -> %s", from, code, g_latest, state == 1 ? "up to date" : state == 2 ? "update available" : "unknown");
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
