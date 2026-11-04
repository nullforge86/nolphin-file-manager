/* nolphin-git-page.h - Git- und GitHub-Verwaltung als Seite im
 * rechten Arbeitsbereich.
 *
 * Alle Funktionen führen echte git- bzw. gh-Befehle (GitHub-CLI) im
 * Ordner der aktiven Ansicht aus; Ausgabe und Fortschritt erscheinen
 * im Ausgabefeld unten auf der Seite. Es gibt keine Platzhalter. */

#ifndef NOLPHIN_GIT_PAGE_H
#define NOLPHIN_GIT_PAGE_H

#include <gtk/gtk.h>
#include "nolphin-window.h"

G_BEGIN_DECLS

/* @quick_tab: der bisherige "Schnell"-Reiter (wird als erster Reiter
 * eingebaut), @back_button: "Zur Vorschau"-Knopf oben auf der Seite. */
GtkWidget *nolphin_git_page_new (NolphinWindow *window,
                                  GtkWidget     *quick_tab,
                                  GtkWidget     *back_button);

/* Wählt einen Reiter der Seite: "changes", "history", "branches",
 * "server", "tags", "conflicts", "github". */
void       nolphin_git_page_show_tab (GtkWidget *git_page, const gchar *tab_id);

/* Schreibt @text ins Ausgabefeld der Seite. */
void       nolphin_git_page_log (GtkWidget *git_page, const gchar *text);

G_END_DECLS

#endif /* NOLPHIN_GIT_PAGE_H */
