#!/usr/bin/env python3

import unicodedata
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "include" / "text_width_table.hpp"
PER_LINE = 3


def ranges_for(pred):
    runs = []
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        if pred(cp):
            if runs and cp == runs[-1][1] + 1:
                runs[-1][1] = cp
            else:
                runs.append([cp, cp])
    return runs


def zero_pred(cp):
    return unicodedata.category(chr(cp)) in ("Mn", "Me", "Cf", "Cc", "Zl", "Zp")


def wide_pred(cp):
    return unicodedata.east_asian_width(chr(cp)) in ("W", "F")


zero = ranges_for(zero_pred)
zero_set = set()
for lo, hi in zero:
    zero_set.update(range(lo, hi + 1))

wide = [r for r in ranges_for(wide_pred) if r[0] not in zero_set or r[1] not in zero_set]
wide = ranges_for(lambda cp: wide_pred(cp) and cp not in zero_set)

print(f"unicode {unicodedata.unidata_version}: "
      f"wide={len(wide)} ranges, zero={len(zero)} ranges")


def emit(name, table):
    lines = [f"inline constexpr Range {name}[] = {{"]
    for i in range(0, len(table), PER_LINE):
        chunk = table[i:i + PER_LINE]
        lines.append("    " + " ".join(f"{{0x{lo:04X}, 0x{hi:04X}}}," for lo, hi in chunk))
    lines.append("};")
    return "\n".join(lines)


header = f"""// -- include/text_width_table.hpp

#ifndef TEXT_WIDTH_TABLE_HPP
#define TEXT_WIDTH_TABLE_HPP

#include <cstdint>

namespace text {{
namespace width_table {{

struct Range {{
  std::uint32_t lo;
  std::uint32_t hi;
}};

{emit("kWide", wide)}

{emit("kZero", zero)}

}} // namespace width_table
}} // namespace text

#endif // TEXT_WIDTH_TABLE_HPP
"""

OUT.write_text(header, encoding="utf-8")
print(f"wrote {OUT} ({len(header.splitlines())} lines)")
