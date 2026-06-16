#!/usr/bin/env python3
"""Generate docs/function-index.md — a complete, auto-current catalog of every C
function in the engine (src/engine/boot.c) and the shim/HAL (compat/, platform/),
with its origin (CODE N + 0xXXXX / JT[N]) and a one-line purpose pulled from the
function's own leading comment.

    python3 tools/function_index.py        # writes docs/function-index.md

Why: the engine has ~1200 functions, a mix of faithful lifts (jtNNN / lXXXX) and
port-side stand-ins (port_*, cg_*, qd_*). Without an index it's easy to re-derive
or re-implement something that already exists. This index is regenerated from the
source so it never drifts. Grep it before writing a new function.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = [
    "src/engine/boot.c", "src/engine/core.c", "src/engine/master.c",
    "compat/quickdraw.c", "compat/events.c", "compat/files.c",
    "compat/menus.c", "compat/windows.c", "compat/controls.c",
    "compat/dialogs.c", "compat/textedit.c", "compat/resources.c",
    "compat/sound.c", "compat/toolbox.c", "compat/macmemory.c",
    "platform/display_videl.c", "platform/input.c", "platform/sound_falcon.c",
    "src/main.c",
]

CODE_RE = re.compile(r'(CODE\s+\d+\s*\+\s*0x[0-9A-Fa-f]+'
                     r'|JT\[\d+\]'
                     r'|\bL[0-9a-fA-F]{3,5}\b)')
KEYWORDS = {"if", "for", "while", "switch", "else", "do", "return",
            "sizeof", "typedef", "struct", "union", "enum", "static",
            "case", "default", "goto"}


def gather_comment(lines, idx):
    """Collect a /* ... */ block whose */ is at or above line idx, skipping any
    intervening forward-declaration / __attribute__ lines (the comment usually
    sits above a function's forward decl, not the body)."""
    skipped = 0
    while idx >= 0 and skipped < 6:
        s = lines[idx].strip()
        if s == "" or s.endswith(";") or s.startswith("__attribute__") \
                or s.endswith("__attribute__((unused))"):
            idx -= 1
            skipped += 1
            continue
        break
    if idx < 0 or "*/" not in lines[idx]:
        return ""
    block = []
    while idx >= 0:
        block.insert(0, lines[idx])
        if "/*" in lines[idx]:
            break
        idx -= 1
    text = "\n".join(block)
    # strip comment markers
    text = text.replace("/*", " ").replace("*/", " ")
    text = re.sub(r'^\s*\*', " ", text, flags=re.M)
    return text


def first_sentence(comment):
    if not comment:
        return ""
    s = " ".join(comment.split())
    # drop ASCII-art separators / banner lines (===, ---, ***)
    s = re.sub(r'[=\-*]{4,}', " ", s).strip()
    # if what's left is mostly punctuation, treat as no doc
    if len(re.sub(r'[^A-Za-z]', "", s)) < 4:
        return ""
    # cut at the first sentence-ending period followed by space/cap, or 160 chars
    m = re.search(r'\.\s', s)
    if m and m.start() < 200:
        s = s[:m.start()]
    return s[:200].strip(" —-:")


def func_name(sig):
    """The identifier that immediately precedes the argument '('."""
    # drop trailing __attribute__((...)) and the arg list
    head = sig
    m = re.search(r'\b([A-Za-z_]\w*)\s*\(', head)
    last = None
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', head):
        last = m.group(1)
        break
    return last


def kind_of(name):
    if re.fullmatch(r'jt\d+', name):
        return "jt"
    if re.fullmatch(r'l[0-9a-f]{3,5}', name) or re.fullmatch(r'l[0-9a-f]{3,5}_c\d+', name):
        return "l"
    if name.startswith("port_") or name.startswith("cg_"):
        return "port"
    if name.startswith("qd_") or name.startswith("plat_") or name.startswith("dsp_"):
        return "shim"
    return "other"


def parse_file(path):
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        return []
    lines = open(full, encoding="utf-8", errors="replace").read().split("\n")
    out, n, i = [], len(lines), 0
    while i < n:
        line = lines[i]
        sig = name = None
        comment_idx = None

        # body open on its own line: backtrack for the signature
        if re.match(r'^\{', line) and i > 0:
            parts, j = [], i - 1
            while j >= 0 and i - j <= 6:
                s = lines[j].strip()
                parts.insert(0, s)
                if s.endswith(";") or s.endswith("}") or s.endswith("*/") or s == "":
                    parts.pop(0)
                    break
                if "(" in s and re.match(r'^[A-Za-z_]', s):
                    break
                j -= 1
            sigtext = " ".join(parts).strip()
            if (sigtext and sigtext.endswith(")") is False and ")" in sigtext
                    and re.search(r'[A-Za-z_]\w*\s*\(', sigtext)):
                pass
            if re.search(r'\)\s*(__attribute__\s*\(\([^)]*\)\)\s*)?$', sigtext) \
                    and re.match(r'^[A-Za-z_]', sigtext):
                sig = sigtext
                comment_idx = j - 1 if parts else i - 2

        # one-liner: `static ... ) { ... }`
        elif re.match(r'^[A-Za-z_].*\)\s*(__attribute__\s*\(\([^)]*\)\)\s*)?\{', line):
            head = line.split("{", 1)[0]
            if ";" not in head and re.search(r'[A-Za-z_]\w*\s*\(', head):
                sig = head.strip()
                comment_idx = i - 1

        if sig:
            name = func_name(sig)
            if name and name not in KEYWORDS:
                comment = gather_comment(lines, comment_idx) if comment_idx is not None else ""
                cm = CODE_RE.search(comment) or CODE_RE.search(sig)
                code = cm.group(1) if cm else ""
                code = re.sub(r'\s+', " ", code)
                out.append({
                    "name": name, "sig": re.sub(r'\s+', " ", sig),
                    "code": code, "purpose": first_sentence(comment),
                    "file": os.path.basename(path), "kind": kind_of(name),
                    "line": i + 1,
                })
        i += 1
    return out


def main():
    funcs = []
    for src in SOURCES:
        funcs += parse_file(src)
    # de-dup by name (keep the one with the richest comment)
    by_name = {}
    for f in funcs:
        cur = by_name.get(f["name"])
        if cur is None or (len(f["purpose"]) > len(cur["purpose"])):
            by_name[f["name"]] = f
    funcs = sorted(by_name.values(), key=lambda f: f["name"])

    counts = {}
    for f in funcs:
        counts[f["kind"]] = counts.get(f["kind"], 0) + 1

    out = []
    out.append("# Function index (auto-generated)\n")
    out.append("Regenerate with `python3 tools/function_index.py`. **Grep this "
               "before writing a new function** — most of what you need is already "
               "lifted. `jt` = jump-table lift, `l` = CODE-local lift, `port` = "
               "port-side stand-in (re-implementation risk), `shim` = Toolbox/HAL.\n")
    out.append("Totals: " + ", ".join("%s %d" % (k, counts[k])
               for k in sorted(counts)) + ", **%d total**.\n" % len(funcs))
    out.append("| Function | Origin | Kind | Purpose | File:line |")
    out.append("|---|---|---|---|---|")
    for f in funcs:
        purpose = f["purpose"].replace("|", "\\|")
        out.append("| `%s` | %s | %s | %s | %s:%d |" % (
            f["name"], f["code"] or "—", f["kind"],
            purpose or "—", f["file"], f["line"]))
    out.append("")

    dst = os.path.join(ROOT, "docs", "function-index.md")
    open(dst, "w", encoding="utf-8").write("\n".join(out))
    print("wrote %s: %d functions (%s)" % (
        dst, len(funcs), ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
