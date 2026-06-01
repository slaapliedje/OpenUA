#!/usr/bin/env python3
"""Look up a word in the FRUA manual for the game's copy-protection prompt.

FRUA's doc-check asks for "page P, paragraph G, word W" from the printed
manual. The manual PDF is a 2-up scan (two printed pages side by side per
landscape PDF page) with an embedded text layer, so no OCR is needed: this
crops the correct half with pdftotext's region option and prints the word.

Printed page P -> PDF page 4 + (P-1)//2, left half (x 0) if P is odd, right
half (x HALF) if even. Front matter is PDF pages 1-3. Override with
--pdf-page / --half if a particular page is off.

The manual is copyrighted and git-ignored under data/; this only emits the
single requested word (use --context to sanity-check counting).

Usage:
    tools/manual_lookup.py PAGE PARAGRAPH WORD
    tools/manual_lookup.py PAGE PARAGRAPH WORD --context
    tools/manual_lookup.py PAGE --show            # page numbers + paragraph count
    tools/manual_lookup.py PAGE PARAGRAPH WORD --pdf-page N --half L|R
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(__file__)
PDF = os.path.join(HERE, "..", "data", "frua_man.pdf")
PAGE_W, PAGE_H, HALF = 792, 612, 396     # landscape letter, split down the middle


def page_text(pdf_page, half):
    """Text of one printed page: PDF page `pdf_page` (1-based), half 'L'/'R'."""
    x = 0 if half == "L" else HALF
    out = subprocess.run(
        ["pdftotext", "-layout", "-f", str(pdf_page), "-l", str(pdf_page),
         "-x", str(x), "-y", "0", "-W", str(HALF), "-H", str(PAGE_H), PDF, "-"],
        capture_output=True, text=True, check=True).stdout
    return out


def locate(printed):
    """(pdf_page, half) for a printed page number, via the 2-up layout."""
    return 4 + (printed - 1) // 2, ("L" if printed % 2 == 1 else "R")


def paragraphs(text):
    paras = re.split(r"\n\s*\n", text)
    return [re.sub(r"\s+", " ", p).strip() for p in paras
            if re.sub(r"\s+", " ", p).strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("page", type=int)
    ap.add_argument("paragraph", nargs="?", type=int)
    ap.add_argument("word", nargs="?", type=int)
    ap.add_argument("--pdf-page", type=int)
    ap.add_argument("--half", choices=["L", "R"])
    ap.add_argument("--show", action="store_true",
                    help="show detected page numbers + paragraph count")
    ap.add_argument("--context", action="store_true")
    a = ap.parse_args()

    pdf_page, half = locate(a.page)
    if a.pdf_page is not None:
        pdf_page = a.pdf_page
    if a.half is not None:
        half = a.half

    text = page_text(pdf_page, half)
    paras = paragraphs(text)

    if a.show:
        nums = re.findall(r"\b\d{1,3}\b", "\n".join(text.splitlines()[:2] + text.splitlines()[-2:]))
        print(f"printed {a.page} -> PDF page {pdf_page} half {half}; "
              f"{len(paras)} paragraphs; corner numbers seen: {nums}")
        return

    if a.paragraph is None or a.word is None:
        sys.exit("need PAGE PARAGRAPH WORD")
    if not 1 <= a.paragraph <= len(paras):
        sys.exit(f"page has {len(paras)} paragraphs; asked for {a.paragraph} "
                 f"(try --show, or --pdf-page/--half)")
    words = paras[a.paragraph - 1].split(" ")
    if not 1 <= a.word <= len(words):
        sys.exit(f"paragraph has {len(words)} words; asked for {a.word}")

    if a.context:
        print(f"[PDF {pdf_page} {half}, para {a.paragraph}/{len(paras)}, "
              f"{len(words)} words]\n{paras[a.paragraph - 1]}\n-> ", end="")
    print(words[a.word - 1].strip(".,;:!?\"'()[]"))


if __name__ == "__main__":
    main()
