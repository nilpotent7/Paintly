# "Edit with Paintly" in file managers

How to get Paintly into the right-click menu of image files, per file manager and per
distribution channel.

Assumes the app ID and desktop entry from
[distribution.md § 2](distribution.md#2-the-shared-foundation):
`io.github.nilpotent7.Paintly`.

**Contents**

- [The two levels of integration](#the-two-levels-of-integration)
- [Level 1: MIME association](#level-1-mime-association-works-everywhere)
- [A trick that does not work](#a-trick-that-does-not-work)
- [Nautilus (GNOME Files)](#nautilus-gnome-files)
- [Dolphin (KDE)](#dolphin-kde)
- [Nemo (Cinnamon)](#nemo-cinnamon)
- [Thunar (XFCE)](#thunar-xfce)
- [Caja, PCManFM and others](#caja-pcmanfm-and-others)
- [What ships with which channel](#what-ships-with-which-channel)
- [Testing](#testing)
- [One caveat worth knowing](#one-caveat-worth-knowing-about)

---

## The two levels of integration

It is worth being precise about this up front, because the two are often conflated and
only one of them is portable.

**Level 1 — MIME association.** One `MimeType=` line in the desktop entry. Paintly
appears under the file manager's **Open With** menu for image files, in *every* file
manager, on *every* distribution, from *every* packaging channel, with no extra code.
The label is the app's name: **Paintly**.

**Level 2 — a literal top-level "Edit with Paintly" item.** Requires a per-file-manager
plugin or action file. There is no cross-desktop standard for this. It is a small amount
of work per file manager, and it cannot be shipped from inside a Flatpak or Snap.

Do Level 1 first — it is one line and it is what most users actually reach for. Add
Level 2 for Nautilus and Dolphin if you want the polish.

---

## Level 1: MIME association (works everywhere)

Already covered in [distribution.md § 2.1](distribution.md#21-the-desktop-entry). The
operative line is:

```ini
MimeType=image/png;image/jpeg;image/bmp;image/gif;
```

The mechanism: on install, `update-desktop-database` scans
`/usr/share/applications/*.desktop`, collects every `MimeType=` declaration, and writes
`mimeinfo.cache`. File managers query that cache through GIO
(`g_app_info_get_recommended_for_type()`) or KDE's equivalent to build their Open With
menus.

**Do not make Paintly the default handler from a package.** Hijacking `image/png` from
the user's image viewer is a good way to collect angry bug reports. Leave it as one
option among many. Users who *want* it as default can run:

```bash
xdg-mime default io.github.nilpotent7.Paintly.desktop image/png
```

Offering that as a one-line tip in your README is the right amount of pushiness.

---

## A trick that does not work

A tempting shortcut: ship a *second* desktop entry named "Edit with Paintly" and mark it
`NoDisplay=true` so it stays out of the application grid but still shows up in Open With
with a nicer label.

It does not work. `NoDisplay=true` makes GIO report the entry as not-showable, and
GTK's app chooser — which is what Nautilus's "Open With…" dialog is built on — skips
exactly those entries. Verified:

```
--- recommended for image/png ---
  test-edit-hidden.desktop      should_show=False  name='Edit with TestApp (hidden)'
  test-edit-visible.desktop     should_show=True   name='Edit with TestApp (visible)'
```

Both entries are returned by `get_recommended_for_type()`, but the `NoDisplay` one
reports `should_show=False` and gets filtered out before it reaches the menu.

Dropping `NoDisplay` makes it appear — and also puts a duplicate "Edit with Paintly"
launcher in the user's app grid, next to the real one. That is worse than the problem it
solves.

So: Level 1 gives you the label "Paintly". For anything else, use the per-file-manager
approaches below.

---

## Nautilus (GNOME Files)

### What you get for free

Less than you might assume, and it is worth knowing before you promise anything. Modern
Nautilus — checked against 46 and against current `main` — offers exactly two entry
points in an image file's context menu:

- **Open** → `view.open-with-default-application`, labelled with the *default* handler
  for that MIME type.
- **Open With…** → `view.open-with-other-application`, which opens a chooser dialog
  listing every registered handler.

There is no inline submenu enumerating the alternatives — that was GNOME 3.x-era
behaviour. So unless Paintly is the user's default for `image/png`, Level 1 puts it one
dialog deep. That is the strongest argument for the extension below, and the reason a
top-level item is worth the effort on GNOME specifically.

### Level 2: a nautilus-python extension

This is the only way to get a literal top-level *"Edit with Paintly"* item in Nautilus.

Install location — system-wide for a package, or the first path for testing:

```
~/.local/share/nautilus-python/extensions/paintly.py
/usr/share/nautilus-python/extensions/paintly.py
```

```python
"""Adds an "Edit with Paintly" item to the Nautilus context menu for images."""

import gi

gi.require_version("Nautilus", "4.0")
from gi.repository import GObject, Gio, Nautilus

APP_ID = "io.github.nilpotent7.Paintly.desktop"

# Kept in step with the MimeType= line in the desktop entry, which in turn
# matches what gdk_pixbuf_new_from_file() can decode on a default install.
SUPPORTED = {"image/png", "image/jpeg", "image/bmp", "image/gif"}


class PaintlyExtension(GObject.GObject, Nautilus.MenuProvider):
    def _usable(self, files):
        if not files:
            return False
        for f in files:
            if f.is_directory() or f.get_uri_scheme() != "file":
                return False
            if f.get_mime_type() not in SUPPORTED:
                return False
        return True

    def get_file_items(self, files):
        if not self._usable(files):
            return []
        item = Nautilus.MenuItem(
            name="PaintlyExtension::edit",
            label="Edit with Paintly",
            tip="Open the selected image in Paintly",
        )
        item.connect("activate", self._activate, files)
        return [item]

    def _activate(self, menu, files):
        # Launch through the installed desktop entry rather than by running
        # "paintly" directly.  That keeps this file identical whether Paintly
        # came from a .deb, a Flatpak or a Snap - each installs an entry under
        # the same ID with an Exec line appropriate to itself.
        app = Gio.DesktopAppInfo.new(APP_ID)
        if app is None:
            return
        app.launch([f.get_location() for f in files], None)
```

Three details that matter:

- **`Nautilus 4.0`, not `3.0`.** The 4.0 API arrived in Nautilus 43. Its
  `get_file_items()` takes `(self, files)` — the old `window` argument is gone. Passing
  the 3.0 signature to a modern Nautilus silently loads nothing.
- **Launch via `Gio.DesktopAppInfo`, not `subprocess`.** Spawning `paintly` by name
  fails for Flatpak and Snap users, where the binary is not on `PATH`. Going through the
  desktop ID works for all three.
- **The `file://` check.** `app_open()` in `src/ui/app.c` calls `g_file_get_path()` and
  errors out on anything else, so the menu item is hidden rather than offering an action
  that will fail.

Reload Nautilus to pick it up:

```bash
nautilus -q
```

**Runtime dependency:** `python3-nautilus` on Debian/Ubuntu, `nautilus-python` on
Fedora and Arch. Ship this in a **separate package** (`paintly-nautilus`) so the main
package doesn't drag in a Python interpreter for a menu label. See
[distribution.md § 4.3](distribution.md#43-desktop-entry-and-file-manager-integration-in-the-deb).

### The Flatpak/Snap limitation

Extensions are loaded into the *host's* Nautilus process from host paths. A sandboxed
app cannot write there — by design; that would be a sandbox escape. So:

| Channel | Open With ▸ Paintly | "Edit with Paintly" |
| --- | --- | --- |
| `.deb` / RPM / AUR / Nix | ✅ | ✅ (companion package) |
| Flatpak | ✅ | ❌ |
| Snap | ✅ | ❌ |
| AppImage | ❌ (unless integrated) | ❌ |

If you want the label for Flatpak users, publish the standalone `paintly-nautilus`
package to the distro channels and mention it on the Flathub page. Most projects don't
bother; Open With is the expected GNOME idiom.

### Legacy alternative: a Nautilus script

Zero dependencies, user-local only, and it appears under a **Scripts ▸** submenu rather
than top-level. Fine as a stopgap you document in the README, not something to package.

`~/.local/share/nautilus/scripts/Edit with Paintly`, `chmod +x`:

```bash
#!/bin/sh
exec gio launch /usr/share/applications/io.github.nilpotent7.Paintly.desktop "$@"
```

---

## Dolphin (KDE)

The best case of all four: KDE has a first-class mechanism, it needs no dependencies,
and it puts the item at the **top level** of the context menu.

Install to `/usr/share/kio/servicemenus/` from a package, or
`~/.local/share/kio/servicemenus/` for testing.

`io.github.nilpotent7.Paintly.EditWith.desktop`:

```ini
[Desktop Entry]
Type=Service
MimeType=image/png;image/jpeg;image/bmp;image/gif;
Actions=editWithPaintly;
X-KDE-Priority=TopLevel
# ServiceTypes is obsolete on KF6 but harmless, and is still required by KF5.
ServiceTypes=KonqPopupMenu/Plugin

[Desktop Action editWithPaintly]
Name=Edit with Paintly
Icon=io.github.nilpotent7.Paintly
Exec=paintly %f
```

- **`X-KDE-Priority=TopLevel`** is what promotes the item out of the *Actions* submenu
  and onto the context menu directly. Without it you get *Actions ▸ Edit with Paintly*.
- **`%f`**, matching the desktop entry, for the reason in
  [distribution.md § 2.1](distribution.md#21-the-desktop-entry). Use `%F` instead if you
  want one invocation to receive a multi-file selection — `app_open()` handles that,
  spawning one window per extra file.
- **`Exec=paintly`** must be a real command here. Unlike a `.desktop` *application*
  entry, a service menu is never rewritten by Flatpak, so for a Flatpak install this
  line has to read `flatpak run io.github.nilpotent7.Paintly %f`. Another reason to ship
  service menus from distro packages.

**Files installed to the user-local directory must be executable** — KIO treats the
`~/.local` path as unauthorized otherwise and silently ignores the file. This is the
single most common reason a service menu "doesn't work":

```bash
chmod +x ~/.local/share/kio/servicemenus/io.github.nilpotent7.Paintly.EditWith.desktop
```

Files in `/usr/share/kio/servicemenus/` are trusted and need mode `644`.

Package this as `paintly-kde` — one data file, no dependencies, `Enhances: dolphin`.

---

## Nemo (Cinnamon)

Nemo has its own action format. Install to `/usr/share/nemo/actions/` (package) or
`~/.local/share/nemo/actions/` (testing), as `paintly.nemo_action`:

```ini
[Nemo Action]
Name=Edit with Paintly
Comment=Open %N in Paintly
Exec=paintly %F
Icon-Name=io.github.nilpotent7.Paintly
Selection=NotNone
Mimetypes=image/png;image/jpeg;image/bmp;image/gif;
Dependencies=paintly;
```

- `Selection=NotNone` allows single or multi-file selections; use `Selection=S` to
  restrict to exactly one.
- `Dependencies=paintly;` makes Nemo hide the action when the binary is absent — useful
  if the action file ever gets left behind by an incomplete uninstall.
- Specify `Mimetypes=` **or** `Extensions=`, not both. `Mimetypes` is the correct choice
  here since `load_image_path()` dispatches on content, not on filename.

The item appears at the top level of the context menu. No dependencies, no restart —
Nemo re-reads the actions directory on the fly.

---

## Thunar (XFCE)

Thunar's *Custom Actions* live in a single `uca.xml` file, which Thunar owns and
rewrites. That makes it awkward to package: Thunar reads only the first `uca.xml` it
finds across the XDG config path, so dropping a system copy in
`/etc/xdg/Thunar/uca.xml` works **only for users who have never opened the Custom
Actions dialog** — the moment they do, Thunar writes `~/.config/Thunar/uca.xml` and your
system file is shadowed for good.

So: don't package it. Document it. Put this in your README, or link users here.

**Via the GUI** (recommended): Thunar → **Edit ▸ Configure custom actions… ▸ +**

| Field | Value |
| --- | --- |
| Name | `Edit with Paintly` |
| Command | `paintly %f` |
| Icon | `io.github.nilpotent7.Paintly` |
| File Pattern (Appearance tab) | `*` |
| Appears if selection contains | ☑ **Image Files** |

**By hand**, appended inside the `<actions>` element of `~/.config/Thunar/uca.xml`:

```xml
<action>
  <icon>io.github.nilpotent7.Paintly</icon>
  <name>Edit with Paintly</name>
  <unique-id>paintly-edit-1</unique-id>
  <command>paintly %f</command>
  <description>Open the selected image in Paintly</description>
  <patterns>*</patterns>
  <image-files/>
</action>
```

`<image-files/>` is Thunar's own filter, and it is the reason `<patterns>` can stay `*`.
`<unique-id>` need only be unique within the file. Thunar picks up changes without a
restart.

---

## Caja, PCManFM and others

**Caja (MATE)** mirrors Nautilus. The Python extension above works nearly verbatim
against `caja-python` — swap `gi.require_version("Nautilus", "4.0")` for
`gi.require_version("Caja", "2.0")`, `Nautilus.MenuProvider` for `Caja.MenuProvider`,
and restore the `window` first argument to `get_file_items()`, which Caja's older API
still passes. Install to `/usr/share/caja-python/extensions/`.

**PCManFM, PCManFM-Qt, and anything using the File Manager Actions spec** read
`.desktop` files with `Type=Action` from `/usr/share/file-manager/actions/`:

```ini
[Desktop Entry]
Type=Action
Name=Edit with Paintly
Icon=io.github.nilpotent7.Paintly
Profiles=paintly;

[X-Action-Profile paintly]
Name=Default
Exec=paintly %f
MimeTypes=image/png;image/jpeg;image/bmp;image/gif;
```

Support for this spec is patchy and shrinking. Ship it only if you have a user asking.

**Everything else** — Files (COSMIC), Nautilus forks, terminal file managers — gets
Level 1 through the MIME cache and nothing more. That is the correct baseline.

---

## What ships with which channel

| Artifact | Path | Ships from |
| --- | --- | --- |
| Desktop entry (`MimeType=`) | `share/applications/io.github.nilpotent7.Paintly.desktop` | **every** channel |
| Nautilus extension | `/usr/share/nautilus-python/extensions/paintly.py` | `paintly-nautilus` deb/RPM/AUR |
| Dolphin service menu | `/usr/share/kio/servicemenus/…EditWith.desktop` | `paintly-kde` deb/RPM/AUR |
| Nemo action | `/usr/share/nemo/actions/paintly.nemo_action` | `paintly-nemo`, or the main package |
| Thunar action | `~/.config/Thunar/uca.xml` | user-configured; document only |

The Nemo action and Dolphin service menu are single dependency-free data files. If
splitting packages feels like overkill for a 0.1.0, put both in the main package — a few
kilobytes of inert config on a machine running the other desktop is harmless. Only the
Nautilus extension genuinely needs its own package, because of `python3-nautilus`.

---

## Testing

Confirm the MIME cache picked Paintly up:

```bash
gio mime image/png
```

Expect Paintly among the "Registered applications". If it is missing, the cache is
stale:

```bash
sudo update-desktop-database /usr/share/applications
```

List handlers programmatically, including whether each will actually be *shown*:

```bash
python3 -c "import gi; gi.require_version('Gio','2.0'); from gi.repository import Gio; [print(a.get_id(), a.get_name(), a.should_show()) for a in Gio.AppInfo.get_recommended_for_type('image/png')]"
```

Check the entry itself, and that the launch actually works:

```bash
desktop-file-validate /usr/share/applications/io.github.nilpotent7.Paintly.desktop
```

```bash
gio launch /usr/share/applications/io.github.nilpotent7.Paintly.desktop ~/Pictures/test.png
```

Test against a sandboxed install without disturbing your system:

```bash
flatpak run --command=sh io.github.nilpotent7.Paintly
```

To exercise a file manager's context menu without touching your live desktop session,
use the nested-X approach — see the project's testing notes.

---

## One caveat worth knowing about

Paintly **opens** PNG, JPEG, BMP and GIF, but `do_save()` only ever writes PNG:

```c
cairo_status_t st = cairo_surface_write_to_png(flat, path);
```

So *"Edit with Paintly"* on `holiday.jpg` opens the image correctly, but Ctrl+S writes
`holiday.jpg.png` — a second file, leaving the original untouched. That is surprising
behaviour to reach from a context menu labelled *Edit*, and it is the first thing users
coming in through that menu will hit.

Two ways to handle it, in increasing order of effort:

1. **Narrow the advertised types.** Set `MimeType=image/png;` in the desktop entry and
   in each action file, so the context-menu item only offers itself for files Paintly can
   round-trip. Users can still open a JPEG through File ▸ Open. One-line change; no
   surprises.
2. **Add multi-format export** so the format follows the extension, and the
   suffix-appending logic in `on_save_done()` stops firing for non-PNG files.

Option 2 is the right end state, and it also resolves the sandbox save bug in
[distribution.md § 1.4](distribution.md#14-saving-under-a-sandbox-will-fail-for-names-without-png).
Until then, option 1 keeps the context-menu integration honest.
