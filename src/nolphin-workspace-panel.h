/* nolphin-workspace-panel.h
 *
 * Rechte Arbeitsbereich-Leiste (F11): ein GtkStack ohne Reiterleiste.
 * "Vorschau" ist die Ruhelage und immer da, solange F11 an ist.
 * Eigenschaften, Archiv, Terminal, Git und der .deb-Paket-Ersteller
 * blenden sich nur ein, wenn ihre jeweilige Funktion tatsächlich
 * ausgelöst wird (Menü, Kontextmenü, Alt+Enter, F4, …) - über die
 * show_*()-Funktionen unten - statt dauerhaft als Reiter sichtbar zu
 * sein.
 */

#ifndef NOLPHIN_WORKSPACE_PANEL_H
#define NOLPHIN_WORKSPACE_PANEL_H

#include <gtk/gtk.h>

#include "nolphin-window.h"

G_BEGIN_DECLS

/* @preview_widget wird als "preview"-Seite eingehaengt, @terminal_widget
 * als "terminal"-Seite. Der Aufrufer bleibt weiterhin fuer deren
 * Lebenszyklus verantwortlich - dieses Modul haelt nur die Referenz zum
 * Einhaengen. */
GtkWidget *nolphin_workspace_panel_new (NolphinWindow *window,
					GtkWidget     *preview_widget,
					GtkWidget     *terminal_widget);

/* Wechselt zurück zur Vorschau-Ruhelage, ohne das Panel ein-/auszublenden. */
void       nolphin_workspace_panel_show_preview      (GtkWidget *workspace_panel);

/* Baut die Eigenschaften-Seite für @files neu auf, zeigt sie und blendet
 * das Panel ein (F11) - der Ersatz für nolphin_properties_window_present(). */
void       nolphin_workspace_panel_show_properties   (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       GList *files);

/* Wie _show_properties(), findet das Fenster selbst (Elternfenster von
 * @parent_widget, sonst das aktive, sonst ein neues). */
void       nolphin_workspace_panel_show_properties_anywhere (GList *files, GtkWidget *parent_widget);

/* Aktualisiert die Eigenschaften-Seite bei Änderung der Auswahl (nur wenn sichtbar). */
void       nolphin_workspace_panel_sync_properties   (GtkWidget *workspace_panel,
						       GList *selection,
						       NolphinFile *directory_as_file);

/* Behälter der Such-Seite: die Suchleiste (NolphinQueryEditor) des aktiven
 * Tabs wird beim Start einer Suche hierher verschoben und danach wieder
 * zurückgesetzt (siehe nolphin-window-slot.c). */
GtkWidget *nolphin_workspace_panel_get_search_host (GtkWidget *workspace_panel);

/* Zeigt die Archiv-Seite (Komprimieren) und blendet das Panel ein. */
void       nolphin_workspace_panel_show_archive      (GtkWidget *workspace_panel,
						       NolphinWindow *window);

/* Zeigt die Archiv-Verwaltung (Inhalt anzeigen, Dateien hinzufügen, ersetzen,
 * entfernen, einzelne Einträge entpacken, prüfen) für das Archiv @archive. */
void       nolphin_workspace_panel_show_archive_manager (GtkWidget *workspace_panel,
							  NolphinWindow *window,
							  GFile *archive);

/* Zeigt Regeln anwenden (Ordner) bzw. Stapelverarbeitung (Auswahl, Pfadliste). */
void       nolphin_workspace_panel_show_rules (GtkWidget *workspace_panel,
					       NolphinWindow *window,
					       GFile *folder,
					       GList *selection_paths,
					       gboolean batch_mode);

/* Zeigt Ordner vergleichen/synchronisieren mit @folder als lokalem Ordner. */
void       nolphin_workspace_panel_show_sync (GtkWidget *workspace_panel,
					      NolphinWindow *window,
					      GFile *folder);

/* Zeigt die Versionen einer Datei (Bearbeiten ▸ Werkzeuge ▸ Versionen). */
void       nolphin_workspace_panel_show_versions (GtkWidget *workspace_panel,
						  NolphinWindow *window,
						  GFile *file);

/* Zeigt die Duplikat-Suche für @folder (Bearbeiten ▸ Werkzeuge ▸ Duplikate finden). */
void       nolphin_workspace_panel_show_duplicates (GtkWidget *workspace_panel,
						    NolphinWindow *window,
						    GFile *folder);

/* Zeigt die Diagnose-Seite (0 Protokolle, 1 System, 2 Plugins, 3 Fehlerbericht). */
void       nolphin_workspace_panel_show_diagnostics (GtkWidget *workspace_panel,
						      NolphinWindow *window,
						      gint tab);

/* Zeigt die Arbeitsbereiche-Seite (Speichern, Laden, Duplizieren, Löschen). */
void       nolphin_workspace_panel_show_workspaces (GtkWidget *workspace_panel,
						     NolphinWindow *window,
						     gboolean focus_name);

/* Zeigt die Git-Seite, blendet das Panel ein und stößt sofort eine
 * Status-Aktualisierung an. */
void       nolphin_workspace_panel_show_git          (GtkWidget *workspace_panel,
						       NolphinWindow *window);
void       nolphin_workspace_panel_git_report        (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       const gchar *text);
void       nolphin_workspace_panel_show_git_action   (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       const gchar *action);

/* Zeigt die README.md des GID-Projekts @project_folder und blendet
 * das Panel ein. */
void       nolphin_workspace_panel_show_gid          (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       GFile *project_folder);

/* Zeigt den Handy-Austausch (Seitenleiste „Handy“, Datei ▸ Handy-Austausch)
 * und blendet das Panel ein. @autostart startet den Server sofort, wenn er
 * noch nicht läuft und ein lokales Netz vorhanden ist. */
void       nolphin_workspace_panel_show_phone        (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       gboolean autostart);

/* Zeigt den .deb-Paket-Ersteller und blendet das Panel ein. */
void       nolphin_workspace_panel_show_deb_builder  (GtkWidget *workspace_panel,
						       NolphinWindow *window);

/* Zeigt das eingebettete Terminal (F4) und blendet das Panel ein. */
void       nolphin_workspace_panel_show_terminal     (GtkWidget *workspace_panel,
						       NolphinWindow *window);

/* Löst die vorhandene Suche/Filterleiste aus (Pflicht-Panel
 * "Suche") und blendet das Panel ein. */
void       nolphin_workspace_panel_show_search       (GtkWidget *workspace_panel,
						       NolphinWindow *window);

/* Zeigt die Massenumbenennung für @files und blendet das Panel ein. */
void       nolphin_workspace_panel_show_batch_rename (GtkWidget *workspace_panel,
						       NolphinWindow *window,
						       GList *files);

G_END_DECLS

#endif /* NOLPHIN_WORKSPACE_PANEL_H */
