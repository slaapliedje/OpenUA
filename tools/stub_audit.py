#!/usr/bin/env python3
"""Reconcile every "this is a stub" claim in src/engine/boot.c against the body.

Stale stub comments are not cosmetic: three separate sessions have gone hunting
for work that did not exist because a header said "PROBE stub pending its own
lift" over a function that had been fully lifted months earlier (l3ac6/l40b4,
jt985/l11a2, and the whole l076e/l08b4 combat chain). This is the check that
stops it recurring; run it whenever a stub is lifted.

    python3 tools/stub_audit.py            # report
    python3 tools/stub_audit.py --quiet    # exit 1 if any stale claim exists

Classifying a body as a STUB is the delicate part, and a false "REAL" is the
dangerous direction — it would flag an ACCURATE comment as stale and invite
someone to "fix" it into a lie. So the stub test is deliberately generous: a
body is a stub if every statement is bookkeeping (a PROBE/dbg_log, a (void)
cast, a constant return, or writing a constant into an out-param). Anything
that calls another function, branches, or loops is REAL.
"""
import argparse
import re
import sys

SRC = 'src/engine/boot.c'

FUNC_RE = re.compile(r'^static\s+[\w \*]+?\**(\w+)\s*\(')
NAME_RE = re.compile(r'\b([lL][0-9a-f]{4}(?:_c\d+)?|jt\d{1,4})\b')

# A claim that some function is CURRENTLY a stub.
CLAIM_RE = re.compile(
    r'[^.;]*\b(PROBE stub|leaf stub|stubs?\b[^.;]{0,40}?(?:pending|for now|here)|'
    r'pending (?:its|their) own lift|is a stub|are stubs|'
    r'lands? as (?:a )?PROBE stubs?|not yet lifted|deferred lift)[^.;]*', re.I)

# ...unless it is talking about the PAST ("was a stub, now lifted").
HIST_RE = re.compile(
    r'\bwas\b|\bwere\b|\bold\b|earlier|used to|no longer|already (?:fully )?lifted|'
    r'\bnow\b|since lifted|has been lifted|An earlier revision|full body over|'
    r'full lift over|full CFG over|full call shape over', re.I)

# Statements that do not count as "doing something".
TRIVIAL = [
    re.compile(r'^PROBE\('),
    re.compile(r'^dbg_log'),
    # (void) casts of a plain NAME are bookkeeping; (void)f(x) is a real CALL
    re.compile(r'^(\(void\)\s*[\w\[\]\.\->]+\s*;\s*)+$'),
    re.compile(r'^return\s*[^;]*;$'),                   # see is_const_return
    re.compile(r'^\*?\w+(\[\w*\]|->\w+|\.\w+)*\s*=\s*[^;=]+;$'),  # see is_const_store
]
CONST_RET = re.compile(r'^return\s*(\(\s*\w+\s*\)\s*)?-?(\d+|noErr|NULL|nil)?\s*;$')
CONST_STORE = re.compile(
    r'^\*?\(?\*?\w+\)?(\[\s*\d+\s*\])?\s*=\s*\(?\s*-?\s*\d+\s*\)?\s*;$')


def load(path):
    return open(path).read().split('\n')


ONE_LINE = re.compile(r'^static\s+[\w \*]+?\**(\w+)\s*\([^;]*\)\s*\{.*\}')

# Braces also live inside CHARACTER LITERALS — `case '{':` is exactly how the
# CODE-8 word-wrap char classes (l2dca/l2d5e) spell their punctuation arms. A
# naive count sees that as an unbalanced brace, ends the function early, and
# then desynchronises every function after it (lifting l2dca cost the parser
# 570 of boot.c's 2127 functions and silently "removed" 25 stubs). Strip
# literals and inline comments before counting.
LITERAL = re.compile(r"'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\"")
INLINE_COMMENT = re.compile(r'/\*.*?\*/|//.*$')


def depth_delta(line):
    """Net brace depth contributed by a line, ignoring literals/comments."""
    code = INLINE_COMMENT.sub('', LITERAL.sub('', line))
    return code.count('{') - code.count('}')


def parse_funcs(lines):
    """(name, sig_idx, open_idx, close_idx) for every DEFINITION.

    Handles the one-line form too — `static void jt510(void) { PROBE("jt510"); }`
    — which the brace-matcher alone would skip, leaving those functions absent
    from the status map and their stub claims misattributed."""
    funcs, i = [], 0
    while i < len(lines):
        m1 = ONE_LINE.match(lines[i])
        if m1:
            funcs.append((m1.group(1), i, i, i))
            i += 1
            continue
        m = FUNC_RE.match(lines[i])
        if not m:
            i += 1
            continue
        j, ok = i, True
        while j < len(lines) and j - i < 6:
            # A forward declaration can end ");  /* comment */", which does not
            # end in ';' — strip the trailing comment before deciding.
            st = re.sub(r'/\*.*?\*/\s*$', '', lines[j]).rstrip()
            if st.endswith('{'):
                break
            if st.endswith(';'):
                ok = False
                break
            j += 1
        if not ok or j >= len(lines) or not lines[j].rstrip().endswith('{'):
            i += 1
            continue
        k, depth = j + 1, 1
        while k < len(lines) and depth > 0:
            depth += depth_delta(lines[k])
            k += 1
        funcs.append((m.group(1), i, j, k - 1))
        i = k
    return funcs


def classify(lines, op, cl):
    """STUB only if EVERY statement is bookkeeping. Bias towards REAL."""
    if op == cl:                                   # one-liner: body is inside {}
        inner = lines[op]
        inner = inner[inner.index('{') + 1:inner.rindex('}')]
        body = [x.strip() + ';' for x in inner.split(';') if x.strip()]
        for t in body:
            if TRIVIAL[0].match(t) or TRIVIAL[1].match(t) or TRIVIAL[2].match(t):
                continue
            if t.startswith('return') and CONST_RET.match(t):
                continue
            if CONST_STORE.match(t):
                continue
            return 'REAL'
        return 'STUB'
    incomment = False
    for raw in lines[op + 1:cl]:
        t = raw.strip()
        # Track block-comment state properly. A continuation line starts with
        # '*' — but so does a POINTER DEREFERENCE (`*(char *)p = 1;`), and
        # treating those as comments hid real bodies (jt321) behind a STUB.
        if incomment:
            if '*/' in t:
                incomment = False
                t = t.split('*/', 1)[1].strip()
            else:
                continue
        while t.startswith('/*'):
            if '*/' in t:
                t = t.split('*/', 1)[1].strip()
            else:
                incomment = True
                t = ''
                break
        if not t or t.startswith('//') or t.startswith('#') or t in ('{', '}'):
            continue
        if TRIVIAL[0].match(t) or TRIVIAL[1].match(t):
            continue
        if TRIVIAL[2].match(t):                    # (void) casts
            continue
        if t.startswith('return'):
            if CONST_RET.match(t):
                continue
            return 'REAL'                          # returns a computed value
        if 'g_a5_' in t:
            return 'REAL'      # writing an A5 global IS the work (jt174, jt321)
        if CONST_STORE.match(t):                   # *out = -1;  buf[0] = 0;
            continue
        return 'REAL'
    return 'STUB'


def doc_above(lines, sig_idx):
    k = sig_idx - 1
    while k >= 0 and (lines[k].strip() == '' or
                      re.match(r'^static .*;\s*$', lines[k].strip())):
        k -= 1
    if k < 0 or not lines[k].rstrip().endswith('*/'):
        return None, None
    end = k
    while k >= 0 and not lines[k].lstrip().startswith('/*'):
        k -= 1
    if k < 0:
        return None, None
    return k, end


def audit(path=SRC):
    lines = load(path)
    funcs = parse_funcs(lines)
    status = {n: classify(lines, op, cl) for n, _, op, cl in funcs}

    stale, seen = [], set()
    for name, sig, op, cl in funcs:
        cs, ce = doc_above(lines, sig)
        if cs is None:
            continue
        flat = re.sub(r'\s*\*\s*', ' ',
                      '\n'.join(lines[cs:ce + 1]).replace('/*', ' ').replace('*/', ' '))
        for mt in CLAIM_RE.finditer(flat):
            sent = ' '.join(mt.group(0).split())
            if HIST_RE.search(sent):
                continue
            # WHICH function is being called a stub? Only one named BEFORE the
            # stub phrase is its subject ("jt592 is a PROBE stub"). A name after
            # it is context ("PROBE stub, so jt349 skips the header row") and is
            # NOT being claimed to be a stub — attributing it there produced
            # false positives on accurate comments.
            kw = re.search(r'\bstub|not yet lifted|pending', sent, re.I)
            head = sent[:kw.start()] if kw else sent
            named = [x.lower() for x in NAME_RE.findall(head)]
            targets = [t for t in named if t in status] or [name]
            for t in targets:
                if status.get(t) != 'REAL':
                    continue
                key = (name, t)
                if key in seen:
                    continue
                seen.add(key)
                stale.append({'host': name, 'comment': (cs + 1, ce + 1),
                              'target': t, 'self': t == name, 'claim': sent})
    return funcs, status, stale


# A body that is empty ON THE MAC TOO is not a gap — the comment says so.
NOOP_RE = re.compile(
    r'no-op|noop|bare `?rts|empty body|faithfully empty|literally `moveq|just `rts|'
    r'rts only|compiled-out|link/unlk/rts|empty \(rts|is literally|genuinely a no|'
    r'faithful.{0,12}empty|the constant|constant \(', re.I)


def triage(path=SRC):
    """Split the stub bodies into faithful-no-op / live gap / uncalled gap."""
    lines = load(path)
    funcs = parse_funcs(lines)
    status = {n: classify(lines, op, cl) for n, _, op, cl in funcs}
    src = '\n'.join(lines)
    noop, live, dead = [], [], []
    for name, sig, op, cl in funcs:
        if status[name] != 'STUB':
            continue
        cs, ce = doc_above(lines, sig)
        doc = ' '.join(' '.join(lines[cs:ce + 1]).split()) if cs is not None else ''
        for k in range(sig, min(op + 2, len(lines))):
            m = re.search(r'/\*(.*?)\*/', lines[k])
            if m:
                doc += ' ' + m.group(1)
        hits = len(re.findall(r'\b%s\s*\(' % re.escape(name), src))
        decls = len(re.findall(r'^static [^;\n]*\b%s\s*\([^;]*;\s*(?:/\*.*)?$'
                               % re.escape(name), src, re.M))
        calls = max(hits - decls - 1, 0)
        row = (name, sig + 1, calls, ' '.join(doc.replace('/*', ' ').replace('*/', ' ').split()))
        if NOOP_RE.search(doc):
            noop.append(row)
        elif calls:
            live.append(row)
        else:
            dead.append(row)
    return noop, live, dead


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--stubs', action='store_true',
                    help='triage the remaining stubs instead of auditing comments')
    ap.add_argument('--file', default=SRC)
    a = ap.parse_args()

    if a.stubs:
        noop, live, dead = triage(a.file)
        print('%d stub bodies: %d faithful no-ops (NOT gaps), '
              '%d live gaps, %d uncalled gaps\n'
              % (len(noop) + len(live) + len(dead), len(noop), len(live), len(dead)))
        print('=== LIVE GAPS — lifted code calls these, so they gate behaviour ===')
        for n, l, c, d in sorted(live, key=lambda r: -r[2]):
            print('  %-10s line %-6d %2d call(s)' % (n, l, c))
        print('\n=== UNCALLED GAPS ===')
        for n, l, c, d in sorted(dead):
            print('  %-10s line %-6d' % (n, l))
        print('\n=== FAITHFUL NO-OPS (the Mac body is empty too — leave them) ===')
        print('  ' + ', '.join(sorted(n for n, _, _, _ in noop)))
        return 0

    funcs, status, stale = audit(a.file)
    nstub = sum(1 for v in status.values() if v == 'STUB')

    if not a.quiet:
        print('%d functions, %d still PROBE stubs\n' % (len(funcs), nstub))
        print('STALE STUB CLAIMS (comment says stub, body is a real lift): %d' % len(stale))
        for s in sorted(stale, key=lambda x: x['comment'][0]):
            print('  %-11s comment %5d-%-5d  %-6s %-11s | %s'
                  % (s['host'], s['comment'][0], s['comment'][1],
                     'SELF' if s['self'] else 'callee', s['target'], s['claim'][:80]))
    else:
        print('%d stale stub claims' % len(stale))
    return 1 if stale else 0


if __name__ == '__main__':
    sys.exit(main())
