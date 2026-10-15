# CC-CEDICT offline dictionary

Community maintained free Chinese-English dictionary, published by MDBG.
CC-CEDICT contributors; referenced work: CEDICT, Copyright (C) 1997, 1998 Paul Andrew Denisowski.

- Source: https://www.mdbg.net/chinese/dictionary?page=cc-cedict
- Export: https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz
- Export date: 2026-10-14 21:50:50 GMT; 125,275 entries.
- Download SHA-256: `7870df51f14efee6c180bd69e97c672ab83eacf2eea330f5e9696c1426fb05aa`
- License: Creative Commons Attribution-ShareAlike 4.0 International, https://creativecommons.org/licenses/by-sa/4.0/ (full legal code in LICENSE.html).
- Corrections upstream: https://cc-cedict.org/editor/editor.php

FastPin's `dictionary/cedict.tsv.qz` is a derived lookup table under the same CC BY-SA 4.0 license. It retains original traditional headwords, numbered pinyin and English definitions. Traditional and simplified headwords are indexed. A reverse index uses exact English glosses of up to three words, with parenthetical qualifiers and initial infinitive `to` removed from lookup keys. Duplicate definitions are merged; results are sorted, not ranked by frequency. This is a dictionary, not sentence translation.

Rebuild with Python's standard library:

```sh
python3 tools/build-dictionary.py /path/to/cedict_1_0_ts_utf-8_mdbg.txt.gz
```

The resulting 269,744 lookup keys are stored as a qCompress-compatible binary (four-byte big-endian uncompressed length followed by zlib data), embedded in Qt resources, and decompressed only on the first lookup. The plain TSV is a local intermediate and is excluded from Git. No dictionary download or online request is required at runtime. The derived data and generator are distributed with the FastPin source repository.
