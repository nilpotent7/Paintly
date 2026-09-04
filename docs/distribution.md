# Distributing Paintly

How to ship Paintly to users on Linux — Flatpak, `.deb`/apt, and the rest — and how to
make each channel install a proper desktop entry and hook into file managers.

Everything here is written against the tree as it stands: a plain `Makefile`, GTK4,
GResource-embedded assets, `org.paintly.Paintly` as the GApplication ID.

**Contents**

1. [Fix these first](#1-fix-these-first)
2. [The shared foundation](#2-the-shared-foundation) — app ID, `.desktop`, AppStream, icons
3. [Flatpak / Flathub](#3-flatpak--flathub)
4. [Debian / Ubuntu (`.deb`, PPA, apt)](#4-debian--ubuntu-deb-ppa-apt)
5. [Arch / AUR](#5-arch--aur)
6. [Fedora / COPR](#6-fedora--copr)
7. [Snap](#7-snap)
8. [AppImage](#8-appimage)
9. [Nix / nixpkgs](#9-nix--nixpkgs)
10. [Open Build Service](#10-open-build-service-many-distros-at-once)
11. [Recommended order](#11-recommended-order)
12. [Release checklist](#12-release-checklist)

File-manager context menus get their own document:
**[file-manager-integration.md](file-manager-integration.md)**.

---

## 1. Fix these first

These are real blockers found in the current tree. None is more than a few minutes'
work, but each one breaks something downstream.

### 1.1 `paintly.desktop` is not in the repository

`.gitignore` ends with:

```
/compile_commands.json
/paintly.desktop
/run
```

`make install` installs `paintly.desktop`, but the file is untracked — so a `git clone`
or a release tarball has no desktop entry at all, and `make install` fails. Every
packaging recipe below feeds on that file.

Drop the `/paintly.desktop` line from `.gitignore` and commit the file (renamed, per
§2.1). Keep `/run` and `/compile_commands.json` ignored.

### 1.2 The app ID will not pass Flathub, and breaks icon matching

`src/main.c` uses:

```c
app->gapp = gtk_application_new("org.paintly.Paintly", ...);
```

Two problems:

- **Flathub requires an ID you can prove you control** — a reverse-DNS name under a
  domain you own, or the `io.github.<user>.<App>` form if the project lives on GitHub.
  You do not own `paintly.org`, so `org.paintly.Paintly` will be rejected.
- **The desktop entry is matched to the window by ID.** On Wayland GTK4 sets the surface
  `app_id` from the GApplication ID. The shell then looks for
  `<app_id>.desktop`. Today the window says `org.paintly.Paintly` and the file is called
  `paintly.desktop`, so no match: generic icon in the dock, wrong name in the
  window switcher.

Fix both at once by adopting `io.github.nilpotent7.Paintly` (your repo is
`github.com/nilpotent7/Paintly`) and naming the desktop file to match:

```c
app->gapp = gtk_application_new("io.github.nilpotent7.Paintly",
                                G_APPLICATION_NON_UNIQUE |
                                G_APPLICATION_HANDLES_OPEN);
```

The rest of this guide assumes that ID. If you register a real domain later, switch to
`org.yourdomain.Paintly` before your first Flathub release — changing an app ID after
publication means users have to reinstall.

> The GResource prefix (`/org/paintly/...` in `data/resources.xml`) is internal to the
> binary and is *not* the app ID. You can leave it alone. If you like tidiness, GTK
> conventionally uses `/io/github/nilpotent7/Paintly/` and the icon-theme resource path
> registered in `app_startup` would move with it.

### 1.3 The link step drops `LDFLAGS`

```make
$(BUILD)/$(APP): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)
```

Debian, Fedora and openSUSE all inject hardening flags (`-Wl,-z,relro`,
`-Wl,-z,now`, `--as-needed`) through `LDFLAGS`. As written they are silently
discarded, and `lintian` will flag `hardening-no-bindnow`. Add it:

```make
$(BUILD)/$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
```

`CFLAGS` is already `+=`, so compiler hardening flags survive. Nothing to do there.

### 1.4 Saving under a sandbox will fail for names without `.png`

`on_save_done` in `src/ui/app.c` fixes up the *path* after the dialog returns:

```c
if (!g_str_has_suffix(path, ".png")) {
    char *fixed = g_strconcat(path, ".png", NULL);
    ...
}
```

Unsandboxed this is fine. Inside Flatpak or Snap, `GtkFileDialog` goes through the
file-chooser portal and hands back a document-portal path like
`/run/user/1000/doc/a1b2c3/untitled`. That directory is a FUSE mount that exposes
*exactly one* exported file — creating `untitled.png` beside it is not permitted, so
the save fails and the user gets an error dialog for a filename that looks perfectly
reasonable.

Fix it on the input side instead of the output side: set the initial name and a
`.png` filter on the `GtkFileDialog` so the portal exports a document that already
carries the suffix, and treat whatever path comes back as final. `save_with_dialog`
already calls `gtk_file_dialog_set_initial_name(fd, "untitled.png")` for new
documents; the gap is the "Save As" case where the user retypes the name.

Worth fixing before the first sandboxed build — it is the kind of bug that generates
"Paintly can't save" issues from Flatpak users only.

### 1.5 There is no version number

Nothing in the tree carries a version. `.deb`, RPM, AppStream `<release>` and the
Flathub listing all need one. Add to the `Makefile`:

```make
VERSION := 0.1.0
CFLAGS  += -DPAINTLY_VERSION=\"$(VERSION)\"
```

and tag releases `v0.1.0`. A `--version` flag and an About dialog can follow later;
the packaging only needs the number.

### 1.6 Decide the licence text

`LICENSE` is GPLv3, but no source file carries a header saying whether it is
"version 3 only" or "version 3 or later". AppStream and every distro's metadata need
one SPDX identifier. Pick `GPL-3.0-or-later` (the usual choice — it is what the GPLv3
"How to Apply" appendix recommends) or `GPL-3.0-only`, then say so in `README.md` and
in the AppStream file. This guide uses `GPL-3.0-or-later`.

---

## 2. The shared foundation

Build these four things once. Every channel below installs the same set.

### 2.1 The desktop entry

Rename `paintly.desktop` to `data/io.github.nilpotent7.Paintly.desktop` — the basename
**must** equal the app ID for the shell to associate window to launcher, and Flathub
enforces it.

```ini
[Desktop Entry]
Type=Application
Name=Paintly
GenericName=Image Editor
Comment=Fast, simple raster paint program
Exec=paintly %f
Icon=io.github.nilpotent7.Paintly
Terminal=false
Categories=Graphics;2DGraphics;RasterGraphics;GTK;
MimeType=image/png;image/jpeg;image/bmp;image/gif;
Keywords=paint;draw;drawing;image;editor;raster;bitmap;sketch;canvas;
StartupNotify=true
```

Notes specific to this codebase:

- **`%f`, not `%U`.** `app_open()` calls `g_file_get_path()` and bails with
  *"Only local files can be opened."* on anything else. `%f` makes the file manager
  hand over a local path (gvfs fuse-mounts remote locations for you); `%U` would pass
  `sftp://…` straight through and produce that error. Keep `%f`.
- **`MimeType` drives every "Open With" menu.** This one line is what makes Paintly
  appear in a file manager's context menu at all — see
  [file-manager-integration.md](file-manager-integration.md).
- **`Icon=` is the icon *name*, not a path**, and it must match the installed icon's
  filename (§2.3).
- The list matches what `load_image_path()` can actually open — it uses
  `gdk_pixbuf_new_from_file()`, so PNG, JPEG, BMP and GIF all load. Do not add formats
  gdk-pixbuf can't decode on a default install (e.g. `image/webp` needs a separate
  loader package) or users get an error dialog instead of an image.
- **`Exec=paintly` stays as-is for Flatpak and Snap.** Both rewrite the `Exec` line when
  they export the entry to the host. Do not hand-write `flatpak run …` here.

Do *not* add `StartupWMClass` blindly. Once §1.2 is done the IDs match and it is
unnecessary; a wrong value is worse than none. Verify after building:

```bash
xprop WM_CLASS   # then click the Paintly window (X11 only)
```

If that prints `"paintly", "Paintly"` rather than the app ID, add
`StartupWMClass=Paintly` to cover X11 sessions.

Validate before committing:

```bash
desktop-file-validate data/io.github.nilpotent7.Paintly.desktop
```

Silence means valid. The entry above passes.

### 2.2 AppStream metadata

Required by Flathub, and it is what fills in Paintly's page in GNOME Software, KDE
Discover, and the `appstream` catalogue that apt-based systems ship. Without it the app
is invisible in every graphical store.

Create `data/io.github.nilpotent7.Paintly.metainfo.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>io.github.nilpotent7.Paintly</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>

  <name>Paintly</name>
  <summary>Fast, simple raster paint program</summary>

  <developer id="io.github.nilpotent7">
    <name>Nilpotent</name>
  </developer>

  <description>
    <p>
      Paintly is a native Linux painting app for getting things done quickly: raster
      tools, layers, and an interface that stays out of the way. Nothing loads before
      the window appears.
    </p>
    <p>Features:</p>
    <ul>
      <li>Pencil, brush, eraser, fill bucket and simple shapes</li>
      <li>Layers with reordering, visibility toggles, per-layer opacity and live thumbnails</li>
      <li>Dual primary/secondary colour selection on left and right mouse buttons</li>
      <li>Undo history, selection and canvas resizing</li>
      <li>Opens PNG, JPEG, BMP and GIF images; saves PNG</li>
    </ul>
  </description>

  <launchable type="desktop-id">io.github.nilpotent7.Paintly.desktop</launchable>

  <screenshots>
    <screenshot type="default">
      <image>https://raw.githubusercontent.com/nilpotent7/Paintly/main/AppScreenshot.png</image>
      <caption>Editing an image in Paintly</caption>
    </screenshot>
  </screenshots>

  <url type="homepage">https://github.com/nilpotent7/Paintly</url>
  <url type="bugtracker">https://github.com/nilpotent7/Paintly/issues</url>
  <url type="vcs-browser">https://github.com/nilpotent7/Paintly</url>

  <content_rating type="oars-1.1"/>

  <branding>
    <color type="primary" scheme_preference="light">#f6f5f4</color>
    <color type="primary" scheme_preference="dark">#353535</color>
  </branding>

  <releases>
    <release version="0.1.0" date="2026-08-26">
      <description><p>First public release.</p></description>
    </release>
  </releases>
</component>
```

Requirements worth knowing:

- `<id>` must equal the app ID, and the file must be named `<app-id>.metainfo.xml`.
- Screenshot URLs must be absolute and publicly reachable — Flathub's build bot
  downloads them. The `raw.githubusercontent.com` link above works once
  `AppScreenshot.png` is on `main`, which it already is.
- `<content_rating type="oars-1.1"/>` with no children means "no objectionable
  content". Flathub rejects submissions without a rating.
- Add a new `<release>` for every version. Flathub shows the newest one as the
  changelog.

Validate:

```bash
appstreamcli validate data/io.github.nilpotent7.Paintly.metainfo.xml
```

The file above passes cleanly. With `--pedantic` you will also see
`cid-contains-uppercase-letter` — that is a style hint about the `Paintly` component in
the ID, not an error, and it is the normal shape for `io.github.<user>.<App>` IDs across
Flathub. Ignore it.

### 2.3 The icon

The app icon must be installed as `<app-id>.svg` under `hicolor/scalable/apps/`.
`data/icons/paintly-app.svg` is the source; it just gets renamed on install.

```
/usr/share/icons/hicolor/scalable/apps/io.github.nilpotent7.Paintly.svg
```

The other SVGs in `data/icons/` are toolbar icons baked into the binary by
`glib-compile-resources` — they are not installed to disk and need no packaging.

If you want a crisp icon in places that don't render SVG well (some docks, older
tooling), also install PNGs:

```
/usr/share/icons/hicolor/{64x64,128x128,256x256}/apps/io.github.nilpotent7.Paintly.png
```

Optional. The scalable SVG alone is enough for Flathub and for GNOME/KDE.

### 2.4 An install target that covers all of it

Replace the `install`/`uninstall` targets in the `Makefile`. This one target is what
every packaging recipe below calls, so getting it right here means the per-distro files
stay tiny.

```make
APPID   := io.github.nilpotent7.Paintly
DATADIR := $(DESTDIR)$(PREFIX)/share

install: all
	install -Dm755 $(BUILD)/$(APP)  $(DESTDIR)$(PREFIX)/bin/$(APP)
	install -Dm644 data/$(APPID).desktop \
	        $(DATADIR)/applications/$(APPID).desktop
	install -Dm644 data/$(APPID).metainfo.xml \
	        $(DATADIR)/metainfo/$(APPID).metainfo.xml
	install -Dm644 data/icons/paintly-app.svg \
	        $(DATADIR)/icons/hicolor/scalable/apps/$(APPID).svg
	@echo "==> installed to $(PREFIX)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(APP)
	rm -f $(DATADIR)/applications/$(APPID).desktop
	rm -f $(DATADIR)/metainfo/$(APPID).metainfo.xml
	rm -f $(DATADIR)/icons/hicolor/scalable/apps/$(APPID).svg
```

Two rules this deliberately follows:

- **`DESTDIR` is honoured everywhere.** Package builders install into a staging
  directory, never the live filesystem. The existing target already does this — keep it.
- **No post-install commands.** Do *not* call `update-desktop-database` or
  `gtk-update-icon-cache` from `make install`. Every package manager runs those itself
  (dpkg triggers, RPM file triggers, pacman hooks), and running them against a `DESTDIR`
  staging tree is wrong. They only matter for a manual `sudo make install`:

  ```bash
  sudo update-desktop-database /usr/local/share/applications
  ```

---

## 3. Flatpak / Flathub

The highest-value channel: one build reaches every distro, sandboxed, with automatic
updates and a store listing. Do this one first.

### 3.1 Manifest

Create `io.github.nilpotent7.Paintly.yml` in the repo root (or `build-aux/`):

```yaml
id: io.github.nilpotent7.Paintly
runtime: org.gnome.Platform
runtime-version: '50'
sdk: org.gnome.Sdk
command: paintly

finish-args:
  - --share=ipc
  - --socket=wayland
  - --socket=fallback-x11
  - --device=dri

modules:
  - name: paintly
    buildsystem: simple
    build-commands:
      - make PREFIX=/app
      - make install PREFIX=/app
    sources:
      - type: git
        url: https://github.com/nilpotent7/Paintly.git
        tag: v0.1.0
        commit: <full-sha-of-the-tag>
```

Points that matter:

- **Runtime version.** `50` is current as of August 2026 (`49` is still supported;
  `48` went EOL in March 2026). Check what's live before you submit:

  ```bash
  flatpak remote-ls flathub --system | grep org.gnome.Platform
  ```

  The GNOME runtime already carries GTK4 and the gdk-pixbuf JPEG/BMP/GIF loaders
  `load_image_path()` depends on, so there are no extra modules to build.

- **`finish-args` is deliberately minimal — no `--filesystem=` at all.** Paintly uses
  `GtkFileDialog`, which inside a sandbox automatically routes through the
  XDG file-chooser portal. The user picks a file in a host-side dialog, the document
  portal exports just that file into the sandbox, and `g_file_get_path()` returns a real
  path (`/run/user/1000/doc/…`) that the existing code opens without modification.
  Resist adding `--filesystem=home` — reviewers push back on it, and it earns the app a
  "Potentially unsafe" badge on its Flathub page.

  Same story for **Open With**: the file manager launches the exported entry, the
  document portal exports that one image, and `app_open()` receives a usable path. No
  extra permission needed.

- **`--device=dri`** lets GTK4 composite on the GPU, which is the performance claim in
  the README. Keep it.

- **Multi-file opens keep working.** `spawn_instance()` re-execs `/proc/self/exe`, which
  inside the sandbox is `/app/bin/paintly` — the new process starts inside the same
  sandbox instance. Nothing to change.

- **`type: git` with a pinned `commit:`** is what Flathub requires; `type: dir` (handy
  for local testing) is rejected on submission.

### 3.2 Build and test locally

```bash
flatpak install flathub org.gnome.Sdk//50 org.gnome.Platform//50 org.flatpak.Builder
```

```bash
flatpak run org.flatpak.Builder --force-clean --user --install \
  builddir io.github.nilpotent7.Paintly.yml
```

```bash
flatpak run io.github.nilpotent7.Paintly
```

Lint exactly as the reviewers will:

```bash
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest io.github.nilpotent7.Paintly.yml
```

```bash
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

The linter is strict about precisely the things in §2: the desktop file, metainfo file
and icon must all be named for the app ID and land in
`/app/share/{applications,metainfo,icons/hicolor/scalable/apps}`.

### 3.3 Desktop entry and file-manager integration under Flatpak

You do nothing extra. On install, Flatpak copies the entry to
`~/.local/share/flatpak/exports/share/applications/` (or the system equivalent) and
rewrites `Exec` to something like:

```ini
Exec=/usr/bin/flatpak run --branch=stable --arch=x86_64 --command=paintly io.github.nilpotent7.Paintly %f
```

Because your `MimeType=` line came along for the ride, Paintly shows up under
**Open With** for PNG/JPEG/BMP/GIF in every file manager, with no host-side files.

**The one real limitation:** a Flatpak cannot install a Nautilus extension. Extensions
are Python or C plugins loaded *into the host's Nautilus process*, and a sandboxed app
has no way to put files there. So under Flatpak you get **Open With ▸ Paintly**, not a
literal top-level *"Edit with Paintly"* item. If you want that exact label for Flatpak
users, ship the extension as a separate, tiny distro package — see
[file-manager-integration.md § Nautilus](file-manager-integration.md#nautilus-gnome-files).

### 3.4 Submitting to Flathub

1. Verify the ID is right — `io.github.nilpotent7.*` requires that
   `github.com/nilpotent7` is yours. It is.
2. Fork <https://github.com/flathub/flathub>.
3. Branch **from `new-pr`**, not from `master`:

   ```bash
   git checkout -b paintly new-pr
   ```

4. Add only `io.github.nilpotent7.Paintly.yml` (plus a `flathub.json` if you need to
   restrict architectures). Commit and push.
5. Open a PR titled `Add io.github.nilpotent7.Paintly`.
6. Comment `bot, build` to trigger a test build; fix whatever the reviewers raise.
7. On merge you get write access to a dedicated `flathub/io.github.nilpotent7.Paintly`
   repo. **GitHub 2FA must be enabled** to accept the invite.

After that first review, pushing a new tag + updated manifest to that repo publishes an
update automatically. No further review, ever.

---

## 4. Debian / Ubuntu (`.deb`, PPA, apt)

Two audiences: people who download a `.deb` from your GitHub Releases page, and people
who want `apt install paintly`. Same `debian/` directory serves both.

Install the tooling:

```bash
sudo apt install devscripts debhelper dh-make lintian
```

### 4.1 The `debian/` directory

`debian/control`:

```
Source: paintly
Section: graphics
Priority: optional
Maintainer: Nilpotent <behrozkhan480@gmail.com>
Build-Depends: debhelper-compat (= 13),
               pkg-config,
               libgtk-4-dev,
               libglib2.0-dev-bin,
               desktop-file-utils
Standards-Version: 4.7.0
Homepage: https://github.com/nilpotent7/Paintly
Rules-Requires-Root: no

Package: paintly
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Suggests: paintly-nautilus
Description: fast, simple raster paint program
 Paintly is a native GTK4 painting application for getting things done
 quickly: raster tools, layers, and an interface that stays out of the way.
 .
 It provides pencil, brush, eraser, fill bucket and shape tools, layers with
 reordering and per-layer opacity, undo history, selections and canvas
 resizing. It opens PNG, JPEG, BMP and GIF images and saves PNG.
```

`libglib2.0-dev-bin` is what ships `glib-compile-resources`. `libgtk-4-dev` pulls it in
transitively, but naming it makes the dependency honest. Runtime dependencies are
derived automatically by `dh_shlibdeps` from the linked libraries — do not hand-write
`libgtk-4-1`.

`debian/rules` — must be executable (`chmod +x`):

```make
#!/usr/bin/make -f

%:
	dh $@

override_dh_auto_install:
	dh_auto_install -- PREFIX=/usr
```

`dh_auto_install` calls `make install DESTDIR=debian/paintly`, but does not set
`PREFIX`, so without that override everything lands in `/usr/local` inside the package —
where no desktop database or icon theme will look. This one line is the single most
common packaging mistake for `Makefile`-based projects.

`debian/source/format`:

```
3.0 (quilt)
```

`debian/changelog` — generate rather than hand-write:

```bash
dch --create --package paintly --newversion 0.1.0-1 --distribution unstable
```

`debian/copyright` — DEP-5 format, declaring `GPL-3.0-or-later` (§1.6).

### 4.2 Build

Quilt format expects an upstream tarball one directory up:

```bash
git archive --format=tar.gz --prefix=paintly-0.1.0/ -o ../paintly_0.1.0.orig.tar.gz v0.1.0
```

```bash
dpkg-buildpackage -us -uc -b
```

```bash
lintian ../paintly_0.1.0-1_amd64.deb
```

Sanity-check the payload:

```bash
dpkg -c ../paintly_0.1.0-1_amd64.deb
```

You should see the binary in `/usr/bin`, and the desktop, metainfo and icon files under
`/usr/share`. If they are under `/usr/local/share`, the `override_dh_auto_install` above
is missing or misspelled.

### 4.3 Desktop entry and file-manager integration in the `.deb`

Because `make install` (§2.4) places the desktop file in
`/usr/share/applications/`, dpkg's trigger machinery handles the rest:

- `desktop-file-utils` has a trigger on `/usr/share/applications` and runs
  `update-desktop-database` after your package unpacks, rebuilding the MIME→app cache.
  That cache is what puts Paintly in every file manager's **Open With** menu.
- `hicolor-icon-theme` triggers the icon-cache refresh for `/usr/share/icons/hicolor`.

So there is **no `postinst` to write**. Debian explicitly wants you not to; hand-rolled
maintainer scripts that call these tools are a lintian error.

For a real *"Edit with Paintly"* context-menu item, ship a **second, separate package**
`paintly-nautilus` carrying only the Python extension. It has to `Depends: paintly,
python3-nautilus`, and dragging a Python interpreter plus GObject bindings into the main
package for a menu label is not a trade worth making — hence `Suggests:` above. Build it
by adding to `debian/control`:

```
Package: paintly-nautilus
Architecture: all
Depends: paintly (= ${binary:Version}), python3-nautilus, ${misc:Depends}
Enhances: nautilus
Description: Edit with Paintly context menu for GNOME Files
 Adds an "Edit with Paintly" entry to the right-click menu for image files
 in Nautilus.
```

and a `debian/paintly-nautilus.install` listing the extension's path. The extension
source itself is in
[file-manager-integration.md](file-manager-integration.md#nautilus-gnome-files).

The same pattern gives you `paintly-kde` (a Dolphin service menu, no dependencies at
all) — see that document.

### 4.4 Getting it into apt

Ranked by effort:

**A GitHub Release `.deb`** — zero infrastructure. Users download and
`sudo apt install ./paintly_0.1.0-1_amd64.deb`. No automatic updates. Fine for a first
release; attach the `.deb` to the same tag you gave Flathub.

**A Launchpad PPA** — real `apt` integration for Ubuntu, with updates. Users run:

```bash
sudo add-apt-repository ppa:nilpotent7/paintly && sudo apt install paintly
```

To publish: create the PPA on Launchpad, upload a GPG key, then build *source-only*
packages (Launchpad builds the binaries itself) once per Ubuntu series:

```bash
debuild -S -sa
```

```bash
dput ppa:nilpotent7/paintly ../paintly_0.1.0-1_source.changes
```

Each series needs its own version — `0.1.0-1~noble1`, `0.1.0-1~plucky1` — since
Launchpad refuses a version it has already seen. Bump the `~seriesN` suffix in
`debian/changelog` and re-upload.

**Debian proper** — needs a Debian Developer to sponsor the package through the NEW
queue, and commits you to Debian's policy and release cadence. Worth doing eventually,
not for a 0.1.0.

---

## 5. Arch / AUR

The cheapest channel after Flatpak: one file, no build infrastructure, users get updates
through their normal AUR helper.

`PKGBUILD`:

```bash
pkgname=paintly
pkgver=0.1.0
pkgrel=1
pkgdesc="Fast, simple raster paint program"
arch=('x86_64' 'aarch64')
url="https://github.com/nilpotent7/Paintly"
license=('GPL-3.0-or-later')
depends=('gtk4')
makedepends=('pkgconf' 'glib2-devel')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "Paintly-$pkgver"
  make PREFIX=/usr
}

package() {
  cd "Paintly-$pkgver"
  make install PREFIX=/usr DESTDIR="$pkgdir"
}
```

Replace `SKIP` with the real hash from `updpkgsums` before publishing.

Desktop and file-manager integration need nothing extra: pacman hooks
(`desktop-file-utils`, `gtk-update-icon-cache`) fire on `/usr/share/applications` and
`/usr/share/icons/hicolor` automatically. For the Nautilus extension, add a companion
`paintly-nautilus` AUR package depending on `python-nautilus`.

Publish by pushing `PKGBUILD` + `.SRCINFO` to `ssh://aur@aur.archlinux.org/paintly.git`:

```bash
makepkg --printsrcinfo > .SRCINFO
```

---

## 6. Fedora / COPR

COPR is Fedora's equivalent of a PPA — you push a spec, it builds RPMs for every Fedora
and EPEL release you tick.

`paintly.spec`:

```spec
Name:           paintly
Version:        0.1.0
Release:        1%{?dist}
Summary:        Fast, simple raster paint program

License:        GPL-3.0-or-later
URL:            https://github.com/nilpotent7/Paintly
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc make pkgconfig(gtk4) glib2-devel
BuildRequires:  desktop-file-utils libappstream-glib

%description
Paintly is a native GTK4 painting application with raster tools, layers and
an interface that stays out of the way.

%prep
%autosetup -n Paintly-%{version}

%build
%make_build PREFIX=%{_prefix}

%install
%make_install PREFIX=%{_prefix}

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.nilpotent7.Paintly.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/io.github.nilpotent7.Paintly.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/paintly
%{_datadir}/applications/io.github.nilpotent7.Paintly.desktop
%{_metainfodir}/io.github.nilpotent7.Paintly.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.nilpotent7.Paintly.svg
```

`%make_install` already passes `DESTDIR=%{buildroot}`, so `PREFIX=%{_prefix}` is the only
addition needed. Modern Fedora runs `update-desktop-database` and the icon-cache refresh
through RPM file triggers, so no `%post` scriptlets.

Upload with `copr-cli build <project> paintly.spec`, or point COPR at the repo for
automatic rebuilds on tag.

---

## 7. Snap

Worth doing if you want Ubuntu Software coverage without a PPA. The `gnome` extension
supplies GTK4 and the theming.

`snap/snapcraft.yaml`:

```yaml
name: paintly
base: core24
adopt-info: paintly
summary: Fast, simple raster paint program
description: |
  Paintly is a native GTK4 painting application with raster tools, layers and
  an interface that stays out of the way.
license: GPL-3.0-or-later
grade: stable
confinement: strict

apps:
  paintly:
    command: usr/bin/paintly
    desktop: usr/share/applications/io.github.nilpotent7.Paintly.desktop
    extensions: [gnome]
    plugs:
      - home
      - removable-media

parts:
  paintly:
    plugin: make
    source: https://github.com/nilpotent7/Paintly.git
    source-tag: v0.1.0
    make-parameters: [PREFIX=/usr]
    build-packages: [libgtk-4-dev, pkg-config, libglib2.0-dev-bin]
    override-pull: |
      craftctl default
      craftctl set version="0.1.0"
```

The `desktop:` key is what does the integration work: snapd copies the entry to
`/var/lib/snapd/desktop/applications/`, rewrites `Exec` to `snap run paintly %f`, and
rewrites `Icon=` to the unpacked icon. Your `MimeType=` line carries over, so **Open
With** works.

`home` and `removable-media` are listed because Snap's portal story is less complete
than Flatpak's; without `home`, opening a file from `~/Pictures` may fail depending on
the session. As with Flatpak, a Snap cannot install a Nautilus extension.

```bash
snapcraft
```

```bash
sudo snap install --dangerous ./paintly_0.1.0_amd64.snap
```

Publish via `snapcraft login && snapcraft upload --release=stable paintly_0.1.0_amd64.snap`.

---

## 8. AppImage

A single executable file the user downloads and runs. Good for "try it now" links,
**bad for the goal in this guide**: an AppImage is not installed, so nothing registers
its desktop entry or MIME types. There is no "Edit with Paintly" — and no
"Open With ▸ Paintly" either — unless the user separately runs `appimaged` or a tool
like Gear Lever to integrate it.

If you still want one, `linuxdeploy` with the GTK plugin is the path:

```bash
make PREFIX=/usr
make install PREFIX=/usr DESTDIR=AppDir
```

```bash
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin gtk \
  -d AppDir/usr/share/applications/io.github.nilpotent7.Paintly.desktop \
  -i AppDir/usr/share/icons/hicolor/scalable/apps/io.github.nilpotent7.Paintly.svg \
  --output appimage
```

The GTK plugin has to bundle the GTK4 stack, gdk-pixbuf loaders (needed by
`load_image_path()` for JPEG/BMP/GIF), GSettings schemas and an icon theme. Expect a
~120 MB file and expect to debug missing loaders on distros older than your build host.

Given Flatpak covers the same "works everywhere" need with proper integration, treat
AppImage as optional.

---

## 9. Nix / nixpkgs

Small, and Nix users will otherwise package it themselves. `wrapGAppsHook4` is
mandatory — without it GTK4 can't find its schemas or the icon theme at runtime.

```nix
{ lib, stdenv, fetchFromGitHub, pkg-config, wrapGAppsHook4, gtk4, glib }:

stdenv.mkDerivation rec {
  pname = "paintly";
  version = "0.1.0";

  src = fetchFromGitHub {
    owner = "nilpotent7";
    repo = "Paintly";
    rev = "v${version}";
    hash = lib.fakeHash;   # replace with the real hash
  };

  nativeBuildInputs = [ pkg-config wrapGAppsHook4 glib ];
  buildInputs = [ gtk4 ];

  makeFlags = [ "PREFIX=$(out)" ];

  meta = with lib; {
    description = "Fast, simple raster paint program";
    homepage = "https://github.com/nilpotent7/Paintly";
    license = licenses.gpl3Plus;
    mainProgram = "paintly";
    platforms = platforms.linux;
  };
}
```

NixOS collects desktop entries from every installed package into
`/run/current-system/sw/share/applications`, so Open With works once the package is in
`environment.systemPackages`.

---

## 10. Open Build Service (many distros at once)

If maintaining separate `debian/` and `.spec` recipes becomes tiresome, openSUSE's
[Open Build Service](https://build.opensuse.org) takes both and builds `.deb` and `.rpm`
for Debian, Ubuntu, Fedora, openSUSE and more from one project, publishing per-distro
apt/dnf repositories. It reuses the exact `debian/` and `paintly.spec` from §4 and §6 —
no new packaging work, just a build farm and hosted repos.

Worth setting up once you have more than two RPM/DEB targets to keep in sync.

---

## 11. Recommended order

1. **§1 fixes + §2 foundation.** Nothing else works until these land.
2. **Flatpak → Flathub.** Widest reach for the least work, and the store listing doubles
   as a landing page.
3. **AUR.** One file, and Arch users are disproportionately likely to try a new paint app.
4. **`.deb` on GitHub Releases.** Covers Debian/Ubuntu/Mint/Zorin — your README's stated
   audience — without running a repository.
5. **PPA and/or COPR** once releases are regular enough to justify the upload cadence.
6. **Snap, Nix, AppImage** on demand.

Skip nothing in step 1. Steps 3–6 are all optional.

---

## 12. Release checklist

```bash
# validate metadata
desktop-file-validate data/io.github.nilpotent7.Paintly.desktop
appstreamcli validate data/io.github.nilpotent7.Paintly.metainfo.xml

# clean build from a pristine checkout
make clean && make

# verify the install layout with a staging dir
make install PREFIX=/usr DESTDIR=/tmp/paintly-stage
find /tmp/paintly-stage -type f
```

Then, per release:

- [ ] Bump `VERSION` in the `Makefile`
- [ ] Add a `<release>` entry to the metainfo file
- [ ] `git tag -a v0.1.0 -m "Paintly 0.1.0"` and push the tag
- [ ] Update the Flathub manifest's `tag:` **and** `commit:`
- [ ] Update `PKGBUILD` `pkgver` + `sha256sums`, regenerate `.SRCINFO`
- [ ] `dch -v 0.1.0-1`, rebuild the `.deb`, attach it to the GitHub Release
