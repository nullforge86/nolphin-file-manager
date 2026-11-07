/* nolphin-markdown-view.h
 *
 * Zeigt Markdown als gerenderten, markier- und kopierbaren Text in
 * einem GtkTextView. Die Umsetzung ist eigenständig (GtkTextTags, keine
 * zusätzliche Bibliothek).
 *
 * Unterstützt: Überschriften (# und Setext), Absätze, harte Zeilenumbrüche,
 * Aufzählungen und nummerierte Listen (auch verschachtelt, Aufgabenlisten),
 * Zitate, Code (Inline und eingezäunte Blöcke), Hervorhebungen (fett,
 * kursiv, durchgestrichen), Links, automatisch erkannte URLs, horizontale
 * Linien.
 *
 * Bilder: lokale Bilder innerhalb des Basisordners werden angezeigt.
 *
 * Tabellen (GFM) erscheinen als echte Tabellen mit Rahmen, Kopfzeile,
 * Zeilenstreifen und Spaltenausrichtung; Aufgabenlisten mit Kästchen.
 *
 * Nicht unterstützt und deshalb als Text dargestellt: entfernte Bilder (als Alternativtext), HTML (Tags
 * entfallen, der enthaltene Text bleibt erhalten; <br> wird zum
 * Zeilenumbruch).
 */

#ifndef NOLPHIN_MARKDOWN_VIEW_H
#define NOLPHIN_MARKDOWN_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*NolphinMarkdownLinkFunc) (GtkWidget   *view,
					 const gchar *href,
					 gpointer     user_data);

/* Schreibgeschütztes, markierbares GtkTextView. Der Aufrufer legt es bei
 * Bedarf in ein GtkScrolledWindow. */
GtkWidget *nolphin_markdown_view_new          (void);

/* Ersetzt den angezeigten Inhalt. NULL oder "" leert die Ansicht. */
void       nolphin_markdown_view_set_text     (GtkWidget  *view,
					       const gchar *markdown);

/* Wie _set_text(); relative Bildpfade werden gegen @base_dir aufgelöst. Es
 * werden nur lokale Bilder innerhalb von @base_dir angezeigt (Dateien bis
 * 16 MiB, Formate nach GdkPixbuf); entfernte Bilder (http/https) werden nicht
 * geladen und erscheinen als Alternativtext. Bilder in Links sind klickbar. */
void       nolphin_markdown_view_set_text_with_base (GtkWidget   *view,
						     const gchar *markdown,
						     const gchar *base_dir);

/* Begrenzt die Anzeigebreite von Bildern (Pixel, 0 = nur die Textbreite). */
void       nolphin_markdown_view_set_max_image_width (GtkWidget *view, gint max_width);

/* Wird beim Klick auf einen Link (ohne aktive Textauswahl) aufgerufen. */
void       nolphin_markdown_view_set_link_handler (GtkWidget               *view,
						   NolphinMarkdownLinkFunc  func,
						   gpointer                 user_data);

/* Springt zur Überschrift mit dem GitHub-Anker @anchor (z. B. "mein-abschnitt",
 * ohne "#"). Liefert FALSE, wenn es keine solche Überschrift gibt. */
gboolean   nolphin_markdown_view_scroll_to_anchor (GtkWidget *view, const gchar *anchor);

/* Rendert @markdown in @buffer (legt die benötigten Tags selbst an und
 * ersetzt den bisherigen Inhalt). Für nolphin_markdown_view_set_text() und
 * Tests ohne Display. Link-Ziele hängen als Objektdaten
 * "nolphin-md-href" an den Link-Tags. */
void       nolphin_markdown_render_to_buffer  (GtkTextBuffer *buffer,
					       const gchar   *markdown);

void       nolphin_markdown_render_to_buffer_with_base (GtkTextBuffer *buffer,
							const gchar   *markdown,
							const gchar   *base_dir);

/* Liefert das Link-Ziel, falls @tag ein Link-Tag ist, sonst NULL. */
const gchar *nolphin_markdown_tag_get_href    (GtkTextTag *tag);

/* Beschreibt den Inhalt eines Ankers als Text ("table:RxC:Ausrichtung:Zellen",
 * "check:0|1", "image:Pfad"), sonst NULL. Für Tests. Mit g_free() freigeben. */
gchar       *nolphin_markdown_anchor_describe (GtkTextChildAnchor *anchor);

G_END_DECLS

#endif /* NOLPHIN_MARKDOWN_VIEW_H */
