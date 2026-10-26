[Startseite](../README.md) | [Der rechte Arbeitsbereich →](arbeitsbereich.md)

# Bauen und Installieren

## Voraussetzungen

Nolphin läuft ausschließlich unter Linux und ist für Linux Mint mit Cinnamon optimiert.

* Meson (≥ 0.64), Ninja, C-Compiler (C11)
* GTK3, GLib, GIO, GVfs, VTE 2.91
* optional: XApp (`-Dxapp=disabled`), cinnamon-desktop (`-Dcinnamon_desktop=disabled`), libexif, exempi
* Laufzeitwerkzeuge, je nach Funktion: `zip`/`unzip`, `tar`, `7z`, `zstd`, `lz4`, `xz`, `git`, `gpg`, `getfacl`/`setfacl`, `rsync`, `pdfinfo`/`pdftocairo` (poppler-utils), `gst-discoverer-1.0` (gstreamer1.0-tools)

## Bauen und testen

```bash
meson setup build
ninja -C build
meson test -C build
```

## Handy-App bauen (optional)

Für den Handy-Austausch gibt es eine kleine Android-App. Sie wird **vor** dem Installieren gebaut und dann mitinstalliert:

```bash
../nolphin-handy/build.sh
```

Voraussetzungen: JDK und das Android-SDK (`ANDROID_HOME`, standardmäßig `~/Android/Sdk`). Ohne die App gibt es **keinen Handy-Austausch** (der Weg über den Browser des Handys ist entfallen); alles andere in Nolphin läuft unverändert. Die App liegt in einem eigenen Projekt (`../nolphin-handy`, GitHub: kaysiebke-cell/nolphin-handy); mehr in dessen README.

## Installieren

```bash
sudo ninja -C build install
```

## Debian-Paket bauen (Linux Mint 22.3 / Ubuntu 24.04)

Nolphin wird als **genau ein Paket `nolphin`** gebaut. Es enthält Programm, Daten, Übersetzungen, die Bibliothek `libnolphin-extension`, die GIR-Daten und die Entwicklerdateien für Erweiterungen.

```bash
sudo apt install devscripts equivs
sudo mk-build-deps -i -r debian/control     # Build-Abhängigkeiten aus debian/control
dpkg-buildpackage -us -uc -b                # Tests überspringen: DEB_BUILD_OPTIONS=nocheck
sudo apt install ../nolphin_*_amd64.deb
```

Installiert wird nach `/usr/bin/nolphin`, `/usr/lib/<Architektur>/nolphin/`, `/usr/share/nolphin/` und das Schema `org.nolphin.gschema.xml`. Ältere, aufgeteilte Pakete (`nolphin-data`, `libnolphin-extension1`, `libnolphin-extension-dev`, `gir1.2-nolphin-3.0`, `nolphin-dbg`) ersetzt `apt` dabei automatisch.

Deinstallieren: `sudo apt remove nolphin` (Einstellungen in dconf und Dateien im persönlichen Ordner bleiben erhalten).

---

[Weiter: Der rechte Arbeitsbereich →](arbeitsbereich.md)
