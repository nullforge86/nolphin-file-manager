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

Nolphin wird als **genau ein Paket `nolphin`** gebaut. Es enthält Programm, Daten, Übersetzungen, die Bibliothek `libnolphin-extension` und ihre Typelib. Entwicklerdateien (Header, `.pc`, `.gir`) sind nicht enthalten, damit das Paket keine `-dev`-Pakete auf das System zieht.

**Nur auf Linux Mint bauen.** Die Cinnamon-Bibliothek heißt bei Mint `libcinnamon-desktop4` (6.6), bei Ubuntu `libcinnamon-desktop4t64` (6.0). Ein auf Ubuntu gebautes Paket zieht auf Mint die ältere Ubuntu-Bibliothek nach, ersetzt damit die Cinnamon-Bibliothek, und der Desktop startet nicht mehr. Deshalb bricht `debian/rules` ab, wenn `libcinnamon-desktop4t64` installiert ist, und `debian/check-depends` verweigert jedes Paket, das `-dev`- oder Ubuntu-Cinnamon-Pakete voraussetzt.

```bash
sudo apt install devscripts equivs
sudo mk-build-deps -i -r debian/control     # Build-Abhängigkeiten aus debian/control
dpkg-buildpackage -us -uc -b                # Tests überspringen: DEB_BUILD_OPTIONS=nocheck
utils/paket-pruefen.sh ../nolphin_*_amd64.deb   # prüft, ändert nichts
sudo apt install ../nolphin_*_amd64.deb     # nur wenn die Prüfung "keine Risiken" meldet
```

`utils/paket-pruefen.sh` zeigt Mint-Version, Ubuntu-Basis und Architektur, die Felder `Depends`, `Conflicts`, `Breaks`, `Replaces` und `Provides`, Dateien, die schon anderen Paketen gehören, und das Ergebnis von `apt-get -s install`. Würde apt Desktop-Komponenten (Cinnamon, Muffin, GTK, GLib, XApp) ändern oder andere Pakete entfernen, meldet es ein Risiko und endet mit Rückgabewert 1.

Installiert wird nach `/usr/bin/nolphin`, `/usr/lib/<Architektur>/nolphin/`, `/usr/share/nolphin/` und das Schema `org.nolphin.gschema.xml`. Ältere, aufgeteilte Pakete (`nolphin-data`, `libnolphin-extension1`, `libnolphin-extension-dev`, `gir1.2-nolphin-3.0`, `nolphin-dbg`) ersetzt `apt` dabei automatisch.

Deinstallieren: `sudo apt remove nolphin` (Einstellungen in dconf und Dateien im persönlichen Ordner bleiben erhalten).

---

[Weiter: Der rechte Arbeitsbereich →](arbeitsbereich.md)
