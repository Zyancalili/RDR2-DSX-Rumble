"""DualSense Native edition: PlayStation button prompts from the game's own art.

RDR2's gamepad_icons_pc.gfx (the newer copy, from the update archive) holds both icon sets: Xbox `button_N_img`
and PlayStation `ps4_button_N_img`. With Steam Input the game always draws the Xbox slots. This copies each PS4
image record (everything after the character ID) into its Xbox twin, so every Xbox slot shows the matching PS4 image.

usage: python -I tools/make_ps_prompts.py <exported gamepad_icons_pc.gfx> <output .gfx>
"""
import re
import struct
import sys

EXT_IMAGE = 1009   # Scaleform DefineExternalImage2: u16 id, u16, u16 format, u16 w, u16 h, name, file name, 0


def read_tags(d):
    nbits = d[8] >> 3
    start = 8 + (5 + 4 * nbits + 7) // 8 + 4          # RECT, frame rate, frame count
    tags, p = [], start
    while p < len(d):
        h = struct.unpack_from("<H", d, p)[0]
        code, ln, p = h >> 6, h & 63, p + 2
        long_form = ln == 63
        if long_form:
            ln, p = struct.unpack_from("<I", d, p)[0], p + 4
        tags.append([code, d[p:p + ln], long_form])
        p += ln
        if code == 0:
            break
    return d[:start], tags


def write_tags(head, tags):
    out = bytearray(head)
    for code, body, long_form in tags:
        if long_form or len(body) >= 63:
            out += struct.pack("<HI", code << 6 | 63, len(body))
        else:
            out += struct.pack("<H", code << 6 | len(body))
        out += body
    struct.pack_into("<I", out, 4, len(out))
    return bytes(out)


def image_name(body):
    n = body[10]
    return body[11:11 + n].decode("latin-1")


def main(src, dst):
    d = open(src, "rb").read()
    if d[:3] != b"GFX":
        sys.exit("not an uncompressed Scaleform file")
    head, tags = read_tags(d)
    images = {image_name(t[1]): t for t in tags if t[0] == EXT_IMAGE}
    pairs = [(n, "ps4_" + n) for n in images if re.fullmatch(r"button_\d+_img", n) and "ps4_" + n in images]
    if not pairs:
        sys.exit("no Xbox/PS4 icon pairs found: this is not the newer gamepad_icons_pc.gfx (export the larger one)")
    # Copy, not swap: the PS4 slots keep their images, so a real or DSX-emulated DualShock 4 still shows PlayStation buttons.
    for xb, ps in pairs:
        a, b = images[xb], images[ps]
        a[1] = a[1][:2] + b[1][2:]
    out = write_tags(head, tags)

    # self-check: only the Xbox image records changed, each now holding its PS4 twin's record under its own ID
    _, check = read_tags(out)
    before = read_tags(d)[1]
    assert len(check) == len(before) and struct.unpack_from("<I", out, 4)[0] == len(out)
    copied = {id(images[xb]) for xb, _ in pairs}
    for t0, t1, t in zip(before, check, tags):
        assert t0[0] == t1[0] and (id(t) in copied or t0[1] == t1[1])
    ps_ids = {image_name(t[1]) for t in check if t[0] == EXT_IMAGE}
    assert all(xb not in ps_ids for xb, _ in pairs)   # no slot draws an Xbox image any more
    open(dst, "wb").write(out)
    print(f"{len(pairs)} Xbox icons now show their PS4 twin -> {dst} ({len(out)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
