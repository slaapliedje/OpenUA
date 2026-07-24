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
#
# "PROBE-only" is the phrasing that got away: the audit ran green for months
# while l1bfe's header said "L1aea ... and JT[138] / JT[139] stay PROBE-only"
# over three functions that were all fully lifted — and someone read that and
# asked for l1aea to be lifted a second time. Any wording that means "this is
# still a stub" belongs here.
CLAIM_RE = re.compile(
    r'[^.;]*\b(PROBE stub|leaf stub|stubs?\b[^.;]{0,40}?(?:pending|for now|here)|'
    r'pending (?:its|their) own lift|is a stub|are stubs|'
    r'lands? as (?:a )?PROBE stubs?|not yet lifted|deferred lift|'
    r'PROBE[- ]only|probe[- ]only|stay(?:s)? (?:a )?PROBE|'
    r'remain(?:s)? (?:a )?(?:PROBE|stub))[^.;]*', re.I)

# ...unless it is talking about the PAST ("was a stub, now lifted").
#
# NB "now" must not match "for NOW" — that is a PRESENT-tense claim ("a stub
# for now"), and treating it as history silently excused every comment phrased
# that way.
HIST_RE = re.compile(
    r'\bwas\b|\bwere\b|\bold\b|earlier|used to|no longer|already (?:fully )?lifted|'
    r'(?<!for )\bnow\b|since lifted|has been lifted|An earlier revision|full body over|'
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
        j, ok, wrapped = i, True, False
        while j < len(lines) and j - i < 6:
            # A forward declaration can end ");  /* comment */", which does not
            # end in ';' — strip the trailing comment before deciding.
            st = re.sub(r'/\*.*?\*/\s*$', '', lines[j]).rstrip()
            if st.endswith('{'):
                break
            # WRAPPED ONE-LINER: the signature is on its own line and the whole
            # body follows on the next, opening AND closing its braces there:
            #
            #     static unsigned char jt933(long ev, short exit_arm, ...)
            #         { PROBE("jt933"); (void)ev; ...; return 0; }  /* comment */
            #
            # This ends with '}', not '{', and is not a declaration, so the
            # old scan fell through both arms and DROPPED the function — it
            # never reached the status map, so its stub was invisible to the
            # triage and `--stubs` reported "0 live gaps" while jt933 (the
            # treasure take-commit) sat un-lifted on a live path.
            if j > i and st.lstrip().startswith('{') and st.endswith('}') \
                    and st.count('{') == st.count('}'):
                wrapped = True
                break
            if st.endswith(';'):
                ok = False
                break
            j += 1
        if wrapped:
            funcs.append((m.group(1), i, j, j))
            i = j + 1
            continue
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


# A complete definition on one line: `static void jt260(void) { PROBE(...); }`
ONELINE_DEF = re.compile(r'^static\s.*\{.*\}\s*$')


def mentions(doc, name):
    """Does `doc` name this function? The comments use the ASM spelling."""
    pats = [r'\b%s\b' % re.escape(name)]
    m = re.fullmatch(r'jt(\d+)', name)
    if m:
        pats.append(r'JT\[%s\]' % m.group(1))            # jt709 -> "JT[709]"
    m = re.fullmatch(r'l([0-9a-f]{4})(?:_c\d+)?', name)
    if m:
        pats.append(r'\bL%s\b' % m.group(1))             # l5304 -> "L5304"
    return any(re.search(p, doc, re.I) for p in pats)


def doc_for(lines, sig_idx, name):
    """The doc comment describing this function — possibly a SHARED one.

    jt260 and jt709 are both a bare Mac `rts` and sit under one header that
    documents the pair. doc_above() stops dead at jt260's one-line body, so
    jt709 looked undocumented and the triage called it a live gap. Walk past
    sibling definitions to find the header — but only accept it if it actually
    names us, or every function in a run would inherit its neighbour's doc.
    """
    cs, ce = doc_above(lines, sig_idx)
    if cs is not None:
        return cs, ce
    k = sig_idx - 1
    while k >= 0 and (lines[k].strip() == '' or
                      re.match(r'^static .*;\s*$', lines[k].strip()) or
                      ONELINE_DEF.match(lines[k].strip())):
        k -= 1
    if k < 0 or not lines[k].rstrip().endswith('*/'):
        return None, None
    end = k
    while k >= 0 and not lines[k].lstrip().startswith('/*'):
        k -= 1
    if k < 0:
        return None, None
    doc = ' '.join(' '.join(lines[k:end + 1]).split())
    return (k, end) if mentions(doc, name) else (None, None)


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
    r'faithful.{0,12}empty|the constant|constant \(|empty `?rts', re.I)


# ---------------------------------------------------------------------------
# SWITCH-ARM GAPS
#
# ★ The measurement that lied (docs/enhancements.md). `--stubs` counts PROBE
# stub BODIES, and an unimplemented switch arm is not a body:
#
#     case 2:                 /* Cast — TODO: L06d6 */
#             break;
#
# It calls nothing, so it is invisible to the stub triage, and at runtime the
# command silently does nothing. CAST and INV were BOTH this, and the port
# shipped for months with two dead buttons while the audit read "0 live gaps".
# This scan is the missing half: every `case`/`default` arm whose body is
# empty-or-break-only, classified by what its comment says about WHY.
#
# Most empty arms are legitimate — a fallthrough group (`case 1: case 2:`), a
# faithfully-empty Mac arm, or work handled by the code after the switch. The
# scan cannot know which, so it does not guess: it reports the arm with its
# comment and splits on the wording. A DEFERRED arm claims to be unfinished; an
# EXPLAINED arm gives a reason; a BARE arm says nothing at all and is the one
# worth a human look, because that is the shape CAST and INV had.

CASE_RE = re.compile(r'^\s*(case\s+[^:]+|default)\s*:\s*(.*)$')
# Wording that says "this arm is unfinished".
ARM_TODO_RE = re.compile(
    r'\bTODO\b|\bdeferred?\b|\bstub\b|not (?:yet )?(?:lifted|implemented|wired)|'
    r'unimplemented|pending', re.I)
# Wording that explains an intentionally empty arm.
ARM_OK_RE = re.compile(
    r'no-op|noop|nothing|fall[- ]?s? ?through|falls thru|handled|ignored?|'
    r'unused|never|not reached|unreachable|faithful|empty on the mac|'
    r'the mac does|same as|skip', re.I)


def arm_gaps(path=SRC):
    """Every empty switch arm in the file, split deferred / explained / bare.

    Returns rows of (func, line, label, kind, comment). `kind` is 'deferred'
    (the comment admits it is unfinished), 'explained' (the comment gives a
    reason) or 'bare' (no comment at all — judge it by hand)."""
    lines = load(path)
    funcs = parse_funcs(lines)
    owner = {}
    for name, _sig, op, cl in funcs:
        for k in range(op, cl + 1):
            owner[k] = name

    rows = []
    for i, raw in enumerate(lines):
        m = CASE_RE.match(raw)
        if not m:
            continue
        label = m.group(1).strip()
        # Walk forward collecting this arm's statements + comments, stopping at
        # the next label or at the switch's closing brace (depth < 0).
        stmts, comment, depth, incomment = [], [], 0, False
        # A GROUPED label (`case 16:` immediately above `case 17: do();`) is
        # ordinary C, not an unimplemented arm — the work is in the last label
        # of the run. Distinguish by how the arm ENDS: its own `break;` (or the
        # end of the switch) means it really is a no-op arm; running into the
        # next label with no break means it falls through and is fine.
        ended, fellthrough = False, False
        # Anything on the label's own line counts as body ("case 3: x = 1;").
        pending = [m.group(2)] + lines[i + 1:i + 400]
        for off, line in enumerate(pending):
            t = line.strip()
            if off:
                if CASE_RE.match(line) and depth == 0:
                    fellthrough = not ended
                    break
                depth += depth_delta(line)
                if depth < 0:
                    break
            # Peel comments off, keeping their text for classification.
            if incomment:
                if '*/' in t:
                    comment.append(t.split('*/', 1)[0])
                    t, incomment = t.split('*/', 1)[1].strip(), False
                else:
                    comment.append(t)
                    continue
            while '/*' in t:
                head, rest = t.split('/*', 1)
                if '*/' in rest:
                    c, t = rest.split('*/', 1)
                    comment.append(c)
                    t = (head + ' ' + t).strip()
                else:
                    comment.append(rest)
                    t, incomment = head.strip(), True
                    break
            if t.startswith('//'):
                comment.append(t[2:])
                continue
            t = t.strip()
            if t in ('break;', 'break ;'):
                ended = True
                continue
            if t and t not in ('{', '}'):
                stmts.append(t)
                break                              # a real statement: not a gap
        if stmts or fellthrough:
            continue
        doc = ' '.join(' '.join(comment).replace('*', ' ').split())
        if ARM_TODO_RE.search(doc):
            kind = 'deferred'
        elif doc and ARM_OK_RE.search(doc):
            kind = 'explained'
        elif doc:
            kind = 'commented'
        elif label == 'default':
            # An empty `default:` is ordinary C — the catch-all that does
            # nothing. Not the CAST/INV shape, and there are ~200 of them.
            kind = 'default'
        else:
            kind = 'bare'
        rows.append((owner.get(i, '?'), i + 1, label, kind, doc))
    return rows


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
        cs, ce = doc_for(lines, sig, name)
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
    ap.add_argument('--arms', action='store_true',
                    help='report EMPTY SWITCH ARMS — the gaps --stubs cannot see')
    ap.add_argument('--file', default=SRC)
    a = ap.parse_args()

    if a.arms:
        rows = arm_gaps(a.file)
        by = {}
        for r in rows:
            by.setdefault(r[3], []).append(r)
        print('%d empty switch arms: %d deferred, %d bare case, %d commented, '
              '%d explained, %d empty default\n'
              % (len(rows), len(by.get('deferred', [])), len(by.get('bare', [])),
                 len(by.get('commented', [])), len(by.get('explained', [])),
                 len(by.get('default', []))))
        print('=== DEFERRED — the arm says it is unfinished (these are gaps) ===')
        for f, l, lab, _k, d in by.get('deferred', []):
            print('  %-14s line %-6d %-22s %s' % (f, l, lab, d[:96]))
        print('\n=== BARE CASE — a NAMED arm, empty, no comment. '
              'This is the shape CAST and INV had. ===')
        for f, l, lab, _k, _d in by.get('bare', []):
            print('  %-14s line %-6d %s' % (f, l, lab))
        print('\n=== COMMENTED — a comment, but not obviously either way ===')
        for f, l, lab, _k, d in by.get('commented', []):
            print('  %-14s line %-6d %-22s %s' % (f, l, lab, d[:80]))
        print('\nOmitted: %d explained arms (fallthrough / faithfully empty / '
              'handled elsewhere) and %d empty `default:` catch-alls.'
              % (len(by.get('explained', [])), len(by.get('default', []))))
        return 1 if by.get('deferred') else 0

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
