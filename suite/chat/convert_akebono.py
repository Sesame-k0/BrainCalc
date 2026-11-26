#!/usr/bin/env python3
"""Download and convert hisakata-ai/akebono-10m-chat (Apache-2.0) to TTQ8 v2.

Pure-Python implementation using only the standard library so it works in
minimal MSYS2 / Windows / Linux environments without torch/numpy/sentencepiece.
"""

import array
import hashlib
import json
import os
import struct
import sys
import urllib.request

HF_BASE_URL = "https://huggingface.co/hisakata-ai/akebono-10m-chat/resolve/main"
SAFETENSORS_NAME = "model.safetensors"
TOKENIZER_NAME = "tokenizer.model"

EXPECTED_SHA256 = {
    SAFETENSORS_NAME: "8eef42db4b29928086b022c1dc9d8ee0d23f74e725bdbd45fc56c28340c89771",
    TOKENIZER_NAME: "b5578c547c593640cd412d66d094eaf8d8fc79eb2e9ddbacc041903f94eab63b",
}

OUT_MODEL_NAME = "akebono-10m-chat_q8.bin"
OUT_TOK_NAME = "akebono-10m-chat_q8.tok"

TTQ8_MAGIC = 0x38515454  # "TTQ8" LE
TTQ8_VER_2 = 2

# Architecture constants from config.json / modeling_japanesechat10m.py / runtime.json
DIM = 256
HIDDEN_DIM = 768
N_LAYERS = 8
N_HEADS = 4
N_KV_HEADS = 4
VOCAB_SIZE = 12288
SEQ_LEN = 512
GROUP_SIZE = 32
SYSTEM_PROMPT_UTF8 = "あなたは日本語で話すAIです。自然で簡潔に返答してください。".encode("utf-8")
DISPLAY_NAME_BYTES = b"Akebono 10M"


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def ensure_downloaded(raw_dir: str, filename: str) -> str:
    dst = os.path.join(raw_dir, filename)
    expected = EXPECTED_SHA256[filename]
    if os.path.isfile(dst) and os.path.getsize(dst) > 0:
        actual = sha256_file(dst)
        if actual == expected:
            return dst
        print(f"[WARN] SHA-256 mismatch on cached {filename} ({actual}), re-downloading...")

    os.makedirs(raw_dir, exist_ok=True)
    url = f"{HF_BASE_URL}/{filename}"
    tmp = dst + ".tmp"
    print(f"[INFO] Downloading Akebono-10M asset ({filename})...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=180) as resp, open(tmp, "wb") as out:
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    actual = sha256_file(tmp)
    if actual != expected:
        os.remove(tmp)
        raise RuntimeError(f"SHA-256 verification failed for {filename}: got {actual}, expected {expected}")
    os.replace(tmp, dst)
    print(f"[INFO] Verified {filename} ({os.path.getsize(dst):,} B)")
    return dst


def read_varint(buf: bytes, idx: int) -> tuple:
    shift = 0
    val = 0
    while True:
        b = buf[idx]
        idx += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val, idx
        shift += 7


def parse_proto_fields(buf: bytes) -> list:
    idx = 0
    n = len(buf)
    fields = []
    while idx < n:
        tag, idx = read_varint(buf, idx)
        fnum = tag >> 3
        wtype = tag & 7
        if wtype == 0:
            val, idx = read_varint(buf, idx)
        elif wtype == 1:
            val = buf[idx : idx + 8]
            idx += 8
        elif wtype == 2:
            ln, idx = read_varint(buf, idx)
            val = buf[idx : idx + ln]
            idx += ln
        elif wtype == 5:
            val = buf[idx : idx + 4]
            idx += 4
        else:
            raise ValueError(f"Unsupported protobuf wire type {wtype}")
        fields.append((fnum, wtype, val))
    return fields


def convert_tokenizer(sp_path: str, out_tok_path: str) -> None:
    with open(sp_path, "rb") as f:
        data = f.read()

    pieces = []
    for fnum, wtype, val in parse_proto_fields(data):
        if fnum == 1 and wtype == 2:
            piece_bytes = b""
            score = 0.0
            for pf, pw, pv in parse_proto_fields(val):
                if pf == 1 and pw == 2:
                    piece_bytes = pv
                elif pf == 2 and pw == 5:
                    score = struct.unpack("<f", pv)[0]
            pieces.append((piece_bytes, score))

    if len(pieces) != VOCAB_SIZE:
        raise RuntimeError(f"Expected {VOCAB_SIZE} tokenizer pieces, found {len(pieces)}")

    max_len = max(len(p[0]) for p in pieces)
    tmp = out_tok_path + ".tmp"
    with open(tmp, "wb") as out:
        out.write(struct.pack("<I", max_len))
        for piece_bytes, score in pieces:
            out.write(struct.pack("<fi", score, len(piece_bytes)))
            out.write(piece_bytes)
    os.replace(tmp, out_tok_path)
    print(f"[INFO] Wrote {out_tok_path} ({len(pieces)} pieces, {os.path.getsize(out_tok_path):,} B)")


def quantize_write_q8_q15(out_file, raw_f32_bytes: bytes, rows: int, cols: int, gs: int = GROUP_SIZE) -> None:
    if cols % gs != 0:
        raise ValueError(f"cols={cols} not divisible by gs={gs}")
    arr = array.array("f")
    arr.frombytes(raw_f32_bytes)
    if sys.byteorder != "little":
        arr.byteswap()
    if len(arr) != rows * cols:
        raise ValueError(f"Expected {rows * cols} floats, got {len(arr)}")

    ng = cols // gs
    q_bytes = bytearray(rows * cols)
    s_q15 = array.array("H", [0]) * (rows * ng)
    row_scales = array.array("f", [0.0]) * rows

    # Local bindings improve loop throughput cleanly in pure Python
    mv_arr = arr
    q_idx = 0
    s_idx = 0
    for i in range(rows):
        base = i * cols
        rmax = 0.0
        row_group_scales = [0.0] * ng
        for g in range(ng):
            start = base + g * gs
            end = start + gs
            chunk = mv_arr[start:end]
            cmin = min(chunk)
            cmax = max(chunk)
            wmax = cmax if cmax >= -cmin else -cmin
            sc = wmax / 127.0
            row_group_scales[g] = sc
            if sc > rmax:
                rmax = sc
            if sc > 1e-12:
                inv = 1.0 / sc
                for v in chunk:
                    qi = round(v * inv)
                    if qi > 127:
                        qi = 127
                    elif qi < -127:
                        qi = -127
                    q_bytes[q_idx] = qi & 0xFF
                    q_idx += 1
            else:
                q_idx += gs

        row_scales[i] = rmax
        if rmax > 1e-12:
            rinv = 32767.0 / rmax
            for g in range(ng):
                sq = round(row_group_scales[g] * rinv)
                if sq < 0:
                    sq = 0
                elif sq > 32767:
                    sq = 32767
                s_q15[s_idx] = sq
                s_idx += 1
        else:
            s_idx += ng

    out_file.write(q_bytes)
    if sys.byteorder != "little":
        s_q15.byteswap()
        row_scales.byteswap()
    out_file.write(s_q15.tobytes())
    if (rows * ng) & 1:
        out_file.write(b"\x00\x00")
    out_file.write(row_scales.tobytes())


def convert_weights(safetensors_path: str, out_model_path: str) -> None:
    with open(safetensors_path, "rb") as f:
        hlen = struct.unpack("<Q", f.read(8))[0]
        hdr = json.loads(f.read(hlen).decode("utf-8"))
        data_base = 8 + hlen

        def read_tensor(name: str, expected_elems: int) -> bytes:
            meta = hdr[name]
            if meta["dtype"] != "F32":
                raise RuntimeError(f"Unexpected dtype for {name}: {meta['dtype']}")
            s, e = meta["data_offsets"]
            if e - s != expected_elems * 4:
                raise RuntimeError(f"Unexpected byte size for {name}: {e - s} vs {expected_elems * 4}")
            f.seek(data_base + s)
            return f.read(e - s)

        sys_len = len(SYSTEM_PROMPT_UTF8)
        sys_padded_len = (sys_len + 3) & ~3
        sys_padded = SYSTEM_PROMPT_UTF8 + b"\x00" * (sys_padded_len - sys_len)
        disp_padded = DISPLAY_NAME_BYTES[:31].ljust(32, b"\x00")

        # 128-byte TTQ8 v2 header
        # flags: bit0=EscapeSpaces(1) | bit1=MultiTurn(2) | bit3=SuppressCtrlTokens(8) = 0x0B
        header = struct.pack(
            "<I9i3I7i2f2I32s",
            TTQ8_MAGIC,
            TTQ8_VER_2,
            DIM,
            HIDDEN_DIM,
            N_LAYERS,
            N_HEADS,
            N_KV_HEADS,
            VOCAB_SIZE,     # positive => tied embed & lm_head
            SEQ_LEN,
            GROUP_SIZE,
            0,              # quantType = 0 (Q8_Q15)
            1,              # promptStyle = 1 (Akebono multi-turn)
            0x0B,           # flags
            2,              # bosId
            3,              # eosId
            7,              # endId
            4,              # systemId
            5,              # userId
            6,              # assistantId
            8,              # newlineId
            1e-5,           # rmsEps
            10000.0,        # ropeTheta
            sys_len,
            sys_padded_len,
            disp_padded,
        )
        assert len(header) == 128

        tmp = out_model_path + ".tmp"
        print(f"[INFO] Quantizing Akebono-10M weights -> TTQ8 v2 ({out_model_path})...")
        with open(tmp, "wb") as out:
            out.write(header)
            out.write(sys_padded)

            # RMSNorm float32 weights
            for l in range(N_LAYERS):
                out.write(read_tensor(f"blocks.{l}.attn_norm.weight", DIM))
            for l in range(N_LAYERS):
                out.write(read_tensor(f"blocks.{l}.ff_norm.weight", DIM))
            out.write(read_tensor("norm.weight", DIM))

            # Embedding / LM head [12288, 256]
            quantize_write_q8_q15(out, read_tensor("embed.weight", VOCAB_SIZE * DIM), VOCAB_SIZE, DIM)

            # Split fused QKV [3*256, 256] into separate wq, wk, wv per layer
            qkv_list = [read_tensor(f"blocks.{l}.attn.qkv.weight", 3 * DIM * DIM) for l in range(N_LAYERS)]
            mat_bytes = DIM * DIM * 4
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, qkv_list[l][0:mat_bytes], DIM, DIM)
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, qkv_list[l][mat_bytes : 2 * mat_bytes], DIM, DIM)
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, qkv_list[l][2 * mat_bytes : 3 * mat_bytes], DIM, DIM)
            del qkv_list

            # wo [256, 256]
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, read_tensor(f"blocks.{l}.attn.proj.weight", DIM * DIM), DIM, DIM)

            # Split fused SwiGLU gate_up [2*768, 256] -> w1 (gate) & w3 (up), plus w2 (down)
            gu_list = [read_tensor(f"blocks.{l}.gate_up.weight", 2 * HIDDEN_DIM * DIM) for l in range(N_LAYERS)]
            gu_bytes = HIDDEN_DIM * DIM * 4
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, gu_list[l][0:gu_bytes], HIDDEN_DIM, DIM)
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, read_tensor(f"blocks.{l}.down.weight", DIM * HIDDEN_DIM), DIM, HIDDEN_DIM)
            for l in range(N_LAYERS):
                quantize_write_q8_q15(out, gu_list[l][gu_bytes : 2 * gu_bytes], HIDDEN_DIM, DIM)

        os.replace(tmp, out_model_path)
    print(f"[INFO] Wrote {out_model_path} ({os.path.getsize(out_model_path):,} B)")


def outputs_are_valid(model_path: str, tok_path: str) -> bool:
    if not (os.path.isfile(model_path) and os.path.isfile(tok_path)):
        return False
    if os.path.getsize(model_path) < 10_000_000 or os.path.getsize(tok_path) < 180_000:
        return False
    with open(model_path, "rb") as f:
        hdr = f.read(16)
    if len(hdr) < 16:
        return False
    magic, ver, dim, hdim = struct.unpack("<I3i", hdr)
    return magic == TTQ8_MAGIC and ver == TTQ8_VER_2 and dim == DIM and hdim == HIDDEN_DIM


def main() -> int:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join("suite", "chat", "models")
    os.makedirs(out_dir, exist_ok=True)
    model_path = os.path.join(out_dir, OUT_MODEL_NAME)
    tok_path = os.path.join(out_dir, OUT_TOK_NAME)

    if outputs_are_valid(model_path, tok_path):
        print(f"[INFO] Cached Akebono-10M TTQ8 v2 assets already present ({os.path.getsize(model_path):,} B).")
        return 0

    raw_dir = os.path.join(out_dir, "raw_akebono")
    sp_path = ensure_downloaded(raw_dir, TOKENIZER_NAME)
    st_path = ensure_downloaded(raw_dir, SAFETENSORS_NAME)
    convert_tokenizer(sp_path, tok_path)
    convert_weights(st_path, model_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
