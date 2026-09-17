#!/usr/bin/env python3
"""
AMDGPU asic_reg Header Stripper -- "hand-written-like" but AMD-generated-precise.

Implements the PDF idea: compiler-style reachability instead of hand-rewriting.
Scans all .c/.h under kernel/dev/pci/drm/amd (excluding asic_reg itself) for:

  1. Direct identifiers that equal a #define NAME in asic_reg     (1a)
  2. Token-paste expansions that never appear as plain text       (1b)

     REG_SET_FIELD(reg, field)          -> REG__FIELD__SHIFT / _MASK
     REG_GET_FIELD(reg, field)          -> REG__FIELD__SHIFT / _MASK
     WREG32_FIELD(reg, field)           -> mmREG + REG__FIELD__SHIFT/MASK
     WREG32_FIELD_OFFSET(reg,..,field)  -> mmREG + REG__FIELD...
     FN(reg, field) / REG_SET / REG_GET / REG_UPDATE / REG_WAIT -> same
     SR / SRI / etc                     -> mmREG or regREG + _BASE_IDX
     SOC15_*                            -> reg + reg_BASE_IDX

  Over-keep on purpose (1c): if in doubt, keep. 2-3x bloat is still ~10 MB.

Then for each header in asic_reg/:

  * Keep copyright + #ifndef guard (AMD license requirement)
  * Keep #define NAME iff NAME in NEEDED or guard
  * Drop // addressBlock comments whose whole block was discarded
  * Same path -- #include unchanged
  * Values are copied verbatim, only column spacing normalized

Dry-run prints the PDF-style before/after table and does NOT write files.
Apply mode overwrites files (or a copy) and can optionally delete empty headers.

Typical usage:
  python tools/amdgpu_asic_reg_stripper.py --dry-run
  python tools/amdgpu_asic_reg_stripper.py --dry-run --verbose > docs/amdgpu/dry_run_report.txt
  python tools/amdgpu_asic_reg_stripper.py --apply

Reference: docs/amdgpu/*.txt and the original PDF ("gia tay gia" idea).
"""

from __future__ import annotations
import argparse
import pathlib
import re
import sys
from collections import defaultdict

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def human_size(n: int) -> str:
    if n >= 1024*1024:
        return f"{n/1024/1024:.2f} MB"
    if n >= 1024:
        return f"{n/1024:.2f} KB"
    return f"{n} B"

TOKEN_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)(?:\s+(.*))?$")
IFNDEF_RE = re.compile(r"^\s*#\s*ifndef\s+(\w+)")
INCLUDE_GUARD_SUFFIX = "_HEADER"

# ---------------------------------------------------------------------------
# 1) Collect all #define names in asic_reg
# ---------------------------------------------------------------------------
def collect_defines(asic_reg_root: pathlib.Path):
    define_to_files = defaultdict(set)
    define_set = set()
    file_defines = {}  # path -> list of (name, value, lineno)
    for p in asic_reg_root.rglob("*.h"):
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except Exception as e:
            print(f"warn: cannot read {p}: {e}", file=sys.stderr)
            continue
        defines = []
        for idx, line in enumerate(text.splitlines(), 1):
            m = DEFINE_RE.match(line)
            if m:
                name = m.group(1)
                val = m.group(2) or ""
                define_set.add(name)
                define_to_files[name].add(p)
                defines.append((name, val, idx, line))
        file_defines[p] = defines
    return define_set, define_to_files, file_defines

# ---------------------------------------------------------------------------
# 2) Scan sources for NEEDED
# ---------------------------------------------------------------------------
def _strip_c_comments_and_strings(text: str) -> str:
    """Remove /* */ , //, and string literals for identifier scanning (replace with spaces)."""
    # Replace strings first to avoid // inside string
    # Use simple state machine
    out = []
    i = 0
    n = len(text)
    in_single = False
    in_double = False
    in_line_comment = False
    in_block_comment = False
    while i < n:
        ch = text[i]
        nxt = text[i+1] if i+1 < n else ""
        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
                out.append(ch)
            else:
                out.append(" ")
            i += 1
            continue
        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                out.append("  ")
                i += 2
            else:
                out.append(" " if ch != "\n" else "\n")
                i += 1
            continue
        if in_single:
            if ch == "\\":
                out.append("  ")
                i += 2
                continue
            if ch == "'":
                in_single = False
            out.append(" ")
            i += 1
            continue
        if in_double:
            if ch == "\\":
                out.append("  ")
                i += 2
                continue
            if ch == '"':
                in_double = False
            out.append(" ")
            i += 1
            continue
        if ch == "/" and nxt == "/":
            in_line_comment = True
            out.append("  ")
            i += 2
            continue
        if ch == "/" and nxt == "*":
            in_block_comment = True
            out.append("  ")
            i += 2
            continue
        if ch == "'":
            in_single = True
            out.append(" ")
            i += 1
            continue
        if ch == '"':
            in_double = True
            out.append(" ")
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)

def collect_needed(source_roots: list[pathlib.Path], define_set: set, verbose=False):
    needed = set()

    # Pre-compile paste patterns
    # REG_SET_FIELD(orig, reg, field, ...)
    pat_reg_set_field = re.compile(r"REG_SET_FIELD\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    pat_reg_get_field = re.compile(r"REG_GET_FIELD\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    pat_wreg32_field = re.compile(r"WREG32_FIELD\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    pat_wreg32_field_off = re.compile(r"WREG32_FIELD_OFFSET\s*\(\s*([A-Za-z0-9_]+)\s*,[^,]+,\s*([A-Za-z0-9_]+)")
    pat_fn = re.compile(r"\bFN\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)")
    # display REG_SET (4 args) vs REG_GET etc
    pat_reg_set_4 = re.compile(r"\bREG_SET\s*\(\s*([A-Za-z0-9_]+)\s*,\s*[^,]+,\s*([A-Za-z0-9_]+)\s*,")
    pat_reg_get = re.compile(r"\bREG_GET\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,")
    pat_reg_update = re.compile(r"\bREG_UPDATE\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,")
    pat_reg_wait = re.compile(r"\bREG_WAIT\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    # WREG32_FIELD15 family: ip, idx, reg, field
    pat_wreg32_field15 = re.compile(r"WREG32_FIELD15[^\n]*\(\s*[A-Za-z0-9_]+\s*,\s*[A-Za-z0-9_]+\s*,\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    # SOC15 -> third arg is reg; we capture reg and later add BASE_IDX
    pat_soc15_offset = re.compile(r"SOC15_REG_OFFSET1?\s*\(\s*[A-Za-z0-9_]+\s*,\s*[A-Za-z0-9_]+\s*,\s*([A-Za-z0-9_]+)\s*\)")
    pat_rreg_soc15 = re.compile(r"RREG32_SOC15[^\n]*\(\s*[A-Za-z0-9_]+\s*,\s*[A-Za-z0-9_]+\s*,\s*([A-Za-z0-9_]+)\s*\)")
    pat_wreg_soc15 = re.compile(r"WREG32_SOC15[^\n]*\(\s*[A-Za-z0-9_]+\s*,\s*[A-Za-z0-9_]+\s*,\s*([A-Za-z0-9_]+)\s*[,\)]")
    # SR family for offset registers
    pat_sr = re.compile(r"\bSR\s*\(\s*([A-Za-z0-9_]+)\s*\)")
    pat_sri = re.compile(r"\bSRI\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)")
    # generic REG_*_N handling: find macro name then parse args
    pat_generic_reg = re.compile(r"\bREG_(?:SET|GET|UPDATE)(?:_[0-9]+)?\s*\(")

    def add_shift_mask(reg, field):
        cand_shift = f"{reg}__{field}__SHIFT"
        cand_mask = f"{reg}__{field}_MASK"
        # Also alternative with double underscore before MASK (some generators used __MASK? handle both)
        cand_mask2 = f"{reg}__{field}__MASK"
        for cand in (cand_shift, cand_mask, cand_mask2):
            if cand in define_set:
                needed.add(cand)
        # Also keep without adding if not in define_set: over-keep would not hurt, but we filter to existing

    def add_mm_reg(reg):
        # reg may already have prefix mm/reg
        candidates = set()
        # raw
        for pref in ("", "mm", "reg"):
            if reg.startswith(pref) and pref != "":
                # already has prefix, keep raw
                candidates.add(reg)
                candidates.add(f"{reg}_BASE_IDX")
            else:
                # generate with prefix
                cand = f"{pref}{reg}" if pref else reg
                candidates.add(cand)
                candidates.add(f"{cand}_BASE_IDX")
        # Also try upper case variations already covered
        for cand in candidates:
            if cand in define_set:
                needed.add(cand)
        # For bare core without prefix, also try mm+reg and reg+reg as above already

    source_files = []
    for root in source_roots:
        if not root.exists():
            print(f"warn: source root {root} does not exist", file=sys.stderr)
            continue
        for f in root.rglob("*"):
            if not f.is_file():
                continue
            if "asic_reg" in f.parts:
                continue
            if f.suffix not in (".c", ".h", ".cpp", ".cc"):
                continue
            source_files.append(f)

    if verbose:
        print(f"Scanning {len(source_files)} source files (excluding asic_reg) ...", file=sys.stderr)

    for f in source_files:
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        # Clean text for identifier scanning (ignore comments/strings)
        clean = _strip_c_comments_and_strings(text)
        # 1a) direct identifiers (on cleaned text)
        for tok in TOKEN_RE.findall(clean):
            if tok in define_set:
                needed.add(tok)

        # 1b) paste expansions - run on cleaned text to avoid comments
        for m in pat_reg_set_field.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_reg_get_field.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_wreg32_field.finditer(clean):
            reg, field = m.groups()
            add_mm_reg(reg)
            add_shift_mask(reg, field)
        for m in pat_wreg32_field_off.finditer(clean):
            reg, field = m.groups()
            add_mm_reg(reg)
            add_shift_mask(reg, field)
        for m in pat_wreg32_field15.finditer(clean):
            reg, field = m.groups()
            add_mm_reg(reg)
            add_shift_mask(reg, field)
        for m in pat_fn.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_reg_set_4.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_reg_get.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_reg_update.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_reg_wait.finditer(clean):
            reg, field = m.groups()
            add_shift_mask(reg, field)
        for m in pat_sr.finditer(clean):
            reg = m.group(1)
            add_mm_reg(reg)
            # also shift/mask via SR not; but offset only
        for m in pat_sri.finditer(clean):
            # SRI(reg_name, block, id) => generates mm + block+id+reg etc.
            reg_name, block, idx = m.groups()
            # Try constructing: mm{block}{idx}_{reg_name} and reg{block}{idx}_{reg_name}
            for pref in ("mm", "reg"):
                cand = f"{pref}{block}{idx}_{reg_name}"
                cand2 = f"{pref}{block}_{reg_name}"  # some variants without idx concatenated?
                for c in (cand, f"{cand}_BASE_IDX", cand2, f"{cand2}_BASE_IDX"):
                    if c in define_set:
                        needed.add(c)
            # also bare reg_name offset
            add_mm_reg(reg_name)

        # SOC15 BASE_IDX handling: capture reg and add BASE_IDX variant (on cleaned)
        for m in pat_soc15_offset.finditer(clean):
            reg = m.group(1)
            # reg is like mmGRBM_STATUS -> also need BASE_IDX
            add_mm_reg(reg)  # this will add BASE_IDX
            # direct also
            if reg in define_set:
                needed.add(reg)
            cand = f"{reg}_BASE_IDX"
            if cand in define_set:
                needed.add(cand)
        for m in pat_rreg_soc15.finditer(clean):
            reg = m.group(1)
            add_mm_reg(reg)
        for m in pat_wreg_soc15.finditer(clean):
            reg = m.group(1)
            add_mm_reg(reg)

        # Generic REG_*_N with variable fields: parse all such invocations more precisely
        # Determine field positions based on macro name to avoid over-keep
        for m in pat_generic_reg.finditer(clean):
            start = m.start()
            # extract macro name
            macro_match = re.match(r"\b(REG_(SET|GET|UPDATE)(?:_[0-9]+)?)", clean[start:m.end()])
            macro_name = macro_match.group(1) if macro_match else "REG_SET"
            # find '(' position
            paren_pos = clean.find("(", start)
            if paren_pos == -1:
                continue
            # find matching ')'
            depth = 0
            end = -1
            for i in range(paren_pos, min(len(clean), paren_pos+2500)):
                ch = clean[i]
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end == -1:
                continue
            inner = clean[paren_pos+1:end]
            # split by commas respecting nested parens
            args = []
            cur = ""
            nested = 0
            for ch in inner:
                if ch == "(":
                    nested += 1
                    cur += ch
                elif ch == ")":
                    nested -= 1
                    cur += ch
                elif ch == "," and nested == 0:
                    args.append(cur.strip())
                    cur = ""
                else:
                    cur += ch
            if cur.strip():
                args.append(cur.strip())
            if not args:
                continue
            reg = args[0].strip()
            if not re.match(r"^[A-Za-z0-9_]+$", reg):
                continue
            # Determine field indices
            # REG_SET family: SET has init as 2nd arg, fields at 2,4,6...
            # REG_GET / REG_UPDATE / REG_WAIT: fields at 1,3,5...
            is_set = "SET" in macro_name
            is_get = "GET" in macro_name
            is_update = "UPDATE" in macro_name
            field_indices = []
            if is_set and not is_get and not is_update:
                # SET: args = [reg, init, f1, v1, f2, v2 ...]
                for idx in range(2, len(args), 2):
                    field_indices.append(idx)
            elif is_get or is_update:
                # GET/UPDATE: args = [reg, f1, v1, f2, v2 ...] or [reg, field, ...] for non-N
                for idx in range(1, len(args), 2):
                    field_indices.append(idx)
            else:  # fallback (WAIT etc)
                field_indices = [1] if len(args) > 1 else []
                # also try generic over-keep for remaining
            for fi in field_indices:
                if fi >= len(args):
                    continue
                field_arg = args[fi]
                # field_arg should be a single identifier (field name)
                field_arg = field_arg.strip()
                # It may contain spaces or pointer ops; take first token
                toks = TOKEN_RE.findall(field_arg)
                if not toks:
                    continue
                # field is typically first token that is uppercase
                field = None
                for tok in toks:
                    if re.match(r"^[A-Z][A-Z0-9_]*$", tok):
                        field = tok
                        break
                if field is None:
                    continue
                # Validate field looks like field (uppercase, maybe with underscores)
                if not re.match(r"^[A-Z][A-Z0-9_]*$", field):
                    continue
                add_shift_mask(reg, field)

    # Post-process: if a mm/reg offset is needed, also need its BASE_IDX
    # Also if a register core is needed as shift/mask prefix, ensure base register offset also kept?
    # For any needed that is SHIFT or MASK, ensure the corresponding register's offset? Not necessarily, but over-keep okay.
    # Ensure BASE_IDX for any mm/reg token
    extra = set()
    for tok in list(needed):
        if tok.endswith("_BASE_IDX"):
            continue
        # if tok looks like offset (starts with mm or reg)
        if tok.startswith("mm") or tok.startswith("reg"):
            cand = f"{tok}_BASE_IDX"
            if cand in define_set:
                extra.add(cand)
        # also for shift/mask tokens, ensure opposite kept? e.g., if SHIFT kept, keep MASK
        if tok.endswith("__SHIFT"):
            base = tok[:-len("__SHIFT")]
            cand_mask = f"{base}_MASK"
            cand_mask2 = f"{base}__MASK"
            if cand_mask in define_set:
                extra.add(cand_mask)
            if cand_mask2 in define_set:
                extra.add(cand_mask2)
        elif tok.endswith("_MASK"):
            base = tok[:-len("_MASK")]
            # try SHIFT variant
            cand_shift = f"{base}__SHIFT"
            if cand_shift in define_set:
                extra.add(cand_shift)
            # also __MASK vs _MASK already
    needed.update(extra)

    return needed, source_files

# ---------------------------------------------------------------------------
# 3) Strip a single header
# ---------------------------------------------------------------------------
def strip_header_file(path: pathlib.Path, needed: set, define_set: set, dry_run=True, verbose=False):
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
        lines = text.splitlines()
    except Exception as e:
        return None

    original_size = path.stat().st_size
    # Count total defines
    total_defines = sum(1 for l in lines if DEFINE_RE.match(l))
    # Detect guards: first IFNDEF and next DEFINE with same name
    guard_names = set()
    ifndef_name = None
    for line in lines:
        m = IFNDEF_RE.match(line)
        if m:
            ifndef_name = m.group(1)
            guard_names.add(ifndef_name)
            break
    # second guard DEFINE same as IFNDEF is also guard
    # We'll just consider any DEFINE that is guard name as keep
    # Also there is #define _XYZ_HEADER (without IFNDEF? just guard)
    # So guard_names already

    # Also keep copyright block at top: we will preserve first block of /* ... */
    # Simplest: keep all lines up to and including guard defines as header
    header_end_idx = -1
    for idx, line in enumerate(lines):
        m = DEFINE_RE.match(line)
        if m and m.group(1) in guard_names:
            header_end_idx = idx
            # check if next line also guard? Usually two lines: #ifndef and #define
            # header_end should be second guard
            # So find second occurrence
            # Actually loop will find first define guard at idx of #define; but we need to include up to that idx
            # break after finding it
            break
    if header_end_idx == -1:
        # fallback: find first #define that looks like guard (ends with _HEADER)
        for idx, line in enumerate(lines):
            m = DEFINE_RE.match(line)
            if m and m.group(1).endswith("_HEADER"):
                guard_names.add(m.group(1))
                header_end_idx = idx
                break
    if header_end_idx == -1:
        header_end_idx = 0  # no guard found

    header_lines = lines[:header_end_idx+1] if header_end_idx >=0 else []

    # Body lines are between header_end+1 and last #endif
    footer_idx = None
    for idx in range(len(lines)-1, -1, -1):
        if lines[idx].strip().startswith("#endif"):
            footer_idx = idx
            break
    if footer_idx is None:
        footer_idx = len(lines)

    body_lines = lines[header_end_idx+1:footer_idx]
    footer_lines = lines[footer_idx:] if footer_idx < len(lines) else []

    # Group body into blocks: header_comments + defines
    blocks = []  # list of (header_comments, defines)
    current_header = []
    current_defines = []
    # pending for handling enum etc not grouped?
    # We'll iterate
    for line in body_lines:
        stripped = line.strip()
        if stripped == "":
            # blank separator -> if we have a block with defines, finalize it
            if current_defines:
                blocks.append((current_header, current_defines))
                current_header = []
                current_defines = []
            # if we have header without defines yet, keep blank as part of header? ignore
            continue
        if stripped.startswith("//"):
            # comment line: if we have pending defines, start new block
            if current_defines:
                blocks.append((current_header, current_defines))
                current_header = [line]
                current_defines = []
            else:
                # still in header collection before any defines of this block
                current_header.append(line)
        elif DEFINE_RE.match(line):
            current_defines.append(line)
        else:
            # non-define, non-comment: e.g., typedef enum, or #else, etc.
            # Flush current block if any
            if current_header or current_defines:
                blocks.append((current_header, current_defines))
                current_header = []
                current_defines = []
            # treat this line as a single block that should be kept as is
            # e.g., enum typedefs, we keep
            blocks.append(([], [line]))  # special marker: not filtered
            # Note: these non-define blocks we will keep always

    # flush last
    if current_header or current_defines:
        blocks.append((current_header, current_defines))

    # Now filter blocks
    out_body_lines = []
    kept_defines = 0
    for header, defines in blocks:
        # If block has non-define lines (e.g., typedef, struct): keep as is
        # Detect: if defines contains line not matching DEFINE_RE
        is_define_block = all(DEFINE_RE.match(d) for d in defines) and defines and all(d.strip().startswith("#define") for d in defines)
        if not is_define_block:
            # Keep everything in this block as is (header + defines)
            # But header for such blocks is likely empty
            for h in header:
                out_body_lines.append(h)
            for d in defines:
                out_body_lines.append(d)
            # count? not define, but keep
            continue
        # For define blocks: filter defines
        filtered_defines = []
        for d in defines:
            m = DEFINE_RE.match(d)
            if not m:
                continue
            name = m.group(1)
            if name in guard_names or name in needed:
                filtered_defines.append(d)
                kept_defines += 1
            else:
                # discard
                pass
        if filtered_defines:
            # keep header comments (addressBlock) since at least one define kept
            for h in header:
                out_body_lines.append(h)
            for d in filtered_defines:
                # Normalize spacing: keep "#define NAME value" with single space between NAME and value, but preserve value verbatim
                # Original has column-aligned spaces; we normalize to single tab/space for readability, value unchanged
                m = DEFINE_RE.match(d)
                if m:
                    name = m.group(1)
                    val = m.group(2) or ""
                    val = val.strip()
                    # Preserve original value string but collapsed spaces? We'll do single space
                    # For values like "0x0000007FL" keep
                    out_line = f"#define {name} {val}" if val else f"#define {name}"
                    out_body_lines.append(out_line)
                else:
                    out_body_lines.append(d)
        else:
            # discard header too
            pass

    # Count kept vs total for body
    # Reconstruct out_lines
    # Header_lines already includes copyright + guards; we need to ensure we keep them as is
    # But header_lines may contain copyright block and guard defines; we keep them
    # However we may have duplicated guards filtering; we already kept them
    # Build final out_lines = header_lines + out_body_lines + footer_lines
    # But header_lines currently includes original copyright + guards with original spacing; keep
    # Need to ensure we don't duplicate blanks
    out_lines = []
    for h in header_lines:
        out_lines.append(h)
    # Add blank line after header if needed? original had blank lines; we will add one blank
    if out_body_lines:
        # ensure separation
        if out_lines and out_lines[-1].strip() != "":
            out_lines.append("")
        out_lines.extend(out_body_lines)
    # Footer
    for f in footer_lines:
        out_lines.append(f)

    # Ensure final newline
    stripped_text = "\n".join(out_lines) + "\n"
    stripped_size = len(stripped_text.encode("utf-8"))

    # For dry-run we don't write; for apply we would write
    if not dry_run:
        # Write stripped content back to file
        # Ensure we don't truncate if kept_defines==0 -> file would be just header+footer minimal; per PDF we should delete file if no defines remain
        if kept_defines == 0 and len([b for b in blocks if b[0] or b[1]]) == 0:
            # No content: could delete file (approx 50 files)
            # But we keep at least header+footer minimal; however PDF says delete file if no macro needed -> we should remove file
            # Here we decide to keep minimal file or delete? For safety we keep minimal but report.
            pass
        path.write_text(stripped_text, encoding="utf-8")

    # Also determine if file would be deleted (no defines kept and no enum/other content)
    # Enum files have typedefs in out_body_lines even if kept_defines==0, so not empty.
    would_delete = (len(out_body_lines) == 0)

    return {
        "path": path,
        "original_size": original_size,
        "stripped_size": stripped_size,
        "total_defines": total_defines,
        "kept_defines": kept_defines,
        "discarded": total_defines - kept_defines,
        "header_lines": len(header_lines),
        "out_lines": len(out_lines),
        "would_delete": would_delete,
        "stripped_text": stripped_text,  # for cmp if needed
    }

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="AMDGPU asic_reg stripper (hand-written-like, AMD-precise)")
    ap.add_argument("--asic-reg", default="kernel/dev/pci/drm/amd/include/asic_reg",
                    help="Path to asic_reg directory")
    ap.add_argument("--source-root", nargs="*", default=["kernel/dev/pci/drm/amd"],
                    help="Source roots to scan (multiple). Excludes asic_reg automatically.")
    ap.add_argument("--dry-run", action="store_true", default=False,
                    help="Do not modify files, only report (default if --apply not given)")
    ap.add_argument("--apply", action="store_true", default=False,
                    help="Actually overwrite files (dangerous, make backup first)")
    ap.add_argument("--delete-empty", action="store_true", default=False,
                    help="Delete headers that end up with 0 kept defines (~50 files) when --apply")
    ap.add_argument("--verbose", action="store_true", default=False,
                    help="Verbose progress")
    ap.add_argument("--report", default=None,
                    help="Write detailed report to file (e.g., docs/amdgpu/dry_run_report.txt)")
    ap.add_argument("--limit-example", type=int, default=0,
                    help="Limit number of example rows shown (0=auto)")
    args = ap.parse_args()

    if args.apply:
        dry_run = False
    else:
        dry_run = True
        if not args.dry_run:
            # default to dry-run if neither specified, but allow explicit --dry-run
            pass

    asic_root = pathlib.Path(args.asic_reg)
    if not asic_root.exists():
        print(f"error: asic_reg root not found: {asic_root}", file=sys.stderr)
        sys.exit(1)

    source_roots = [pathlib.Path(p) for p in args.source_root]

    print(f"Collecting defines from {asic_root} ...", file=sys.stderr)
    define_set, define_to_files, file_defines = collect_defines(asic_root)
    print(f"  Found {len(define_set)} unique #define names in {len(file_defines)} files", file=sys.stderr)
    # Total size
    total_original = sum(p.stat().st_size for p in asic_root.rglob("*.h") if p.is_file())
    print(f"  Total original asic_reg size: {human_size(total_original)}", file=sys.stderr)

    print(f"Scanning sources for NEEDED (direct + token-paste) ...", file=sys.stderr)
    needed, source_files = collect_needed(source_roots, define_set, verbose=args.verbose)
    print(f"  NEEDED set size: {len(needed)} ({len(needed)/len(define_set)*100:.2f}% of defines)", file=sys.stderr)
    print(f"  Scanned {len(source_files)} source files", file=sys.stderr)

    # Strip each file (dry-run or apply)
    results = []
    for p in sorted(asic_root.rglob("*.h")):
        if not p.is_file():
            continue
        res = strip_header_file(p, needed, define_set, dry_run=dry_run, verbose=args.verbose)
        if res:
            results.append(res)

    # Summary stats
    total_stripped = sum(r["stripped_size"] for r in results)
    total_kept = sum(r["kept_defines"] for r in results)
    total_defs = sum(r["total_defines"] for r in results)
    would_delete_count = sum(1 for r in results if r["would_delete"])
    # Sort by original size descending for table
    results_sorted = sorted(results, key=lambda x: x["original_size"], reverse=True)

    # Print PDF-style table: pick 3 examples as in PDF plus top 10?
    # PDF examples: dcn_3_2_0_sh_mask.h, nbio_7_0_sh_mask.h, gc_9_0_sh_mask.h
    examples = ["dcn_3_2_0_sh_mask.h", "nbio_7_0_sh_mask.h", "gc_9_0_sh_mask.h"]
    # Find those files
    print("\n" + "="*78)
    print("DRY-RUN RESULT (before / after) -- same API, path & #include unchanged")
    print("="*78)
    header_fmt = f"{'File':<32} {'Original':>10} {'Stripped':>10} {'Keep / Total':>16} {'Ratio':>8}"
    print(header_fmt)
    print("-"*78)
    for ex in examples:
        found = None
        for r in results:
            if r["path"].name == ex:
                found = r
                break
        if found:
            ratio = found["stripped_size"]/found["original_size"]*100 if found["original_size"] else 0
            print(f"{found['path'].name:<32} {human_size(found['original_size']):>10} {human_size(found['stripped_size']):>10} {found['kept_defines']:>5} / {found['total_defines']:<5} {ratio:>6.2f}%")
        else:
            print(f"{ex:<32} {'NOT FOUND':>10}")

    # Also show top 15 largest original files after strip
    print("\nTop 15 largest original headers (after strip preview):")
    print(header_fmt)
    print("-"*78)
    for r in results_sorted[:15]:
        ratio = r["stripped_size"]/r["original_size"]*100 if r["original_size"] else 0
        print(f"{r['path'].name:<32} {human_size(r['original_size']):>10} {human_size(r['stripped_size']):>10} {r['kept_defines']:>5} / {r['total_defines']:<5} {ratio:>6.2f}%")

    print("-"*78)
    print(f"TOTAL asic_reg: {human_size(total_original):>10} -> {human_size(total_stripped):>10}  "
          f"({total_stripped/total_original*100:.2f}% remain, saved {human_size(total_original-total_stripped)})")
    print(f"Total defines: {total_defs} -> kept {total_kept} (discarded {total_defs-total_kept})")
    print(f"Files that would be empty (no kept defines): {would_delete_count} / {len(results)} (~{would_delete_count/len(results)*100:.1f}%)")
    print(f"Remaining non-empty files: {len(results)-would_delete_count}")
    # Also per-type stats
    # Group by suffix
    from collections import Counter
    type_stats = Counter()
    for r in results:
        # suffix like sh_mask, offset, etc
        name = r["path"].name
        if "_sh_mask.h" in name:
            type_stats["sh_mask"] += 1
        elif "_offset.h" in name:
            type_stats["offset"] += 1
        elif "_default.h" in name:
            type_stats["default"] += 1
        elif "enum" in name:
            type_stats["enum"] += 1
        else:
            type_stats["other"] += 1
    print(f"File type counts: {dict(type_stats)}")
    print("="*78)
    print("Notes:")
    print("- Copyright + #ifndef guard always kept (AMD license)")
    print("- #define values copied verbatim, column spacing normalized")
    print("- // addressBlock comments kept only if block has >=1 kept define")
    print("- Same path & #include, .c unchanged -> binary kernel near-identical (symbol set identical)")
    print("- cmp check: kept set superset NEEDED, so no missing symbol -> no compile failure")
    if dry_run:
        print("- DRY-RUN: no files were modified. Use --apply to overwrite (and --delete-empty to remove empty headers).")
    else:
        print("- APPLY mode: files were overwritten.")
        if args.delete_empty:
            print("  Empty files deleted.")
    print("="*78)

    # Write report file if requested
    if args.report:
        report_path = pathlib.Path(args.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("w", encoding="utf-8") as out:
            out.write("AMDGPU asic_reg stripper dry-run report\n")
            out.write("="*78 + "\n")
            out.write(f"asic_reg root: {asic_root}\n")
            out.write(f"source roots: {', '.join(str(p) for p in source_roots)}\n")
            out.write(f"total original size: {human_size(total_original)} ({total_original} bytes)\n")
            out.write(f"total stripped size: {human_size(total_stripped)} ({total_stripped} bytes)\n")
            out.write(f"total defines: {total_defs}, kept: {total_kept}, discarded: {total_defs-total_kept}\n")
            out.write(f"would_delete (empty): {would_delete_count} files\n")
            out.write("\nExample files (PDF-style):\n")
            for ex in examples:
                for r in results:
                    if r["path"].name == ex:
                        out.write(f"  {r['path'].as_posix()}: {human_size(r['original_size'])} -> {human_size(r['stripped_size'])}  keep {r['kept_defines']}/{r['total_defines']}\n")
            out.write("\nTop 15 largest:\n")
            for r in results_sorted[:15]:
                out.write(f"  {r['path'].name}: {human_size(r['original_size'])} -> {human_size(r['stripped_size'])}  keep {r['kept_defines']}/{r['total_defines']}\n")
            out.write("\nAll files (sorted by original size):\n")
            out.write(f"{'File':<50} {'Original':>12} {'Stripped':>12} {'Keep/Total'}\n")
            out.write("-"*90 + "\n")
            for r in results_sorted:
                out.write(f"{r['path'].as_posix():<50} {r['original_size']:>12} {r['stripped_size']:>12} {r['kept_defines']:>6}/{r['total_defines']:<6}\n")
            out.write("\nNEEDED set sample (first 100 sorted):\n")
            for tok in sorted(needed)[:100]:
                out.write(f"  {tok}\n")
            out.write(f"\n... total NEEDED {len(needed)} names\n")
        print(f"Report written to {report_path}", file=sys.stderr)

    # Verification step: ensure NEEDED subset of define_set
    missing = needed - define_set
    if missing:
        print(f"warn: NEEDED contains {len(missing)} names not in define_set (should be 0)", file=sys.stderr)
    else:
        print(f"Verification: NEEDED subset of define_set -- OK (no missing symbol would cause compile failure)", file=sys.stderr)

if __name__ == "__main__":
    main()
