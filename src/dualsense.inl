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
