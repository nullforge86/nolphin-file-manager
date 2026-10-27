#!/bin/bash
# Prüft eine Nolphin-.deb vor der Installation, ohne etwas zu verändern.
# Aufruf: utils/paket-pruefen.sh ../nolphin_<Version>_amd64.deb
# Rückgabe 0: keine Risiken gefunden; 1: nicht installieren.
set -euo pipefail
export LC_ALL=C

deb="${1:-}"
if [ -z "$deb" ] || [ ! -f "$deb" ]; then
	echo "Aufruf: $0 PAKET.deb" >&2
	exit 2
fi
deb=$(realpath "$deb")
risiko=0

warnung() {
	echo "RISIKO: $*"
	risiko=1
}

echo "== System"
if [ -r /etc/linuxmint/info ]; then
	grep -E '^(RELEASE|CODENAME|EDITION)=' /etc/linuxmint/info
else
	warnung "kein Linux Mint (/etc/linuxmint/info fehlt)"
fi
grep -E '^(UBUNTU_CODENAME|VERSION_CODENAME)=' /etc/os-release || true
echo "Architektur: $(dpkg --print-architecture)"

echo
echo "== Paket"
for feld in Package Version Architecture Pre-Depends Depends Conflicts Breaks Replaces Provides; do
	wert=$(dpkg-deb -f "$deb" "$feld" || true)
	[ -n "$wert" ] && echo "$feld: $wert"
done

paket=$(dpkg-deb -f "$deb" Package)
arch=$(dpkg-deb -f "$deb" Architecture)
if [ "$arch" != all ] && [ "$arch" != "$(dpkg --print-architecture)" ]; then
	warnung "Architektur $arch passt nicht zum System"
fi

abh=$(dpkg-deb -f "$deb" Pre-Depends Depends | sed 's/^[^:]*: //' | sed 's/[,|]/\n/g' | sed 's/^ *//; s/ .*//' | grep -v '^$' || true)
for p in $abh; do
	case "$p" in
	*-dev) warnung "hängt von Entwicklerpaket $p ab" ;;
	libcinnamon*t64 | libmuffin*t64 | cinnamon*t64) warnung "hängt von Ubuntu-Paket $p ab (Mint verwendet einen anderen Namen)" ;;
	esac
done

echo
echo "== Dateien, die schon anderen Paketen gehören"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
dpkg-deb --fsys-tarfile "$deb" | tar -t | sed 's|^\./|/|; s|/$||' | grep -v '^/\?$' | sort -u >"$tmp/deb"
for liste in /var/lib/dpkg/info/*.list; do
	name=$(basename "$liste" .list)
	name=${name%%:*}
	case "$name" in
	"$paket" | nolphin-data | libnolphin-extension1 | libnolphin-extension-dev | gir1.2-nolphin-3.0 | nolphin-dbg) continue ;;
	esac
	awk -v p="$name" '{ print $0 "\t" p }' "$liste"
done | sort -t "$(printf '\t')" -k1,1 >"$tmp/system"
join -t "$(printf '\t')" "$tmp/deb" "$tmp/system" >"$tmp/doppelt" || true
# Verzeichnisse wie /usr/share teilen sich alle Pakete
fremd=0
while IFS="$(printf '\t')" read -r datei besitzer; do
	if [ ! -d "$datei" ]; then
		warnung "$datei gehört schon zu $besitzer"
		fremd=1
	fi
done <"$tmp/doppelt"
[ "$fremd" = 0 ] && echo "keine"

echo
echo "== Simulation: apt-get -s install"
if ! apt-get -s install "$deb" >"$tmp/apt" 2>&1; then
	cat "$tmp/apt"
	warnung "apt kann das Paket nicht installieren"
fi
grep -E '^(Inst|Remv)' "$tmp/apt" || true
while read -r aktion name rest; do
	[ "$name" = "$paket" ] && continue
	case "$aktion" in
	Remv)
		case "$name" in
		nolphin-data | libnolphin-extension1 | libnolphin-extension-dev | gir1.2-nolphin-3.0 | nolphin-dbg) ;;
		*) warnung "apt würde $name entfernen" ;;
		esac
		;;
	Inst)
		case "$name" in
		*cinnamon* | *muffin* | libgtk-3* | libglib2.0* | gir1.2-gtk* | gir1.2-glib* | libxapp* | xapp*)
			if [[ "$rest" == \[* ]]; then
				warnung "apt würde die Systembibliothek $name ändern: $rest"
			else
				warnung "apt würde die Desktop-Komponente $name neu installieren: $rest"
			fi
			;;
		esac
		;;
	esac
done <"$tmp/apt"

echo
if [ "$risiko" = 0 ]; then
	echo "Ergebnis: keine Risiken gefunden. Installieren mit: sudo apt install $deb"
else
	echo "Ergebnis: NICHT installieren, Risiken siehe oben."
fi
exit "$risiko"
