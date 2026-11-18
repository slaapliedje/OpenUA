"""modman.gui — the desktop window (tkinter, which ships with Python on
Windows, macOS and Linux, so there is nothing else to install).

    Available modules          | details of the one selected | Installed in this folder
    (catalog + archives added) |                              |
    [Add archive] [Add folder] | [Install ->]                 | [Remove]
    OpenUA folder: [........] [Browse]       catalog: [........] [Load]

Downloading and copying run on a worker thread; the window polls for their
results so it never freezes on a slow card or connection.
"""
import configparser
import os
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import catalog, core

CONFIG = os.path.join(os.path.expanduser("~"), ".config", "openua-modman.ini")
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "openua-modman")


class Item:
    """A row in the Available list: a catalog entry or a local archive/folder."""

    def __init__(self, label, entry=None, path=None):
        self.label = label
        self.entry = entry
        self.path = path
        self.plan = None
        self.error = None


class App:
    def __init__(self, root):
        self.root = root
        self.items = []
        self.jobs = queue.Queue()
        self.busy = False
        self.cfg = configparser.ConfigParser()
        self.cfg.read(CONFIG)
        if not self.cfg.has_section("modman"):
            self.cfg.add_section("modman")
        root.title("OpenUA Module Manager")
        root.minsize(980, 480)
        self.build()
        self.target_var.set(self.cfg.get("modman", "target", fallback=""))
        self.catalog_var.set(self.cfg.get("modman", "catalog", fallback=""))
        self.refresh_target()
        if self.catalog_var.get():
            self.load_catalog(quiet=True)
        root.after(100, self.poll)

    # ---------------------------------------------------------- layout
    def build(self):
        r = self.root
        r.columnconfigure(0, weight=1)
        r.rowconfigure(1, weight=1)

        top = ttk.Frame(r, padding=(8, 8, 8, 0))
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(1, weight=1)
        ttk.Label(top, text="OpenUA folder:").grid(row=0, column=0, sticky="w")
        self.target_var = tk.StringVar()
        e = ttk.Entry(top, textvariable=self.target_var)
        e.grid(row=0, column=1, sticky="ew", padx=4)
        e.bind("<Return>", lambda _e: self.refresh_target())
        ttk.Button(top, text="Browse…", command=self.browse_target).grid(row=0, column=2)
        self.target_state = ttk.Label(top, text="", foreground="#a33")
        self.target_state.grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(top, text="Catalog:").grid(row=2, column=0, sticky="w", pady=(4, 0))
        self.catalog_var = tk.StringVar()
        ttk.Entry(top, textvariable=self.catalog_var).grid(row=2, column=1, sticky="ew",
                                                           padx=4, pady=(4, 0))
        ttk.Button(top, text="Load", command=self.load_catalog).grid(row=2, column=2,
                                                                    pady=(4, 0))

        body = ttk.Frame(r, padding=8)
        body.grid(row=1, column=0, sticky="nsew")
        for c, w in ((0, 3), (1, 4), (2, 3)):
            body.columnconfigure(c, weight=w)
        body.rowconfigure(1, weight=1)

        ttk.Label(body, text="Available").grid(row=0, column=0, sticky="w")
        self.avail = tk.Listbox(body, exportselection=False, activestyle="none", width=30)
        self.avail.grid(row=1, column=0, sticky="nsew")
        self.avail.bind("<<ListboxSelect>>", lambda _e: self.show_selected())
        b = ttk.Frame(body)
        b.grid(row=2, column=0, sticky="w", pady=(4, 0))
        ttk.Button(b, text="Add archive…", command=self.add_archive).pack(side="left")
        ttk.Button(b, text="Add folder…", command=self.add_folder).pack(side="left", padx=4)

        ttk.Label(body, text="Details").grid(row=0, column=1, sticky="w", padx=8)
        self.details = tk.Text(body, wrap="word", height=12, state="disabled",
                               relief="flat", background=r.cget("background"))
        self.details.grid(row=1, column=1, sticky="nsew", padx=8)
        mid = ttk.Frame(body)
        mid.grid(row=2, column=1, sticky="ew", padx=8, pady=(4, 0))
        self.install_btn = ttk.Button(mid, text="Install →", command=self.install)
        self.install_btn.pack(side="left")
        self.progress = ttk.Progressbar(mid, mode="determinate", length=160)
        self.progress.pack(side="left", padx=8, fill="x", expand=True)

        ttk.Label(body, text="Installed in this folder").grid(row=0, column=2, sticky="w")
        self.inst = tk.Listbox(body, exportselection=False, activestyle="none", width=30)
        self.inst.grid(row=1, column=2, sticky="nsew")
        ttk.Button(body, text="Remove", command=self.remove).grid(row=2, column=2,
                                                                  sticky="w", pady=(4, 0))

        self.status = ttk.Label(r, text="", padding=(8, 0, 8, 6))
        self.status.grid(row=2, column=0, sticky="ew")

    # ---------------------------------------------------------- state
    def save_cfg(self):
        self.cfg.set("modman", "target", self.target_var.get())
        self.cfg.set("modman", "catalog", self.catalog_var.get())
        os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
        with open(CONFIG, "w") as fh:
            self.cfg.write(fh)

    def target(self):
        return core.Target(self.target_var.get().strip())

    def refresh_target(self):
        t = self.target()
        why = t.check() if t.path else "Pick the folder that holds frua.prg / frua."
        self.target_state.config(text=why or "", foreground="#a33" if why else "#3a3")
        self.inst.delete(0, "end")
        self.installed = []
        if not why:
            self.installed = t.designs()
            for folder, title in self.installed:
                self.inst.insert("end", "%-13s %s" % (folder, title))
            self.save_cfg()

    def set_details(self, text):
        self.details.config(state="normal")
        self.details.delete("1.0", "end")
        self.details.insert("1.0", text)
        self.details.config(state="disabled")

    def say(self, text):
        self.status.config(text=text)

    def selected(self):
        sel = self.avail.curselection()
        return self.items[sel[0]] if sel else None

    def add_item(self, item):
        self.items.append(item)
        self.avail.insert("end", item.label)

    # ---------------------------------------------------------- actions
    def browse_target(self):
        d = filedialog.askdirectory(title="The OpenUA folder (it holds frua.prg or frua)")
        if d:
            self.target_var.set(d)
            self.refresh_target()

    def load_catalog(self, quiet=False):
        where = self.catalog_var.get().strip()
        if not where:
            where = filedialog.askopenfilename(title="Module catalog",
                                               filetypes=[("Catalog", "*.txt"), ("All", "*")])
            if not where:
                return
            self.catalog_var.set(where)
        try:
            entries = catalog.load(where)
        except Exception as exc:          # a file or network problem, shown not raised
            if not quiet:
                messagebox.showerror("Catalog", str(exc))
            self.say("Catalog not loaded: %s" % exc)
            return
        keep = [i for i in self.items if i.entry is None]
        self.items = []
        self.avail.delete(0, "end")
        for e in entries:
            self.add_item(Item("%s%s" % (e.title, "  — " + e.author if e.author else ""),
                               entry=e))
        for i in keep:
            self.add_item(i)
        self.save_cfg()
        self.say("%d modules in the catalog" % len(entries))

    def add_archive(self):
        for p in filedialog.askopenfilenames(title="Module archive",
                                             filetypes=[("Zip archive", "*.zip"), ("All", "*")]):
            self.add_local(p)

    def add_folder(self):
        p = filedialog.askdirectory(title="An unpacked module folder")
        if p:
            self.add_local(p)

    def add_local(self, path):
        item = Item(os.path.basename(os.path.normpath(path)), path=path)
        self.add_item(item)
        self.avail.selection_clear(0, "end")
        self.avail.selection_set("end")
        self.show_selected()

    def describe(self, item):
        if item.error:
            return "Cannot install this:\n\n" + item.error
        lines = []
        if item.entry:
            e = item.entry
            lines += [e.title, "by " + e.author if e.author else "", "", e.about,
                      "", "From: " + e.url]
            if item.plan is None:
                lines += ["", "Install downloads it, checks it and copies it in."]
        p = item.plan
        if p:
            lines += [""] if lines else [p.title or "(untitled design)", ""]
            lines.append("Installs as %s (%d files)" % (p.folder, len(p.files)))
            lines += ["• " + n for n in p.notes]
            if any(f.upper() == p.folder.upper() for f, _ in self.installed):
                lines.append("• Already installed here: Install replaces it "
                             "(saved games are kept)")
            if p.skipped:
                lines += ["", "Left out (%d):" % len(p.skipped)]
                lines += ["  %s — %s" % s for s in p.skipped[:12]]
                if len(p.skipped) > 12:
                    lines.append("  … and %d more" % (len(p.skipped) - 12))
        return "\n".join(l for l in lines if l is not None)

    def show_selected(self):
        item = self.selected()
        if item is None:
            self.set_details("")
            return
        if item.path and item.plan is None and item.error is None:
            try:
                item.plan = core.plan(core.Source.from_path(item.path))
            except (core.ModError, OSError) as exc:
                item.error = str(exc)
        self.set_details(self.describe(item))

    def install(self):
        item = self.selected()
        t = self.target()
        if self.busy or item is None:
            return
        why = t.check()
        if why:
            messagebox.showerror("Install", why)
            return
        if item.error:
            messagebox.showerror("Install", item.error)
            return
        self.busy = True
        self.install_btn.state(["disabled"])
        threading.Thread(target=self.worker, args=(item, t), daemon=True).start()

    def worker(self, item, t):
        post = self.jobs.put
        try:
            if item.plan is None:
                post(("say", "Downloading %s…" % item.entry.title))
                path = catalog.fetch(item.entry, CACHE,
                                     progress=lambda g, n: post(("bar", g, n)))
                item.path = path
                item.plan = core.plan(core.Source.from_path(path))
            p = item.plan
            replace = t.find(p.folder) is not None
            if replace:
                ok = threading.Event()
                answer = []
                post(("ask", "%s is already installed. Replace it? Saved games "
                      "are kept." % p.folder, ok, answer))
                ok.wait()
                if not answer[0]:
                    post(("done", "Not installed."))
                    return
            post(("say", "Copying %s…" % p.folder))
            t.install(p, replace=replace, progress=lambda i, n, _r: post(("bar", i, n)))
            post(("done", "%s installed as %s. Start OpenUA and pick it with "
                          "Select a Design." % (p.title or p.design, p.folder)))
        except Exception as exc:          # every failure reaches the window
            item.error = item.error or (str(exc) if isinstance(exc, core.ModError) else None)
            post(("fail", str(exc)))

    def poll(self):
        try:
            while True:
                job = self.jobs.get_nowait()
                kind = job[0]
                if kind == "say":
                    self.say(job[1])
                elif kind == "bar":
                    self.progress.config(maximum=max(job[2], 1), value=job[1])
                elif kind == "ask":
                    job[3].append(messagebox.askyesno("Install", job[1]))
                    job[2].set()
                elif kind in ("done", "fail"):
                    self.busy = False
                    self.install_btn.state(["!disabled"])
                    self.progress.config(value=0)
                    self.refresh_target()
                    self.show_selected()
                    self.say(job[1])
                    if kind == "fail":
                        messagebox.showerror("Install", job[1])
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def remove(self):
        sel = self.inst.curselection()
        if not sel:
            return
        folder, title = self.installed[sel[0]]
        if not messagebox.askyesno("Remove", "Remove %s (%s) and its saved games?"
                                   % (folder, title or "untitled")):
            return
        try:
            self.target().remove(folder)
        except (core.ModError, OSError) as exc:
            messagebox.showerror("Remove", str(exc))
        self.refresh_target()
        self.say("%s removed." % folder)


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()
