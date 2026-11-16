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
    memset(buf, 0, sizeof buf);   // firmware unreadable (1.8.1): Bluetooth falls back to rumble v1
    Check("G  Bluetooth, rumble v1, motors only", buf, DS5::Build(buf, true, 0, false, m),
          "31 00 10 03 00 01 FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 59 31 D0 AD");

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
