#!/usr/bin/env python3
"""Build the offline lookup table from an official CC-CEDICT UTF-8 gzip export."""
import collections
import gzip
import pathlib
import re
import sys
import struct
import zlib

entries = collections.defaultdict(set)
for line in gzip.open(sys.argv[1], "rt", encoding="utf-8"):
    match = re.fullmatch(r"(\S+) (\S+) \[(.*?)\] /(.*)/\n?", line)
    if not match:
        continue
    traditional, simplified, pinyin, glosses = match.groups()
    for key in {traditional, simplified}:
        entries[key.casefold()].add(f"{traditional} [{pinyin}] — {glosses.replace('/', '; ')}")
    for gloss in glosses.split("/"):
        key = re.sub(r"\([^)]*\)", "", gloss).strip().lower()
        if key.startswith("to "):
            key = key[3:]
        if re.fullmatch(r"[a-z]+(?:[ '-][a-z]+){0,2}", key):
            entries[key].add(f"{traditional} [{pinyin}] — {gloss}")

destination = pathlib.Path(__file__).resolve().parents[1] / "dictionary" / "cedict.tsv.qz"
destination.parent.mkdir(exist_ok=True)
data = "".join(key + "\t" + "\\n".join(sorted(values)) + "\n" for key, values in sorted(entries.items())).encode("utf-8")
# Qt qCompress format: big-endian uncompressed length followed by zlib data.
destination.write_bytes(struct.pack(">I", len(data)) + zlib.compress(data, 9))
print(f"Wrote {len(entries):,} lookup keys to {destination}")
