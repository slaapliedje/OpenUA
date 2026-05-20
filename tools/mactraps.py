#!/usr/bin/env python3
"""Classic Mac A-line trap names, keyed by canonical trap word.

An A-line instruction ($Axxx) is a Toolbox or OS call. Bit 11 selects the
dispatcher: set = Toolbox ($A800 base, 10-bit number), clear = OS ($A000
base, 8-bit number, bits 8-10 are flags). canonical() normalises a raw trap
word to its table key.

This table is curated and intentionally partial -- it covers the traps a
game/editor of this era leans on. Unknown traps still get flagged by the
disassembler, just without a name. Add entries as the decompilation needs.
"""

# OS traps -- canonical key 0xA000 | number
_OS = {
    0xA000: "_Open",        0xA001: "_Close",       0xA002: "_Read",
    0xA003: "_Write",       0xA004: "_Control",     0xA005: "_Status",
    0xA008: "_Create",      0xA009: "_Delete",      0xA00A: "_OpenRF",
    0xA00C: "_GetFileInfo", 0xA00D: "_SetFileInfo", 0xA011: "_GetEOF",
    0xA012: "_SetEOF",      0xA013: "_FlushVol",    0xA014: "_GetVol",
    0xA015: "_SetVol",      0xA018: "_GetFPos",     0xA044: "_SetFPos",
    0xA01C: "_FreeMem",     0xA01E: "_NewPtr",      0xA01F: "_DisposPtr",
    0xA020: "_SetPtrSize",  0xA021: "_GetPtrSize",  0xA022: "_NewHandle",
    0xA023: "_DisposHandle", 0xA024: "_SetHandleSize", 0xA025: "_GetHandleSize",
    0xA027: "_ReallocHandle", 0xA029: "_HLock",     0xA02A: "_HUnlock",
    0xA02E: "_BlockMove",   0xA02F: "_PostEvent",   0xA032: "_FlushEvents",
    0xA036: "_MoreMasters", 0xA040: "_ResrvMem",    0xA049: "_HPurge",
    0xA04A: "_HNoPurge",    0xA063: "_MaxApplZone",
}

# Toolbox traps -- canonical key 0xA800 | number
_TOOLBOX = {
    0xA800: "_SoundDispatch",
    0xA850: "_InitCursor",  0xA851: "_SetCursor",   0xA852: "_HideCursor",
    0xA853: "_ShowCursor",  0xA856: "_ObscureCursor", 0xA861: "_Random",
    0xA860: "_WaitNextEvent",
    0xA86D: "_InitPort",    0xA86E: "_InitGraf",    0xA86F: "_OpenPort",
    0xA873: "_SetPort",     0xA874: "_GetPort",     0xA878: "_SetOrigin",
    0xA879: "_SetClip",     0xA87A: "_GetClip",     0xA87B: "_ClipRect",
    0xA87C: "_BackPat",     0xA87D: "_SetRect",     0xA87E: "_OffsetRect",
    0xA87F: "_InsetRect",   0xA880: "_SectRect",    0xA881: "_UnionRect",
    0xA883: "_DrawChar",    0xA884: "_DrawString",  0xA885: "_DrawText",
    0xA886: "_TextWidth",   0xA887: "_TextFont",    0xA888: "_TextFace",
    0xA889: "_TextMode",    0xA88A: "_TextSize",    0xA88B: "_GetFontInfo",
    0xA88C: "_StringWidth", 0xA891: "_LineTo",      0xA892: "_Line",
    0xA893: "_MoveTo",      0xA894: "_Move",        0xA896: "_HidePen",
    0xA897: "_ShowPen",     0xA89A: "_GetPen",      0xA89B: "_PenSize",
    0xA89C: "_PenMode",     0xA89D: "_PenPat",      0xA89E: "_PenNormal",
    0xA8A1: "_FrameRect",   0xA8A2: "_PaintRect",   0xA8A3: "_EraseRect",
    0xA8A4: "_InvertRect",  0xA8A5: "_FillRect",    0xA8A6: "_EqualRect",
    0xA8AD: "_PtInRect",    0xA8AE: "_EmptyRect",   0xA8B0: "_FrameRoundRect",
    0xA8B7: "_FrameOval",   0xA8B8: "_PaintOval",   0xA8B9: "_EraseOval",
    0xA8D2: "_FrameRgn",    0xA8D3: "_PaintRgn",    0xA8D4: "_EraseRgn",
    0xA8D6: "_FillRgn",     0xA8D8: "_NewRgn",      0xA8D9: "_DisposeRgn",
    0xA8DA: "_OpenRgn",     0xA8DB: "_CloseRgn",    0xA8DC: "_CopyRgn",
    0xA8DF: "_RectRgn",     0xA8E8: "_PtInRgn",     0xA8EC: "_CopyBits",
    0xA8EF: "_ScrollRect",  0xA8F6: "_DrawPicture",
    0xA904: "_DrawGrowIcon", 0xA906: "_NewString",  0xA907: "_SetString",
    0xA912: "_InitWindows", 0xA913: "_NewWindow",   0xA914: "_DisposeWindow",
    0xA915: "_ShowWindow",  0xA916: "_HideWindow",  0xA917: "_GetWRefCon",
    0xA918: "_SetWRefCon",  0xA91A: "_SetWTitle",   0xA91B: "_MoveWindow",
    0xA91D: "_SizeWindow",  0xA91E: "_TrackGoAway", 0xA91F: "_SelectWindow",
    0xA922: "_BeginUpdate", 0xA923: "_EndUpdate",   0xA924: "_FrontWindow",
    0xA925: "_DragWindow",  0xA927: "_InvalRgn",    0xA928: "_InvalRect",
    0xA929: "_ValidRgn",    0xA92A: "_ValidRect",   0xA92B: "_GrowWindow",
    0xA92C: "_FindWindow",  0xA92D: "_CloseWindow",
    0xA930: "_InitMenus",   0xA931: "_NewMenu",     0xA933: "_AppendMenu",
    0xA934: "_ClearMenuBar", 0xA935: "_InsertMenu", 0xA936: "_DeleteMenu",
    0xA937: "_DrawMenuBar", 0xA938: "_HiliteMenu",  0xA939: "_EnableItem",
    0xA93A: "_DisableItem", 0xA93B: "_GetMenuBar",  0xA93C: "_SetMenuBar",
    0xA93D: "_MenuSelect",  0xA93E: "_MenuKey",     0xA945: "_CheckItem",
    0xA946: "_GetItem",     0xA947: "_SetItem",     0xA949: "_GetMHandle",
    0xA94B: "_PlotIcon",    0xA94D: "_AddResMenu",  0xA950: "_CountMItems",
    0xA954: "_NewControl",  0xA955: "_DisposeControl", 0xA957: "_ShowControl",
    0xA958: "_HideControl", 0xA959: "_MoveControl", 0xA95C: "_SizeControl",
    0xA95D: "_HiliteControl", 0xA960: "_GetCtlValue", 0xA961: "_GetCtlMin",
    0xA962: "_GetCtlMax",   0xA963: "_SetCtlValue", 0xA964: "_SetCtlMin",
    0xA965: "_SetCtlMax",   0xA966: "_TestControl", 0xA968: "_TrackControl",
    0xA969: "_DrawControls", 0xA96C: "_FindControl",
    0xA970: "_GetNextEvent", 0xA971: "_EventAvail", 0xA972: "_GetMouse",
    0xA973: "_StillDown",   0xA974: "_Button",      0xA975: "_TickCount",
    0xA976: "_GetKeys",     0xA97B: "_InitDialogs", 0xA97C: "_GetNewDialog",
    0xA97D: "_NewDialog",   0xA97F: "_IsDialogEvent", 0xA980: "_DialogSelect",
    0xA981: "_DrawDialog",  0xA982: "_CloseDialog", 0xA983: "_DisposDialog",
    0xA985: "_Alert",       0xA986: "_StopAlert",   0xA987: "_NoteAlert",
    0xA988: "_CautionAlert", 0xA98B: "_ParamText",  0xA98D: "_GetDItem",
    0xA98E: "_SetDItem",    0xA98F: "_SetIText",    0xA990: "_GetIText",
    0xA991: "_ModalDialog", 0xA992: "_DetachResource", 0xA994: "_CurResFile",
    0xA997: "_OpenResFile", 0xA998: "_UseResFile",  0xA999: "_UpdateResFile",
    0xA99A: "_CloseResFile", 0xA99B: "_SetResLoad", 0xA99C: "_CountResources",
    0xA99D: "_GetIndResource", 0xA9A0: "_GetResource", 0xA9A1: "_GetNamedResource",
    0xA9A2: "_LoadResource", 0xA9A3: "_ReleaseResource", 0xA9A4: "_HomeResFile",
    0xA9A6: "_GetResAttrs", 0xA9A8: "_GetResInfo",  0xA9AA: "_ChangedResource",
    0xA9AB: "_AddResource", 0xA9AD: "_RmveResource", 0xA9AF: "_ResError",
    0xA9B0: "_WriteResource", 0xA9B1: "_CreateResFile", 0xA9B2: "_SystemEvent",
    0xA9B3: "_SystemClick", 0xA9B4: "_SystemTask",  0xA9B5: "_SystemMenu",
    0xA9BA: "_GetString",   0xA9BB: "_GetIcon",     0xA9BC: "_GetPicture",
    0xA9BD: "_GetNewWindow", 0xA9BE: "_GetNewControl", 0xA9BF: "_GetMenu",
    0xA9C1: "_UniqueID",    0xA9C8: "_SysBeep",     0xA9C9: "_SysError",
    0xA9CC: "_TEInit",      0xA9CD: "_TEDispose",   0xA9CF: "_TESetText",
    0xA9D0: "_TECalText",   0xA9D2: "_TENew",       0xA9D3: "_TEUpdate",
    0xA9D4: "_TEClick",     0xA9D5: "_TECopy",      0xA9D6: "_TECut",
    0xA9D7: "_TEDelete",    0xA9D8: "_TEActivate",  0xA9D9: "_TEDeactivate",
    0xA9DA: "_TEIdle",      0xA9DB: "_TEPaste",     0xA9DC: "_TEKey",
    0xA9DE: "_TEInsert",    0xA9E0: "_Munger",      0xA9E1: "_HandToHand",
    0xA9E2: "_PtrToXHand",  0xA9E3: "_PtrToHand",   0xA9E4: "_HandAndHand",
    0xA9EA: "_Pack3",       0xA9EF: "_PtrAndHand",  0xA9F0: "_LoadSeg",
    0xA9F1: "_UnLoadSeg",   0xA9F2: "_Launch",      0xA9F3: "_Chain",
    0xA9F4: "_ExitToShell", 0xA9F5: "_GetAppParms",
    0xAA14: "_RGBForeColor", 0xAA15: "_RGBBackColor",
    0xA81F: "_Get1Resource", 0xA80D: "_Count1Resources",
    0xA80E: "_Get1IndResource",
}


def canonical(word):
    """Normalise a raw $Axxx trap word to (kind, table-key, flag-bits)."""
    if word & 0x0800:
        return "Toolbox", 0xA800 | (word & 0x03FF), word & 0x0400
    return "OS", 0xA000 | (word & 0x00FF), word & 0x0700


def trap_name(word):
    """Return a readable name for a raw $Axxx trap word."""
    kind, key, _flags = canonical(word)
    table = _TOOLBOX if kind == "Toolbox" else _OS
    return table.get(key, f"{kind} trap {word:#06x}")
