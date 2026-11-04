/* nolphin-git-page.c - Git- und GitHub-Verwaltung (siehe Header).
 *
 * Aufbau:
 *  - Ein gemeinsamer Befehls-Runner startet git bzw. gh (GitHub-CLI) im
 *    Ordner der aktiven Ansicht, liest die Ausgabe laufend mit (so
 *    erscheint der Push-/Pull-Fortschritt) und zeigt sie unten an.
 *  - Jede Schaltfläche ist eine Zeile in einer Tabelle (GAct) mit einer
 *    Befehlsvorlage, z. B. "git branch -m {branch} {newname}". Die
 *    Platzhalter kommen aus den Eingabefeldern bzw. Listenauswahlen.
 *  - Gefährliche Aktionen fragen vorher nach.
 *  - Konflikte werden erkannt und in einer eigenen Ansicht bearbeitet.
 *
 * Platzhalter in Vorlagen (jeweils als ganzes Wort):
 *   {feld}            Pflichtfeld, ein Argument
 *   {feld?}           optional (entfällt, wenn leer)
 *   {feld*}           mehrere Argumente (durch Leerzeichen getrennt)
 *   {opt:--flag:feld} "--flag wert", nur wenn das Feld gefüllt ist
 *   {chk:name:--flag} "--flag", nur wenn das Häkchen gesetzt ist
 *   {files}/{files?}  Dateien aus Änderungsliste bzw. Dateiauswahl
 *   {$feld}           innerhalb eines Wortes ersetzt (z. B. Pfade)
 * "&&" trennt mehrere nacheinander auszuführende Befehle. */

#include <config.h>
#include "nolphin-git-page.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <string.h>

#include <libnolphin-private/nolphin-file.h>
#include "nolphin-view.h"
#include "nolphin-window-slot.h"

/* ------------------------------------------------------------------ */
/* Typen                                                                */
/* ------------------------------------------------------------------ */

typedef struct _GitPage GitPage;
typedef struct _GAct GAct;
typedef void (*GActFunc) (GitPage *p);
typedef void (*RunDone) (gpointer user, gboolean ok, GString *out);

enum {
	GA_ANY  = 1 << 0,   /* braucht kein Repository, läuft im Ordner der Ansicht */
	GA_PUSH = 1 << 1,   /* Push: Upstream automatisch setzen, Build-Status zeigen */
};

struct _GAct {
	const gchar *label;
	const gchar *tip;
	const gchar *cmd;       /* Befehlsvorlage oder NULL */
	const gchar *confirm;   /* Rückfrage oder NULL */
	guint        flags;
	GActFunc     func;      /* Sonderfunktion statt Vorlage */
};

typedef enum { PARSE_TAB, PARSE_PORCELAIN, PARSE_REMOTES } ParseMode;

typedef struct {
	const gchar *id;
	ParseMode    mode;
	const gchar *cmd;
	gint         ncols;
	const gchar *titles[5];
	const gchar *fills[5];
	gboolean     multi;
	gint         height;
	gboolean     any;        /* ohne Repository nutzbar (GitHub-Listen) */
} ListDef;

typedef struct {
	GitPage        *p;
	const ListDef  *def;
	GtkListStore   *store;
	GtkWidget      *tree;
} ListSpec;

struct _GitPage {
	NolphinWindow *window;
	GtkWidget     *root;
	GtkWidget     *status_label;
	GtkWidget     *log_view;
	GtkTextBuffer *log;
	GtkTextMark   *line_mark;
	GString       *partial;
	gboolean       overwrite;
	gboolean       colorize;
	GtkWidget     *progress;
	GtkWidget     *cancel_btn;
	GtkWidget     *auto_chk;
	GtkWidget     *notebook;
	GtkWidget     *gh_notebook;
	GtkWidget     *gh_status;
	GHashTable    *fields;   /* name -> GPtrArray<GtkWidget*> */
	GHashTable    *labels;   /* name -> Beschriftung */
	GHashTable    *checks;   /* name -> GtkToggleButton* */
	GList         *lists;    /* ListSpec* */
	GSubprocess   *proc;
	gboolean       busy;
	guint          pulse_id;
	GtkWidget     *conf_result;
	GtkWidget     *conf_ours;
	GtkWidget     *conf_theirs;
	GHashTable    *tab_pages;   /* id -> Seitennummer */
};

typedef struct {
	GitPage *p;
	const GAct *act;
} ActBtn;

/* ------------------------------------------------------------------ */
/* Kontext: Ordner, Repository, Auswahl                                 */
/* ------------------------------------------------------------------ */

static gchar *
page_dir (GitPage *p)
{
	NolphinWindowSlot *slot = nolphin_window_get_active_slot (p->window);
	NolphinFile *dir;
	GFile *loc;
	gchar *path;

	if (slot == NULL || slot->content_view == NULL) {
		return NULL;
	}
	dir = nolphin_view_get_directory_as_file (NOLPHIN_VIEW (slot->content_view));
	if (dir == NULL) {
		return NULL;
	}
	loc = nolphin_file_get_location (dir);
	path = g_file_get_path (loc);
	g_object_unref (loc);
	return path;
}

static gchar *
find_root (const gchar *dir)
{
	gchar *d;

	if (dir == NULL) {
		return NULL;
	}
	d = g_strdup (dir);
	for (;;) {
		gchar *g = g_build_filename (d, ".git", NULL);
		gboolean found = g_file_test (g, G_FILE_TEST_EXISTS);
		gchar *parent;

		g_free (g);
		if (found) {
			return d;
		}
		parent = g_path_get_dirname (d);
		if (g_strcmp0 (parent, d) == 0) {
			g_free (parent);
			g_free (d);
			return NULL;
		}
		g_free (d);
		d = parent;
	}
}

/* Pfade der im Hauptfenster markierten Dateien. */
static GPtrArray *
fm_selection_paths (GitPage *p)
{
	GPtrArray *res = g_ptr_array_new_with_free_func (g_free);
	NolphinWindowSlot *slot = nolphin_window_get_active_slot (p->window);
	GList *sel, *l;

	if (slot == NULL || slot->content_view == NULL) {
		return res;
	}
	sel = nolphin_view_get_selection (NOLPHIN_VIEW (slot->content_view));
	for (l = sel; l != NULL; l = l->next) {
		GFile *loc = nolphin_file_get_location (NOLPHIN_FILE (l->data));
		gchar *path = g_file_get_path (loc);

		g_object_unref (loc);
		if (path != NULL) {
			g_ptr_array_add (res, path);
		}
	}
	nolphin_file_list_free (sel);
	return res;
}

/* ------------------------------------------------------------------ */
/* Felder und Häkchen                                                   */
/* ------------------------------------------------------------------ */

static gchar *
widget_text (GtkWidget *w)
{
	gchar *t;

	if (GTK_IS_ENTRY (w)) {
		t = g_strdup (gtk_entry_get_text (GTK_ENTRY (w)));
	} else {
		GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));
		GtkTextIter s, e;

		gtk_text_buffer_get_bounds (b, &s, &e);
		t = gtk_text_buffer_get_text (b, &s, &e, FALSE);
	}
	return g_strstrip (t);
}

static void
widget_set_text (GtkWidget *w, const gchar *text)
{
	if (GTK_IS_ENTRY (w)) {
		gtk_entry_set_text (GTK_ENTRY (w), text);
	} else {
		gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (w)), text, -1);
	}
}

static gboolean
field_known (GitPage *p, const gchar *name)
{
	return g_hash_table_contains (p->fields, name);
}

/* Erster nicht leerer Wert aller Eingabefelder mit diesem Namen. */
static gchar *
field_get (GitPage *p, const gchar *name)
{
	GPtrArray *arr = g_hash_table_lookup (p->fields, name);
	guint i;

	if (arr != NULL) {
		for (i = 0; i < arr->len; i++) {
			gchar *t = widget_text (g_ptr_array_index (arr, i));

			if (t[0] != '\0') {
				return t;
			}
			g_free (t);
		}
	}
	return g_strdup ("");
}

static void
field_set (GitPage *p, const gchar *name, const gchar *text)
{
	GPtrArray *arr = g_hash_table_lookup (p->fields, name);
	guint i;

	if (arr == NULL) {
		return;
	}
	for (i = 0; i < arr->len; i++) {
		widget_set_text (g_ptr_array_index (arr, i), text);
	}
}

static const gchar *
field_label (GitPage *p, const gchar *name)
{
	const gchar *l = g_hash_table_lookup (p->labels, name);

	return l != NULL ? l : name;
}

/* ------------------------------------------------------------------ */
/* Ausgabefeld                                                          */
/* ------------------------------------------------------------------ */

static void
log_scroll_end (GitPage *p)
{
	GtkTextIter end;

	gtk_text_buffer_get_end_iter (p->log, &end);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (p->log_view), &end, 0.0, FALSE, 0, 0);
}

/* Ersetzt die aktuelle (noch nicht mit Zeilenumbruch beendete) Zeile. */
static void
log_render_partial (GitPage *p)
{
	GtkTextIter s, e;

	gtk_text_buffer_get_iter_at_mark (p->log, &s, p->line_mark);
	gtk_text_buffer_get_end_iter (p->log, &e);
	gtk_text_buffer_delete (p->log, &s, &e);
	gtk_text_buffer_get_end_iter (p->log, &e);
	gtk_text_buffer_insert (p->log, &e, p->partial->str, -1);
}

static void
log_finish_line (GitPage *p)
{
	GtkTextIter s, e;
	const gchar *tag = NULL;
	gchar c = p->partial->str[0];

	log_render_partial (p);
	if (p->colorize) {
		if (g_str_has_prefix (p->partial->str, "@@")) {
			tag = "hunk";
		} else if (c == '+' && !g_str_has_prefix (p->partial->str, "+++")) {
			tag = "add";
		} else if (c == '-' && !g_str_has_prefix (p->partial->str, "---")) {
			tag = "del";
		}
		if (tag != NULL) {
			gtk_text_buffer_get_iter_at_mark (p->log, &s, p->line_mark);
			gtk_text_buffer_get_end_iter (p->log, &e);
			gtk_text_buffer_apply_tag_by_name (p->log, tag, &s, &e);
		}
	}
	gtk_text_buffer_get_end_iter (p->log, &e);
	gtk_text_buffer_insert (p->log, &e, "\n", -1);
	gtk_text_buffer_get_end_iter (p->log, &e);
	gtk_text_buffer_move_mark (p->log, p->line_mark, &e);
	g_string_truncate (p->partial, 0);
	p->overwrite = FALSE;
}

static void
log_feed (GitPage *p, const guint8 *data, gssize len)
{
	gssize i;

	for (i = 0; i < len; i++) {
		gchar c = (gchar) data[i];

		if (c == '\n') {
			log_finish_line (p);
		} else if (c == '\r') {
			/* Fortschrittszeile: die nächste Ausgabe überschreibt sie. */
			p->overwrite = TRUE;
		} else {
			if (p->overwrite) {
				g_string_truncate (p->partial, 0);
				p->overwrite = FALSE;
			}
			g_string_append_c (p->partial, c);
		}
	}
	log_render_partial (p);
	log_scroll_end (p);
}

static void
log_flush (GitPage *p)
{
	if (p->partial->len > 0) {
		log_finish_line (p);
	}
}

static void
log_line (GitPage *p, const gchar *tag, const gchar *fmt, ...) G_GNUC_PRINTF (3, 4);

static void
log_line (GitPage *p, const gchar *tag, const gchar *fmt, ...)
{
	va_list ap;
	gchar *text;
	GtkTextIter s, e;

	log_flush (p);
	va_start (ap, fmt);
	text = g_strdup_vprintf (fmt, ap);
	va_end (ap);

	gtk_text_buffer_get_end_iter (p->log, &s);
	gtk_text_buffer_insert (p->log, &s, text, -1);
	gtk_text_buffer_get_end_iter (p->log, &e);
	if (tag != NULL) {
		GtkTextIter start = e;
		gint off = gtk_text_iter_get_offset (&e) - (gint) g_utf8_strlen (text, -1);

		gtk_text_buffer_get_iter_at_offset (p->log, &start, off);
		gtk_text_buffer_apply_tag_by_name (p->log, tag, &start, &e);
	}
	gtk_text_buffer_insert (p->log, &e, "\n", -1);
	gtk_text_buffer_get_end_iter (p->log, &e);
	gtk_text_buffer_move_mark (p->log, p->line_mark, &e);
	log_scroll_end (p);
	g_free (text);
}

/* ------------------------------------------------------------------ */
/* Hinweise zu typischen Fehlern                                        */
/* ------------------------------------------------------------------ */

static const gchar *
hint_for (const gchar *out)
{
	if (out == NULL) {
		return NULL;
	}
	if (strstr (out, "non-fast-forward") || strstr (out, "fetch first") || strstr (out, "[rejected]")) {
		return _("Der Server hat neuere Änderungen als du. Erst „Pull“ ausführen und dann erneut pushen. "
			 "Nur wenn du den Serverstand bewusst überschreiben willst: „Force-with-lease“.");
	}
	if (strstr (out, "Authentication failed") || strstr (out, "could not read Username")
	    || strstr (out, "Permission denied (publickey)") || strstr (out, "Invalid username or password")
	    || strstr (out, "HTTP 401") || strstr (out, "gh auth login")) {
		return _("Anmeldung bei GitHub fehlt oder ist abgelaufen. Im Terminal einmalig „gh auth login“ und "
			 "danach „gh auth setup-git“ ausführen.");
	}
	if (strstr (out, "has no upstream branch")) {
		return _("Dieser Branch ist noch nicht mit dem Server verknüpft. „Push mit Upstream“ benutzen.");
	}
	if (strstr (out, "CONFLICT") || strstr (out, "Merge conflict") || strstr (out, "fix conflicts")) {
		return _("Es gibt Merge-Konflikte. Im Reiter „Konflikte“ lösen, dann als gelöst markieren und "
			 "den Merge abschließen.");
	}
	if (strstr (out, "tell me who you are") || strstr (out, "empty ident")) {
		return _("Git kennt deinen Namen noch nicht. Im Terminal: git config --global user.name \"Dein Name\" "
			 "und git config --global user.email \"deine@mail.de\"");
	}
	if (strstr (out, "not a git repository")) {
		return _("Dieser Ordner ist kein Git-Repository. Im Reiter „Änderungen“ mit „Repository anlegen“ erzeugen.");
	}
	if (strstr (out, "nothing to commit") || strstr (out, "no changes added to commit")) {
		return _("Es gibt nichts zu committen. Erst Dateien ändern oder „Alle hinzufügen“ benutzen.");
	}
	if (strstr (out, "Could not resolve host") || strstr (out, "unable to access")
	    || strstr (out, "Connection timed out") || strstr (out, "Network is unreachable")) {
		return _("Keine Verbindung zum Server. Internetverbindung prüfen.");
	}
	if (strstr (out, "Repository not found") || strstr (out, "does not appear to be a git repository")
	    || strstr (out, "Could not resolve to a Repository")) {
		return _("Repository oder Remote nicht gefunden. Adresse und Zugriffsrechte prüfen (Reiter „Server“).");
	}
	if (strstr (out, "unrelated histories")) {
		return _("Die Verläufe haben nichts gemeinsam. Bewusst zusammenführen? Dann im Terminal: "
			 "git pull --allow-unrelated-histories");
	}
	if (strstr (out, "would be overwritten by")) {
		return _("Lokale Änderungen stehen im Weg. Erst committen oder im Reiter „Änderungen“ per „Stash“ zurückstellen.");
	}
	if (strstr (out, "already exists")) {
		return _("Dieser Name existiert schon.");
	}
	if (strstr (out, "No such file or directory") && strstr (out, "gh")) {
		return _("Die GitHub-CLI „gh“ ist nicht installiert: sudo apt install gh");
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Runner                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
	GtkWidget   *keep;
	GitPage     *p;
	GSubprocess *proc;
	GInputStream *in;
	GString     *out;
	gboolean     quiet;
	RunDone      done;
	gpointer     user;
	guint8       buf[4096];
} Run;

static void run_read_cb (GObject *src, GAsyncResult *res, gpointer ud);

static void
run_free (Run *r)
{
	g_object_unref (r->keep);
	g_clear_object (&r->proc);
	g_string_free (r->out, TRUE);
	g_free (r);
}

static void
run_wait_cb (GObject *src, GAsyncResult *res, gpointer ud)
{
	Run *r = ud;
	gboolean ok;

	g_subprocess_wait_finish (G_SUBPROCESS (src), res, NULL);
	ok = g_subprocess_get_successful (r->proc);
	if (!r->quiet) {
		log_flush (r->p);
	}
	if (r->p->proc == r->proc) {
		r->p->proc = NULL;
	}
	r->done (r->user, ok, r->out);
	run_free (r);
}

static void
run_read_cb (GObject *src, GAsyncResult *res, gpointer ud)
{
	Run *r = ud;
	GError *e = NULL;
	gssize n = g_input_stream_read_finish (G_INPUT_STREAM (src), res, &e);

	if (n > 0) {
		g_string_append_len (r->out, (const gchar *) r->buf, n);
		if (!r->quiet) {
			log_feed (r->p, r->buf, n);
		}
		g_input_stream_read_async (r->in, r->buf, sizeof r->buf, G_PRIORITY_DEFAULT, NULL, run_read_cb, r);
		return;
	}
	g_clear_error (&e);
	g_subprocess_wait_async (r->proc, NULL, run_wait_cb, r);
}

/* Startet @argv (NULL-terminiert, Besitz geht an die Funktion). */
static void
run_proc (GitPage *p, GPtrArray *argv, const gchar *cwd, gboolean quiet, RunDone done, gpointer user)
{
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *err = NULL;
	Run *r;

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	if (cwd != NULL) {
		g_subprocess_launcher_set_cwd (launcher, cwd);
	}
	/* Englische Meldungen, damit die Fehlererkennung sicher greift. */
	g_subprocess_launcher_setenv (launcher, "LC_ALL", "C.UTF-8", TRUE);
	g_subprocess_launcher_setenv (launcher, "LANGUAGE", "en", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_ASKPASS", "true", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_EDITOR", "true", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_SEQUENCE_EDITOR", "true", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_MERGE_AUTOEDIT", "no", TRUE);
	g_subprocess_launcher_setenv (launcher, "GIT_PAGER", "cat", TRUE);
	g_subprocess_launcher_setenv (launcher, "GH_PAGER", "cat", TRUE);
	g_subprocess_launcher_setenv (launcher, "GH_PROMPT_DISABLED", "1", TRUE);
	g_subprocess_launcher_setenv (launcher, "NO_COLOR", "1", TRUE);

	g_ptr_array_add (argv, NULL);
	proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv->pdata, &err);
	g_object_unref (launcher);

	if (proc == NULL) {
		GString *msg = g_string_new (err != NULL ? err->message : "");

		if (!quiet) {
			log_line (p, "err", "%s", msg->str);
		}
		g_clear_error (&err);
		g_ptr_array_free (argv, TRUE);
		done (user, FALSE, msg);
		g_string_free (msg, TRUE);
		return;
	}
	g_ptr_array_free (argv, TRUE);

	r = g_new0 (Run, 1);
	r->keep = g_object_ref (p->root);
	r->p = p;
	r->proc = proc;
	r->in = g_subprocess_get_stdout_pipe (proc);
	r->out = g_string_new (NULL);
	r->quiet = quiet;
	r->done = done;
	r->user = user;
	if (!quiet) {
		p->proc = proc;
	}
	g_input_stream_read_async (r->in, r->buf, sizeof r->buf, G_PRIORITY_DEFAULT, NULL, run_read_cb, r);
}

/* Bequem: git/gh-Befehl ohne Ausgabe im Log (Listen, Status). */
static void
quiet_run (GitPage *p, const gchar *cwd, RunDone done, gpointer user, const gchar *first, ...)
{
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	va_list ap;
	const gchar *a = first;

	va_start (ap, first);
	while (a != NULL) {
		g_ptr_array_add (argv, g_strdup (a));
		a = va_arg (ap, const gchar *);
	}
	va_end (ap);
	run_proc (p, argv, cwd, TRUE, done, user);
}

/* ------------------------------------------------------------------ */
/* Vorlagen auflösen                                                    */
/* ------------------------------------------------------------------ */

static GPtrArray *
gather_files (GitPage *p)
{
	GPtrArray *files = g_ptr_array_new_with_free_func (g_free);
	GList *l;

	for (l = p->lists; l != NULL; l = l->next) {
		ListSpec *ls = l->data;
		GtkTreeSelection *sel;
		GList *rows, *r;
		GtkTreeModel *model;

		if (g_strcmp0 (ls->def->id, "changes") != 0) {
			continue;
		}
		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (ls->tree));
		rows = gtk_tree_selection_get_selected_rows (sel, &model);
		for (r = rows; r != NULL; r = r->next) {
			GtkTreeIter it;
			gchar *path = NULL;

			if (gtk_tree_model_get_iter (model, &it, r->data)) {
				gtk_tree_model_get (model, &it, 1, &path, -1);
				if (path != NULL) {
					g_ptr_array_add (files, path);
				}
			}
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	}
	if (files->len == 0) {
		GPtrArray *fm = fm_selection_paths (p);
		guint i;

		for (i = 0; i < fm->len; i++) {
			g_ptr_array_add (files, g_strdup (g_ptr_array_index (fm, i)));
		}
		g_ptr_array_free (fm, TRUE);
	}
	return files;
}

static void
split_words_into (GPtrArray *argv, const gchar *text)
{
	gchar **w = g_strsplit_set (text, " \t\n", -1);
	gint i;

	for (i = 0; w[i] != NULL; i++) {
		if (w[i][0] != '\0') {
			g_ptr_array_add (argv, g_strdup (w[i]));
		}
	}
	g_strfreev (w);
}

static void
err_missing (GitPage *p, GString *err, const gchar *name)
{
	g_string_append_printf (err, _("Bitte das Feld „%s“ ausfüllen.\n"), field_label (p, name));
}

static gboolean
expand_token (GitPage *p, const gchar *t, GPtrArray *argv, GString *err)
{
	gsize len = strlen (t);

	if (g_str_equal (t, "{files}") || g_str_equal (t, "{files?}")) {
		gboolean opt = (t[6] == '?');
		GPtrArray *f = gather_files (p);
		guint i;

		if (f->len == 0 && !opt) {
			g_string_append (err, _("Bitte zuerst Dateien in der Liste oder im Hauptfenster auswählen.\n"));
			g_ptr_array_free (f, TRUE);
			return FALSE;
		}
		for (i = 0; i < f->len; i++) {
			g_ptr_array_add (argv, g_strdup (g_ptr_array_index (f, i)));
		}
		g_ptr_array_free (f, TRUE);
		return TRUE;
	}

	if (g_str_has_prefix (t, "{opt:") && t[len - 1] == '}') {
		gchar *inner = g_strndup (t + 5, len - 6);
		gchar **parts = g_strsplit (inner, ":", 2);

		if (parts[0] != NULL && parts[1] != NULL) {
			gchar *v = field_get (p, parts[1]);

			if (v[0] != '\0') {
				g_ptr_array_add (argv, g_strdup (parts[0]));
				g_ptr_array_add (argv, v);
			} else {
				g_free (v);
			}
		}
		g_strfreev (parts);
		g_free (inner);
		return TRUE;
	}

	if (g_str_has_prefix (t, "{chk:") && t[len - 1] == '}') {
		gchar *inner = g_strndup (t + 5, len - 6);
		gchar **parts = g_strsplit (inner, ":", 2);
		GtkWidget *chk = parts[0] != NULL ? g_hash_table_lookup (p->checks, parts[0]) : NULL;

		if (chk != NULL && parts[1] != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (chk))) {
			g_ptr_array_add (argv, g_strdup (parts[1]));
		}
		g_strfreev (parts);
		g_free (inner);
		return TRUE;
	}

	if (t[0] == '{' && t[len - 1] == '}' && len > 2 && t[1] != '$') {
		gchar *inner = g_strndup (t + 1, len - 2);
		gboolean opt = FALSE, multi = FALSE;
		gsize il = strlen (inner);

		if (il > 0 && inner[il - 1] == '?') {
			opt = TRUE;
			inner[--il] = '\0';
		}
		if (il > 0 && inner[il - 1] == '*') {
			multi = TRUE;
			inner[--il] = '\0';
		}
		if (field_known (p, inner)) {
			gchar *v = field_get (p, inner);
			gboolean ok = TRUE;

			if (v[0] == '\0') {
				if (!opt) {
					err_missing (p, err, inner);
					ok = FALSE;
				}
				g_free (v);
			} else if (multi) {
				split_words_into (argv, v);
				g_free (v);
			} else {
				g_ptr_array_add (argv, v);
			}
			g_free (inner);
			return ok;
		}
		g_free (inner);
		/* unbekannt: wörtlich weiterreichen (z. B. {owner}/{repo} für gh) */
	}

	if (strstr (t, "{$") != NULL) {
		GString *res = g_string_new (NULL);
		const gchar *s = t;
		gboolean ok = TRUE;

		while (*s != '\0') {
			const gchar *open = strstr (s, "{$");
			const gchar *close;

			if (open == NULL || (close = strchr (open, '}')) == NULL) {
				g_string_append (res, s);
				break;
			}
			g_string_append_len (res, s, open - s);
			{
				gchar *name = g_strndup (open + 2, close - open - 2);
				gboolean opt = FALSE;
				gsize nl = strlen (name);
				gchar *v;

				if (nl > 0 && name[nl - 1] == '?') {
					opt = TRUE;
					name[nl - 1] = '\0';
				}
				v = field_get (p, name);
				if (v[0] == '\0' && !opt) {
					err_missing (p, err, name);
					ok = FALSE;
				}
				g_string_append (res, v);
				g_free (v);
				g_free (name);
			}
			s = close + 1;
		}
		g_ptr_array_add (argv, g_string_free (res, FALSE));
		return ok;
	}

	g_ptr_array_add (argv, g_strdup (t));
	return TRUE;
}

/* Zerlegt @cmd in Einzelbefehle (getrennt durch "&&"). Jeder Eintrag ist
 * ein GPtrArray* mit argv. NULL bei Fehler (Text in @err). */
static GPtrArray *
build_commands (GitPage *p, const gchar *cmd, GString *err)
{
	GPtrArray *cmds = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	gchar **segments = g_strsplit (cmd, " && ", -1);
	gint i;
	gboolean ok = TRUE;

	for (i = 0; segments[i] != NULL && ok; i++) {
		gchar **tokens = NULL;
		GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
		GError *e = NULL;
		gint j;

		if (!g_shell_parse_argv (segments[i], NULL, &tokens, &e)) {
			g_string_append (err, e->message);
			g_clear_error (&e);
			g_ptr_array_unref (argv);
			ok = FALSE;
			break;
		}
		for (j = 0; tokens[j] != NULL; j++) {
			if (!expand_token (p, tokens[j], argv, err)) {
				ok = FALSE;
			}
		}
		g_strfreev (tokens);
		g_ptr_array_add (cmds, argv);
	}
	g_strfreev (segments);
	if (!ok) {
		g_ptr_array_unref (cmds);
		return NULL;
	}
	return cmds;
}

/* ------------------------------------------------------------------ */
/* Aufträge (mehrere Befehle nacheinander)                              */
/* ------------------------------------------------------------------ */

typedef struct {
	GitPage    *p;
	GtkWidget  *keep;
	GPtrArray  *cmds;
	guint       idx;
	gchar      *cwd;
	guint       flags;
	GString    *all;
	gboolean    retried;
	gboolean    silent;   /* Hilfsauftrag (Build-Status nach Push) */
} Job;

static void refresh_everything (GitPage *p);
static void job_next (Job *j);
static void goto_tab (GitPage *p, const gchar *id);
static void refresh_conflicts (GitPage *p);

static gchar *
cmdline (GPtrArray *argv)
{
	GString *s = g_string_new ("$");
	guint i;

	for (i = 0; i < argv->len; i++) {
		gchar *q = g_shell_quote (g_ptr_array_index (argv, i));
		const gchar *a = g_ptr_array_index (argv, i);
		gboolean plain = (strpbrk (a, " \t'\"\\$&|;<>*?()[]{}") == NULL);

		g_string_append_printf (s, " %s", plain ? a : q);
		g_free (q);
	}
	return g_string_free (s, FALSE);
}

static void
set_busy (GitPage *p, gboolean busy)
{
	p->busy = busy;
	gtk_widget_set_sensitive (p->cancel_btn, busy);
	gtk_widget_set_visible (p->progress, busy);
}

static gboolean
pulse_cb (gpointer data)
{
	GitPage *p = data;

	if (!p->busy) {
		p->pulse_id = 0;
		return G_SOURCE_REMOVE;
	}
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (p->progress));
	return G_SOURCE_CONTINUE;
}

static void
push_followup_done (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = user;

	if (ok && out->len > 0) {
		log_line (p, "hint", "%s", _("GitHub Actions nach dem Push:"));
		log_feed (p, (const guint8 *) out->str, out->len);
		log_flush (p);
	}
}

static void
job_finish (Job *j, gboolean ok)
{
	GitPage *p = j->p;
	const gchar *hint;

	if (ok) {
		log_line (p, "ok", "%s", _("✔ Fertig."));
	} else {
		log_line (p, "err", "%s", _("✘ Fehlgeschlagen."));
		hint = hint_for (j->all->str);
		if (hint != NULL) {
			log_line (p, "hint", "%s %s", _("Hinweis:"), hint);
		}
		if (strstr (j->all->str, "CONFLICT") != NULL || strstr (j->all->str, "Merge conflict") != NULL
		    || strstr (j->all->str, "fix conflicts") != NULL) {
			refresh_conflicts (p);
			goto_tab (p, "conflicts");
		}
	}

	/* Push ohne Upstream: einmal automatisch mit Verknüpfung wiederholen. */
	if (!ok && (j->flags & GA_PUSH) && !j->retried && strstr (j->all->str, "has no upstream branch") != NULL) {
		GPtrArray *cmds = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
		GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
		Job *n;

		g_ptr_array_add (argv, g_strdup ("git"));
		g_ptr_array_add (argv, g_strdup ("push"));
		g_ptr_array_add (argv, g_strdup ("--progress"));
		g_ptr_array_add (argv, g_strdup ("--set-upstream"));
		g_ptr_array_add (argv, g_strdup ("origin"));
		g_ptr_array_add (argv, g_strdup ("HEAD"));
		g_ptr_array_add (cmds, argv);

		log_line (p, "hint", "%s", _("Verknüpfe den Branch automatisch mit „origin“ und pushe erneut …"));
		n = g_new0 (Job, 1);
		n->p = p;
		n->keep = g_object_ref (p->root);
		n->cmds = cmds;
		n->cwd = g_strdup (j->cwd);
		n->flags = j->flags;
		n->all = g_string_new (NULL);
		n->retried = TRUE;
		job_next (n);
		goto done;
	}

	set_busy (p, FALSE);

	if (ok && (j->flags & GA_PUSH) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (p->auto_chk))) {
		/* Automatische Aktionen nach dem Push: Build-Status der Workflows.
		 * Still: bei Nicht-GitHub-Servern gibt es dazu nichts zu zeigen. */
		quiet_run (p, j->cwd, push_followup_done, p, "gh", "run", "list", "-L", "3", NULL);
	}
	refresh_everything (p);

done:
	g_object_unref (j->keep);
	g_ptr_array_unref (j->cmds);
	g_free (j->cwd);
	g_string_free (j->all, TRUE);
	g_free (j);
}

static void
job_step_done (gpointer user, gboolean ok, GString *out)
{
	Job *j = user;

	g_string_append (j->all, out->str);
	if (!ok || j->idx >= j->cmds->len) {
		job_finish (j, ok);
		return;
	}
	job_next (j);
}

static void
job_next (Job *j)
{
	GitPage *p = j->p;
	GPtrArray *argv = g_ptr_array_ref (g_ptr_array_index (j->cmds, j->idx));
	GPtrArray *copy = g_ptr_array_new_with_free_func (g_free);
	gchar *line;
	guint i;

	for (i = 0; i < argv->len; i++) {
		g_ptr_array_add (copy, g_strdup (g_ptr_array_index (argv, i)));
	}
	j->idx++;

	line = cmdline (copy);
	log_line (p, "cmd", "%s", line);
	p->colorize = (strstr (line, " diff") != NULL || strstr (line, " show") != NULL);
	g_free (line);

	set_busy (p, TRUE);
	if (p->pulse_id == 0) {
		p->pulse_id = g_timeout_add (120, pulse_cb, p);
	}
	g_ptr_array_unref (argv);
	run_proc (p, copy, j->cwd, FALSE, job_step_done, j);
}

static void
confirm_and_run (GitPage *p, GPtrArray *cmds, const gchar *cwd, const gchar *confirm, guint flags);

typedef struct {
	GitPage   *p;
	GPtrArray *cmds;
	gchar     *cwd;
	guint      flags;
} Pending;

static void
start_job (GitPage *p, GPtrArray *cmds, const gchar *cwd, guint flags)
{
	Job *j = g_new0 (Job, 1);

	j->p = p;
	j->keep = g_object_ref (p->root);
	j->cmds = cmds;
	j->cwd = g_strdup (cwd);
	j->flags = flags;
	j->all = g_string_new (NULL);
	job_next (j);
}

static void
confirm_and_run (GitPage *p, GPtrArray *cmds, const gchar *cwd, const gchar *confirm, guint flags)
{
	if (confirm != NULL) {
		GtkWidget *top = gtk_widget_get_toplevel (p->root);
		GtkWidget *dlg = gtk_message_dialog_new (GTK_IS_WINDOW (top) ? GTK_WINDOW (top) : NULL,
							 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", confirm);
		GtkWidget *go;
		gint resp;

		gtk_dialog_add_button (GTK_DIALOG (dlg), _("Abbrechen"), GTK_RESPONSE_CANCEL);
		go = gtk_dialog_add_button (GTK_DIALOG (dlg), _("Ausführen"), GTK_RESPONSE_OK);
		gtk_style_context_add_class (gtk_widget_get_style_context (go), "destructive-action");
		gtk_dialog_set_default_response (GTK_DIALOG (dlg), GTK_RESPONSE_CANCEL);
		resp = gtk_dialog_run (GTK_DIALOG (dlg));
		gtk_widget_destroy (dlg);
		if (resp != GTK_RESPONSE_OK) {
			g_ptr_array_unref (cmds);
			log_line (p, "hint", "%s", _("Abgebrochen."));
			return;
		}
	}
	start_job (p, cmds, cwd, flags);
}

/* Führt eine Vorlage aus (nach Prüfung von Repository, Feldern, Rückfrage). */
static void
run_template (GitPage *p, const gchar *cmd, const gchar *confirm, guint flags)
{
	GString *err;
	GPtrArray *cmds;
	gchar *dir, *root, *cwd;

	if (p->busy) {
		log_line (p, "hint", "%s", _("Es läuft noch ein Vorgang – bitte warten oder abbrechen."));
		return;
	}
	dir = page_dir (p);
	if (dir == NULL) {
		log_line (p, "err", "%s", _("Kein lokaler Ordner geöffnet."));
		return;
	}
	root = find_root (dir);
	if (root == NULL && !(flags & GA_ANY)) {
		log_line (p, "err", "%s", _("Dieser Ordner ist kein Git-Repository. Im Reiter „Änderungen“ mit "
					   "„Repository anlegen“ ein neues erzeugen oder ein Repository klonen."));
		g_free (dir);
		return;
	}
	cwd = ((flags & GA_ANY) || root == NULL) ? dir : root;

	err = g_string_new (NULL);
	cmds = build_commands (p, cmd, err);
	if (cmds == NULL) {
		log_line (p, "err", "%s", err->str);
	} else {
		confirm_and_run (p, cmds, cwd, confirm, flags);
	}
	g_string_free (err, TRUE);
	g_free (dir);
	g_free (root);
}

static void
on_act_clicked (GtkButton *button, gpointer user_data)
{
	ActBtn *a = user_data;

	if (a->act->func != NULL) {
		a->act->func (a->p);
	} else {
		run_template (a->p, a->act->cmd, a->act->confirm, a->act->flags);
	}
}

static void
on_cancel_clicked (GtkButton *button, gpointer user_data)
{
	GitPage *p = user_data;

	if (p->proc != NULL) {
		g_subprocess_force_exit (p->proc);
		log_line (p, "hint", "%s", _("Vorgang wird abgebrochen …"));
	}
}

/* ------------------------------------------------------------------ */
/* Listen                                                               */
/* ------------------------------------------------------------------ */

static gchar *
status_label_text (const gchar *xy)
{
	GString *s = g_string_new (NULL);
	gchar x = xy[0], y = xy[1];

	if (x == '?' && y == '?') {
		return g_strdup (_("neu (nicht versioniert)"));
	}
	if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
		return g_strdup (_("KONFLIKT"));
	}
	if (x != ' ') {
		g_string_append_printf (s, _("vorgemerkt: %s"),
					x == 'M' ? _("geändert") : x == 'A' ? _("neu") : x == 'D' ? _("gelöscht")
					: x == 'R' ? _("umbenannt") : x == 'C' ? _("kopiert") : "?");
	}
	if (y != ' ') {
		if (s->len > 0) {
			g_string_append (s, " + ");
		}
		g_string_append_printf (s, _("offen: %s"), y == 'M' ? _("geändert") : y == 'D' ? _("gelöscht") : "?");
	}
	return g_string_free (s, FALSE);
}

static void
list_fill (ListSpec *ls, GString *out)
{
	GtkTreeIter it;
	gint n = ls->def->ncols;
	gint i;

	gtk_list_store_clear (ls->store);

	if (ls->def->mode == PARSE_PORCELAIN) {
		gsize pos = 0;

		while (pos < out->len) {
			const gchar *e = out->str + pos;
			gsize l = strlen (e);

			if (l >= 4) {
				gchar xy[3] = { e[0], e[1], 0 };
				gchar *label = status_label_text (xy);

				gtk_list_store_append (ls->store, &it);
				gtk_list_store_set (ls->store, &it, 0, label, 1, e + 3, -1);
				g_free (label);
				if (xy[0] == 'R' || xy[0] == 'C') {
					pos += l + 1;
					if (pos < out->len) {
						pos += strlen (out->str + pos);
					}
				}
			}
			pos += l + 1;
		}
		return;
	}

	{
		gchar **lines = g_strsplit (out->str, "\n", -1);
		gint k;

		for (k = 0; lines[k] != NULL; k++) {
			gchar *line = lines[k];

			if (line[0] == '\0') {
				continue;
			}
			gtk_list_store_append (ls->store, &it);
			if (ls->def->mode == PARSE_REMOTES) {
				gchar **parts = g_strsplit (line, "\t", 2);
				gchar *url;

				if (parts[1] == NULL || g_str_has_suffix (parts[1], "(push)")) {
					g_strfreev (parts);
					gtk_list_store_remove (ls->store, &it);
					continue;
				}
				url = g_strdup (parts[1]);
				if (g_str_has_suffix (url, " (fetch)")) {
					url[strlen (url) - 8] = '\0';
				}
				gtk_list_store_set (ls->store, &it, 0, parts[0], 1, url, -1);
				g_free (url);
				g_strfreev (parts);
			} else {
				gchar **parts = g_strsplit (line, "\t", n);

				for (i = 0; i < n; i++) {
					gtk_list_store_set (ls->store, &it, i, (parts[i] != NULL) ? parts[i] : "", -1);
					if (parts[i] == NULL) {
						break;
					}
				}
				g_strfreev (parts);
			}
		}
		g_strfreev (lines);
	}
}

static void
list_refresh_done (gpointer user, gboolean ok, GString *out)
{
	ListSpec *ls = user;

	if (ok) {
		list_fill (ls, out);
	} else {
		gtk_list_store_clear (ls->store);
	}
}

static void
list_refresh (ListSpec *ls)
{
	GitPage *p = ls->p;
	gchar *dir = page_dir (p);
	gchar *root = find_root (dir);
	GString *err = g_string_new (NULL);
	GPtrArray *cmds;

	if (dir == NULL || (root == NULL && !ls->def->any)) {
		gtk_list_store_clear (ls->store);
		goto out;
	}
	cmds = build_commands (p, ls->def->cmd, err);
	if (cmds != NULL) {
		GPtrArray *argv = g_ptr_array_ref (g_ptr_array_index (cmds, 0));
		GPtrArray *copy = g_ptr_array_new_with_free_func (g_free);
		guint i;

		for (i = 0; i < argv->len; i++) {
			g_ptr_array_add (copy, g_strdup (g_ptr_array_index (argv, i)));
		}
		g_ptr_array_unref (argv);
		g_ptr_array_unref (cmds);
		run_proc (p, copy, ls->def->any ? dir : (root != NULL ? root : dir), TRUE, list_refresh_done, ls);
	}
out:
	g_string_free (err, TRUE);
	g_free (dir);
	g_free (root);
}

static void
on_list_selection_changed (GtkTreeSelection *sel, gpointer user_data)
{
	ListSpec *ls = user_data;
	GtkTreeModel *model;
	GList *rows = gtk_tree_selection_get_selected_rows (sel, &model);
	GtkTreeIter it;
	gint i;

	if (rows != NULL && gtk_tree_model_get_iter (model, &it, rows->data)) {
		for (i = 0; i < ls->def->ncols; i++) {
			if (ls->def->fills[i] != NULL) {
				gchar *v = NULL;

				gtk_tree_model_get (model, &it, i, &v, -1);
				if (v != NULL) {
					field_set (ls->p, ls->def->fills[i], v);
				}
				g_free (v);
			}
		}
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
}

static void
on_list_map (GtkWidget *w, gpointer user_data)
{
	list_refresh (user_data);
}

static void
refresh_status_done (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = user;

	if (!ok) {
		gtk_label_set_text (GTK_LABEL (p->status_label), _("Kein Git-Repository in diesem Ordner."));
	} else {
		gchar **lines = g_strsplit (out->str, "\n", -1);
		gint count = 0, i;
		gchar *text;
		const gchar *first = lines[0] != NULL ? lines[0] : "";

		for (i = 1; lines[i] != NULL; i++) {
			if (lines[i][0] != '\0') {
				count++;
			}
		}
		if (g_str_has_prefix (first, "## ")) {
			first += 3;
		}
		text = g_strdup_printf (_("Branch: %s  •  %d geänderte Datei(en)"), first, count);
		gtk_label_set_text (GTK_LABEL (p->status_label), text);
		g_free (text);
		g_strfreev (lines);
	}
}

static void
refresh_status (GitPage *p)
{
	gchar *dir = page_dir (p);
	gchar *root = find_root (dir);

	if (root == NULL) {
		gtk_label_set_text (GTK_LABEL (p->status_label),
				    dir == NULL ? _("Kein lokaler Ordner geöffnet.")
						: _("Kein Git-Repository in diesem Ordner. Im Reiter „Änderungen“ anlegen oder klonen."));
	} else {
		quiet_run (p, root, refresh_status_done, p, "git", "status", "-sb", NULL);
	}
	g_free (dir);
	g_free (root);
}

static void
refresh_everything (GitPage *p)
{
	GList *l;

	refresh_status (p);
	for (l = p->lists; l != NULL; l = l->next) {
		ListSpec *ls = l->data;

		if (gtk_widget_get_mapped (ls->tree)) {
			list_refresh (ls);
		}
	}
}

static void
on_header_refresh_clicked (GtkButton *b, gpointer user_data)
{
	refresh_everything (user_data);
}

/* ------------------------------------------------------------------ */
/* Bausteine für die Oberfläche                                         */
/* ------------------------------------------------------------------ */

#define SEC_ACTS_END { NULL, NULL, NULL, NULL, 0, NULL }

static GtkWidget *
tab_box (GitPage *p, GtkWidget *notebook, const gchar *id, const gchar *title)
{
	GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gint idx;

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_set_border_width (GTK_CONTAINER (box), 10);
	gtk_container_add (GTK_CONTAINER (scroller), box);
	idx = gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scroller, gtk_label_new (title));
	if (notebook == p->notebook) {
		g_hash_table_insert (p->tab_pages, g_strdup (id), GINT_TO_POINTER (idx + 1));
	}
	return box;
}

static void
on_fill_from_selection (GtkButton *b, gpointer user_data)
{
	GitPage *p = user_data;
	const gchar *name = g_object_get_data (G_OBJECT (b), "field");
	GPtrArray *fm = fm_selection_paths (p);
	GString *s = g_string_new (NULL);
	guint i;

	for (i = 0; i < fm->len; i++) {
		gchar *q = g_shell_quote (g_ptr_array_index (fm, i));

		if (s->len > 0) {
			g_string_append_c (s, ' ');
		}
		/* Ohne Leerzeichen im Pfad unverändert, sonst unser Wort-Trenner-Problem vermeiden. */
		g_string_append (s, strchr (g_ptr_array_index (fm, i), ' ') ? q : g_ptr_array_index (fm, i));
		g_free (q);
	}
	field_set (p, name, s->str);
	g_string_free (s, TRUE);
	g_ptr_array_free (fm, TRUE);
}

static void
add_field_full (GitPage *p, GtkWidget *box, const gchar *name, const gchar *label, const gchar *placeholder,
		gboolean multiline, const gchar *initial, gboolean selection_button)
{
	GtkWidget *row;
	GtkWidget *l = gtk_label_new (label);
	GtkWidget *w;
	GPtrArray *arr;

	gtk_label_set_xalign (GTK_LABEL (l), 0.0);
	gtk_style_context_add_class (gtk_widget_get_style_context (l), "dim-label");

	if (multiline) {
		GtkWidget *sc = gtk_scrolled_window_new (NULL, NULL);

		row = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
		gtk_box_pack_start (GTK_BOX (row), l, FALSE, FALSE, 0);
		w = gtk_text_view_new ();
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (w), GTK_WRAP_WORD_CHAR);
		gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc), GTK_SHADOW_IN);
		gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc), 48);
		gtk_container_add (GTK_CONTAINER (sc), w);
		gtk_box_pack_start (GTK_BOX (row), sc, FALSE, FALSE, 0);
	} else {
		/* Beschriftung links neben dem Feld statt darüber: spart eine Zeile. */
		row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_label_set_width_chars (GTK_LABEL (l), 13);
		gtk_label_set_max_width_chars (GTK_LABEL (l), 13);
		gtk_label_set_ellipsize (GTK_LABEL (l), PANGO_ELLIPSIZE_END);
		gtk_widget_set_tooltip_text (l, label);
		gtk_box_pack_start (GTK_BOX (row), l, FALSE, FALSE, 0);
		w = gtk_entry_new ();
		gtk_entry_set_placeholder_text (GTK_ENTRY (w), placeholder != NULL ? placeholder : "");
		gtk_widget_set_tooltip_text (w, label);
		gtk_box_pack_start (GTK_BOX (row), w, TRUE, TRUE, 0);
		if (selection_button) {
			GtkWidget *bt = gtk_button_new_with_label (_("Auswahl"));

			gtk_widget_set_tooltip_text (bt, _("Dateien aus dem Hauptfenster übernehmen"));
			g_object_set_data_full (G_OBJECT (bt), "field", g_strdup (name), g_free);
			g_signal_connect (bt, "clicked", G_CALLBACK (on_fill_from_selection), p);
			gtk_box_pack_start (GTK_BOX (row), bt, FALSE, FALSE, 0);
		}
	}
	if (initial != NULL) {
		widget_set_text (w, initial);
	}
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);

	arr = g_hash_table_lookup (p->fields, name);
	if (arr == NULL) {
		arr = g_ptr_array_new ();
		g_hash_table_insert (p->fields, g_strdup (name), arr);
		g_hash_table_insert (p->labels, g_strdup (name), g_strdup (label));
	}
	g_ptr_array_add (arr, w);
}

static void
add_field (GitPage *p, GtkWidget *box, const gchar *name, const gchar *label, const gchar *placeholder,
	   const gchar *initial)
{
	add_field_full (p, box, name, label, placeholder, FALSE, initial, FALSE);
}

static void
add_text (GitPage *p, GtkWidget *box, const gchar *name, const gchar *label)
{
	add_field_full (p, box, name, label, NULL, TRUE, NULL, FALSE);
}

static void
add_check (GitPage *p, GtkWidget *box, const gchar *name, const gchar *label, gboolean initial)
{
	GtkWidget *c = gtk_check_button_new_with_label (label);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (c), initial);
	gtk_box_pack_start (GTK_BOX (box), c, FALSE, FALSE, 0);
	g_hash_table_insert (p->checks, g_strdup (name), c);
}

/* Einklappbare Gruppe: Überschrift als Pfeil-Zeile, Inhalt darunter. */
static GtkWidget *
group_begin (GtkWidget *box, const gchar *caption, gboolean expanded)
{
	GtkWidget *exp = gtk_expander_new (NULL);
	GtkWidget *inner = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *l = gtk_label_new (NULL);
	gchar *m = g_markup_printf_escaped ("<small><b>%s</b></small>", caption);

	gtk_label_set_markup (GTK_LABEL (l), m);
	g_free (m);
	gtk_expander_set_label_widget (GTK_EXPANDER (exp), l);
	gtk_expander_set_expanded (GTK_EXPANDER (exp), expanded);
	gtk_widget_set_margin_top (inner, 4);
	gtk_container_add (GTK_CONTAINER (exp), inner);
	gtk_box_pack_start (GTK_BOX (box), exp, FALSE, FALSE, 0);
	return inner;
}

static void
add_actions (GitPage *p, GtkWidget *box, const gchar *caption, const GAct *acts)
{
	GtkWidget *flow;
	gint i;

	if (caption != NULL) {
		box = group_begin (box, caption, FALSE);
	}
	flow = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), FALSE);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 3);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 3);
	gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 1);
	gtk_box_pack_start (GTK_BOX (box), flow, FALSE, FALSE, 0);

	for (i = 0; acts[i].label != NULL; i++) {
		GtkWidget *b = gtk_button_new_with_label (_(acts[i].label));
		ActBtn *a = g_new0 (ActBtn, 1);

		a->p = p;
		a->act = &acts[i];
		g_object_set_data_full (G_OBJECT (b), "act", a, g_free);
		if (acts[i].tip != NULL) {
			gtk_widget_set_tooltip_text (b, _(acts[i].tip));
		}
		g_signal_connect (b, "clicked", G_CALLBACK (on_act_clicked), a);
		gtk_container_add (GTK_CONTAINER (flow), b);
	}
}

static void
add_list (GitPage *p, GtkWidget *box, const ListDef *def)
{
	ListSpec *ls = g_new0 (ListSpec, 1);
	GType *types = g_new0 (GType, def->ncols);
	GtkWidget *sc, *h, *refresh;
	GtkTreeSelection *sel;
	gint i;

	for (i = 0; i < def->ncols; i++) {
		types[i] = G_TYPE_STRING;
	}
	ls->p = p;
	ls->def = def;
	ls->store = gtk_list_store_newv (def->ncols, types);
	g_free (types);
	ls->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (ls->store));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (ls->tree), TRUE);
	for (i = 0; i < def->ncols; i++) {
		GtkCellRenderer *r = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *c;

		g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		c = gtk_tree_view_column_new_with_attributes (_(def->titles[i]), r, "text", i, NULL);
		gtk_tree_view_column_set_resizable (c, TRUE);
		gtk_tree_view_column_set_min_width (c, (i == 0 && g_strcmp0 (def->id, "changes") == 0) ? 170 : (i == 0 && g_strcmp0 (def->id, "ghrepos") == 0) ? 220 : 60);
		gtk_tree_view_append_column (GTK_TREE_VIEW (ls->tree), c);
	}
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (ls->tree));
	gtk_tree_selection_set_mode (sel, def->multi ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);
	g_signal_connect (sel, "changed", G_CALLBACK (on_list_selection_changed), ls);
	g_signal_connect (ls->tree, "map", G_CALLBACK (on_list_map), ls);

	sc = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc), GTK_SHADOW_IN);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc), def->height);
	gtk_container_add (GTK_CONTAINER (sc), ls->tree);

	(void) h;
	(void) refresh;
	gtk_box_pack_start (GTK_BOX (box), sc, FALSE, FALSE, 0);

	p->lists = g_list_append (p->lists, ls);
}

/* ------------------------------------------------------------------ */
/* Sonderaktionen                                                       */
/* ------------------------------------------------------------------ */

static void
goto_tab (GitPage *p, const gchar *id)
{
	gpointer v = g_hash_table_lookup (p->tab_pages, id);

	if (v != NULL) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (p->notebook), GPOINTER_TO_INT (v) - 1);
	}
}

/* Versionsvorschlag aus dem letzten Tag (vX.Y.Z) -------------------- */
static void
version_done (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = ((gpointer *) user)[0];
	gint which = GPOINTER_TO_INT (((gpointer *) user)[1]);
	gint a = 0, b = 0, c = 0;
	gchar *s = g_strstrip (g_strdup (out->str));
	const gchar *num = (s[0] == 'v' || s[0] == 'V') ? s + 1 : s;
	gchar *next;

	g_free (user);
	if (!ok || sscanf (num, "%d.%d.%d", &a, &b, &c) < 2) {
		a = 0;
		b = 0;
		c = 0;
		which = 1;   /* noch kein Versions-Tag: mit 0.1.0 beginnen */
		if (!ok || s[0] == '\0') {
			next = g_strdup ("v0.1.0");
			field_set (p, "tag", next);
			log_line (p, "hint", _("Noch kein Versions-Tag – Vorschlag: %s"), next);
			g_free (next);
			g_free (s);
			return;
		}
	}
	if (which == 0) {
		c++;
	} else if (which == 1) {
		b++;
		c = 0;
	} else {
		a++;
		b = 0;
		c = 0;
	}
	next = g_strdup_printf ("v%d.%d.%d", a, b, c);
	field_set (p, "tag", next);
	log_line (p, "hint", _("Neue Version: %s (bisher: %s)"), next, s);
	g_free (next);
	g_free (s);
}

static void
version_bump (GitPage *p, gint which)
{
	gchar *dir = page_dir (p);
	gchar *root = find_root (dir);
	gpointer *ctx;

	if (root == NULL) {
		log_line (p, "err", "%s", _("Kein Git-Repository."));
	} else {
		ctx = g_new0 (gpointer, 2);
		ctx[0] = p;
		ctx[1] = GINT_TO_POINTER (which);
		quiet_run (p, root, version_done, ctx, "git", "describe", "--tags", "--abbrev=0", NULL);
	}
	g_free (dir);
	g_free (root);
}

static void act_ver_patch (GitPage *p) { version_bump (p, 0); }
static void act_ver_minor (GitPage *p) { version_bump (p, 1); }
static void act_ver_major (GitPage *p) { version_bump (p, 2); }

/* Konflikte ----------------------------------------------------------- */

static void
conflict_colorize (GtkTextBuffer *buf)
{
	GtkTextIter it, end;
	gint state = 0;   /* 0 außen, 1 lokal, 2 remote */
	gint line = 0;

	gtk_text_buffer_get_bounds (buf, &it, &end);
	gtk_text_buffer_remove_all_tags (buf, &it, &end);
	while (!gtk_text_iter_is_end (&it)) {
		GtkTextIter ls = it, le = it;
		gchar *t;

		gtk_text_iter_forward_to_line_end (&le);
		t = gtk_text_buffer_get_text (buf, &ls, &le, FALSE);
		if (g_str_has_prefix (t, "<<<<<<<")) {
			gtk_text_buffer_apply_tag_by_name (buf, "mark", &ls, &le);
			state = 1;
		} else if (state > 0 && g_str_has_prefix (t, "=======")) {
			gtk_text_buffer_apply_tag_by_name (buf, "mark", &ls, &le);
			state = 2;
		} else if (state > 0 && g_str_has_prefix (t, ">>>>>>>")) {
			gtk_text_buffer_apply_tag_by_name (buf, "mark", &ls, &le);
			state = 0;
		} else if (state == 1) {
			gtk_text_buffer_apply_tag_by_name (buf, "ours", &ls, &le);
		} else if (state == 2) {
			gtk_text_buffer_apply_tag_by_name (buf, "theirs", &ls, &le);
		}
		g_free (t);
		line++;
		if (!gtk_text_iter_forward_line (&it)) {
			break;
		}
	}
}

typedef struct {
	GitPage *p;
	gboolean ours;
} StageCtx;

static void
conflict_stage_done (gpointer user, gboolean ok, GString *out)
{
	StageCtx *c = user;
	GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (c->ours ? c->p->conf_ours : c->p->conf_theirs));

	gtk_text_buffer_set_text (b, ok ? out->str : _("(in dieser Stufe nicht vorhanden)"), -1);
	g_free (c);
}

static void
conflict_load (GitPage *p)
{
	gchar *dir = page_dir (p);
	gchar *root = find_root (dir);
	gchar *file = field_get (p, "cfile");

	if (root != NULL && file[0] != '\0') {
		gchar *full = g_build_filename (root, file, NULL);
		gchar *content = NULL;
		gchar *spec2 = g_strdup_printf (":2:%s", file);
		gchar *spec3 = g_strdup_printf (":3:%s", file);
		StageCtx *a = g_new0 (StageCtx, 1), *b = g_new0 (StageCtx, 1);

		if (g_file_get_contents (full, &content, NULL, NULL)) {
			GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result));

			gtk_text_buffer_set_text (buf, content, -1);
			conflict_colorize (buf);
			g_free (content);
		} else {
			gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result)),
						  _("(Datei nicht lesbar)"), -1);
		}
		a->p = p;
		a->ours = TRUE;
		b->p = p;
		b->ours = FALSE;
		quiet_run (p, root, conflict_stage_done, a, "git", "show", spec2, NULL);
		quiet_run (p, root, conflict_stage_done, b, "git", "show", spec3, NULL);
		g_free (spec2);
		g_free (spec3);
		g_free (full);
	}
	g_free (file);
	g_free (dir);
	g_free (root);
}

static void
on_cfile_changed (GtkEditable *e, gpointer user_data)
{
	conflict_load (user_data);
}

static void
refresh_conflicts (GitPage *p)
{
	GList *l;

	for (l = p->lists; l != NULL; l = l->next) {
		ListSpec *ls = l->data;

		if (g_strcmp0 (ls->def->id, "conflicts") == 0) {
			list_refresh (ls);
		}
	}
}

/* Bereich um den Textcursor: von "<<<<<<<" bis ">>>>>>>". */
static gboolean
conflict_block_at_cursor (GtkTextBuffer *buf, GtkTextIter *start, GtkTextIter *mid, GtkTextIter *end)
{
	GtkTextIter cur, it;
	gint cline;
	gint s = -1, m = -1, e = -1, n, total;

	gtk_text_buffer_get_iter_at_mark (buf, &cur, gtk_text_buffer_get_insert (buf));
	cline = gtk_text_iter_get_line (&cur);
	total = gtk_text_buffer_get_line_count (buf);

	for (n = cline; n >= 0 && s < 0; n--) {
		gchar *t;
		GtkTextIter ls, le;

		gtk_text_buffer_get_iter_at_line (buf, &ls, n);
		le = ls;
		gtk_text_iter_forward_to_line_end (&le);
		t = gtk_text_buffer_get_text (buf, &ls, &le, FALSE);
		if (g_str_has_prefix (t, "<<<<<<<")) {
			s = n;
		} else if (n < cline && g_str_has_prefix (t, ">>>>>>>")) {
			g_free (t);
			return FALSE;
		}
		g_free (t);
	}
	if (s < 0) {
		return FALSE;
	}
	for (n = s + 1; n < total && e < 0; n++) {
		gchar *t;
		GtkTextIter ls, le;

		gtk_text_buffer_get_iter_at_line (buf, &ls, n);
		le = ls;
		gtk_text_iter_forward_to_line_end (&le);
		t = gtk_text_buffer_get_text (buf, &ls, &le, FALSE);
		if (m < 0 && g_str_has_prefix (t, "=======")) {
			m = n;
		} else if (g_str_has_prefix (t, ">>>>>>>")) {
			e = n;
		}
		g_free (t);
	}
	if (m < 0 || e < 0 || cline > e) {
		return FALSE;
	}
	gtk_text_buffer_get_iter_at_line (buf, start, s);
	gtk_text_buffer_get_iter_at_line (buf, mid, m);
	if (e + 1 < total) {
		gtk_text_buffer_get_iter_at_line (buf, end, e + 1);
	} else {
		gtk_text_buffer_get_end_iter (buf, end);
	}
	(void) it;
	return TRUE;
}

static void
conflict_take (GitPage *p, gint which /* 0 lokal, 1 remote, 2 beide */)
{
	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result));
	GtkTextIter s, m, e, ours_start, theirs_start, theirs_end;
	gchar *ours, *theirs, *res;
	gint eline;
	gchar *tmp;

	if (!conflict_block_at_cursor (buf, &s, &m, &e)) {
		log_line (p, "hint", "%s", _("Den Cursor in einen Konfliktbereich setzen (zwischen <<<<<<< und >>>>>>>)."));
		return;
	}
	ours_start = s;
	gtk_text_iter_forward_line (&ours_start);
	ours = gtk_text_buffer_get_text (buf, &ours_start, &m, FALSE);
	theirs_start = m;
	gtk_text_iter_forward_line (&theirs_start);
	theirs_end = e;
	eline = gtk_text_iter_get_line (&e);
	/* letzte Zeile (>>>>>>>) ausschließen */
	gtk_text_buffer_get_iter_at_line (buf, &theirs_end, eline - 1 >= 0 ? eline - 1 : 0);
	{
		GtkTextIter marker_start;
		gint total = gtk_text_buffer_get_line_count (buf);
		gint end_line = (gtk_text_iter_is_end (&e)) ? total - 1 : gtk_text_iter_get_line (&e) - 1;

		gtk_text_buffer_get_iter_at_line (buf, &marker_start, end_line);
		theirs_end = marker_start;
	}
	theirs = gtk_text_buffer_get_text (buf, &theirs_start, &theirs_end, FALSE);
	res = which == 0 ? g_strdup (ours) : which == 1 ? g_strdup (theirs) : g_strconcat (ours, theirs, NULL);
	(void) tmp;

	gtk_text_buffer_delete (buf, &s, &e);
	gtk_text_buffer_get_iter_at_mark (buf, &s, gtk_text_buffer_get_insert (buf));
	gtk_text_buffer_insert (buf, &s, res, -1);
	conflict_colorize (buf);
	g_free (ours);
	g_free (theirs);
	g_free (res);
}

static void act_conf_ours (GitPage *p) { conflict_take (p, 0); }
static void act_conf_theirs (GitPage *p) { conflict_take (p, 1); }
static void act_conf_both (GitPage *p) { conflict_take (p, 2); }

static void
act_conf_next (GitPage *p)
{
	GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result));
	GtkTextIter it, found;
	gint n, total = gtk_text_buffer_get_line_count (buf), start;

	gtk_text_buffer_get_iter_at_mark (buf, &it, gtk_text_buffer_get_insert (buf));
	start = gtk_text_iter_get_line (&it) + 1;
	for (n = 0; n < total; n++) {
		gint line = (start + n) % total;
		GtkTextIter ls, le;
		gchar *t;

		gtk_text_buffer_get_iter_at_line (buf, &ls, line);
		le = ls;
		gtk_text_iter_forward_to_line_end (&le);
		t = gtk_text_buffer_get_text (buf, &ls, &le, FALSE);
		if (g_str_has_prefix (t, "<<<<<<<")) {
			found = ls;
			gtk_text_buffer_place_cursor (buf, &found);
			gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (p->conf_result), &found, 0.1, FALSE, 0, 0);
			g_free (t);
			return;
		}
		g_free (t);
	}
	log_line (p, "ok", "%s", _("Keine Konfliktbereiche mehr in dieser Datei."));
}

static void
conflict_saved_done (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = user;

	log_line (p, ok ? "ok" : "err", "%s", ok ? _("✔ Gespeichert und als gelöst markiert.") : out->str);
	refresh_conflicts (p);
	refresh_status (p);
}

static void
act_conf_save (GitPage *p)
{
	gchar *dir = page_dir (p);
	gchar *root = find_root (dir);
	gchar *file = field_get (p, "cfile");

	if (root == NULL || file[0] == '\0') {
		log_line (p, "hint", "%s", _("Zuerst eine Datei aus der Konfliktliste wählen."));
	} else {
		gchar *text = widget_text (p->conf_result);
		gchar *full = g_build_filename (root, file, NULL);
		GError *err = NULL;
		gboolean markers = (strstr (text, "\n<<<<<<<") != NULL || g_str_has_prefix (text, "<<<<<<<")
				    || strstr (text, "\n>>>>>>>") != NULL);

		if (markers) {
			log_line (p, "err", "%s", _("Es sind noch Konfliktmarker (<<<<<<< / >>>>>>>) in der Datei. "
						   "Erst alle Bereiche lösen."));
		} else {
			GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result));
			GtkTextIter s, e;
			gchar *full_text;

			/* Ursprünglichen Text ohne Abschneiden speichern (widget_text trimmt). */
			gtk_text_buffer_get_bounds (b, &s, &e);
			full_text = gtk_text_buffer_get_text (b, &s, &e, FALSE);
			if (!g_file_set_contents (full, full_text, -1, &err)) {
				log_line (p, "err", "%s", err->message);
				g_clear_error (&err);
			} else {
				quiet_run (p, root, conflict_saved_done, p, "git", "add", "--", file, NULL);
			}
			g_free (full_text);
		}
		g_free (full);
		g_free (text);
	}
	g_free (file);
	g_free (dir);
	g_free (root);
}

/* Dateien im GitHub-Repository ---------------------------------------- */

static void
repo_file_loaded (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = user;

	if (ok) {
		field_set (p, "filebody", out->str);
		log_line (p, "ok", "%s", _("✔ Datei geladen – unten bearbeiten und „Änderung speichern“ klicken."));
	} else {
		log_line (p, "err", "%s", out->str);
	}
}

static void
act_repo_file_load (GitPage *p)
{
	gchar *path = field_get (p, "path");
	gchar *dir = page_dir (p);

	if (path[0] == '\0') {
		log_line (p, "hint", "%s", _("Bitte den Dateipfad im Repository eingeben (Feld „Pfad“)."));
	} else {
		gchar *ep = g_strdup_printf ("repos/{owner}/{repo}/contents/%s", path);

		quiet_run (p, dir, repo_file_loaded, p, "gh", "api", "-H", "Accept: application/vnd.github.raw", ep, NULL);
		g_free (ep);
	}
	g_free (path);
	g_free (dir);
}


/* Roher (nicht gekürzter) Text des ersten Feldes dieses Namens. */
static gchar *
field_raw (GitPage *p, const gchar *name)
{
	GPtrArray *arr = g_hash_table_lookup (p->fields, name);
	GtkWidget *w;
	GtkTextIter s, e;

	if (arr == NULL || arr->len == 0) {
		return g_strdup ("");
	}
	w = g_ptr_array_index (arr, 0);
	if (GTK_IS_ENTRY (w)) {
		return g_strdup (gtk_entry_get_text (GTK_ENTRY (w)));
	}
	gtk_text_buffer_get_bounds (gtk_text_view_get_buffer (GTK_TEXT_VIEW (w)), &s, &e);
	return gtk_text_buffer_get_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (w)), &s, &e, FALSE);
}

typedef struct {
	GitPage *p;
	gchar   *path;
	gchar   *msg;
	gchar   *b64;
	gchar   *dir;
} SaveCtx;

static void
repo_file_sha_done (gpointer user, gboolean ok, GString *out)
{
	SaveCtx *c = user;
	GPtrArray *cmds = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	gchar *sha = g_strstrip (g_strdup (ok ? out->str : ""));

	g_ptr_array_add (argv, g_strdup ("gh"));
	g_ptr_array_add (argv, g_strdup ("api"));
	g_ptr_array_add (argv, g_strdup ("-X"));
	g_ptr_array_add (argv, g_strdup ("PUT"));
	g_ptr_array_add (argv, g_strdup_printf ("repos/{owner}/{repo}/contents/%s", c->path));
	g_ptr_array_add (argv, g_strdup ("-f"));
	g_ptr_array_add (argv, g_strdup_printf ("message=%s", c->msg));
	g_ptr_array_add (argv, g_strdup ("-f"));
	g_ptr_array_add (argv, g_strdup_printf ("content=%s", c->b64));
	if (sha[0] != '\0') {
		g_ptr_array_add (argv, g_strdup ("-f"));
		g_ptr_array_add (argv, g_strdup_printf ("sha=%s", sha));
	}
	g_ptr_array_add (cmds, argv);
	start_job (c->p, cmds, c->dir, 0);

	g_free (sha);
	g_free (c->path);
	g_free (c->msg);
	g_free (c->b64);
	g_free (c->dir);
	g_free (c);
}

static void
act_repo_file_save (GitPage *p)
{
	gchar *path = field_get (p, "path");
	gchar *msg = field_get (p, "rfmsg");
	gchar *body = field_raw (p, "filebody");
	gchar *dir = page_dir (p);

	if (p->busy) {
		log_line (p, "hint", "%s", _("Es läuft noch ein Vorgang."));
	} else if (path[0] == '\0' || msg[0] == '\0') {
		log_line (p, "hint", "%s", _("Bitte „Pfad“ und „Commit-Nachricht (Datei)“ ausfüllen."));
	} else if (dir == NULL) {
		log_line (p, "err", "%s", _("Kein lokaler Ordner geöffnet."));
	} else {
		SaveCtx *c = g_new0 (SaveCtx, 1);
		gchar *ep = g_strdup_printf ("repos/{owner}/{repo}/contents/%s", path);

		c->p = p;
		c->path = g_strdup (path);
		c->msg = g_strdup (msg);
		c->b64 = g_base64_encode ((const guchar *) body, strlen (body));
		c->dir = g_strdup (dir);
		quiet_run (p, dir, repo_file_sha_done, c, "gh", "api", ep, "--jq", ".sha", NULL);
		g_free (ep);
	}
	g_free (path);
	g_free (msg);
	g_free (body);
	g_free (dir);
}

static void
repo_file_dl_done (gpointer user, gboolean ok, GString *out)
{
	gpointer *ctx = user;
	GitPage *p = ctx[0];
	gchar *dest = ctx[1];
	GError *err = NULL;

	if (!ok) {
		log_line (p, "err", "%s", out->str);
	} else if (!g_file_set_contents (dest, out->str, out->len, &err)) {
		log_line (p, "err", "%s", err->message);
		g_clear_error (&err);
	} else {
		log_line (p, "ok", _("✔ Heruntergeladen nach %s"), dest);
	}
	g_free (dest);
	g_free (ctx);
}

static void
act_repo_file_download (GitPage *p)
{
	gchar *path = field_get (p, "path");
	gchar *dir = page_dir (p);

	if (path[0] == '\0' || dir == NULL) {
		log_line (p, "hint", "%s", _("Bitte den Dateipfad im Repository eingeben (Feld „Pfad“)."));
	} else {
		gchar *base = g_path_get_basename (path);
		gpointer *ctx = g_new0 (gpointer, 2);
		gchar *ep = g_strdup_printf ("repos/{owner}/{repo}/contents/%s", path);

		ctx[0] = p;
		ctx[1] = g_build_filename (dir, base, NULL);
		quiet_run (p, dir, repo_file_dl_done, ctx, "gh", "api", "-H", "Accept: application/vnd.github.raw", ep, NULL);
		g_free (ep);
		g_free (base);
	}
	g_free (path);
	g_free (dir);
}

/* ------------------------------------------------------------------ */
/* Tabellen: Schaltflächen und Listen                                    */
/* ------------------------------------------------------------------ */

#define A(l, t, c)         { l, t, c, NULL, 0, NULL }
#define AC(l, t, c, q)     { l, t, c, q, 0, NULL }
#define AA(l, t, c)        { l, t, c, NULL, GA_ANY, NULL }
#define AAC(l, t, c, q)    { l, t, c, q, GA_ANY, NULL }
#define AP(l, t, c)        { l, t, c, NULL, GA_PUSH, NULL }
#define AF(l, t, f)        { l, t, NULL, NULL, 0, f }

static const GAct acts_view[] = {
	A ("Status", "Vollständiger Git-Status", "git status"),
	A ("Diff (offen)", "Noch nicht vorgemerkte Änderungen (Auswahl optional)", "git diff --no-color -- {files?}"),
	A ("Diff (vorgemerkt)", "Vorgemerkte Änderungen (Auswahl optional)", "git diff --cached --no-color -- {files?}"),
	SEC_ACTS_END
};
static const GAct acts_stage[] = {
	A ("Auswahl hinzufügen", "Markierte Dateien für den Commit vormerken (git add)", "git add -- {files}"),
	A ("Alle hinzufügen", "Alle Änderungen vormerken (git add -A)", "git add -A"),
	A ("Aus Vormerkung nehmen", "git restore --staged", "git restore --staged -- {files}"),
	SEC_ACTS_END
};
static const GAct acts_files[] = {
	AC ("Aus Git + Ordner löschen", "git rm", "git rm -r -- {files}",
	    "Die markierten Dateien werden aus Git UND vom Datenträger gelöscht. Fortfahren?"),
	AC ("Nur aus Git nehmen", "git rm --cached: Datei bleibt auf der Platte", "git rm -r --cached -- {files}",
	    "Die markierten Dateien werden nicht mehr versioniert (bleiben aber im Ordner). Fortfahren?"),
	A ("Verschieben/Umbenennen", "git mv: Ziel im Feld „Neuer Pfad“", "git mv {files} {pathto}"),
	AC ("Änderungen verwerfen", "git restore: macht Änderungen an der Datei rückgängig", "git restore -- {files}",
	    "Nicht gespeicherte Änderungen an den markierten Dateien gehen unwiderruflich verloren. Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_commit[] = {
	A ("Commit (nur Vorgemerktes)", "Speichert nur, was vorgemerkt ist", "git commit -m {msg}"),
	A ("Alles committen", "Merkt alle Änderungen vor und speichert sie", "git add -A && git commit -m {msg}"),
	AP ("Commit + Push", "Alles speichern und sofort zum Server hochladen",
	    "git add -A && git commit -m {msg} && git push --progress"),
	AC ("Letzten Commit ändern", "Schreibt die Nachricht des letzten Commits neu", "git commit --amend -m {msg}",
	    "Der letzte Commit wird neu geschrieben. Das ist nur sicher, solange er noch nicht gepusht wurde. Fortfahren?"),
	AC ("Letzten Commit ergänzen", "Nimmt weitere Änderungen in den letzten Commit auf",
	    "git add -A && git commit --amend --no-edit",
	    "Der letzte Commit wird neu geschrieben. Das ist nur sicher, solange er noch nicht gepusht wurde. Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_reset[] = {
	A ("Reset (mixed)", "Branch zurücksetzen, Dateien behalten (nicht vorgemerkt)", "git reset {target}"),
	A ("Reset (soft)", "Branch zurücksetzen, Änderungen bleiben vorgemerkt", "git reset --soft {target}"),
	AC ("Reset (hard)", "Branch UND Dateien zurücksetzen", "git reset --hard {target}",
	    "ACHTUNG: Alle nicht committeten Änderungen gehen verloren und der Branch springt auf das Ziel. Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_stash[] = {
	A ("Zurückstellen", "git stash push (inkl. neuer Dateien, Name optional im Feld „Nachricht“)", "git stash push -u {opt:-m:msg}"),
	A ("Liste", "Alle Stashes anzeigen", "git stash list"),
	A ("Zurückholen (pop)", "Neuester bzw. gewählter Stash zurück und entfernen", "git stash pop {stash?}"),
	A ("Anwenden", "Stash anwenden und behalten", "git stash apply {stash?}"),
	AC ("Verwerfen (drop)", "Stash löschen", "git stash drop {stash?}", "Der Stash wird endgültig gelöscht. Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_clean[] = {
	A ("Vorschau", "Zeigt, was bereinigt würde (git clean -nd)", "git clean -nd"),
	AC ("Bereinigen", "Löscht alle nicht versionierten Dateien und Ordner", "git clean -fd",
	    "ACHTUNG: Alle nicht versionierten Dateien und Ordner werden unwiderruflich gelöscht! Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_repo[] = {
	AA ("Repository anlegen (init)", "git init im aktuellen Ordner", "git init"),
	AA ("Klonen", "git clone in den aktuellen Ordner (Adresse im Feld)", "git clone --progress {url}"),
	SEC_ACTS_END
};

static const GAct acts_hist[] = {
	A ("Details", "git show --stat", "git show --stat --no-color {commit}"),
	A ("Änderungen", "git show", "git show --no-color {commit}"),
	A ("Graph", "Verlauf als Baum", "git log --oneline --graph --decorate -n 60"),
	A ("Verlauf der Datei", "Verlauf der in der Liste/Hauptfenster gewählten Datei", "git log --follow --oneline -- {files}"),
	AC ("Revert", "Macht den Commit durch einen neuen Commit rückgängig", "git revert --no-edit {commit}",
	    "Es wird ein neuer Commit erzeugt, der den gewählten Commit rückgängig macht. Fortfahren?"),
	A ("Cherry-pick", "Übernimmt den Commit auf den aktuellen Branch", "git cherry-pick {commit}"),
	AC ("Auschecken", "Wechselt auf diesen Commit (detached HEAD)", "git checkout {commit}",
	    "Du wechselst auf einen einzelnen Commit (losgelöster HEAD). Neue Commits gehen dort leicht verloren. Fortfahren?"),
	SEC_ACTS_END
};

static const GAct acts_branch[] = {
	A ("Neu", "git branch", "git branch {newname}"),
	A ("Neu + wechseln", "git switch -c", "git switch -c {newname}"),
	A ("Wechseln", "git switch", "git switch {branch}"),
	A ("Remote-Branch auschecken", "Legt einen lokalen Branch an, der den Server-Branch verfolgt", "git switch --track {branch}"),
	A ("Umbenennen", "Gewählter Branch → „Neuer Name“", "git branch -m {branch} {newname}"),
	AC ("Löschen", "git branch -d (nur wenn zusammengeführt)", "git branch -d {branch}", "Branch löschen?"),
	AC ("Löschen erzwingen", "git branch -D", "git branch -D {branch}",
	    "Der Branch wird auch gelöscht, wenn er nicht zusammengeführt ist. Commits können verloren gehen. Fortfahren?"),
	SEC_ACTS_END
};
static const GAct acts_merge[] = {
	A ("Zusammenführen (merge)", "Gewählten Branch in den aktuellen mergen", "git merge {branch}"),
	AC ("Neu aufbauen (rebase)", "Aktuellen Branch auf den gewählten setzen", "git rebase {branch}",
	    "Rebase schreibt den Verlauf des aktuellen Branches um. Bei bereits gepushten Branches ist das riskant. Fortfahren?"),
	A ("Merge abbrechen", "git merge --abort", "git merge --abort"),
	A ("Rebase fortsetzen", "git rebase --continue", "git rebase --continue"),
	A ("Rebase abbrechen", "git rebase --abort", "git rebase --abort"),
	A ("Merge abschließen", "Nach gelösten Konflikten: git commit --no-edit", "git commit --no-edit"),
	SEC_ACTS_END
};
static const GAct acts_compare[] = {
	A ("Commits vergleichen", "Was ist nur links (<) bzw. nur rechts (>)?",
	   "git log --oneline --left-right --cherry-pick {branch}...{target}"),
	A ("Unterschiede (diff)", "git diff zwischen beiden Branches", "git diff --no-color {branch} {target}"),
	A ("Dateien", "Geänderte Dateien zwischen beiden Branches", "git diff --stat --no-color {branch} {target}"),
	A ("Branch-Status", "Stand + Verknüpfung aller Branches", "git status -sb && git branch -vv"),
	SEC_ACTS_END
};
static const GAct acts_default[] = {
	A ("Standard-Branch (lokal)", "Setzt init.defaultBranch für neue Repositories", "git config --global init.defaultBranch {branch}"),
	AA ("Standard-Branch (GitHub)", "Ändert den Standard-Branch des GitHub-Repositories", "gh repo edit --default-branch {branch}"),
	SEC_ACTS_END
};

static const GAct acts_remote[] = {
	A ("Hinzufügen", "git remote add", "git remote add {remote} {url}"),
	A ("Upstream hinzufügen", "Fügt die Quelle eines Forks als „upstream“ hinzu", "git remote add upstream {url}"),
	AC ("Entfernen", "git remote remove", "git remote remove {remote}", "Verbindung zum Server entfernen?"),
	A ("Umbenennen", "Gewählter Remote → „Neuer Name“", "git remote rename {remote} {newname}"),
	A ("URL ändern", "git remote set-url", "git remote set-url {remote} {url}"),
	A ("Informationen", "git remote show", "git remote show {remote}"),
	A ("Remote-Branches", "git branch -r", "git branch -r"),
	SEC_ACTS_END
};
static const GAct acts_fetch[] = {
	A ("Abrufen (fetch)", "Neues vom Server holen, nichts ändern", "git fetch --progress {remote?}"),
	A ("Alle Remotes", "git fetch --all --prune", "git fetch --all --prune --progress"),
	A ("Einzelner Branch", "git fetch Remote Branch", "git fetch --progress {remote} {branch}"),
	A ("Tags abrufen", "git fetch --tags", "git fetch --tags --progress {remote?}"),
	SEC_ACTS_END
};
static const GAct acts_pull[] = {
	A ("Pull (merge)", "Holen und zusammenführen", "git pull --no-rebase --progress"),
	A ("Pull (rebase)", "Holen und eigene Commits obendrauf setzen", "git pull --rebase --progress"),
	A ("Nur Fast-Forward", "Bricht ab, wenn ein Merge nötig wäre", "git pull --ff-only --progress"),
	A ("Einzelner Branch", "git pull Remote Branch", "git pull --progress {remote} {branch}"),
	SEC_ACTS_END
};
static const GAct acts_sync[] = {
	A ("Synchronisierungsstatus", "Stand und Vorsprung/Rückstand", "git status -sb"),
	A ("Lokal ↔ Remote vergleichen", "< nur lokal, > nur auf dem Server", "git log --oneline --left-right --cherry-pick @{u}...HEAD"),
	A ("Remote-Änderungen", "Was sich auf dem Server geändert hat", "git diff --stat --no-color HEAD @{u}"),
	A ("Konflikte prüfen", "Listet Dateien mit ungelösten Konflikten", "git diff --name-only --diff-filter=U"),
	A ("Vor dem Push prüfen", "Offene Änderungen und noch nicht gepushte Commits", "git status -sb && git log --oneline @{u}..HEAD"),
	A ("Push-Verlauf", "Letzte Bewegungen des Server-Branches", "git reflog show -n 30 --date=iso @{u}"),
	SEC_ACTS_END
};
static const GAct acts_push[] = {
	AP ("Push (aktueller Branch)", "Lädt den aktuellen Branch hoch; setzt die Verknüpfung bei Bedarf selbst", "git push --progress"),
	A ("Push Branch", "git push Remote Branch", "git push --progress {remote} {branch}"),
	A ("Push mit Upstream", "git push --set-upstream", "git push --progress --set-upstream {remote} {branch}"),
	A ("Alle Branches", "git push --all", "git push --progress --all {remote?}"),
	A ("Tag", "Einzelnen Tag hochladen", "git push --progress {remote} {tag}"),
	A ("Alle Tags", "git push --tags", "git push --progress --tags {remote?}"),
	AC ("Force-with-lease", "Überschreibt den Server nur, wenn niemand sonst gepusht hat",
	    "git push --progress --force-with-lease {remote} {branch}",
	    "Der Branch auf dem Server wird mit deinem Stand überschrieben (nur wenn dort nichts Neues ist). Fortfahren?"),
	A ("Testlauf (dry run)", "Zeigt, was gepusht würde, ohne etwas zu senden", "git push --dry-run --progress {remote?} {branch?}"),
	AC ("Remote-Branch löschen", "git push --delete", "git push {remote} --delete {branch}",
	    "Der Branch wird auf dem Server gelöscht. Fortfahren?"),
	A ("Atomar mehrere Refs", "Alle oder keine: Refs im Feld „Refs“", "git push --atomic --progress {remote} {refs*}"),
	SEC_ACTS_END
};

static const GAct acts_tag[] = {
	A ("Leichter Tag", "git tag", "git tag {tag} {commit?}"),
	A ("Annotierter Tag", "git tag -a mit Nachricht", "git tag -a {tag} -m {tagmsg} {commit?}"),
	AC ("Neu erstellen (ersetzen)", "Überschreibt einen vorhandenen Tag", "git tag -f -a {tag} -m {tagmsg} {commit?}",
	    "Der vorhandene Tag wird auf den neuen Stand gesetzt. Fortfahren?"),
	AC ("Löschen (lokal)", "git tag -d", "git tag -d {tag}", "Tag lokal löschen?"),
	A ("Anzeigen", "git show Tag", "git show --no-color {tag}"),
	A ("Versionsverlauf", "Alle Versions-Tags mit Commits", "git log --tags --simplify-by-decoration --pretty=format:%h%x20%d%x20%s -n 40"),
	SEC_ACTS_END
};
static const GAct acts_tag_ver[] = {
	AF ("Patch +1", "Nächste Fehlerkorrektur-Version (x.y.Z+1)", act_ver_patch),
	AF ("Minor +1", "Nächste Funktions-Version (x.Y+1.0)", act_ver_minor),
	AF ("Major +1", "Nächste Hauptversion (X+1.0.0)", act_ver_major),
	SEC_ACTS_END
};
static const GAct acts_tag_srv[] = {
	A ("Herunterladen", "git fetch --tags", "git fetch --tags --progress {remote?}"),
	AP ("Hochladen", "Tag zu GitHub hochladen", "git push --progress {remote} {tag}"),
	AP ("Alle hochladen", "git push --tags", "git push --progress --tags {remote}"),
	AC ("Remote-Tag löschen", "Löscht den Tag auf dem Server", "git push {remote} --delete {tag}",
	    "Der Tag wird auf dem Server gelöscht. Fortfahren?"),
	SEC_ACTS_END
};

static const GAct acts_conf[] = {
	AF ("Nächster Konflikt", "Springt zum nächsten Konfliktbereich", act_conf_next),
	AF ("Bereich: Lokal", "Behält im Bereich am Cursor die lokale Version", act_conf_ours),
	AF ("Bereich: Remote", "Behält im Bereich am Cursor die Version vom Server", act_conf_theirs),
	AF ("Bereich: Beide", "Behält beide Versionen (lokal zuerst)", act_conf_both),
	AF ("Speichern + gelöst", "Speichert die Datei und markiert sie als gelöst (git add)", act_conf_save),
	SEC_ACTS_END
};
static const GAct acts_conf_file[] = {
	A ("Ganze Datei: Lokal", "Nimmt die lokale Version", "git checkout --ours -- {cfile} && git add -- {cfile}"),
	A ("Ganze Datei: Remote", "Nimmt die Version vom Server", "git checkout --theirs -- {cfile} && git add -- {cfile}"),
	A ("Merge abschließen", "git commit --no-edit", "git commit --no-edit"),
	A ("Merge abbrechen", "git merge --abort", "git merge --abort"),
	A ("Rebase fortsetzen", "git rebase --continue", "git rebase --continue"),
	A ("Rebase abbrechen", "git rebase --abort", "git rebase --abort"),
	SEC_ACTS_END
};

/* GitHub */
static const GAct acts_gh_repo[] = {
	AA ("Anmeldestatus", "gh auth status", "gh auth status"),
	AA ("Neu (öffentlich)", "Neues öffentliches GitHub-Repository", "gh repo create {repo} --public {opt:--description:desc}"),
	AA ("Neu (privat)", "Neues privates GitHub-Repository", "gh repo create {repo} --private {opt:--description:desc}"),
	A ("Ordner veröffentlichen (öffentlich)", "Legt das Repo an, verbindet origin und pusht",
	   "gh repo create {repo} --public --source=. --remote=origin --push {opt:--description:desc}"),
	A ("Ordner veröffentlichen (privat)", "Legt ein privates Repo an, verbindet origin und pusht",
	   "gh repo create {repo} --private --source=. --remote=origin --push {opt:--description:desc}"),
	SEC_ACTS_END
};
static const GAct acts_gh_repo2[] = {
	AA ("Informationen", "gh repo view", "gh repo view {repo?}"),
	AA ("Im Browser öffnen", "Öffnet das Repository auf github.com", "gh repo view {repo?} --web"),
	AA ("Klonen", "gh repo clone", "gh repo clone {repo}"),
	AA ("Suchen", "Durchsucht GitHub (Feld „Suche“)", "gh search repos {query} --limit 30"),
	AA ("Beschreibung ändern", "gh repo edit --description", "gh repo edit {repo?} --description {desc}"),
	AAC ("Öffentlich machen", "Sichtbarkeit auf öffentlich", "gh repo edit {repo?} --visibility public --accept-visibility-change-consequences",
	     "Das Repository wird für ALLE sichtbar. Fortfahren?"),
	AAC ("Privat machen", "Sichtbarkeit auf privat", "gh repo edit {repo?} --visibility private --accept-visibility-change-consequences",
	     "Das Repository wird privat. Fortfahren?"),
	AA ("Einstellungen (Browser)", "Öffnet die Repository-Einstellungen", "gh browse --settings"),
	AA ("Forken", "Erstellt einen Fork auf deinem Konto", "gh repo fork {repo} --clone=false"),
	AA ("Forks anzeigen", "Forks des aktuellen Repositories", "gh api repos/{owner}/{repo}/forks --jq '.[].full_name'"),
	SEC_ACTS_END
};
static const GAct acts_gh_files[] = {
	AA ("Dateien anzeigen", "Inhalt des Pfades (leer = Hauptebene)",
	    "gh api repos/{owner}/{repo}/contents/{$path?} --jq 'if type==\"array\" then .[]|(.type+\"  \"+.path) else .path end'"),
	AF ("Datei laden", "Lädt die Datei in das Bearbeitungsfeld", act_repo_file_load),
	AF ("Änderung speichern", "Schreibt das Bearbeitungsfeld als Commit ins Repository", act_repo_file_save),
	AF ("Herunterladen", "Speichert die Datei im aktuellen Ordner", act_repo_file_download),
	SEC_ACTS_END
};
static const GAct acts_gh_collab[] = {
	AA ("Collaborators anzeigen", "Personen mit Zugriff", "gh api repos/{owner}/{repo}/collaborators --jq '.[]|[.login,.role_name]|@tsv'"),
	AA ("Hinzufügen / Rechte ändern", "Rolle: pull, triage, push, maintain, admin",
	    "gh api -X PUT repos/{owner}/{repo}/collaborators/{$user} -f permission={$perm}"),
	AAC ("Entfernen", "Zugriff entziehen", "gh api -X DELETE repos/{owner}/{repo}/collaborators/{$user}",
	     "Dieser Person wird der Zugriff auf das Repository entzogen. Fortfahren?"),
	AA ("Rechte der Person", "Aktuelle Berechtigung", "gh api repos/{owner}/{repo}/collaborators/{$user}/permission --jq .permission"),
	SEC_ACTS_END
};

static const GAct acts_pr[] = {
	AA ("Details", "gh pr view", "gh pr view {number}"),
	AA ("Erstellen", "Pull Request für den aktuellen Branch", "gh pr create --title {title} --body {$body?} {opt:--base:base} {opt:--head:head}"),
	AA ("Bearbeiten", "Titel/Beschreibung/Ziel ändern", "gh pr edit {number} {opt:--title:title} {opt:--body:body} {opt:--base:base}"),
	AAC ("Schließen", "gh pr close", "gh pr close {number}", "Pull Request schließen?"),
	AA ("Wieder öffnen", "gh pr reopen", "gh pr reopen {number}"),
	AA ("Auschecken", "Branch des Pull Requests lokal holen", "gh pr checkout {number}"),
	SEC_ACTS_END
};
static const GAct acts_pr_merge[] = {
	AAC ("Merge-Commit", "Zusammenführen mit Merge-Commit", "gh pr merge {number} --merge", "Pull Request zusammenführen?"),
	AAC ("Squash", "Alle Commits zu einem zusammenfassen", "gh pr merge {number} --squash", "Pull Request per Squash zusammenführen?"),
	AAC ("Rebase", "Per Rebase zusammenführen", "gh pr merge {number} --rebase", "Pull Request per Rebase zusammenführen?"),
	SEC_ACTS_END
};
static const GAct acts_pr_review[] = {
	AA ("Änderungen ansehen", "gh pr diff", "gh pr diff {number}"),
	AA ("Geänderte Dateien", "Branch-Vergleich: Dateiliste", "gh pr view {number} --json files --jq '.files[].path'"),
	AA ("Reviews + Kommentare", "gh pr view --comments", "gh pr view {number} --comments"),
	AA ("Review-Status", "Entscheidung, Mergebarkeit, Konflikte", "gh pr view {number} --json reviewDecision,mergeable,mergeStateStatus,state"),
	AA ("Checks", "Ergebnis der automatischen Prüfungen", "gh pr checks {number}"),
	AA ("Kommentieren", "gh pr comment (Beschreibung als Text)", "gh pr comment {number} --body {body}"),
	AA ("Freigeben", "Review: genehmigen", "gh pr review {number} --approve {opt:--body:body}"),
	AA ("Änderungen anfordern", "Review: Änderungen nötig", "gh pr review {number} --request-changes --body {body}"),
	AA ("Review-Kommentar", "Review ohne Entscheidung", "gh pr review {number} --comment --body {body}"),
	SEC_ACTS_END
};

static const GAct acts_issue[] = {
	AA ("Anzeigen", "gh issue view --comments", "gh issue view {number} --comments"),
	AA ("Erstellen", "Neues Issue", "gh issue create --title {title} --body {$body?} {opt:--label:label} {opt:--assignee:assignee} {opt:--milestone:milestone}"),
	AA ("Bearbeiten", "Titel/Text/Label/Zuweisung/Meilenstein ändern",
	    "gh issue edit {number} {opt:--title:title} {opt:--body:body} {opt:--add-label:label} {opt:--add-assignee:assignee} {opt:--milestone:milestone}"),
	AA ("Schließen", "gh issue close", "gh issue close {number}"),
	AA ("Wieder öffnen", "gh issue reopen", "gh issue reopen {number}"),
	AA ("Kommentieren", "gh issue comment", "gh issue comment {number} --body {body}"),
	AA ("Status", "Dir zugewiesene bzw. erwähnte Issues", "gh issue status"),
	AA ("Suchen", "Volltextsuche (Feld „Suche“)", "gh issue list --state all --search {query}"),
	SEC_ACTS_END
};
static const GAct acts_issue_meta[] = {
	AA ("Labels anzeigen", "gh label list", "gh label list"),
	AA ("Label erstellen", "gh label create", "gh label create {label}"),
	AAC ("Label löschen", "gh label delete", "gh label delete {label} --yes", "Das Label wird aus dem Repository gelöscht. Fortfahren?"),
	AA ("Label entfernen (Issue)", "Nimmt das Label vom Issue", "gh issue edit {number} --remove-label {label}"),
	AA ("Mir zuweisen", "Weist das Issue dir zu", "gh issue edit {number} --add-assignee @me"),
	AA ("Zuweisung entfernen", "Nimmt die Person vom Issue", "gh issue edit {number} --remove-assignee {assignee}"),
	AA ("Meilensteine", "Alle Meilensteine", "gh api repos/{owner}/{repo}/milestones?state=all --jq '.[]|[.number,.state,.title,.open_issues]|@tsv'"),
	AA ("Meilenstein erstellen", "Titel im Feld „Meilenstein“", "gh api repos/{owner}/{repo}/milestones -f title={$milestone}"),
	SEC_ACTS_END
};

static const GAct acts_actions[] = {
	AA ("Workflow starten", "gh workflow run (Branch optional)", "gh workflow run {workflow} {opt:--ref:ref}"),
	AA ("Ausführungen anzeigen", "Letzte Läufe samt Status", "gh run list -L 20"),
	AA ("Details", "Status der Ausführung", "gh run view {run}"),
	AA ("Test-Ergebnisse", "Jobs der Ausführung", "gh run view {run} --json jobs --jq '.jobs[]|(.name+\": \"+(.conclusion//.status))'"),
	AA ("Fehlerprotokoll", "Nur fehlgeschlagene Schritte", "gh run view {run} --log-failed"),
	AA ("Vollständiges Protokoll", "gh run view --log", "gh run view {run} --log"),
	AAC ("Abbrechen", "gh run cancel", "gh run cancel {run}", "Die laufende Ausführung abbrechen?"),
	AA ("Erneut ausführen", "gh run rerun", "gh run rerun {run}"),
	AA ("Workflow-Verlauf", "Läufe des gewählten Workflows", "gh run list --workflow {workflow} -L 20"),
	AA ("Deployments", "Deployment-Status", "gh api repos/{owner}/{repo}/deployments --jq '.[]|[.id,.environment,.ref,.created_at]|@tsv'"),
	SEC_ACTS_END
};

static const GAct acts_release[] = {
	AA ("Details", "gh release view", "gh release view {tag}"),
	AA ("Erstellen", "Neues Release (Tag wird bei Bedarf angelegt); Dateien im Feld „Anhänge“",
	    "gh release create {tag} {opt:--title:title} {opt:--notes:notes} {chk:gen:--generate-notes} {chk:draft:--draft} {chk:pre:--prerelease} {file*?}"),
	AA ("Bearbeiten", "Titel/Notizen/Status ändern",
	    "gh release edit {tag} {opt:--title:title} {opt:--notes:notes} {chk:draft:--draft} {chk:pre:--prerelease}"),
	AAC ("Löschen", "gh release delete", "gh release delete {tag} --yes", "Das Release wird gelöscht (der Tag bleibt). Fortfahren?"),
	AA ("Dateien anhängen", "gh release upload", "gh release upload {tag} {file*} --clobber"),
	AA ("Herunterladen", "Alle Dateien des Releases in den aktuellen Ordner", "gh release download {tag} --clobber"),
	AA ("Release Notes erzeugen", "Vorschau der automatischen Notizen",
	    "gh api repos/{owner}/{repo}/releases/generate-notes -f tag_name={$tag} --jq .body"),
	SEC_ACTS_END
};

static const ListDef list_changes = {
	"changes", PARSE_PORCELAIN, "git status --porcelain=v1 -z", 2, { "Status", "Datei" }, { NULL, NULL }, TRUE, 110, FALSE
};
static const ListDef list_stash = {
	"stash", PARSE_TAB, "git stash list '--format=%gd%x09%gs'", 2, { "Stash", "Beschreibung" }, { "stash", NULL }, FALSE, 50, FALSE
};
static const ListDef list_log = {
	"log", PARSE_TAB, "git log -n 200 --date=short '--format=%h%x09%ad%x09%an%x09%s'", 4,
	{ "Commit", "Datum", "Autor", "Titel" }, { "commit", NULL, NULL, NULL }, FALSE, 190, FALSE
};
static const ListDef list_branches = {
	"branches", PARSE_TAB, "git branch -a '--format=%(HEAD)%09%(refname:short)%09%(upstream:short)%09%(subject)'", 4,
	{ "", "Branch", "Verfolgt", "Letzter Commit" }, { NULL, "branch", NULL, NULL }, FALSE, 150, FALSE
};
static const ListDef list_remotes = {
	"remotes", PARSE_REMOTES, "git remote -v", 2, { "Name", "Adresse" }, { "remote", "url" }, FALSE, 60, FALSE
};
static const ListDef list_tags = {
	"tags", PARSE_TAB, "git tag --sort=-creatordate '--format=%(refname:short)%09%(objecttype)%09%(creatordate:short)%09%(subject)'", 4,
	{ "Tag", "Art", "Datum", "Nachricht" }, { "tag", NULL, NULL, NULL }, FALSE, 130, FALSE
};
static const ListDef list_conflicts = {
	"conflicts", PARSE_TAB, "git diff --name-only --diff-filter=U", 1, { "Datei mit Konflikt" }, { "cfile" }, FALSE, 60, FALSE
};
static const ListDef list_ghrepos = {
	"ghrepos", PARSE_TAB, "gh repo list -L 50", 3, { "Repository", "Beschreibung", "Sichtbarkeit" }, { "repo", NULL, NULL }, FALSE, 110, TRUE
};
static const ListDef list_prs = {
	"prs", PARSE_TAB, "gh pr list -L 50 --state all --json number,state,author,title --jq '.[]|[.number,.state,.author.login,.title]|@tsv'", 4,
	{ "Nr.", "Status", "Autor", "Titel" }, { "number", NULL, NULL, "title" }, FALSE, 120, TRUE
};
static const ListDef list_issues = {
	"issues", PARSE_TAB, "gh issue list -L 50 --state all --json number,state,title,labels --jq '.[]|[.number,.state,.title,([.labels[].name]|join(\",\"))]|@tsv'", 4,
	{ "Nr.", "Status", "Titel", "Labels" }, { "number", NULL, "title", NULL }, FALSE, 120, TRUE
};
static const ListDef list_workflows = {
	"workflows", PARSE_TAB, "gh workflow list --all", 3, { "Workflow", "Status", "ID" }, { NULL, NULL, "workflow" }, FALSE, 60, TRUE
};
static const ListDef list_runs = {
	"runs", PARSE_TAB, "gh run list -L 20 --json databaseId,status,conclusion,workflowName,displayTitle --jq '.[]|[.databaseId,.status,(.conclusion//\"-\"),.workflowName,.displayTitle]|@tsv'", 5,
	{ "Lauf", "Status", "Ergebnis", "Workflow", "Titel" }, { "run", NULL, NULL, NULL, NULL }, FALSE, 100, TRUE
};
static const ListDef list_releases = {
	"releases", PARSE_TAB, "gh release list -L 30", 4, { "Titel", "Art", "Tag", "Datum" }, { NULL, NULL, "tag", NULL }, FALSE, 100, TRUE
};

/* ------------------------------------------------------------------ */
/* Seitenaufbau                                                         */
/* ------------------------------------------------------------------ */

static void
on_gh_status_done (gpointer user, gboolean ok, GString *out)
{
	GitPage *p = user;
	gchar **lines = g_strsplit (out->str, "\n", -1);
	gchar *text = NULL;
	gint i;

	for (i = 0; lines[i] != NULL; i++) {
		if (strstr (lines[i], "Logged in to") != NULL) {
			text = g_strstrip (g_strdup (lines[i]));
			break;
		}
	}
	if (text == NULL) {
		text = g_strdup (ok ? out->str : _("Nicht bei GitHub angemeldet – im Terminal „gh auth login“ ausführen."));
	}
	gtk_label_set_text (GTK_LABEL (p->gh_status), text);
	g_free (text);
	g_strfreev (lines);
}

static void
on_gh_map (GtkWidget *w, gpointer user_data)
{
	GitPage *p = user_data;

	quiet_run (p, g_get_home_dir (), on_gh_status_done, p, "gh", "auth", "status", NULL);
}

static void
on_log_clear (GtkButton *b, gpointer user_data)
{
	GitPage *p = user_data;

	gtk_text_buffer_set_text (p->log, "", -1);
	g_string_truncate (p->partial, 0);
	{
		GtkTextIter e;

		gtk_text_buffer_get_end_iter (p->log, &e);
		gtk_text_buffer_move_mark (p->log, p->line_mark, &e);
	}
}

static void
on_log_copy (GtkButton *b, gpointer user_data)
{
	GitPage *p = user_data;
	GtkTextIter s, e;
	gchar *t;

	gtk_text_buffer_get_bounds (p->log, &s, &e);
	t = gtk_text_buffer_get_text (p->log, &s, &e, FALSE);
	gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), t, -1);
	g_free (t);
}

static void
on_root_map (GtkWidget *w, gpointer user_data)
{
	refresh_status (user_data);
}

static void
page_free (GtkWidget *w, gpointer user_data)
{
	GitPage *p = user_data;

	if (p->pulse_id != 0) {
		g_source_remove (p->pulse_id);
	}
	g_hash_table_destroy (p->fields);
	g_hash_table_destroy (p->labels);
	g_hash_table_destroy (p->checks);
	g_hash_table_destroy (p->tab_pages);
	g_list_free_full (p->lists, g_free);
	g_string_free (p->partial, TRUE);
	g_free (p);
}

static void
fields_free (gpointer v)
{
	g_ptr_array_free (v, TRUE);
}

/* Kompakter Aufbau: häufig Gebrauchtes steht offen, der Rest ist in
 * einklappbaren Gruppen. Beschriftungen der Felder stehen links daneben. */

/* Schnellstart: der übliche Ablauf in wenigen Zeilen - vormerken,
 * speichern, mit dem Server abgleichen. */
static const GAct acts_q_stage[] = {
	A ("Status", "Zeigt, welche Dateien geändert, neu oder gelöscht sind", "git status"),
	A ("Alle hinzufügen", "Merkt alle Änderungen für den nächsten Commit vor", "git add -A"),
	A ("Auswahl hinzufügen", "Nur die im Hauptfenster markierten Dateien vormerken", "git add -- {files}"),
	A ("Unterschiede", "Zeigt die noch nicht gespeicherten Änderungen im Detail", "git diff --no-color"),
	SEC_ACTS_END
};
static const GAct acts_q_commit[] = {
	A ("Commit", "Speichert alle Änderungen als neuen Stand (merkt selbst vor, falls nötig)",
	   "git add -A && git commit -m {msg}"),
	AP ("Commit + Push", "Speichert und lädt sofort zum Server hoch",
	    "git add -A && git commit -m {msg} && git push --progress"),
	SEC_ACTS_END
};
static const GAct acts_q_server[] = {
	{ "Synchronisieren", "Holt Neues vom Server und lädt deine Änderungen hoch", "git pull --no-rebase --progress && git push --progress", NULL, GA_PUSH, NULL },
	A ("Abgleichen", "Zeigt, was lokal und auf dem Server unterschiedlich ist, ohne etwas zu ändern",
	   "git fetch --progress && git log --oneline --left-right --cherry-pick @{u}...HEAD"),
	A ("Pull", "Holt Änderungen vom Server", "git pull --no-rebase --progress"),
	AP ("Push", "Sendet deine gespeicherten Änderungen zum Server", "git push --progress"),
	A ("Verlauf", "Zeigt die bisherigen Commits", "git log --oneline --graph --decorate -n 30"),
	SEC_ACTS_END
};
static const GAct acts_q_setup[] = {
	A ("Server eintragen", "Trägt die Adresse eines Servers ein (Remote)", "git remote add {remote} {url}"),
	AA ("Repository klonen", "Lädt ein Repository vom Server in den aktuellen Ordner", "git clone --progress {url}"),
	AA ("Repository anlegen", "Legt hier ein neues Git-Repository an", "git init"),
	SEC_ACTS_END
};

static void
build_tab_quick (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "quick", _("Schnell"));
	GtkWidget *g;

	g = group_begin (b, _("1. Änderungen ansehen und vormerken"), TRUE);
	add_actions (p, g, NULL, acts_q_stage);

	g = group_begin (b, _("2. Speichern (Commit)"), TRUE);
	add_field (p, g, "msg", _("Nachricht"), _("Kurze Beschreibung der Änderungen …"), NULL);
	add_actions (p, g, NULL, acts_q_commit);

	g = group_begin (b, _("3. Mit dem Server (z. B. GitHub)"), TRUE);
	add_actions (p, g, NULL, acts_q_server);

	g = group_begin (b, _("Server eintragen, klonen, neues Repository"), FALSE);
	add_field (p, g, "remote", _("Name"), "origin", "origin");
	add_field (p, g, "url", _("Adresse"), "https://github.com/name/projekt.git", NULL);
	add_actions (p, g, NULL, acts_q_setup);
}

static void
build_tab_changes (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "changes", _("Änderungen"));
	GtkWidget *g;

	add_list (p, b, &list_changes);

	g = group_begin (b, _("Vormerken und ansehen"), TRUE);
	add_actions (p, g, NULL, acts_stage);
	add_actions (p, g, NULL, acts_view);

	g = group_begin (b, _("Commit"), TRUE);
	add_field (p, g, "msg", _("Nachricht"), _("Kurze Beschreibung der Änderungen …"), NULL);
	add_actions (p, g, NULL, acts_commit);

	g = group_begin (b, _("Dateien verschieben, löschen, verwerfen"), FALSE);
	add_field (p, g, "pathto", _("Neuer Pfad"), "z. B. neuer-name.txt", NULL);
	add_actions (p, g, NULL, acts_files);

	g = group_begin (b, _("Zurücksetzen (Reset)"), FALSE);
	add_field (p, g, "target", _("Ziel"), "HEAD~1 oder Commit-Hash", NULL);
	add_actions (p, g, NULL, acts_reset);

	g = group_begin (b, _("Zwischenspeicher (Stash)"), FALSE);
	add_list (p, g, &list_stash);
	add_actions (p, g, NULL, acts_stash);

	g = group_begin (b, _("Nicht versionierte Dateien bereinigen"), FALSE);
	add_actions (p, g, NULL, acts_clean);

	g = group_begin (b, _("Repository anlegen oder klonen"), FALSE);
	add_field (p, g, "url", _("Adresse"), "https://github.com/name/projekt.git", NULL);
	add_actions (p, g, NULL, acts_repo);
}

static void
build_tab_history (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "history", _("Verlauf"));
	GtkWidget *g;

	add_list (p, b, &list_log);
	g = group_begin (b, _("Gewählter Commit"), TRUE);
	add_field (p, g, "commit", _("Commit"), _("aus der Liste wählen"), NULL);
	add_actions (p, g, NULL, acts_hist);
}

static void
build_tab_branches (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "branches", _("Branches"));
	GtkWidget *g;

	add_list (p, b, &list_branches);

	g = group_begin (b, _("Branch verwalten"), TRUE);
	add_field (p, g, "branch", _("Branch"), "aus der Liste wählen", NULL);
	add_field (p, g, "newname", _("Neuer Name"), NULL, NULL);
	add_actions (p, g, NULL, acts_branch);

	g = group_begin (b, _("Zusammenführen und neu aufbauen"), FALSE);
	add_actions (p, g, NULL, acts_merge);

	g = group_begin (b, _("Branches vergleichen"), FALSE);
	add_field (p, g, "target", _("Zweiter Branch"), "main", NULL);
	add_actions (p, g, NULL, acts_compare);

	g = group_begin (b, _("Standard-Branch festlegen"), FALSE);
	add_actions (p, g, NULL, acts_default);
}

static void
build_tab_server (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "server", _("Server"));
	GtkWidget *g;

	g = group_begin (b, _("Push (hochladen)"), TRUE);
	add_field (p, g, "remote", _("Remote"), "origin", "origin");
	add_field (p, g, "branch", _("Branch"), "main", NULL);
	add_actions (p, g, NULL, acts_push);

	g = group_begin (b, _("Pull und Abrufen"), TRUE);
	add_actions (p, g, NULL, acts_pull);
	add_actions (p, g, NULL, acts_fetch);

	g = group_begin (b, _("Vergleichen und prüfen"), FALSE);
	add_actions (p, g, NULL, acts_sync);

	g = group_begin (b, _("Verbindungen (Remotes)"), FALSE);
	add_list (p, g, &list_remotes);
	add_field (p, g, "url", _("Adresse"), "https://github.com/name/projekt.git", NULL);
	add_field (p, g, "newname", _("Neuer Name"), NULL, NULL);
	add_actions (p, g, NULL, acts_remote);

	g = group_begin (b, _("Tag und atomarer Push"), FALSE);
	add_field (p, g, "tag", _("Tag"), "v1.0.0", NULL);
	add_field (p, g, "refs", _("Refs"), "main v1.0.0 (Leerzeichen getrennt)", NULL);
}

static void
build_tab_tags (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "tags", _("Tags"));
	GtkWidget *g;

	add_list (p, b, &list_tags);

	g = group_begin (b, _("Tag erstellen und verwalten"), TRUE);
	add_field (p, g, "tag", _("Tag / Version"), "v1.0.0", NULL);
	add_field (p, g, "tagmsg", _("Nachricht"), "für annotierten Tag", NULL);
	add_field (p, g, "commit", _("Commit"), "leer = aktueller Stand", NULL);
	add_actions (p, g, NULL, acts_tag_ver);
	add_actions (p, g, NULL, acts_tag);

	g = group_begin (b, _("Mit GitHub"), FALSE);
	add_field (p, g, "remote", _("Remote"), "origin", "origin");
	add_actions (p, g, NULL, acts_tag_srv);
}

static void
build_tab_conflicts (GitPage *p, GtkWidget *nb)
{
	GtkWidget *b = tab_box (p, nb, "conflicts", _("Konflikte"));
	GtkWidget *g, *h, *sc1, *sc2, *sc3, *v1, *v2, *col1, *col2, *l1, *l2, *l3;
	GtkTextBuffer *rb;
	GPtrArray *arr;

	add_list (p, b, &list_conflicts);
	add_field (p, b, "cfile", _("Datei"), _("aus der Liste wählen"), NULL);
	arr = g_hash_table_lookup (p->fields, "cfile");
	g_signal_connect (g_ptr_array_index (arr, 0), "changed", G_CALLBACK (on_cfile_changed), p);

	l3 = gtk_label_new (_("Blau = lokal, Orange = Remote. Cursor in einen Bereich setzen und „Bereich: …“ wählen, oder von Hand bearbeiten."));
	gtk_label_set_line_wrap (GTK_LABEL (l3), TRUE);
	gtk_label_set_xalign (GTK_LABEL (l3), 0.0);
	gtk_style_context_add_class (gtk_widget_get_style_context (l3), "dim-label");
	gtk_box_pack_start (GTK_BOX (b), l3, FALSE, FALSE, 0);

	p->conf_result = gtk_text_view_new ();
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (p->conf_result), TRUE);
	rb = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->conf_result));
	gtk_text_buffer_create_tag (rb, "ours", "background-rgba", &(GdkRGBA){ 0.30, 0.55, 1.0, 0.25 }, NULL);
	gtk_text_buffer_create_tag (rb, "theirs", "background-rgba", &(GdkRGBA){ 1.0, 0.65, 0.2, 0.25 }, NULL);
	gtk_text_buffer_create_tag (rb, "mark", "weight", PANGO_WEIGHT_BOLD,
				    "background-rgba", &(GdkRGBA){ 0.6, 0.6, 0.6, 0.35 }, NULL);
	g_signal_connect_swapped (rb, "changed", G_CALLBACK (conflict_colorize), rb);
	sc3 = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc3), GTK_SHADOW_IN);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc3), 150);
	gtk_container_add (GTK_CONTAINER (sc3), p->conf_result);
	gtk_box_pack_start (GTK_BOX (b), sc3, FALSE, FALSE, 0);
	add_actions (p, b, NULL, acts_conf);

	g = group_begin (b, _("Lokal und Remote nebeneinander"), FALSE);
	h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	col1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	col2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	l1 = gtk_label_new (_("Lokal (deine Version)"));
	l2 = gtk_label_new (_("Remote (vom Server)"));
	gtk_label_set_xalign (GTK_LABEL (l1), 0.0);
	gtk_label_set_xalign (GTK_LABEL (l2), 0.0);
	v1 = gtk_text_view_new ();
	v2 = gtk_text_view_new ();
	p->conf_ours = v1;
	p->conf_theirs = v2;
	gtk_text_view_set_editable (GTK_TEXT_VIEW (v1), FALSE);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (v2), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (v1), TRUE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (v2), TRUE);
	sc1 = gtk_scrolled_window_new (NULL, NULL);
	sc2 = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc1), GTK_SHADOW_IN);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc2), GTK_SHADOW_IN);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc1), 110);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc2), 110);
	gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (sc1), 140);
	gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (sc2), 140);
	gtk_container_add (GTK_CONTAINER (sc1), v1);
	gtk_container_add (GTK_CONTAINER (sc2), v2);
	gtk_box_pack_start (GTK_BOX (col1), l1, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (col1), sc1, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (col2), l2, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (col2), sc2, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (h), col1, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (h), col2, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (g), h, FALSE, FALSE, 0);

	g = group_begin (b, _("Ganze Datei übernehmen, Merge abschließen"), FALSE);
	add_actions (p, g, NULL, acts_conf_file);
}

static void
build_tab_github (GitPage *p, GtkWidget *nb)
{
	GtkWidget *outer = tab_box (p, nb, "github", _("GitHub"));
	GtkWidget *inner = gtk_notebook_new ();
	GtkWidget *b, *g;

	p->gh_status = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (p->gh_status), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (p->gh_status), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class (gtk_widget_get_style_context (p->gh_status), "dim-label");
	gtk_box_pack_start (GTK_BOX (outer), p->gh_status, FALSE, FALSE, 0);
	gtk_notebook_set_scrollable (GTK_NOTEBOOK (inner), TRUE);
	gtk_box_pack_start (GTK_BOX (outer), inner, TRUE, TRUE, 0);
	p->gh_notebook = inner;
	g_signal_connect (outer, "map", G_CALLBACK (on_gh_map), p);

	/* Repository */
	b = tab_box (p, inner, "gh-repo", _("Repository"));
	add_list (p, b, &list_ghrepos);
	g = group_begin (b, _("Anlegen und verwalten"), TRUE);
	add_field (p, g, "repo", _("Repository"), "Besitzer/Name", NULL);
	add_field (p, g, "desc", _("Beschreibung"), NULL, NULL);
	add_field (p, g, "query", _("Suche"), NULL, NULL);
	add_actions (p, g, NULL, acts_gh_repo);
	add_actions (p, g, NULL, acts_gh_repo2);
	g = group_begin (b, _("Dateien im Repository"), FALSE);
	add_field (p, g, "path", _("Pfad"), "README.md", NULL);
	add_field (p, g, "rfmsg", _("Commit-Text"), NULL, NULL);
	add_text (p, g, "filebody", _("Dateiinhalt (zum Bearbeiten)"));
	add_actions (p, g, NULL, acts_gh_files);
	g = group_begin (b, _("Collaborators und Rechte"), FALSE);
	add_field (p, g, "user", _("Benutzer"), NULL, NULL);
	add_field (p, g, "perm", _("Berechtigung"), "pull | triage | push | maintain | admin", "push");
	add_actions (p, g, NULL, acts_gh_collab);

	/* Pull Requests */
	b = tab_box (p, inner, "gh-pr", _("Pull Requests"));
	add_list (p, b, &list_prs);
	g = group_begin (b, _("Pull Request"), TRUE);
	add_field (p, g, "number", _("Nummer"), NULL, NULL);
	add_field (p, g, "title", _("Titel"), NULL, NULL);
	add_text (p, g, "body", _("Beschreibung / Kommentar"));
	add_field (p, g, "base", _("Ziel (base)"), "main", NULL);
	add_field (p, g, "head", _("Quelle (head)"), "leer = aktueller Branch", NULL);
	add_actions (p, g, NULL, acts_pr);
	g = group_begin (b, _("Zusammenführen"), FALSE);
	add_actions (p, g, NULL, acts_pr_merge);
	g = group_begin (b, _("Überprüfen und kommentieren"), FALSE);
	add_actions (p, g, NULL, acts_pr_review);

	/* Issues */
	b = tab_box (p, inner, "gh-issues", _("Issues"));
	add_list (p, b, &list_issues);
	g = group_begin (b, _("Issue"), TRUE);
	add_field (p, g, "number", _("Nummer"), NULL, NULL);
	add_field (p, g, "title", _("Titel"), NULL, NULL);
	add_text (p, g, "body", _("Beschreibung / Kommentar"));
	add_field (p, g, "query", _("Suche"), NULL, NULL);
	add_actions (p, g, NULL, acts_issue);
	g = group_begin (b, _("Labels, Zuweisung, Meilensteine"), FALSE);
	add_field (p, g, "label", _("Label"), "bug", NULL);
	add_field (p, g, "assignee", _("Zugewiesen an"), "Benutzername", NULL);
	add_field (p, g, "milestone", _("Meilenstein"), "Titel", NULL);
	add_actions (p, g, NULL, acts_issue_meta);

	/* Actions */
	b = tab_box (p, inner, "gh-actions", _("Actions"));
	add_list (p, b, &list_runs);
	g = group_begin (b, _("Workflows und Ausführungen"), TRUE);
	add_field (p, g, "run", _("Ausführung"), "Nummer aus der Liste", NULL);
	add_field (p, g, "workflow", _("Workflow"), "Name oder ID", NULL);
	add_field (p, g, "ref", _("Branch/Tag"), "optional", NULL);
	add_actions (p, g, NULL, acts_actions);
	g = group_begin (b, _("Alle Workflows"), FALSE);
	add_list (p, g, &list_workflows);

	/* Releases */
	b = tab_box (p, inner, "gh-releases", _("Releases"));
	add_list (p, b, &list_releases);
	g = group_begin (b, _("Release"), TRUE);
	add_field (p, g, "tag", _("Tag / Version"), "v1.0.0", NULL);
	add_field (p, g, "title", _("Titel"), NULL, NULL);
	add_text (p, g, "notes", _("Release Notes"));
	add_field_full (p, g, "file", _("Anhänge"), "Dateipfade, Leerzeichen getrennt", FALSE, NULL, TRUE);
	add_check (p, g, "gen", _("Notes automatisch erzeugen"), FALSE);
	add_check (p, g, "draft", _("Entwurf"), FALSE);
	add_check (p, g, "pre", _("Vorabversion"), FALSE);
	add_actions (p, g, NULL, acts_tag_ver);
	add_actions (p, g, NULL, acts_release);
}

/* Werkzeugleiste: die drei häufigsten Server-Aktionen immer sichtbar. */
static const GAct acts_toolbar[] = {
	A ("↓ Fetch", "Neues vom Server holen, nichts ändern", "git fetch --all --prune --progress"),
	A ("⇣ Pull", "Holen und zusammenführen", "git pull --no-rebase --progress"),
	AP ("⇡ Push", "Aktuellen Branch hochladen (Verknüpfung wird bei Bedarf gesetzt)", "git push --progress"),
	A ("Status", "Vollständiger Git-Status", "git status"),
	SEC_ACTS_END
};

static const gchar *git_page_css =
	".nolphin-git-page button { padding: 1px 8px; min-height: 22px; min-width: 0; }"
	".nolphin-git-page entry { padding: 1px 6px; min-height: 22px; }"
	".nolphin-git-page treeview { font-size: 95%; }"
	".nolphin-git-page notebook > header > tabs > tab { padding: 3px 8px; min-height: 0; }"
	".nolphin-git-page expander title { padding: 2px 0; }";

GtkWidget *
nolphin_git_page_new (NolphinWindow *window, GtkWidget *quick_tab, GtkWidget *back_button)
{
	GitPage *p = g_new0 (GitPage, 1);
	GtkWidget *root, *header, *refresh, *paned, *logbox, *bar, *clear, *copy, *sc;
	GtkTextIter end;

	p->window = window;
	p->fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, fields_free);
	p->labels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	p->checks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	p->tab_pages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	p->partial = g_string_new (NULL);

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	p->root = root;
	g_object_set_data (G_OBJECT (root), "nolphin-git-page", p);
	g_signal_connect (root, "destroy", G_CALLBACK (page_free), p);
	g_signal_connect (root, "map", G_CALLBACK (on_root_map), p);

	if (back_button != NULL) {
		gtk_widget_set_margin_start (back_button, 8);
		gtk_widget_set_margin_top (back_button, 8);
		gtk_box_pack_start (GTK_BOX (root), back_button, FALSE, FALSE, 0);
	}

	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start (header, 10);
	gtk_widget_set_margin_end (header, 10);
	p->status_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (p->status_label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (p->status_label), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start (GTK_BOX (header), p->status_label, TRUE, TRUE, 0);
	refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text (refresh, _("Status und Listen aktualisieren"));
	g_signal_connect (refresh, "clicked", G_CALLBACK (on_header_refresh_clicked), p);
	gtk_box_pack_start (GTK_BOX (header), refresh, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (root), header, FALSE, FALSE, 0);

	{
		GtkWidget *tb = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		GtkCssProvider *css = gtk_css_provider_new ();

		gtk_widget_set_margin_start (tb, 8);
		gtk_widget_set_margin_end (tb, 8);
		add_actions (p, tb, NULL, acts_toolbar);
		gtk_box_pack_start (GTK_BOX (root), tb, FALSE, FALSE, 0);

		gtk_style_context_add_class (gtk_widget_get_style_context (root), "nolphin-git-page");
		gtk_css_provider_load_from_data (css, git_page_css, -1, NULL);
		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (css),
							   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (css);
	}

	/* Ausgabefeld (vor den Reitern anlegen: Schaltflächen schreiben hinein) */
	p->log_view = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (p->log_view), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (p->log_view), TRUE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (p->log_view), GTK_WRAP_WORD_CHAR);
	p->log = gtk_text_view_get_buffer (GTK_TEXT_VIEW (p->log_view));
	gtk_text_buffer_get_end_iter (p->log, &end);
	p->line_mark = gtk_text_buffer_create_mark (p->log, NULL, &end, TRUE);
	gtk_text_buffer_create_tag (p->log, "cmd", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag (p->log, "ok", "foreground", "#3fa65b", NULL);
	gtk_text_buffer_create_tag (p->log, "err", "foreground", "#e5534b", NULL);
	gtk_text_buffer_create_tag (p->log, "hint", "foreground", "#d29922", "style", PANGO_STYLE_ITALIC, NULL);
	gtk_text_buffer_create_tag (p->log, "add", "foreground", "#3fa65b", NULL);
	gtk_text_buffer_create_tag (p->log, "del", "foreground", "#e5534b", NULL);
	gtk_text_buffer_create_tag (p->log, "hunk", "foreground", "#58a6ff", NULL);

	p->notebook = gtk_notebook_new ();
	gtk_notebook_set_scrollable (GTK_NOTEBOOK (p->notebook), TRUE);
	if (quick_tab != NULL) {
		gtk_notebook_append_page (GTK_NOTEBOOK (p->notebook), quick_tab, gtk_label_new (_("Schnell")));
		g_hash_table_insert (p->tab_pages, g_strdup ("quick"), GINT_TO_POINTER (1));
	}
	build_tab_quick (p, p->notebook);
	build_tab_changes (p, p->notebook);
	build_tab_history (p, p->notebook);
	build_tab_branches (p, p->notebook);
	build_tab_server (p, p->notebook);
	build_tab_tags (p, p->notebook);
	build_tab_conflicts (p, p->notebook);
	build_tab_github (p, p->notebook);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (p->notebook), 0);

	logbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start (bar, 10);
	gtk_widget_set_margin_end (bar, 10);
	p->progress = gtk_progress_bar_new ();
	gtk_widget_set_valign (p->progress, GTK_ALIGN_CENTER);
	gtk_box_pack_start (GTK_BOX (bar), p->progress, TRUE, TRUE, 0);
	p->cancel_btn = gtk_button_new_with_label (_("Abbrechen"));
	gtk_widget_set_sensitive (p->cancel_btn, FALSE);
	g_signal_connect (p->cancel_btn, "clicked", G_CALLBACK (on_cancel_clicked), p);
	gtk_box_pack_end (GTK_BOX (bar), p->cancel_btn, FALSE, FALSE, 0);
	copy = gtk_button_new_with_label (_("Kopieren"));
	g_signal_connect (copy, "clicked", G_CALLBACK (on_log_copy), p);
	gtk_box_pack_end (GTK_BOX (bar), copy, FALSE, FALSE, 0);
	clear = gtk_button_new_with_label (_("Leeren"));
	g_signal_connect (clear, "clicked", G_CALLBACK (on_log_clear), p);
	gtk_box_pack_end (GTK_BOX (bar), clear, FALSE, FALSE, 0);
	p->auto_chk = gtk_check_button_new_with_label (_("Nach Push Build-Status zeigen"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (p->auto_chk), TRUE);
	gtk_widget_set_tooltip_text (p->auto_chk, _("Zeigt nach einem erfolgreichen Push die letzten GitHub-Actions-Läufe"));
	gtk_box_pack_end (GTK_BOX (bar), p->auto_chk, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (logbox), bar, FALSE, FALSE, 0);

	sc = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sc), GTK_SHADOW_IN);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sc), 90);
	gtk_container_add (GTK_CONTAINER (sc), p->log_view);
	gtk_widget_set_margin_start (sc, 10);
	gtk_widget_set_margin_end (sc, 10);
	gtk_widget_set_margin_bottom (sc, 8);
	gtk_box_pack_start (GTK_BOX (logbox), sc, TRUE, TRUE, 0);

	paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1 (GTK_PANED (paned), p->notebook, TRUE, FALSE);
	gtk_paned_pack2 (GTK_PANED (paned), logbox, FALSE, TRUE);
	gtk_box_pack_start (GTK_BOX (root), paned, TRUE, TRUE, 0);

	gtk_widget_show_all (root);
	gtk_widget_hide (p->progress);
	return root;
}

void
nolphin_git_page_show_tab (GtkWidget *git_page, const gchar *tab_id)
{
	GitPage *p = g_object_get_data (G_OBJECT (git_page), "nolphin-git-page");

	if (p != NULL && tab_id != NULL) {
		goto_tab (p, tab_id);
	}
}

void
nolphin_git_page_log (GtkWidget *git_page, const gchar *text)
{
	GitPage *p = g_object_get_data (G_OBJECT (git_page), "nolphin-git-page");

	if (p != NULL && text != NULL) {
		log_line (p, NULL, "%s", text);
	}
}
