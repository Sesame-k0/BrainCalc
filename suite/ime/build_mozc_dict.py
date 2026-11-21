#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Builds uncompressed Mozc OSS dictionary -> suite/ime/lilium_ime.dic (LIME v2 format).
- Downloads Mozc OSS dictionary00.txt..dictionary09.txt into suite/ime/mozc_raw/ (cached).
- Filters surfaces against MisakiFont supported glyphs so 0 tofu boxes appear on Brain.
- Selects top frequency/utility Mozc entries + essential kana/symbols (~51,000 pairs, ~970 KB).
- Writes flat sorted UTF-16LE records + O(1) offset table for instant in-RAM binary search.
"""

import heapq
import os
import re
import struct
import sys
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
RAW_DIR = os.path.join(SCRIPT_DIR, "mozc_raw")
MISAKI_CPP = os.path.join(REPO_ROOT, "ion", "src", "simulator", "brain", "misaki_font_data.cpp")
DEFAULT_OUT = os.path.join(SCRIPT_DIR, "lilium_ime.dic")

MOZC_BASE_URL = "https://raw.githubusercontent.com/google/mozc/master/src/data/dictionary_oss"
MOZC_FILES = [f"dictionary{i:02d}.txt" for i in range(10)]

MAX_READING_LEN = 15
MAX_SURFACE_LEN = 24
MAX_CANDS_PER_READING = 12
TARGET_TOTAL_PAIRS = 51000


def load_misaki_glyphs():
    if not os.path.exists(MISAKI_CPP):
        raise FileNotFoundError(f"Missing {MISAKI_CPP}")
    with open(MISAKI_CPP, "r", encoding="utf-8") as f:
        text = f.read()
    codes = set(int(h, 16) for h in re.findall(r"\{\s*0x([0-9A-Fa-f]{4}),", text))
    print(f"[INFO] Loaded {len(codes)} MisakiFont supported glyphs.")
    return codes


def ensure_mozc_files():
    os.makedirs(RAW_DIR, exist_ok=True)
    for fname in MOZC_FILES:
        dst = os.path.join(RAW_DIR, fname)
        if os.path.exists(dst) and os.path.getsize(dst) > 10000:
            continue
        url = f"{MOZC_BASE_URL}/{fname}"
        print(f"[INFO] Downloading {fname} from Mozc OSS...")
        tmp = dst + ".tmp"
        urllib.request.urlretrieve(url, tmp)
        os.replace(tmp, dst)


def is_valid_hiragana_reading(s):
    if not s or len(s) > MAX_READING_LEN:
        return False
    for ch in s:
        cp = ord(ch)
        if not ((0x3041 <= cp <= 0x3093) or cp == 0x30FC):
            return False
    return True


def is_renderable_surface(s, supported_cps):
    if not s or len(s) > MAX_SURFACE_LEN:
        return False
    for ch in s:
        cp = ord(ch)
        if cp > 0xFFFF or cp not in supported_cps:
            return False
    return True


def utf16_key(s):
    return tuple(ord(c) for c in s)


def parse_and_select(supported_cps):
    # Track best (lowest) Mozc cost per (reading, surface)
    best_pairs = {}
    short_buckets = {}  # Keep high-frequency 1-2 char readings reliably

    for fname in MOZC_FILES:
        path = os.path.join(RAW_DIR, fname)
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if not line or line[0] == "#":
                    continue
                parts = line.rstrip("\r\n").split("\t")
                if len(parts) < 5:
                    continue
                reading, _lid, _rid, cost_str, surface = parts[0], parts[1], parts[2], parts[3], parts[4]
                if not is_valid_hiragana_reading(reading):
                    continue
                if not is_renderable_surface(surface, supported_cps):
                    continue
                # Skip entries identical to reading unless short or kana helper
                try:
                    cost = int(cost_str)
                except ValueError:
                    continue
                key = (reading, surface)
                prev = best_pairs.get(key)
                if prev is None or cost < prev:
                    best_pairs[key] = cost

    print(f"[INFO] Parsed {len(best_pairs)} unique valid Mozc (reading, surface) pairs.")

    # Inject/boost essential utility words, Brain/Lilium terms & punctuation so they always rank cleanly
    boost_words = [
        ("にほんご", "日本語", 1200),
        ("にほんご", "日本ご", 5000),
        ("へんしゅう", "編集", 1200),
        ("ほぞん", "保存", 1200),
        ("めも", "メモ", 1200),
        ("じかんわり", "時間割", 1200),
        ("しけん", "試験", 1200),
        ("しけん", "私見", 4200),
        ("けいさん", "計算", 1200),
        ("でんたく", "電卓", 1200),
        ("けいり", "経理", 1200),
        ("かんじ", "漢字", 1200),
        ("かな", "仮名", 1400),
        ("きょう", "今日", 1100),
        ("てんき", "天気", 1100),
        ("いい", "良い", 1400),
        ("あした", "明日", 1100),
        ("よてい", "予定", 1200),
        ("かいぎ", "会議", 1200),
        ("がっこう", "学校", 1200),
        ("だいがく", "大学", 1200),
        ("こうこう", "高校", 1200),
        ("すうがく", "数学", 1200),
        ("えいご", "英語", 1200),
        ("こくご", "国語", 1200),
        ("りか", "理科", 1200),
        ("しゃかい", "社会", 1200),
        ("じょうほう", "情報", 1200),
        ("なまえ", "名前", 1200),
        ("ふぉるだ", "フォルダ", 1200),
        ("ふぁいる", "ファイル", 1200),
        ("しんき", "新規", 1200),
        ("さくじょ", "削除", 1200),
        ("せってい", "設定", 1200),
        ("まーくだうん", "Markdown", 1200),
        ("てきすと", "テキスト", 1200),
        ("みだし", "見出し", 1200),
        ("きごう", "記号", 1200),
        ("。", "。", 800),
        ("、", "、", 800),
        ("・", "・", 800),
        ("「", "「", 800),
        ("」", "」", 800),
        ("きごう", "→", 1500),
        ("やじるし", "→", 1100),
        ("やじるし", "←", 1150),
        ("やじるし", "↑", 1200),
        ("やじるし", "↓", 1250),
        ("まる", "○", 1200),
        ("まる", "◎", 1300),
        ("ばつ", "×", 1200),
        ("ほし", "★", 1200),
        ("ほし", "☆", 1250),
        ("しかく", "■", 1200),
        ("しかく", "□", 1250),
        ("えん", "円", 1100),
        ("えん", "￥", 1400),
        ("ぱーせんと", "％", 1200),
        ("から", "〜", 1200),
        ("かっこ", "（", 1200),
        ("かっこ", "）", 1250),
        ("かっこ", "【", 1300),
        ("かっこ", "】", 1350),
    ]
    for r, s, c in boost_words:
        if is_valid_hiragana_reading(r) and is_renderable_surface(s, supported_cps):
            prev = best_pairs.get((r, s))
            if prev is None or c < prev:
                best_pairs[(r, s)] = c

    # Group by reading and keep top candidates per reading
    by_reading = {}
    for (r, s), c in best_pairs.items():
        lst = by_reading.get(r)
        if lst is None:
            by_reading[r] = [(c, s)]
        else:
            lst.append((c, s))

    # Guarantee 1-2 char kana readings get at least top 6 candidates, plus overall top TARGET_TOTAL_PAIRS
    selected_map = {}  # reading -> list of (cost, surface)
    pool = []
    for r, lst in by_reading.items():
        lst.sort(key=lambda x: (x[0], len(x[1]), x[1]))
        trimmed = lst[:MAX_CANDS_PER_READING]
        if len(r) <= 2:
            keep_n = min(len(trimmed), 6)
            selected_map[r] = trimmed[:keep_n]
            for item in trimmed[keep_n:]:
                pool.append((item[0], r, item[1]))
        else:
            # Always keep at least best 1 entry if reasonably low cost
            for idx, item in enumerate(trimmed):
                # Slight bonus for shorter/common readings & top rank
                rank_pen = idx * 450
                pool.append((item[0] + rank_pen, r, item[1]))

    current_pairs = sum(len(v) for v in selected_map.values())
    rem_budget = max(0, TARGET_TOTAL_PAIRS - current_pairs)
    pool.sort(key=lambda x: x[0])
    for _score, r, s in pool[:rem_budget]:
        lst = selected_map.setdefault(r, [])
        if len(lst) < MAX_CANDS_PER_READING and all(existing[1] != s for existing in lst):
            lst.append((_score, s))

    sorted_readings = sorted(selected_map.keys(), key=utf16_key)
    records = []
    total_pairs = 0
    for r in sorted_readings:
        cands = [s for _, s in selected_map[r]]
        if not cands:
            continue
        records.append((r, cands))
        total_pairs += len(cands)

    return records, total_pairs


def encode_record(reading, cands):
    parts = [struct.pack("<B", len(reading))]
    parts.append(reading.encode("utf-16le"))
    parts.append(struct.pack("<B", len(cands)))
    for s in cands:
        parts.append(struct.pack("<B", len(s)))
        parts.append(s.encode("utf-16le"))
    return b"".join(parts)


def write_lime_dict(out_path, records, total_pairs):
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    header_size = 24
    index_offset = header_size
    pool_offset = index_offset + len(records) * 4

    index_table = bytearray()
    pool_blob = bytearray()
    for r, cands in records:
        index_table.extend(struct.pack("<I", len(pool_blob)))
        pool_blob.extend(encode_record(r, cands))

    header = struct.pack(
        "<4sIIIII",
        b"LIME",
        2,
        len(records),
        total_pairs,
        index_offset,
        pool_offset,
    )

    tmp_path = out_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(header)
        f.write(index_table)
        f.write(pool_blob)
    os.replace(tmp_path, out_path)
    sz = os.path.getsize(out_path)
    print(f"[SUCCESS] Built LIME v2 {out_path}: {len(records)} readings, {total_pairs} pairs, "
          f"{sz:,} bytes ({sz/1024:.1f} KB)")


def self_test_lime(dic_path):
    with open(dic_path, "rb") as f:
        data = f.read()
    magic, ver, r_cnt, p_cnt, idx_off, pool_off = struct.unpack_from("<4sIIIII", data, 0)
    assert magic == b"LIME" and ver == 2, f"Bad header: {magic} v{ver}"
    assert idx_off == 24 and pool_off == idx_off + r_cnt * 4

    def read_record_at(rec_idx):
        rel_off = struct.unpack_from("<I", data, idx_off + rec_idx * 4)[0]
        pos = pool_off + rel_off
        rlen = data[pos]; pos += 1
        r = data[pos : pos + rlen * 2].decode("utf-16le"); pos += rlen * 2
        ccnt = data[pos]; pos += 1
        cands = []
        for _ in range(ccnt):
            slen = data[pos]; pos += 1
            s = data[pos : pos + slen * 2].decode("utf-16le"); pos += slen * 2
            cands.append(s)
        return r, cands

    def lookup(target):
        lo, hi = 0, r_cnt - 1
        tk = utf16_key(target)
        while lo <= hi:
            mid = (lo + hi) // 2
            r, cands = read_record_at(mid)
            rk = utf16_key(r)
            if rk == tk:
                return cands
            if rk < tk:
                lo = mid + 1
            else:
                hi = mid - 1
        return []

    for test_r, expected in [
        ("にほんご", "日本語"),
        ("じかんわり", "時間割"),
        ("へんしゅう", "編集"),
        ("きょう", "今日"),
        ("かんじ", "漢字"),
    ]:
        res = lookup(test_r)
        assert expected in res, f"Missing '{expected}' for '{test_r}': got {res}"
        print(f"  [SelfTest] {test_r} -> {res[:5]}")


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    supported = load_misaki_glyphs()
    ensure_mozc_files()
    records, total_pairs = parse_and_select(supported)
    write_lime_dict(out_path, records, total_pairs)
    self_test_lime(out_path)


if __name__ == "__main__":
    main()
