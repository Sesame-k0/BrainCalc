#!/usr/bin/env python3
"""Shrink freedoom1.wad (~28.8 MB) into Episode 1 + per-lump raw-DEFLATE freedoom1.zwad (~4-5 MB)."""

import os
import struct
import sys
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
IN_WAD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRIPT_DIR, "wad", "freedoom1.wad")
OUT_ZWAD = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SCRIPT_DIR, "wad", "freedoom1.zwad")

MAP_SUBLUMPS = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP",
}
ANIM_TEX_SEQUENCES = [
    ("BLODGR1", "BLODGR4"),
    ("SLADRIP1", "SLADRIP3"),
    ("BLODRIP1", "BLODRIP4"),
    ("FIREWALA", "FIREWALL"),
    ("GSTFONT1", "GSTFONT3"),
    ("FIRELAV3", "FIRELAVA"),
    ("FIREMAG1", "FIREMAG3"),
    ("FIREBLU1", "FIREBLU2"),
    ("ROCKRED1", "ROCKRED3"),
    ("BFALL1", "BFALL4"),
    ("SFALL1", "SFALL4"),
    ("WFALL1", "WFALL4"),
    ("DBRAIN1", "DBRAIN4"),
]
SW_EP1 = {
    "SW1BRCOM", "SW2BRCOM", "SW1BRN1", "SW2BRN1", "SW1BRN2", "SW2BRN2",
    "SW1BRNGN", "SW2BRNGN", "SW1BROWN", "SW2BROWN", "SW1COMM", "SW2COMM",
    "SW1COMP", "SW2COMP", "SW1DIRT", "SW2DIRT", "SW1EXIT", "SW2EXIT",
    "SW1GRAY", "SW2GRAY", "SW1GRAY1", "SW2GRAY1", "SW1METAL", "SW2METAL",
    "SW1PIPE", "SW2PIPE", "SW1SLAD", "SW2SLAD", "SW1STARG", "SW2STARG",
    "SW1STON1", "SW2STON1", "SW1STON2", "SW2STON2", "SW1STONE", "SW2STONE",
    "SW1STRTN", "SW2STRTN",
}


def compress_raw_deflate(data: bytes) -> bytes:
    comp = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=-15)
    return comp.compress(data) + comp.flush()


def main():
    with open(IN_WAD, "rb") as f:
        raw = f.read()

    sig, num_lumps, dir_off = struct.unpack_from("<4sII", raw, 0)
    if sig != b"IWAD":
        raise RuntimeError(f"Expected IWAD header in {IN_WAD}, got {sig!r}")

    orig_lumps = []
    lump_map = {}
    for i in range(num_lumps):
        lpos, lsz, lname = struct.unpack_from("<II8s", raw, dir_off + i * 16)
        name = lname.split(b"\x00", 1)[0].decode("ascii", "replace").upper()
        data = raw[lpos:lpos + lsz]
        orig_lumps.append((name, data))
        lump_map[name] = data

    # 1. Collect all textures referenced by E1M1..E1M9 SIDEDEFS
    e1_maps = {f"E1M{m}" for m in range(1, 10)}
    used_tex = {"AASTINKY", "SKY1"} | SW_EP1
    cur_map = None
    for name, data in orig_lumps:
        if len(name) == 4 and name[0] == "E" and name[2] == "M" and name[1].isdigit() and name[3].isdigit():
            cur_map = name
            continue
        if name not in MAP_SUBLUMPS:
            cur_map = None
            continue
        if cur_map in e1_maps and name == "SIDEDEFS":
            for off in range(0, len(data), 30):
                t1, t2, t3 = struct.unpack_from("<8s8s8s", data, off + 4)
                for tb in (t1, t2, t3):
                    tn = tb.split(b"\x00", 1)[0].decode("ascii", "replace").upper()
                    if tn and tn != "-":
                        used_tex.add(tn)

    # Also ensure SW1/SW2 toggle partners for any switch texture used in E1
    for t in list(used_tex):
        if t.startswith("SW1"):
            used_tex.add("SW2" + t[3:])
        elif t.startswith("SW2"):
            used_tex.add("SW1" + t[3:])

    # 2. Parse PNAMES and TEXTURE1 + TEXTURE2, keeping needed textures in TEXTURE1 order
    pnames_raw = lump_map["PNAMES"]
    n_pnames = struct.unpack_from("<I", pnames_raw, 0)[0]
    old_pnames = [
        pnames_raw[4 + i * 8:12 + i * 8].split(b"\x00", 1)[0].decode("ascii", "replace").upper()
        for i in range(n_pnames)
    ]

    parsed_textures = []
    tex_index_by_name = {}
    for tname in ("TEXTURE1", "TEXTURE2"):
        if tname not in lump_map:
            continue
        td = lump_map[tname]
        ntex = struct.unpack_from("<I", td, 0)[0]
        offs = struct.unpack_from(f"<{ntex}I", td, 4)
        for toff in offs:
            raw_nm = td[toff:toff + 8]
            nm = raw_nm.split(b"\x00", 1)[0].decode("ascii", "replace").upper()
            masked, w, h, obsolete, pcnt = struct.unpack_from("<ihhih", td, toff + 8)
            patches = []
            for pi in range(pcnt):
                ox, oy, pidx, stepd, cmap = struct.unpack_from("<hhhhh", td, toff + 22 + pi * 10)
                patches.append((ox, oy, old_pnames[pidx], stepd, cmap))
            if nm not in tex_index_by_name:
                tex_index_by_name[nm] = len(parsed_textures)
                parsed_textures.append((nm, masked, w, h, obsolete, patches))

    # Ensure full animation frame ranges if start texture is present
    for start_nm, end_nm in ANIM_TEX_SEQUENCES:
        if start_nm in tex_index_by_name and end_nm in tex_index_by_name:
            s_idx = tex_index_by_name[start_nm]
            e_idx = tex_index_by_name[end_nm]
            lo, hi = min(s_idx, e_idx), max(s_idx, e_idx)
            for idx in range(lo, hi + 1):
                used_tex.add(parsed_textures[idx][0])

    # Keep only needed textures (preserving original index 0 dummy texture first)
    kept_textures = []
    kept_patch_names = []
    kept_patch_set = set()
    for idx, tex in enumerate(parsed_textures):
        nm, masked, w, h, obsolete, patches = tex
        if idx == 0 or nm in used_tex:
            for _, _, pname, _, _ in patches:
                if pname not in kept_patch_set:
                    kept_patch_set.add(pname)
                    kept_patch_names.append(pname)
            kept_textures.append(tex)

    patch_to_new_idx = {pname: idx for idx, pname in enumerate(kept_patch_names)}

    # Rebuild PNAMES
    new_pnames = bytearray(struct.pack("<I", len(kept_patch_names)))
    for pname in kept_patch_names:
        new_pnames += pname.encode("ascii")[:8].ljust(8, b"\x00")

    # Rebuild TEXTURE1 (omitting TEXTURE2 saves directory overhead; R_InitTextures handles missing TEXTURE2)
    nt1 = len(kept_textures)
    header_sz = 4 + nt1 * 4
    tex_bodies = bytearray()
    tex_offsets = []
    for nm, masked, w, h, obsolete, patches in kept_textures:
        tex_offsets.append(header_sz + len(tex_bodies))
        tex_bodies += nm.encode("ascii")[:8].ljust(8, b"\x00")
        tex_bodies += struct.pack("<ihhih", masked, w, h, obsolete, len(patches))
        for ox, oy, pname, stepd, cmap in patches:
            tex_bodies += struct.pack("<hhhhh", ox, oy, patch_to_new_idx[pname], stepd, cmap)
    new_texture1 = bytes(struct.pack(f"<I{nt1}I", nt1, *tex_offsets) + tex_bodies)

    # 3. Filter WAD lumps cleanly
    filtered_lumps = []
    in_patches = False
    cur_map = None
    for name, data in orig_lumps:
        if name in ("P_START", "PP_START"):
            in_patches = True
        elif name in ("P_END", "PP_END"):
            in_patches = False

        if len(name) == 4 and name[0] == "E" and name[2] == "M" and name[1].isdigit() and name[3].isdigit():
            cur_map = name
        elif name not in MAP_SUBLUMPS:
            cur_map = None

        # Strip all audio & unused MIDI/GUS synth lookup lumps (keep M_MUSVOL menu graphic!)
        if (name.startswith("DS") or name.startswith("DP") or name.startswith("D_") or
                name in ("GENMIDI", "DMXGUS", "DMXGUSC", "SNDCURVE")):
            continue
        # Strip E2..E4 levels & multi-episode demos
        if cur_map is not None and cur_map not in e1_maps:
            continue
        if name in ("DEMO2", "DEMO3", "DEMO4", "TEXTURE2"):
            continue
        # Strip unused wall patches inside P_START..P_END (keep markers!)
        if in_patches and len(data) > 0 and name not in kept_patch_set:
            continue

        if name == "PNAMES":
            data = bytes(new_pnames)
        elif name == "TEXTURE1":
            data = new_texture1

        filtered_lumps.append((name, data))

    # 4. Compress lumps individually with raw DEFLATE and write ZWAD container
    os.makedirs(os.path.dirname(OUT_ZWAD), exist_ok=True)
    payloads = bytearray()
    directory = bytearray()
    compressed_count = 0
    for name, orig_data in filtered_lumps:
        orig_sz = len(orig_data)
        filepos = 12 + len(payloads)
        stored = orig_data
        comp_sz = orig_sz
        if orig_sz > 64:
            z = compress_raw_deflate(orig_data)
            if len(z) < orig_sz:
                stored = z
                comp_sz = len(z)
                compressed_count += 1
        payloads += stored
        directory += struct.pack("<III8s", filepos, orig_sz, comp_sz, name.encode("ascii")[:8].ljust(8, b"\x00"))

    dir_off_out = 12 + len(payloads)
    header_out = struct.pack("<4sII", b"ZWAD", len(filtered_lumps), dir_off_out)
    with open(OUT_ZWAD, "wb") as f:
        f.write(header_out)
        f.write(payloads)
        f.write(directory)

    out_bytes = len(header_out) + len(payloads) + len(directory)
    print(f"freedoom1.zwad written: {out_bytes} bytes ({out_bytes/1e6:.2f} MB), "
          f"lumps={len(filtered_lumps)} ({compressed_count} deflated), "
          f"textures={len(kept_textures)}, patches={len(kept_patch_names)}")


if __name__ == "__main__":
    main()
