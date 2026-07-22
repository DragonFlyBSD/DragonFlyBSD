#!/usr/bin/env python3
\"\"\"Convert monospaced BDF to syscons raw + C header (256 glyphs).\"\"\"
from __future__ import annotations
import argparse, re
from pathlib import Path

def parse_bdf(path: Path, max_enc: int = 256):
    text = path.read_text(errors=\"replace\")
    m = re.search(r\"FONTBOUNDINGBOX\\s+(\\d+)\\s+(\\d+)\", text)
    if not m:
        raise SystemExit(\"no FONTBOUNDINGBOX\")
    fw, fh = int(m.group(1)), int(m.group(2))
    bpr = (fw + 7) // 8
    glyphs = {i: bytes(fh * bpr) for i in range(max_enc)}
    for part in re.split(r\"\\nSTARTCHAR \", text)[1:]:
        em = re.search(r\"^ENCODING\\s+(-?\\d+)\", part, re.M)
        if not em:
            continue
        enc = int(em.group(1))
        if enc < 0 or enc >= max_enc:
            continue
        bm = re.search(r\"BITMAP\\n(.*?)\\nENDCHAR\", part, re.S)
        if not bm:
            continue
        rows = []
        for line in bm.group(1).strip().splitlines():
            line = line.strip()
            if not line:
                continue
            hexlen = bpr * 2
            if len(line) < hexlen:
                line = line + \"0\" * (hexlen - len(line))
            rows.append(bytes.fromhex(line[:hexlen]))
        if len(rows) < fh:
            rows.extend([bytes(bpr)] * (fh - len(rows)))
        glyphs[enc] = b\"\".join(rows[:fh])
    return fw, fh, bpr, glyphs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(\"bdf\", type=Path)
    ap.add_argument(\"--raw\", type=Path)
    ap.add_argument(\"--header\", type=Path)
    ap.add_argument(\"--name\", default=\"dflt_font\")
    args = ap.parse_args()
    fw, fh, bpr, glyphs = parse_bdf(args.bdf)
    raw = bytearray()
    for i in range(256):
        raw.extend(glyphs[i])
    if args.raw:
        args.raw.write_bytes(raw)
        print(\"raw\", args.raw, len(raw))
    if args.header:
        with args.header.open(\"w\") as f:
            f.write(\"/* Generated from %s */\\n\" % args.bdf.name)
            f.write(\"static const u_char %s[%d*%d*256] = {\\n\" % (args.name, fh, bpr))
            for i in range(256):
                g = glyphs[i]
                f.write(\"\\t/* U+%04X */\\n\\t\" % i)
                f.write(\", \".join(\"0x%02x\" % b for b in g))
                f.write(\",\\n\")
            f.write(\"};\\n\")
        print(\"header\", args.header)
    print(\"geometry %dx%d bpr=%d\" % (fw, fh, bpr))

if __name__ == \"__main__\":
    main()
