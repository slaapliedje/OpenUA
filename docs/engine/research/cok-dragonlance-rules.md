# Champions of Krynn (CoK) — Dragonlance Character-Creation Rules (Oracle)

**Provenance:** Extracted from the user's legally-owned game manuals in this repo:
`Champions of Krynn\Journal.pdf` (the actual rules/appendix) and `Champions of Krynn\Manual.pdf`
(supplementary). **Extraction method:** `pdfplumber` 0.11.9 — page-level `extract_text()` for prose,
plus **word-level `extract_words()` with x/y clustering** to reconstruct the two appendix tables
(the columns interleave badly in flat text and `pdftotext` drops digits, so the digit-level cluster
pass is what makes the numeric tables trustworthy).

Page numbers below are the **printed Journal page numbers** (the appendix charts are on printed
pp. 55–58, which are PDF page indices 28–30). Where a value was visually ambiguous in extraction it
is flagged inline.

> Scope note: this captures CoK's **Dragonlance ruleset overlay** on top of AD&D 1e. The base AD&D 1e
> ability/THAC0/save tables live in `addnd1e-tables.md`; this doc is the CoK-specific deltas
> (race set, ranges, class matrix, Knight/cleric/moon mechanics).

---

## 1. Playable Races — CONFIRMED (confidence: high)

The Journal states explicitly: *"There are seven races … from which you may construct your player
characters."* (printed p. 4). The seven are:

| # | Race (manual's name)        | Notes from prose |
|---|-----------------------------|------------------|
| 1 | **Human**                   | Most common race. No racial ability adjustments. **Cannot multi-class** (humans only). |
| 2 | **Hill Dwarf**              | Resistant to magic & poison; combat bonus vs goblins/hobgoblins; dodge bonus vs ogres/giants. |
| 3 | **Mountain Dwarf**          | "Somewhat clannish and more refined"; otherwise nearly identical to Hill Dwarf. |
| 4 | **Silvanesti Elf** (High Elf) | Nearly immune to sleep/charm; finds hidden doors; combat bonus with long/short swords & bows; **cannot be raised from the dead**. |
| 5 | **Qualinesti Elf**          | "Slightly smaller and friendlier" than Silvanesti; *"identical abilities and bonuses"* to Silvanesti. |
| 6 | **Half-Elf**                | Resistant to sleep/charm; adept at finding hidden doors. |
| 7 | **Kender**                  | Small; fearless/curious; resistant to magic & poison; can **taunt** intelligent foes in combat; only race that can use the **hoopak**. |

> **Important deviation from the task's "expected" list:** CoK uses **Silvanesti Elf** and
> **Qualinesti Elf** as *two separate playable races* (not one generic "Elf"), and it splits dwarves
> into **Hill Dwarf** and **Mountain Dwarf**. There is **no "Gnome / Tinker Gnome"** and **no generic
> "Half-Elf vs Elf"** distinction beyond the above. So the real set is: Human, Hill Dwarf, Mountain
> Dwarf, Silvanesti Elf, Qualinesti Elf, Half-Elf, Kender. (high confidence — stated verbatim.)

---

## 2. Range of Ability Scores by Race — CONFIRMED (confidence: high)

Source: **"Range of Ability Scores by Race"** table, printed **p. 56** (PDF page 29), reconstructed
from word-level x/y clustering. Format `min-max`; `(nn)` after STR is the **maximum exceptional-STR
percentile** for that race (fighter-type classes only — see table footnote). Each race lists a male
row and a `(Females)` row; the female row generally lowers the STR cap.

| Race (sex)              | STR*          | INT   | WIS   | DEX   | CON   | CHA   |
|-------------------------|---------------|-------|-------|-------|-------|-------|
| Human (M)               | 3-18(00)      | 3-18  | 3-18  | 3-18  | 3-18  | 3-18  |
| Human (F)               | 3-18(50)      | 3-18  | 3-18  | 3-18  | 3-18  | 3-18  |
| Silvanesti Elf (M)      | 3-18(75)      | 10-18 | 6-18  | 7-19  | 6-18  | 12-18 |
| Silvanesti Elf (F)      | 3-16(75)      | 10-18 | 6-18  | 7-19  | 6-18  | 12-18 |
| Qualinesti Elf (M)      | 7-18(75)      | 8-18  | 6-18  | 7-19  | 7-18  | 8-18  |
| Qualinesti Elf (F)      | 3-16(75)      | 8-18  | 6-18  | 7-19  | 7-18  | 8-18  |
| Hill Dwarf (M)          | 9-18(99)      | 3-18  | 3-18  | 3-17  | 14-19 | 3-12  |
| Hill Dwarf (F)          | 3-17(99)      | 3-18  | 3-18  | 3-17  | 14-19 | 3-12  |
| Mountain Dwarf (M)      | 8-18(99)      | 3-18  | 3-18  | 3-17  | 12-19 | 3-16  |
| Mountain Dwarf (F)      | 3-17(99)      | 3-18  | 3-18  | 3-17  | 12-19 | 3-16  |
| Half-Elf (M)            | 3-18(90)      | 4-18  | 3-18  | 6-18  | 6-18  | 3-18  |
| Half-Elf (F)            | 3-17(90)      | 4-18  | 3-18  | 6-18  | 6-18  | 3-18  |
| Kender (M & F, "Both")  | 6-16          | 6-18  | 3-16  | 8-19  | 10-18 | 6-18  |

`*` Table footnote: *"Maximum percentage for 18 strength for fighter-type classes only (fighter,
knight, ranger)."* The `(nn)` percentile only applies when the character is a fighter-type and rolls
18 STR. Kender have no `(nn)` — capped at 16 STR.

**Extraction confidence per cell:** high overall. The female STR-cap digit and the leading min for a
few non-human rows came through as split single-characters in the cluster pass but reassembled
unambiguously (e.g. Silvanesti F `3-16`, Mountain Dwarf M `8-18(99)`, Mountain Dwarf CON `12-19`).
DEX max of **19** for elves/kender/dwarves(M-Mtn? no) is correct AD&D-Dragonlance behavior (racial
DEX can exceed 18). The only values I'd double-check against a second source if precision is
load-bearing: **Mountain Dwarf male STR min `8`** vs Hill Dwarf male `9` (both extracted cleanly but
they differ by one, which is exactly the kind of digit this PDF mangles — flagged **medium** for that
single digit). Everything else: high.

---

## 3. Racial Ability Adjustments — PARTIAL (confidence: medium)

The Journal describes adjustments **in prose, not in a dedicated +/- table**. It says
(printed p. 5): *"non-human characters may receive modifiers to the basic ability scores to reflect
differences between the races … all racial modifiers are calculated"* automatically at
`CREATE NEW CHARACTER`. The **only explicit numeric adjustment stated** is:

| Race           | Adjustment stated in manual            | Confidence | Page |
|----------------|----------------------------------------|------------|------|
| Hill/Mountain Dwarf | **+1 CON** ("get a +1 constitution … maximum constitution of 19 instead of 18") | high | p. 5 |
| (others)       | *Not stated as explicit +/- numbers*   | —          | — |

> **Gap / important:** The classic AD&D Elf `+1 DEX / −1 CON` and the standard Dragonlance racial
> shifts are **NOT printed as numbers** in this manual. CoK instead encodes racial differences
> primarily through the **ability-score *ranges*** in §2 (e.g. dwarves' CON floor of 12–14 and ceiling
> of 19; elves' DEX ceiling of 19; kender's STR ceiling of 16). The `+1 CON` for dwarves is the one
> explicit additive modifier the text calls out. Do **not** assume the tabletop `±` adjustments are
> applied additively on top of the ranges — the manual's wording implies the ranges already bake in
> racial differences and the engine "calculates all racial modifiers" internally. Treat additional
> per-race ± values as **unknown / needs ECL or save-file verification** rather than guessing.

---

## 4. Character Classes in CoK — CONFIRMED (confidence: high)

Classes that appear in the appendix **"Maximum Level Limits"** table and prose (printed pp. 5–6):

| Class              | Prime requisite | Notes |
|--------------------|-----------------|-------|
| **Cleric**         | Wisdom          | No spell book; all clerical spells of available level always memorizable; must pick a deity & matching alignment. |
| **Fighter**        | Strength        | Any armor/weapon. |
| **Ranger**         | Str, Int, Wis (multiple) | Any armor/weapon; HP bonus if CON 17+; extra damage vs giant-class; needs STR & INT ≥13, WIS & CON ≥14 minimums. |
| **Knight of Solamnia** (Solamnic Knight) | Strength **and** Wisdom | Three orders (Crown/Sword/Rose). See §7. |
| **Mage** (Magic-User) | Intelligence | White Robe / Red Robe orders (PC); Black Robe is NPC-only/evil. Uses grimoire & scrolls; no armor, few weapons. |
| **Thief**          | Dexterity       | Leather armor only; back-stab; can pick locks etc. |

So the class set = **Cleric, Fighter, Ranger, Knight (of Solamnia), Mage, Thief** (6 classes). This
matches the task's "expected" set (Magic-User = Mage; Knight of Solamnia confirmed).

---

## 5. Race / Class Allowance Matrix — CONFIRMED (confidence: high)

Derived from (a) the per-race prose (printed pp. 4–5) and (b) the appendix
**"Maximum Level Limits by Race, Class and Prime Requisite"** table (printed p. 56), where a class
cell marked **"No"** means *that class is unavailable to that race*. The two sources agree.

`Y` = allowed, `No` = not allowed.

| Class \ Race | Human | Silvanesti Elf | Qualinesti Elf | Half-Elf | Hill Dwarf | Mountain Dwarf | Kender |
|--------------|:-----:|:--------------:|:--------------:|:--------:|:----------:|:--------------:|:------:|
| Cleric       | Y | Y | Y | Y | Y | Y | Y |
| Fighter      | Y | Y | Y | Y | Y | Y | Y |
| Ranger       | Y | Y | Y | Y | Y | **No** | Y |
| Knight       | Y | **No** | **No** | Y | **No** | **No** | **No** |
| Mage         | Y | Y | Y | Y | **No** | **No** | **No** |
| Thief        | Y | **No** | Y | Y | Y | Y | Y |

Notes / cross-checks (high confidence):
- **Knight** is available **only to Human and Half-Elf** (table: all elf/dwarf/kender columns = "No";
  Human and Half-Elf = level 7 cap). Prose confirms Half-Elves *"can be … knights"* and only humans &
  half-elves are described as knight-capable.
- **Mage** unavailable to **both dwarf types and kender** (table "No"; prose lists dwarves as
  fighter/thief/cleric/ranger only, kender as thief/fighter/ranger/cleric only — no mage).
- **Ranger** unavailable to **Mountain Dwarf** (table "No"); Hill Dwarf *can* be a ranger.
- **Thief**: prose lists Silvanesti as *"fighters, mages, clerics, rangers, thieves"* — but the
  appendix table marks **Silvanesti Thief = "No"**. **Conflict flagged.** The appendix table is the
  authoritative class-limit chart, so I take **Silvanesti = no Thief** as the engine rule, but this is
  the one race/class cell where manual prose and table disagree — **verify against the game/ECL**.
  (Qualinesti Thief = `9`, i.e. allowed, in the table.)

---

## 6. Multi-Class (Mixed-Class) Combinations — CONFIRMED rule, examples high / exhaustive-list medium

Rule (printed p. 6, Glossary p. 60): *"Multi-class characters are non-human characters who belong to
two or more classes at the same time."* Therefore:

- **Humans cannot multi-class** (single class only). (high)
- **Non-humans may take 2 or 3 classes simultaneously** ("two or three classes at the same time" /
  "triple-class character"). XP is **split among all classes**; HP per level is **averaged** across the
  classes; the character gains the weapon/equipment benefits of **all** classes. (high)
- Multi-classing across a race's *individually-allowed* classes from §5 — i.e. a race can only combine
  classes it is permitted to take. (high, by construction)

**Combinations explicitly named in the manual** (prose + sample parties, pp. 6–7) — confidence high
that these are *valid*, medium that the list is *complete*:

| Combination          | Notes from manual |
|----------------------|-------------------|
| Cleric/Thief         | "more HP and better AC than a pure thief." |
| Fighter/Mage         | "may cast spells while wearing armor … fights as well as a fighter." |
| Cleric/Fighter/Mage  | "the ultimate split class"; triple-class; can cast cleric spells while armored. |
| Ranger/Cleric        | (sample party: Half-Elf Ranger/Cleric of Majere) |
| Fighter/Mage(Red)    | (sample party: Qualinesti Elf Fighter/Red Mage) |
| Cleric/Fighter/Mage  | (sample party: Qualinesti Cleric of Shinare/Fighter/Red Mage) |
| Cleric/Thief         | (sample party: Kender Cleric of Kiri-Jolith/Thief) |

> The manual does **not** print an exhaustive "allowed combinations per race" matrix. The constraint
> is *combinatorial*: any 2–3 classes a given non-human race may individually take (per §5) may be
> combined. Treat §5 as the generator; the named pairs/triples above are confirmed-valid instances.

---

## 7. Maximum Level Limits by Race & Class — CONFIRMED (confidence: high)

Source: **"Maximum Level Limits by Race, Class and Prime Requisite"**, printed **p. 56** (PDF page 29),
reconstructed by x-position clustering. Columns (left→right) are:
**HUMAN, SILVANESTI ELF, QUALINESTI ELF, HALF-ELF, HILL DWARF, MOUNTAIN DWARF, KENDER**.
`No` = class unavailable to that race. Where a class row is split by **prime-requisite tier**
(STR/INT thresholds), each tier is its own line.

| Class  | Ability tier | Human | Silvanesti | Qualinesti | Half-Elf | Hill Dwarf | Mtn Dwarf | Kender |
|--------|--------------|:-----:|:----------:|:----------:|:--------:|:----------:|:---------:|:------:|
| Cleric | Any          | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
| Fighter| Str 16-      | 8 | 8 | 8 | 8 | 8 | 8 | 5 |
| Fighter| Str 17       | 8 | 8 | 8 | 8 | 8 | 8 | 6 |
| Fighter| Str 18+      | 8 | 8 | 8 | 8 | 8 | 8 | 7 |
| Ranger | Str 16-      | 7 | 7 | 7 | 7 | 7 | No | 5 |
| Ranger | Str 17       | 7 | 7 | 7 | 7 | 7 | No | 6 |
| Ranger | Str 18+      | 7 | 7 | 7 | 7 | 7 | No | 7 |
| Knight | Any          | 7 | No | No | 7 | No | No | No |
| Mage   | Int 16-      | 8 | 8 | 8 | 8 | No | No | No |
| Mage   | Int 17       | 8 | 8 | 8 | 8 | No | No | No |
| Mage   | Int 18       | 8 | 8 | 8 | 8 | No | No | No |
| Thief  | Any          | 9 | No | 9 | 9 | 9 | 8 | 9 |

Observations (all high confidence from the digit-cluster pass):
- This game **caps everything very low** (max level 9, for thieves). That is correct for CoK — it's the
  first game in the trilogy. Most classes top out at **8**, thieves at **9** (Mountain Dwarf thief 8),
  knights/rangers at **7**.
- **Demi-human level caps in this game do NOT vary by prime-requisite for the non-human main races** —
  the STR/INT tiers only actually change the cap for **Kender Fighter/Ranger** (5→6→7) and otherwise
  every tier shows the same number. (i.e. the "prime requisite" columns are present but mostly flat in
  CoK.)
- `No` cells exactly match the §5 availability matrix (Silvanesti Thief = No; Mountain Dwarf Ranger =
  No; all dwarf/kender Mage = No; only Human/Half-Elf Knight).

---

## 8. Knight of Solamnia (Solamnic Knight) — CONFIRMED (confidence: high)

From printed pp. 5–6 (and tithe/XP notes):

- **Available only to Human and Half-Elf** (per §5/§7). Max level **7**.
- **Three orders, ascending**: **Knights of the Crown → Knights of the Sword → Knights of the Rose.**
  A knight petitions the next-higher order once they have high enough ability scores and level.
- **Prime requisites: Strength and Wisdom** (stated: *"Prime requisites for knights are strength and
  wisdom."*).
- **Minimum ability scores to petition each order** (printed pp. 5–6):

  | Order            | Minimum scores | Confidence |
  |------------------|----------------|------------|
  | Knights of the **Crown** | (manual gives Crown as the entry order; the explicit STR/INT/etc. minimums printed are for Sword and Rose) | medium — see note |
  | Knights of the **Sword** | **STR 12, INT 9, WIS 13, DEX 12, CON 15** | high |
  | Knights of the **Rose**  | **STR 15, INT 10, WIS 13, DEX 12, CON 15** | high |

  > Note: the manual text *"To petition to join the Knights of the Sword a knight must have the
  > following minimum ability scores: STR 12, INT 9, …"* and *"…of the Rose … STR 15, INT 10, WIS 13,
  > DEX 12, CON 15."* are extracted cleanly. The **Crown** (lowest) order's explicit minimums were not
  > separately printed in the extracted text — likely just the basic STR/WIS prime-requisite floor.
  > Flagged **medium**: confirm Crown minimums in-game if needed.

- **Combat / does it fight as a fighter?** **Yes.** Knights *"can fight wearing"* armor granted "by
  their deity" and are grouped with fighters/rangers for combat behavior. Specifically the manual
  states Knights use the **fighter combat tables**: *"Fighters and knights of 7th level or greater can
  attack twice every other turn"* and they appear alongside fighters for sweep/multiple-attack and
  giant-damage rules. Knights begin with **plate mail, long sword, and a shield**. (high)
- **High-level knights cast clerical spells.** *"Mages, clerics and high-level knights can cast
  spells."* and *"All clerical spells … always available to a cleric or high-level knight."* So a
  Knight of Solamnia gains **cleric-style spellcasting at high level** (drawing on the clerical list).
  (high)
- **Special economy rules:** Knights take a **vow of poverty** and **tithe** to their order (Crown 10%
  on entering an outpost; Sword/Rose tithe more). Knights **receive XP bonuses for knightly deeds**,
  not for having prime requisites. Knights **cannot use clerical scrolls** even though they cast cleric
  spells. (high)
- Knights **begin at first level** (like clerics), whereas most classes start the campaign at 2nd
  level. (high)

---

## 9. Lunar / Holy Magic — CONFIRMED (confidence: high)

### 9a. The Three Moons (mage magic) — printed pp. 15–16

*"Since the creation of the world, three moons have governed the powers of magic in Krynn. As the
moons wax and wane, so do the powers of magic aligned to them."* Each robe order draws power from one
moon:

| Robe order (PC?)  | Moon              | Alignment |
|-------------------|-------------------|-----------|
| **White Robe**    | **Solinari** (white moon) | Good — PC |
| **Red Robe**      | **Lunitari** (red moon)   | Neutral — PC |
| **Black Robe**    | **Nuitari** (dark moon)   | Evil — **NPC only** (*"Only NPC characters may be evil Black Robe Mages."*) |

The current moon phase is shown at the top of the screen and modifies the relevant mage's power. The
manual prints an explicit **moon-phase effect table** (per the mage's own moon):

| Phase (low→high)          | Saving Throws | Additional Spells* | Effective Level |
|---------------------------|:-------------:|:------------------:|:---------------:|
| **Low Sanction (New Moon)** | −1          | 0                  | −1              |
| **Waning**                | Normal        | 0                  | Even            |
| **Waxing**                | Normal        | +1                 | Even            |
| **High Sanction (Full Moon)** | +1        | +2                 | +1**            |

`*` Additional spells can be of any level the mage can cast.
`**` The **+1 effective level at Full Moon applies only to a mage of 6th level or higher who also has
INT ≥ 15.** (Both footnotes extracted cleanly — high confidence.)

Also: mages must pass the **Test of High Sorcery** to formally join an order; **Rogue mages** (refused
the Test) ignore order restrictions but **gain no moon benefits**. Mage spells are split by robe via
the **Spheres of Magic** (White can use Abjuration/Necromancy etc.; the Spheres table on printed p. 53
governs which spells each robe may cast). Mages may only use **scrolls of their own robe type** (Red
may use Red & White; White may use White only).

### 9b. Clerical "holy" magic — deities & granted powers (printed pp. 17–18)

Clerics are *"mortal messengers of the will of the heavens"*; **clerical magic requires no spell
book** — all clerical spells of an available level are always memorizable, and clerics can cast
clerical scrolls directly. A cleric **must choose a deity**, and **alignment must match the deity**.
Each god grants special **Powers** and/or **Extra spells**:

| Deity (alignment group) | Powers | Extra spells |
|-------------------------|--------|--------------|
| **Paladine** (Good)     | None   | Protection from Evil 10' radius |
| **Majere** (Good)       | Turn undead as if cleric were **2 levels higher** | Silence 15' radius |
| **Kiri-Jolith** (Good)  | **+1 THAC0** | Detect Magic |
| **Mishakal** (Good)     | **+1 die on all healing spells** | Charm Person, Remove Curse, Bless |
| **Sirrion** (Neutral)   | None   | Burning Hands |
| **Reorx** (Neutral)*    | **+1 THAC0 (dwarves only)** | None |
| **Shinare** (Neutral)   | None   | Charm Person |

`*` **All dwarven clerics must select Reorx** (and are therefore neutral). (high)

> The manual does **not** describe a "holy symbol" *item-slot* mechanic per se; the clerical "holy"
> dimension is encoded as (a) deity choice gating alignment, and (b) the per-deity Powers/Extra-spells
> grants above. If "holy symbol mechanics" means an inventory item, that is **not present in the
> manual** (gap). Turn-undead is a granted cleric/high-level-knight power (Majere boosts it; Death
> Knights *cannot be turned*).

---

## 10. Summary of gaps & low/medium-confidence flags

- **§3 racial ± adjustments:** only Dwarf **+1 CON** is printed as an explicit number; other tabletop
  `±` shifts are NOT in the manual (ranges in §2 are the encoded mechanism). **Medium/gap.**
- **§2 Mountain Dwarf male STR min (`8`)** vs Hill Dwarf (`9`): single off-by-one digit in a
  PDF-mangle-prone spot — extracted cleanly but worth a second-source check. **Medium.**
- **§5 Silvanesti Thief:** prose says yes, appendix table says **No** — conflict; I take the table
  (No) as authoritative but **verify in-game/ECL**. **Flagged.**
- **§8 Knights of the Crown minimum scores:** Sword & Rose minimums printed clearly; Crown's explicit
  minimums not separately printed. **Medium.**
- No **Gnome/Tinker Gnome** race exists in CoK (the task's "expected" set was slightly off — the real
  seven are listed in §1).
- No inventory-style "holy symbol" mechanic found; clerical "holy" magic = deity grants (§9b).
