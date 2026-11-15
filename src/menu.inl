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
    static int   rects = 0;           // rects drawn this frame (RDR2 drops everything past ~400)

    static void UpdateScreen() {
        screenW = (float)GetSystemMetrics(SM_CXSCREEN);
        screenH = (float)GetSystemMetrics(SM_CYSCREEN);
        if (screenW < 100.f || screenH < 100.f) { screenW = 1920.f; screenH = 1080.f; }
        aspect = screenW / screenH;
    }

    // x, y, w, h in 0..1 screen units, (x, y) = top-left
    static void Rect(float x, float y, float w, float h, int r, int g, int b, int a) {
        NatV(DRAW_RECT, x + w * 0.5f, y + h * 0.5f, w, h, r, g, b, a, 1, 1);
        rects++;
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
    // lite: fewer bands and columns and no sun (~40 rects instead of ~290), for pages that need the rect budget
    static void Backdrop(float x, float y, float w, float h, float sunX, float sunR, bool rings, bool lite = false) {
        struct Stop { float t; int r, g, b; };
        static const Stop stops[] = { {0.f, 30, 6, 6}, {0.50f, 120, 22, 12}, {0.82f, 215, 95, 25}, {1.f, 120, 45, 15} };
        const int bands = lite ? 14 : 28;
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
        if (!lite) {
            for (int i = 3; i >= 1; i--) Circle(cx, cy, sunR * (1.f + i * 0.45f), 255, 190, 90, 22);   // glow
            Circle(cx, cy, sunR, 255, 200, 120, 255);
        }
        if (rings && !lite) {   // rumble rings: dotted arcs above the sun
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
        const int cols = lite ? 24 : 72;
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

// Title-card art for the advanced pages: one 24x8 pixel scene per setting, drawn on the right of its card
// (forest, cabin and dusk colours, chunky like the menu backdrop). One letter = one pixel, '.' = empty.
// RDR2 draws at most ~400 rects per frame, so every scene stays under ~32 runs of one colour per row.
static void Palette(char ch, int& r, int& g, int& b) {
    switch (ch) {
        case 'k': r = 16;  g = 12;  b = 12;  break;   // near black
        case 'n': r = 28;  g = 38;  b = 46;  break;   // night
        case 'd': r = 58;  g = 36;  b = 28;  break;   // dark wood / earth
        case 'b': r = 112; g = 66;  b = 42;  break;   // wood
        case 'B': r = 158; g = 98;  b = 58;  break;   // light wood
        case 'g': r = 34;  g = 64;  b = 52;  break;   // pine shadow
        case 'G': r = 62;  g = 104; b = 72;  break;   // pine
        case 'L': r = 108; g = 150; b = 92;  break;   // leaf
        case 'm': r = 76;  g = 110; b = 108; break;   // mist
        case 's': r = 128; g = 162; b = 150; break;   // teal sky
        case 'S': r = 186; g = 206; b = 182; break;   // pale sky
        case 'w': r = 44;  g = 84;  b = 104; break;   // deep water
        case 'W': r = 98;  g = 146; b = 164; break;   // light water
        case 'o': r = 214; g = 112; b = 48;  break;   // dusk orange
        case 'O': r = 250; g = 175; b = 80;  break;   // amber
        case 'y': r = 255; g = 222; b = 140; break;   // lamp light
        case 'c': r = 238; g = 226; b = 198; break;   // cream
        case 'r': r = 150; g = 34;  b = 28;  break;   // blood
        case 'R': r = 222; g = 72;  b = 52;  break;   // bright red
        case 'e': r = 96;  g = 96;  b = 92;  break;   // stone
        case 'E': r = 150; g = 148; b = 140; break;   // light stone
        case 'p': r = 74;  g = 48;  b = 70;  break;   // dusk purple
        case 'u': r = 196; g = 120; b = 70;  break;   // sand
        default:  r = g = b = -1;
    }
}

static const struct { const char* key; const char* px; } g_scenes[] = {
    // --- On foot ---
    {"Footsteps",
     "ssssssssssssssssssssssss" "sssssgssssssssssgsssssss" "mmmmgggmmmmmmmmgggmmmmmm" "gggggggggggggggggggggggg"
     "GGGGGbbbbbbbbbbbbbGGGGGG" "GGGbbbdbbbbbdbbbbbbbGGGG" "GGbbbbbbbdbbbbbbdbbbbbGG" "GGGbbbbbbbbbbbbbbbbbbGGG"},
    {"Sliding",
     "Gsssssssssssssssssssssss" "bGGsssssssssssssssssssss" "bbbGGsssssssssssssssssss" "bbbbbGGsssssssEsssssssss"
     "bbbbbbbGGsssssEEssssssss" "bbbbbbbbbGGsssssssEsssss" "bbbbbbbbbbbGGsssssssssss" "bbbbbbbbbbbbbGGGGGGGGGGG"},
    {"Landing",
     "ssssssssssssssssssssssss" "eeeeeeessssssssssccsssss" "eEeeeeesssssssssBBBBssss" "eeeeeeessssssssssddsssss"
     "eeeeeeesssssssssdssdssss" "eeeeeeesssssssssssssssss" "eeeeeeemmmmmmmmmmmmmmmmm" "eeeeeeeGGGGGGGGGGGGGGGGG"},
    {"Swimming",
     "SSSSSSSSSSSSSSSSSSSSSSSS" "ssssssssssssssssssssssss" "gggggggggmmmmmmmmmgggggg" "WWWWWWWWWWWWWWWccWWWWWWW"
     "WWWWWWWWWWWdddWWcWWWWWWW" "wwwwwwwWWWWdcdWWWWWwwwww" "wwwWWwwwwwwwwwwwwwwWWwww" "wwwwwwwwwwwwwwwwwwwwwwww"},
    {"Water",
     "ssssssssssssssssssssssss" "sssssssssggsssssssssssss" "mmmmmmmmggggmmmmmmmmmmmm" "GGGGGGGGGGGGGGGGGGGGGGGG"
     "WWWWWWWWWWWWWWWWWWWLWWWW" "wwwWWWWwwwwwwwwwwwwLwLww" "wwwwwwwwwwWWWWwwwwwLwLww" "wwwwwwwwwwwwwwwwwwwwwwww"},
    {"Doors",
     "sssssssssssbbbbbbsssssss" "ssssssssssbbbbbbbbssssss" "sssssssssbbbbbbbbbbsssss" "GGGGGGGGGBByyBdddBBGGGGG"
     "GGGGGGGGGBByyBdOdBBGGGGG" "GGGGGGGGGBBBBBdddBBGGGGG" "bbbbbbbbbbbbbbbbbbbbbbbb" "dddddddddddddddddddddddd"},
    {"Carry",
     "ssssssssssssssssssssssss" "sssssssssssssddsssssssss" "ssssssssssssBbbBssssssss" "mmmmmmmmmmmBBBBBBmmmmmmm"
     "ggggggggggBBBBBBBBgggggg" "GGGGGGGGGGBBBBBBBBGGGGGG" "GGGGGGGGGGGBBBBBBGGGGGGG" "bbbbbbbbbbbbbbbbbbbbbbbb"},
    {"Items",
     "nnnnnnnnnnnnnnnnnnnnnnnn" "nnnnnnnnnnnnnnnnnnnnnnnn" "nnnnnnnnnnnneeeeennnnnnn" "nnnnnnnnnnnneeeeennnnnnn"
     "nnnnnnnnnnnnnoOonnnnnnnn" "gggggggggggoOyOogggggggg" "ddddddddddbbbbbbbddddddd" "dddddddddddddddddddddddd"},
    // --- Fights and riding ---
    {"Weapons",
     "pppppppppppppppppppppppp" "oooooooooooooooooooooooo" "ooooooooooyOkkkkkkkkoooo" "OOOOOOOOOOOOkkkkkkkkOOOO"
     "OOOOOOOOOOOOOOOkkOkkOOOO" "uuuuuuuuuuuuuuuuuukkkuuu" "uuuuuuuuuuuuuuuuuukkkuuu" "dddddddddddddddddddddddd"},
    {"Reload",
     "dddddddddddddddddddddddd" "ddddddddddddddyddyddyddd" "ddddddddddddddOddOddOddd" "ddddddddddddddOddOddOddd"
     "ddddddddddddddbddbddbddd" "bbbbbbbbbbbbbbbbbbbbbbbb" "BBBBBBBBBBBBBBBBBBBBBBBB" "bbbbbbbbbbbbbbbbbbbbbbbb"},
    {"WeaponSwitch",
     "dddddddddddddddddddddddd" "ddddddddddkkkkkkkkkkkddd" "ddddddddddddddkkkkbbbbdd" "dddddddddddddddddkbbbbbd"
     "dddddddddddddddddddddddd" "ddddddddddddkkkkkkkddddd" "ddddddddddddddddkbbddddd" "bbbbbbbbbbbbbbbbbbbbbbbb"},
    {"Melee",
     "pppppppppppppppppppppppp" "pppppppppppppypppppppppp" "pppppppuuuupyyypuuuupppp" "ppppppuuuuuyyOyyuuuuuppp"
     "pppppppuuuupyyypuuuupppp" "pppppppppppppypppppppppp" "dddddddddddddddddddddddd" "dddddddddddddddddddddddd"},
    {"Damage",
     "kkkkkkkkkkkkkkkkkkkkkkkk" "kkkkkkkkkkkkkkkkkrkkkkkk" "kkkkkkkkkkkkkkkkrRkkkkkk" "kkkkkkkkkkkkkrRRrkkkrkkk"
     "kkkkkkkkkkkkrRRrkkkkkkkk" "kkkkkkkkkkkrRrkkkkrkkkkk" "kkkkkkkkkkrrkkkkkkkkkkkk" "kkkkkkkkkkkkkkkkkkkkkkkk"},
    {"Animals",
     "nnnnnnnnnnnnnnnnnnccnnnn" "nnnnnnnnnnnnnnnnnccccnnn" "nnnnnnnnnnnnnnnnnnccnnnn" "nnnnnnnnnnnnkknnnnnnnnnn"
     "nnnnnnnnnnnnkkknnnnnnnnn" "nnnnnnnnnnnkkkkkkkknnnnn" "ggggggggggggkgggggkggggg" "gggggggggggggggggggggggg"},
    {"HorseRumble",
     "pppppppppppppppppppppppp" "oooooooooooooooooooooooo" "OOOOOOOOOOOOOOOOOkkOOOOO" "OOOOOOOOOOOOOOOOkkOOOOOO"
     "uuuuuuuuuukkkkkkkkuuuuuu" "uuuuuuuuukkkkkkkkkuuuuuu" "ddddddddddkddkddkdkddddd" "dddddddddddddddddddddddd"},
    {"Vehicle",
     "ssssssssssssssssssssssss" "ssssssssscccccccccssssss" "sssssssscccccccccccsssss" "ssssssssbbbbbbbbbbbsssss"
     "GGGGGGGGddddGGGGGddddGGG" "GGGGGGGGdBBdGGGGGdBBdGGG" "bbbbbbbbddddbbbbbddddbbb" "bbbbbbbbbbbbbbbbbbbbbbbb"},
    // --- Extras ---
    {"Heartbeat",
     "kkkkkkkkkkkkkkkkkkkkkkkk" "kkkkkkkkkkkkkrrkrrkkkkkk" "kkkkkkkkkkkkrRrrrrrkkkkk" "kkkkkkkkkkkkrrrrrrrkkkkk"
     "kkkkkkkkkkkkkrrrrrkkkkkk" "kkkkkkkkkkkkkkrrrkkkkkkk" "kkkkOkkkkkkkkkkrkkkkkkkk" "OOOOkOOOOOOOOOOOOOOOOOOO"},
    {"CutsceneRumble",
     "kkkkkkkkkkkkkkkkkkkkkkkk" "oooooooooooooooooooooooo" "OOOOOOOOOOOOOOOOyyOOOOOO" "OOOOOOOOOOOOOOOyyyyOOOOO"
     "uuuuuuuuddddduuuuuuudddd" "dddddddddddddddddddddddd" "kkkkkkkkkkkkkkkkkkkkkkkk" "kkkkkkkkkkkkkkkkkkkkkkkk"},
    {"ButtonRumble",
     "dddddddddddddddddddddddd" "ddddddddddccccccccccdddd" "ddddddddddcBBBBBccccdddd" "ddddddddddccccccccOcdddd"
     "ddddddddddcBBBBccOOOdddd" "ddddddddddccccccccOcdddd" "ddddddddddccccccccccdddd" "dddddddddddddddddddddddd"},
    {"SplashShotRumble",
     "kkkkkkkkkkkkkkkkkkkkkkkk" "kkkkkkkkkkkkkkkOkkkkkkkk" "kkkkkkkkkkkkkOkykOkkkkkk" "kkkkkkkkkkkkkkyyykkkkkkk"
     "kkkkkkkkkkkOyyyyyyyOkkkk" "kkkkkkkkkkkkkkyyykkkkkkk" "kkkkkkkkkkkkkOkykOkkkkkk" "kkkkkkkkkkkkkkkOkkkkkkkk"},
    {"AdaptiveTriggers",
     "nnnnnnnnnnnnnnnnnnnnnnnn" "nnnnnnnnnnnnneeeeeennnnn" "nnnnnnnnnnnneEEEEEEennnn" "nnnnnnnnnnnneEEEEEEennnn"
     "nnnnnnnnnnnneEOOOOEennnn" "nnnnnnnnnnnneeeeeeeennnn" "nnnnnnnnnnnnnnnnnnnnnnnn" "nnnnnnnnnnnnnnnnnnnnnnnn"},
    {"GallopingTriggersOnHorse",
     "ssssssssssssssssssssssss" "sssssssssssssssssskkssss" "sssssssssssssssskkkkssss" "sssssssssskkkkkkkkksssss"
     "ssssssssskkkkkkkkkssssss" "uuuuuuuukuuuuuuuuuukuuuu" "uuEuEuukuuuuuuuuuuuukuuu" "uuuuuuuuuuuuuuuuuuuuuuuu"},
    {"Lightbar",
     "nnnnnnnnnnnnnnnnnnnnnnnn" "nnnnnnnnnnnnnnnkknnnnnnn" "nnnnnnnnnnnnnnkkkknnnnnn" "nnnnnnnnnnnnnoyyyyonnnnn"
     "nnnnnnnnnnnnOoyyyyoOnnnn" "nnnnnnnnnnnnnoyyyyonnnnn" "nnnnnnnnnnnnnnkkkknnnnnn" "nnnnnnnnnnnnnnnnnnnnnnnn"},
};
static const char* SceneFor(const char* key) {
    for (auto& s : g_scenes) if (key && !strcmp(s.key, key)) return s.px;
    return nullptr;
}

static MenuItem g_items[] = {
    {"Rumble strength",        "Rumble",  "Intensity",          0, 100, 0, 200, 10},
    {"Response curve",         "Rumble",  "ResponseCurve",      2, 15,  5, 40,  1},
    {"Footsteps",              "Effects", "Footsteps",          4, 1,   0, 0,   4},
    {"Strength",               "Rumble",  "Footsteps",          0, 100, 0, 200, 10},
    {"Walking",                "Rumble",  "FootWalk",           1, 0},
    {"Running",                "Rumble",  "FootRun",            1, 1},
    {"Sprinting",              "Rumble",  "FootSprint",         1, 1},
    {"Horse",                  "Rumble",  "HorseRumble",        4, 1,   0, 0,   5},
    {"Strength",               "Rumble",  "Horse",              0, 100, 0, 200, 10},
    {"At a walk",              "Rumble",  "HorseWalk",          1, 0},
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
    // Advanced settings: every effect on its own switch ([Effects] in the ini, the strengths stay in
    // [Rumble]), on three tabs - each kind 5 row starts the next one. 8 cards a tab keeps the art in budget.
    {"",                       nullptr,   nullptr,              5},
    {"Footsteps",              "Effects", "Footsteps",          1, 1},
    {"Sliding",                "Effects", "Sliding",            1, 1},
    {"Falls and jumps",        "Effects", "Landing",            1, 1},
    {"Swimming",               "Effects", "Swimming",           1, 1},
    {"Water",                  "Effects", "Water",              1, 1},
    {"Doors",                  "Effects", "Doors",              1, 1},
    {"Carrying",               "Effects", "Carry",              1, 1},
    {"Eating and items",       "Effects", "Items",              1, 1},
    {"Back",                   nullptr,   nullptr,              3},
    {"",                       nullptr,   nullptr,              5},
    {"Gunshots",               "Effects", "Weapons",            1, 1},
    {"Reloading",              "Effects", "Reload",             1, 1},
    {"Weapon switch",          "Effects", "WeaponSwitch",       1, 1},
    {"Fistfights",             "Effects", "Melee",              1, 1},
    {"Getting hurt",           "Effects", "Damage",             1, 1},
    {"Animal attacks",         "Effects", "Animals",            1, 1},
    {"Horse",                  "Rumble",  "HorseRumble",        1, 1},
    {"Wagons",                 "Effects", "Vehicle",            1, 1},
    {"Back",                   nullptr,   nullptr,              3},
    {"",                       nullptr,   nullptr,              5},
    {"Heartbeat",              "Effects", "Heartbeat",          1, 1},
    {"Cutscenes",              "General", "CutsceneRumble",     1, 1},
    {"Menu clicks",            "Menu",    "ButtonRumble",       1, 1},
    {"Startup shot",           "Startup", "SplashShotRumble",   1, 1},
    {"Adaptive triggers",      "DSX",     "AdaptiveTriggers",   1, 1},
    {"Gallop triggers",        "DSX",     "GallopingTriggersOnHorse", 1, 1},
    {"Lightbar",               "DSX",     "Lightbar",           1, 1},
    {"Back",                   nullptr,   nullptr,              3},
};
static const char* const kTabs[] = {"On foot", "Fights and riding", "Extras"};
constexpr int kTabCount = 3;
constexpr int kItems = sizeof g_items / sizeof g_items[0];

static struct {
    bool  open = false, paused = false;
    int   expanded = -1;              // the open dropdown row (one at a time), -1 = none
    int   page = 0;                   // 0 = settings, 1..kTabCount = the advanced tabs
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
static bool Hidden(int i) { return g_items[i].kind == 5 || PageOf(i) != M.page || (GroupOf(i) >= 0 && GroupOf(i) != M.expanded); }

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
    MenuLoadValues();                 // the pages share some keys
    for (int i = 0; i < kItems; i++)  // a tab: its first card; back on the settings page: the "Advanced settings" row
        if (!Hidden(i) && (p > 0 || !strcmp(g_items[i].label, "Advanced settings"))) { M.sel = i; break; }
}
static void MenuTab(int d) { MenuClick(); MenuPage((M.page - 1 + d + kTabCount) % kTabCount + 1); }

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
            if (dir == 0) M.expanded = M.expanded == i ? -1 : i;
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

// Layout (0..1 screen units). The settings page is a list; the advanced tabs are wide title cards with art.
static constexpr float kPanelY = 0.04f, kRowsY = 0.185f, kTabH = 0.034f;
struct Cell { float x, y, w, h; };   // h = 0: not on screen
static Cell  g_cell[kItems];
static float g_rowsEnd;              // y just below the last row

static float PanelW() { return 0.52f / UI::aspect * (16.f / 9.f) * 0.75f; }   // ~0.39 of the width on 16:9
static void  RowsX(float& rx, float& rw) { float pw = PanelW(); rx = 0.5f - pw * 0.5f + pw * 0.06f; rw = pw * 0.88f; }

static void Layout() {
    float rx, rw; RowsX(rx, rw);
    int rows = 0;
    for (int i = 0; i < kItems; i++) rows += !Hidden(i);
    bool adv = M.page > 0;
    float top = adv ? kRowsY + kTabH + 0.012f : kRowsY;
    // settings: rows shrink when the dropdown is open, so the panel always ends above 0.95
    float step = adv ? 0.050f : std::min(0.045f, 0.725f / std::max(rows, 1)), h = step * (adv ? 0.90f : 0.89f);
    int r = 0;
    for (int i = 0; i < kItems; i++) g_cell[i] = Hidden(i) ? Cell{0, 0, 0, 0} : Cell{rx, top + r++ * step, rw, h};
    g_rowsEnd = top + rows * step;
}

// Tab t (0-based) of the advanced pages
static Cell TabCell(int t) {
    float rx, rw; RowsX(rx, rw);
    float gap = 0.006f, w = (rw - gap * (kTabCount - 1)) / kTabCount;
    return {rx + t * (w + gap), kRowsY, w, kTabH};
}
static int TabHit(float cx, float cy) {
    for (int t = 0; M.page > 0 && t < kTabCount; t++) {
        Cell c = TabCell(t);
        if (cx >= c.x && cx <= c.x + c.w && cy >= c.y && cy <= c.y + c.h) return t;
    }
    return -1;
}

// A 24x8 scene from g_scenes, ph = height of one pixel. Runs of one colour are one rect. The first run of
// each row reaches kScenePad pixels further left (free: still one rect), so the scene can fade into the card
// over plain sky and ground.
constexpr int kScenePad = 9;
static void Scene(const char* px, float x, float y, float ph, bool on) {
    if (!px) return;
    float pxw = ph / UI::aspect;      // square pixels
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 24; ) {
            char ch = px[r * 24 + c];
            int n = 1; while (c + n < 24 && px[r * 24 + c + n] == ch) n++;
            int R, G, B; Palette(ch, R, G, B);
            if (R >= 0) {
                if (!on) { int l = (R + G + B) / 3; R = (R + l) / 5; G = (G + l) / 5; B = (B + l) / 5; }   // switched off: dark, grey
                float pad = c == 0 ? kScenePad * pxw : 0.f;
                UI::Rect(x + c * pxw - pad, y + r * ph, n * pxw + pad + 0.0004f, ph + 0.0004f, R, G, B, 255);   // tiny overlap: no seams
            }
            c += n;
        }
}

static void MenuDraw(float cursorX, float cursorY) {
    using namespace UI;
    rects = 0;
    Layout();
    bool adv = M.page > 0;
    float pw = PanelW();
    float px = 0.5f - pw * 0.5f, py = kPanelY;
    const float kPanelH = g_rowsEnd - kPanelY + 0.04f;
    Rect(0, 0, 1, 1, 0, 0, 0, (int)(110 * M.anim));         // dim the game behind
    Backdrop(px, py, pw, kPanelH, 0.74f, 0.075f, true, adv);   // the tabs need the rect budget for their art: no sun there
    Rect(px, py, pw, 0.003f, 245, 230, 200, 120);
    Rect(px, py + kPanelH - 0.003f, pw, 0.003f, 245, 230, 200, 120);

    Text("CONTROLLER RUMBLE", 0.5f, py + 0.034f, 36, "title", Center, 245, 230, 200);
    Rect(0.5f - pw * 0.30f, py + 0.105f, pw * 0.60f, 0.002f, 245, 230, 200, 170);
    Text(adv ? "Advanced settings  -  " MOD_VERSION : "Settings  -  " MOD_VERSION, 0.5f, py + 0.112f, 22, "body", Center, 215, 185, 150);

    for (int t = 0; adv && t < kTabCount; t++) {
        Cell c = TabCell(t);
        bool on = t == M.page - 1;
        Rect(c.x, c.y, c.w, c.h, 15, 5, 3, on ? 225 : 120);
        if (on) Rect(c.x, c.y + c.h - 0.003f, c.w, 0.003f, 250, 175, 80, 255);
        Text(kTabs[t], c.x + c.w * 0.5f, c.y + c.h * 0.5f - 0.0088f, 17, "body", Center, on ? 250 : 215, on ? 175 : 185, on ? 80 : 150);
    }

    for (int i = 0; i < kItems; i++) {
        const Cell& cell = g_cell[i];
        if (cell.h <= 0.f) continue;
        const MenuItem& it = g_items[i];
        const float rx = cell.x, rw = cell.w, y = cell.y, kRowH = cell.h;
        bool sel = i == M.sel, child = GroupOf(i) >= 0, card = adv && it.kind == 1;
        auto border = [&] {
            if (!sel) return;
            Rect(rx, y, 0.004f, kRowH, 250, 175, 80, 255);
            Rect(rx, y, rw, 0.002f, 250, 175, 80, 160);
            Rect(rx, y + kRowH - 0.002f, rw, 0.002f, 250, 175, 80, 160);
        };
        Rect(rx, y, rw, kRowH, 15, 5, 3, card ? 235 : sel ? 215 : child ? 110 : 150);   // cards: solid, so the art fades into them
        if (!card) border();
        int tr = sel ? 250 : 245, tg = sel ? 175 : 230, tb = sel ? 80 : 200;
        float ty = y + kRowH * 0.5f - 0.011f;
        if (it.kind == 3) { Text(it.label, 0.5f, ty, 22, "body", Center, tr, tg, tb); continue; }
        if (card) {   // title card: name and On / Off on the left, its scene on the right fading into the card
            float ph = kRowH / 8.f, pxw = ph / aspect, ax = rx + rw - 24.f * pxw, fx = ax - kScenePad * pxw;
            Scene(SceneFor(it.key), ax, y, ph, it.val != 0);
            for (int k = 0; k < 5; k++)   // fade: the card colour thinning out over the plain left edge of the scene
                Rect(fx + k * 2.f * pxw, y, 2.f * pxw + 0.0004f, kRowH, 15, 5, 3, 235 - k * 47);
            ax = fx;                      // On / Off sits left of the whole scene
            border();
            int dim = it.val ? 255 : 150;
            float cy = y + kRowH * 0.5f - 0.0095f;   // smaller text on the compact cards
            Text(it.label, rx + 0.014f, cy, 19, "body", Left, tr * dim / 255, tg * dim / 255, tb * dim / 255);
            Text(it.val ? "On" : "Off", ax - 0.004f, cy, 18, "body", Right, it.val ? 250 : 160, it.val ? 175 : 140, it.val ? 80 : 120);
            continue;
        }
        float lx = rx + (child ? 0.030f : 0.012f);
        if (it.kind == 4) {   // pixel arrow: > closed, v open
            float ax = rx + 0.012f, ay = y + kRowH * 0.5f, u = 0.0028f;
            for (int k = 0; k < 4; k++) {
                if (M.expanded == i) Rect(ax + k * u * 0.6f, ay - u * 1.5f + k * u, (7 - 2 * k) * u * 0.6f, u, tr, tg, tb, 255);
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
    Text(adv ? "Left / Right or L1 / R1: tabs   -   Cross / click: on - off   -   Circle / Esc: back"
             : "Mouse: click, wheel   -   Pad: D-pad, Cross, Circle   -   F11 / Esc: close",
         0.5f, g_rowsEnd + 0.006f, 16, "body", Center, 215, 185, 150, 220);
    static int most = 0;   // RDR2 drops every rect past ~400 in a frame: log new highs to check the budget
    if (cfg.debug && rects > most) { most = rects; Log("menu: %d rects in one frame", rects); }
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
        if (KeyEdge(VK_UP))     MenuMove(-1);
        if (KeyEdge(VK_DOWN))   MenuMove(+1);
        if (KeyEdge(VK_LEFT))   { if (M.page) MenuTab(-1); else MenuChange(M.sel, -1); }   // advanced: switch tabs
        if (KeyEdge(VK_RIGHT))  { if (M.page) MenuTab(+1); else MenuChange(M.sel, +1); }
        if (KeyEdge(VK_RETURN)) MenuChange(M.sel, 0);
        if (!M.open) return false;
    }
    // --- controller ---
    uint32_t pad = g_padButtons.load(), pressed = pad & ~M.prevPad;
    M.prevPad = pad;
    if (pressed & B_CIRCLE) { if (M.page) MenuPage(0); else { MenuClose(); return false; } }
    if (pressed & B_UP)     MenuMove(-1);
    if (pressed & B_DOWN)   MenuMove(+1);
    if (pressed & B_LEFT)   { if (M.page) MenuTab(-1); else MenuChange(M.sel, -1); }
    if (pressed & B_RIGHT)  { if (M.page) MenuTab(+1); else MenuChange(M.sel, +1); }
    if (M.page && (pressed & B_L1)) MenuTab(-1);
    if (M.page && (pressed & B_R1)) MenuTab(+1);
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
    int tab = TabHit(cx, cy);
    if (click && tab >= 0 && tab != M.page - 1) { MenuClick(); MenuPage(tab + 1); }
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
