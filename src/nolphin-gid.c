/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-gid.c: GID-Projekte
 */

#include <config.h>

#include "nolphin-gid.h"
#include "nolphin-markdown-view.h"
#include "nolphin-tools.h"

#include <glib/gi18n.h>
#include <string.h>

/* Größer als das lädt die Seite nicht (die Textvorschau hat ebenfalls eine Grenze). */
#define README_MAX_BYTES (1024 * 1024)
#define HISTORY_MAX 100

/* --- Panel-Seite --------------------------------------------------------------- */

typedef struct {
	NolphinWindow *window;
	GtkWidget     *title;
	GtkWidget     *path_label;
	GtkWidget     *branch_label;
	GtkWidget     *status_label;
	GtkWidget     *crumbs;      /* Brotkrumen wie auf GitHub */
	GtkWidget     *back_button;
	GtkWidget     *forward_button;
	GtkWidget     *up_button;
	GtkWidget     *home_button;
	GtkWidget     *stack;       /* "readme" | "message" */
	GtkWidget     *message;
	GtkWidget     *view;
	GtkWidget     *scroller;
	GFile         *folder;      /* gewähltes Projekt (Startseite) */
	GFile         *boundary;    /* Obergrenze der Navigation: Git-Wurzel oder Projekt */
	GFile         *current;     /* gerade angezeigte Datei oder Ordner */
	GPtrArray     *back;        /* GFile*, zuletzt besuchte hinten */
	GPtrArray     *forward;
} GidPage;

static void gid_page_show (GidPage *d, GFile *target, gboolean record);

static void
gid_page_free (gpointer data)
{
	GidPage *d = data;

	g_clear_object (&d->folder);
	g_clear_object (&d->boundary);
	g_clear_object (&d->current);
	g_ptr_array_unref (d->back);
	g_ptr_array_unref (d->forward);
	g_free (d);
}

static void
gid_page_show_message (GidPage *d, const gchar *text)
{
	gtk_label_set_text (GTK_LABEL (d->message), text);
	gtk_stack_set_visible_child_name (GTK_STACK (d->stack), "message");
}

/* Findet README.md (ohne Beachtung der Groß-/Kleinschreibung) im Ordner. */
static GFile *
find_readme (GFile *folder)
{
	GFileEnumerator *en;
	GFileInfo *info;
	GFile *found = NULL;

	en = g_file_enumerate_children (folder, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
					G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (en == NULL)
		return NULL;
	while (found == NULL && (info = g_file_enumerator_next_file (en, NULL, NULL)) != NULL) {
		const gchar *name = g_file_info_get_name (info);

		if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY &&
		    (g_ascii_strcasecmp (name, "README.md") == 0 ||
		     g_ascii_strcasecmp (name, "README.markdown") == 0))
			found = g_file_get_child (folder, name);
		g_object_unref (info);
	}
	g_object_unref (en);
	return found;
}

/* Git-Wurzel (Ordner mit .git) oberhalb von @folder, sonst @folder selbst. */
static GFile *
find_boundary (GFile *folder)
{
	GFile *dir = g_object_ref (folder);

	while (dir != NULL) {
		GFile *git = g_file_get_child (dir, ".git");
		gboolean has = g_file_query_exists (git, NULL);
		GFile *parent;

		g_object_unref (git);
		if (has)
			return dir;
		parent = g_file_get_parent (dir);
		g_object_unref (dir);
		dir = parent;
	}
	return g_object_ref (folder);
}

static gboolean
file_is_inside (GFile *root, GFile *file)
{
	return g_file_equal (root, file) || g_file_has_prefix (file, root);
}

static gboolean
name_is_markdown (const gchar *name)
{
	return g_str_has_suffix (name, ".md") || g_str_has_suffix (name, ".MD") ||
	       g_str_has_suffix (name, ".markdown");
}

static gchar *
build_code_markdown (const gchar *text)
{
	GString *md = g_string_new ("```\n");
	gsize n = strlen (text);

	/* Dreifache Zäune im Text würden den Block vorzeitig schließen. */
	if (strstr (text, "```") != NULL) {
		g_string_assign (md, "````\n");
		g_string_append (md, text);
		if (n == 0 || text[n - 1] != '\n')
			g_string_append_c (md, '\n');
		g_string_append (md, "````\n");
	} else {
		g_string_append (md, text);
		if (n == 0 || text[n - 1] != '\n')
			g_string_append_c (md, '\n');
		g_string_append (md, "```\n");
	}
	return g_string_free (md, FALSE);
}

/* Brotkrumen: Projektwurzel / Ordner / Datei, jede Stufe klickbar. */
static void
gid_page_update_crumbs (GidPage *d)
{
	GString *m = g_string_new (NULL);
	GPtrArray *chain = g_ptr_array_new_with_free_func (g_object_unref);
	GFile *it;
	guint i;

	if (d->current == NULL || d->boundary == NULL) {
		gtk_label_set_text (GTK_LABEL (d->crumbs), "");
		g_string_free (m, TRUE);
		g_ptr_array_unref (chain);
		return;
	}
	it = g_object_ref (d->current);
	while (it != NULL) {
		GFile *parent;

		g_ptr_array_add (chain, g_object_ref (it));
		if (g_file_equal (it, d->boundary)) {
			g_object_unref (it);
			break;
		}
		parent = g_file_get_parent (it);
		g_object_unref (it);
		it = parent;
	}
	for (i = chain->len; i > 0; i--) {
		GFile *f = g_ptr_array_index (chain, i - 1);
		gchar *base = g_file_get_basename (f);
		gchar *esc = g_markup_escape_text (base, -1);
		gchar *rel = g_file_get_relative_path (d->boundary, f);
		gchar *href = g_uri_escape_string (rel != NULL ? rel : "", "/", FALSE);

		if (i < chain->len)
			g_string_append (m, "  /  ");
		if (i == 1)
			g_string_append_printf (m, "<b>%s</b>", esc);
		else
			g_string_append_printf (m, "<a href=\"nav:%s\">%s</a>", href, esc);
		g_free (base);
		g_free (esc);
		g_free (rel);
		g_free (href);
	}
	gtk_label_set_markup (GTK_LABEL (d->crumbs), m->str);
	g_string_free (m, TRUE);
	g_ptr_array_unref (chain);
}

static void
gid_page_update_nav (GidPage *d)
{
	GFile *parent = d->current != NULL ? g_file_get_parent (d->current) : NULL;

	gtk_widget_set_sensitive (d->back_button, d->back->len > 0);
	gtk_widget_set_sensitive (d->forward_button, d->forward->len > 0);
	{
		GFile *up_readme = (parent != NULL && d->boundary != NULL && file_is_inside (d->boundary, parent))
				   ? find_readme (parent) : NULL;

		gtk_widget_set_sensitive (d->up_button, up_readme != NULL);
		g_clear_object (&up_readme);
	}
	gtk_widget_set_sensitive (d->home_button, d->folder != NULL);
	g_clear_object (&parent);
	gid_page_update_crumbs (d);
}

static void
gid_page_set_status (GidPage *d, const gchar *text)
{
	gtk_label_set_text (GTK_LABEL (d->status_label), text);
}

/* Zeigt @target (Ordner, Markdown-, Text- oder Bilddatei). */
static void
gid_page_show (GidPage *d, GFile *target, gboolean record)
{
	GFileInfo *info;
	GFile *base_dir;
	gchar *markdown = NULL, *base = NULL;
	gboolean is_dir;
	const gchar *ctype;
	gchar *name;

	info = g_file_query_info (target, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
				  G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info == NULL) {
		gchar *p = g_file_get_path (target);
		gchar *msg = g_strdup_printf (_("Linkziel nicht gefunden: %s"), p != NULL ? p : "");

		gid_page_set_status (d, msg);
		g_free (msg);
		g_free (p);
		return;
	}
	is_dir = g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
	ctype = g_file_info_get_content_type (info);
	name = g_file_get_basename (target);

	if (is_dir) {
		/* Ordner öffnen im Dateimanager, nicht im Panel. */
		nolphin_window_go_to (d->window, target);
		base_dir = NULL;
	} else {
		base_dir = g_file_get_parent (target);
		if (name_is_markdown (name) ||
		    (ctype != NULL && (g_content_type_is_a (ctype, "text/markdown")))) {
			gchar *contents = NULL;
			gsize length = 0;

			if (g_file_info_get_size (info) > README_MAX_BYTES)
				gid_page_set_status (d, _("Die Datei ist zu groß für die Anzeige."));
			else if (!g_file_load_contents (target, NULL, &contents, &length, NULL, NULL))
				gid_page_set_status (d, _("Die Datei konnte nicht gelesen werden."));
			else
				markdown = g_strndup (contents, length);
			g_free (contents);
		} else if (ctype != NULL && g_content_type_is_a (ctype, "image/*")) {
			gchar *esc = g_uri_escape_string (name, NULL, FALSE);

			markdown = g_strdup_printf ("![%s](%s)\n", name, esc);
			g_free (esc);
		} else if (ctype != NULL && g_content_type_is_a (ctype, "text/plain")) {
			gchar *contents = NULL;
			gsize length = 0;

			if (g_file_info_get_size (info) > README_MAX_BYTES)
				gid_page_set_status (d, _("Die Datei ist zu groß für die Anzeige."));
			else if (g_file_load_contents (target, NULL, &contents, &length, NULL, NULL) &&
				 g_utf8_validate (contents, length, NULL)) {
				gchar *text = g_strndup (contents, length);

				markdown = build_code_markdown (text);
				g_free (text);
			} else
				gid_page_set_status (d, _("Die Datei ist keine Textdatei."));
			g_free (contents);
		} else {
			/* Andere Formate öffnet das zuständige Programm. */
			gchar *uri = g_file_get_uri (target);

			g_app_info_launch_default_for_uri (uri, NULL, NULL);
			g_free (uri);
		}
	}

	if (markdown != NULL) {
		if (record && d->current != NULL && !g_file_equal (d->current, target)) {
			g_ptr_array_add (d->back, g_object_ref (d->current));
			if (d->back->len > HISTORY_MAX)
				g_ptr_array_remove_index (d->back, 0);
			g_ptr_array_set_size (d->forward, 0);
		}
		g_set_object (&d->current, target);
		base = base_dir != NULL ? g_file_get_path (base_dir) : NULL;
		nolphin_markdown_view_set_text_with_base (d->view, markdown, base);
		gtk_stack_set_visible_child_name (GTK_STACK (d->stack), "readme");
		/* nach oben scrollen */
		gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (d->scroller)), 0);
		gid_page_set_status (d, "");
	}
	gid_page_update_nav (d);
	g_free (base);
	g_free (markdown);
	g_free (name);
	g_clear_object (&base_dir);
	g_object_unref (info);
}

void
nolphin_gid_page_open (GtkWidget *page, GFile *project_folder)
{
	GidPage *d;
	gchar *path, *name, *branch;
	GFile *readme;

	g_return_if_fail (GTK_IS_WIDGET (page));
	g_return_if_fail (G_IS_FILE (project_folder));

	d = g_object_get_data (G_OBJECT (page), "gid-page-data");
	g_return_if_fail (d != NULL);

	g_set_object (&d->folder, project_folder);
	g_clear_object (&d->boundary);
	g_clear_object (&d->current);
	g_ptr_array_set_size (d->back, 0);
	g_ptr_array_set_size (d->forward, 0);
	path = g_file_get_path (project_folder);
	name = g_file_get_basename (project_folder);
	gtk_label_set_text (GTK_LABEL (d->title), name);
	gtk_label_set_text (GTK_LABEL (d->path_label), path != NULL ? path : "");
	gtk_label_set_text (GTK_LABEL (d->status_label), "");

	branch = path != NULL ? nolphin_gid_project_get_branch (path) : NULL;
	if (branch != NULL) {
		gchar *text = g_strdup_printf (_("Branch: %s"), branch);

		gtk_label_set_text (GTK_LABEL (d->branch_label), text);
		g_free (text);
	} else {
		gtk_label_set_text (GTK_LABEL (d->branch_label), "");
	}
	gtk_widget_set_visible (d->branch_label, branch != NULL);
	g_free (branch);
	g_free (name);

	if (path == NULL || !g_file_query_exists (project_folder, NULL)) {
		gid_page_show_message (d, _("Der Projektordner ist nicht verfügbar."));
		gid_page_update_nav (d);
		g_free (path);
		return;
	}
	g_free (path);

	d->boundary = find_boundary (project_folder);
	readme = find_readme (project_folder);
	if (readme == NULL) {
		gid_page_show_message (d, _("In diesem Projekt gibt es keine README.md."));
		gid_page_update_nav (d);
		return;
	}
	gid_page_show (d, readme, FALSE);
	g_object_unref (readme);
}

void
nolphin_gid_page_show_file (GtkWidget *page, GFile *file)
{
	GidPage *d;
	gchar *name;
	gboolean md;

	g_return_if_fail (GTK_IS_WIDGET (page));
	g_return_if_fail (G_IS_FILE (file));

	d = g_object_get_data (G_OBJECT (page), "gid-page-data");
	if (d == NULL || !g_file_is_native (file))
		return;
	name = g_file_get_basename (file);
	md = name != NULL && name_is_markdown (name);
	g_free (name);
	if (!md || (d->current != NULL && g_file_equal (d->current, file)))
		return;
	if (g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
		return;

	/* Liegt die Datei außerhalb der bisherigen Grenze (z. B. anderer Ordner
	 * im Dateimanager), gilt deren Git-Wurzel. */
	if (d->boundary == NULL || !file_is_inside (d->boundary, file)) {
		GFile *parent = g_file_get_parent (file);

		g_clear_object (&d->boundary);
		d->boundary = find_boundary (parent != NULL ? parent : file);
		g_clear_object (&parent);
	}
	gid_page_show (d, file, TRUE);
}

static void
on_back_clicked (GtkButton *button, gpointer user_data)
{
	GidPage *d = user_data;
	GFile *to;

	if (d->back->len == 0)
		return;
	to = g_object_ref (g_ptr_array_index (d->back, d->back->len - 1));
	g_ptr_array_remove_index (d->back, d->back->len - 1);
	if (d->current != NULL)
		g_ptr_array_add (d->forward, g_object_ref (d->current));
	gid_page_show (d, to, FALSE);
	g_object_unref (to);
}

static void
on_forward_clicked (GtkButton *button, gpointer user_data)
{
	GidPage *d = user_data;
	GFile *to;

	if (d->forward->len == 0)
		return;
	to = g_object_ref (g_ptr_array_index (d->forward, d->forward->len - 1));
	g_ptr_array_remove_index (d->forward, d->forward->len - 1);
	if (d->current != NULL)
		g_ptr_array_add (d->back, g_object_ref (d->current));
	gid_page_show (d, to, FALSE);
	g_object_unref (to);
}

static void
on_up_clicked (GtkButton *button, gpointer user_data)
{
	GidPage *d = user_data;
	GFile *parent = d->current != NULL ? g_file_get_parent (d->current) : NULL;

	if (parent != NULL && d->boundary != NULL && file_is_inside (d->boundary, parent)) {
		GFile *readme = find_readme (parent);

		if (readme != NULL)
			gid_page_show (d, readme, TRUE);
		g_clear_object (&readme);
	}
	g_clear_object (&parent);
}

static void
on_home_clicked (GtkButton *button, gpointer user_data)
{
	GidPage *d = user_data;
	GFile *readme;

	if (d->folder == NULL)
		return;
	readme = find_readme (d->folder);
	gid_page_show (d, readme != NULL ? readme : d->folder, TRUE);
	g_clear_object (&readme);
}

static void
on_open_in_manager_clicked (GtkButton *button, gpointer user_data)
{
	GidPage *d = user_data;
	GFile *show = d->current;
	GFile *parent = NULL;

	if (show == NULL)
		return;
	if (g_file_query_file_type (show, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
		parent = g_file_get_parent (show);
		if (parent != NULL)
			show = parent;
	}
	nolphin_window_go_to (d->window, show);
	g_clear_object (&parent);
}

/* Klick auf eine Brotkrume. */
static gboolean
on_crumb_activated (GtkLabel *label, const gchar *uri, gpointer user_data)
{
	GidPage *d = user_data;

	if (g_str_has_prefix (uri, "nav:") && d->boundary != NULL) {
		gchar *rel = g_uri_unescape_string (uri + 4, NULL);
		GFile *target = (rel != NULL && *rel != '\0') ? g_file_resolve_relative_path (d->boundary, rel)
							      : g_object_ref (d->boundary);

		gid_page_show (d, target, TRUE);
		g_object_unref (target);
		g_free (rel);
	}
	return TRUE;
}

/* Klick auf einen Link der angezeigten Datei. */
static void
on_link_clicked (GtkWidget *view, const gchar *href, gpointer user_data)
{
	GidPage *d = user_data;
	gchar *scheme = g_uri_parse_scheme (href);
	gchar *rel, *unescaped, *joined, *canon;
	GFile *base, *target;
	const gchar *cut;
	gchar *anchor = NULL;

	if (scheme != NULL) {
		g_app_info_launch_default_for_uri (href, NULL, NULL);
		g_free (scheme);
		return;
	}
	if (d->current == NULL || d->boundary == NULL)
		return;

	/* Sprungmarke (#abschnitt) abtrennen */
	{
		const gchar *hash = strchr (href, '#');

		if (hash != NULL && hash[1] != '\0')
			anchor = g_uri_unescape_string (hash + 1, NULL);
	}
	if (href[0] == '#') {
		if (anchor != NULL && !nolphin_markdown_view_scroll_to_anchor (d->view, anchor))
			gid_page_set_status (d, _("Diese Überschrift gibt es auf der Seite nicht."));
		else
			gid_page_set_status (d, "");
		g_free (anchor);
		return;
	}

	rel = g_strdup (href);
	cut = strpbrk (rel, "#?");
	if (cut != NULL)
		rel[cut - rel] = '\0';
	unescaped = g_uri_unescape_string (rel, NULL);
	g_free (rel);
	if (unescaped == NULL || *unescaped == '\0') {
		g_free (unescaped);
		g_free (anchor);
		return;
	}

	/* Relative Links gelten ab dem Ordner der angezeigten Datei; ein
	 * führender "/" bedeutet wie auf GitHub: ab der Projektwurzel. */
	if (g_file_query_file_type (d->current, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
		base = g_object_ref (d->current);
	else
		base = g_file_get_parent (d->current);
	if (unescaped[0] == '/') {
		g_clear_object (&base);
		base = g_object_ref (d->boundary);
	}
	{
		gchar *base_path = g_file_get_path (base);

		joined = g_build_filename (base_path != NULL ? base_path : "/", unescaped, NULL);
		g_free (base_path);
	}
	canon = g_canonicalize_filename (joined, "/");
	target = g_file_new_for_path (canon);
	g_free (joined);
	g_free (canon);
	g_free (unescaped);
	g_object_unref (base);

	if (!file_is_inside (d->boundary, target)) {
		gid_page_set_status (d, _("Das Linkziel liegt außerhalb des Projekts."));
	} else if (!g_file_query_exists (target, NULL)) {
		gchar *msg = g_strdup_printf (_("Linkziel nicht gefunden: %s"), href);

		gid_page_set_status (d, msg);
		g_free (msg);
	} else {
		if (!g_file_equal (target, d->current))
			gid_page_show (d, target, TRUE);
		if (anchor != NULL && !nolphin_markdown_view_scroll_to_anchor (d->view, anchor))
			gid_page_set_status (d, _("Diese Überschrift gibt es auf der Seite nicht."));
	}
	g_free (anchor);
	g_object_unref (target);
}

static GtkWidget *
nav_button_new (const gchar *icon, const gchar *tooltip, GCallback cb, gpointer data)
{
	GtkWidget *b = gtk_button_new_from_icon_name (icon, GTK_ICON_SIZE_BUTTON);

	gtk_widget_set_tooltip_text (b, tooltip);
	g_signal_connect (b, "clicked", cb, data);
	return b;
}

GtkWidget *
nolphin_gid_page_new (NolphinWindow *window)
{
	GidPage *d = g_new0 (GidPage, 1);
	GtkWidget *outer, *header, *navbar, *manager_button;

	d->window = window;
	d->back = g_ptr_array_new_with_free_func (g_object_unref);
	d->forward = g_ptr_array_new_with_free_func (g_object_unref);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (outer), 12);
	gtk_box_pack_start (GTK_BOX (outer), nolphin_tools_back_button_new (window), FALSE, FALSE, 0);

	/* schmale Kopfzeile: Projektname, Pfad, Branch */
	header = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	d->title = nolphin_tools_heading_new (_("GID-Projekt"));
	gtk_label_set_selectable (GTK_LABEL (d->title), TRUE);
	/* Lange Namen/Pfade dürfen das Panel nicht verbreitern. */
	gtk_label_set_ellipsize (GTK_LABEL (d->title), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars (GTK_LABEL (d->title), 1);
	gtk_widget_set_halign (d->title, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (d->title), 0.0);
	gtk_box_pack_start (GTK_BOX (header), d->title, FALSE, FALSE, 0);
	d->path_label = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (d->path_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_selectable (GTK_LABEL (d->path_label), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (d->path_label), 1);
	gtk_widget_set_halign (d->path_label, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (d->path_label), 0.0);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->path_label), "dim-label");
	gtk_box_pack_start (GTK_BOX (header), d->path_label, FALSE, FALSE, 0);
	d->branch_label = gtk_label_new ("");
	gtk_widget_set_halign (d->branch_label, GTK_ALIGN_START);
	gtk_widget_set_no_show_all (d->branch_label, TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->branch_label), "dim-label");
	gtk_box_pack_start (GTK_BOX (header), d->branch_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (outer), header, FALSE, FALSE, 0);

	/* Navigationsleiste: zurück, vor, hoch, Projektwurzel, Brotkrumen */
	navbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	d->back_button = nav_button_new ("go-previous-symbolic", _("Zurück"), G_CALLBACK (on_back_clicked), d);
	d->forward_button = nav_button_new ("go-next-symbolic", _("Vor"), G_CALLBACK (on_forward_clicked), d);
	d->up_button = nav_button_new ("go-up-symbolic", _("README des übergeordneten Ordners"), G_CALLBACK (on_up_clicked), d);
	d->home_button = nav_button_new ("go-home-symbolic", _("Zur README des Projekts"), G_CALLBACK (on_home_clicked), d);
	manager_button = nav_button_new ("folder-open-symbolic", _("Im Dateimanager öffnen"),
					 G_CALLBACK (on_open_in_manager_clicked), d);
	gtk_box_pack_start (GTK_BOX (navbar), d->back_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (navbar), d->forward_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (navbar), d->up_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (navbar), d->home_button, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (navbar), manager_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (outer), navbar, FALSE, FALSE, 0);

	d->crumbs = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (d->crumbs), PANGO_ELLIPSIZE_START);
	gtk_label_set_width_chars (GTK_LABEL (d->crumbs), 1);
	gtk_widget_set_halign (d->crumbs, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (d->crumbs), 0.0);
	g_signal_connect (d->crumbs, "activate-link", G_CALLBACK (on_crumb_activated), d);
	gtk_box_pack_start (GTK_BOX (outer), d->crumbs, FALSE, FALSE, 0);

	d->status_label = gtk_label_new ("");
	gtk_label_set_line_wrap (GTK_LABEL (d->status_label), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (d->status_label), 1);
	gtk_widget_set_halign (d->status_label, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (d->status_label), 0.0);
	gtk_box_pack_start (GTK_BOX (outer), d->status_label, FALSE, FALSE, 0);

	d->stack = gtk_stack_new ();
	d->view = nolphin_markdown_view_new ();
	nolphin_markdown_view_set_link_handler (d->view, on_link_clicked, d);
	d->scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (d->scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (d->scroller), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (d->scroller), d->view);
	gtk_stack_add_named (GTK_STACK (d->stack), d->scroller, "readme");

	d->message = gtk_label_new ("");
	gtk_label_set_line_wrap (GTK_LABEL (d->message), TRUE);
	gtk_label_set_width_chars (GTK_LABEL (d->message), 1);
	gtk_widget_set_halign (d->message, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (d->message), 0.0);
	gtk_widget_set_valign (d->message, GTK_ALIGN_START);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->message), "dim-label");
	gtk_stack_add_named (GTK_STACK (d->stack), d->message, "message");
	gtk_box_pack_start (GTK_BOX (outer), d->stack, TRUE, TRUE, 0);

	gid_page_show_message (d, _("Wähle in der Seitenleiste ein GID-Projekt."));
	gid_page_update_nav (d);

	g_object_set_data_full (G_OBJECT (outer), "gid-page-data", d, gid_page_free);
	return outer;
}
