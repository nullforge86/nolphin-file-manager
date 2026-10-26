/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-archive-manage.c: Archivverwaltung
 */

#include <config.h>

#include "nolphin-archive-manage.h"
#include "nolphin-tool-progress.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

typedef enum { CLASS_NONE, CLASS_ZIP, CLASS_7Z, CLASS_TAR } FormatClass;

typedef enum { OP_LIST, OP_ADD, OP_REMOVE, OP_EXTRACT } Operation;

typedef struct {
	gchar *rel_path;   /* Zielpfad im Archiv */
	GFile *source;
} StageItem;

typedef struct {
	Operation op;
	NolphinArchiveFormat format;
	gchar *archive_path;
	GList *stage_items;   /* StageItem*  (OP_ADD) */
	GList *names;         /* gchar*      (OP_REMOVE, OP_EXTRACT) */
	gchar *destination;   /* OP_EXTRACT */
} Job;

static void
stage_item_free (StageItem *item)
{
	g_free (item->rel_path);
	g_object_unref (item->source);
	g_free (item);
}

static void
job_free (Job *job)
{
	g_free (job->archive_path);
	g_list_free_full (job->stage_items, (GDestroyNotify) stage_item_free);
	g_list_free_full (job->names, g_free);
	g_free (job->destination);
	g_free (job);
}

void
nolphin_archive_entry_free (NolphinArchiveEntry *entry)
{
	if (entry == NULL) {
		return;
	}
	g_free (entry->path);
	g_free (entry->modified);
	g_free (entry);
}

static FormatClass
format_class (NolphinArchiveFormat f)
{
	switch (f) {
	case NOLPHIN_ARCHIVE_FORMAT_ZIP:
	case NOLPHIN_ARCHIVE_FORMAT_JAR:
	case NOLPHIN_ARCHIVE_FORMAT_WAR:
	case NOLPHIN_ARCHIVE_FORMAT_EAR:
	case NOLPHIN_ARCHIVE_FORMAT_EPUB:
	case NOLPHIN_ARCHIVE_FORMAT_CBZ:
	case NOLPHIN_ARCHIVE_FORMAT_CRX:
		return CLASS_ZIP;
	case NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP:
		return CLASS_7Z;
	case NOLPHIN_ARCHIVE_FORMAT_TAR:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_GZ:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_XZ:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_ZST:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZO:
	case NOLPHIN_ARCHIVE_FORMAT_TAR_Z:
		return CLASS_TAR;
	default:
		return CLASS_NONE;
	}
}

static gchar *
find_7z (void)
{
	const gchar *names[] = { "7z", "7zz", "7za", "7zr", NULL };
	guint i;

	for (i = 0; names[i] != NULL; i++) {
		gchar *p = g_find_program_in_path (names[i]);

		if (p != NULL) {
			return p;
		}
	}
	return NULL;
}

gboolean
nolphin_archive_manage_can_list (NolphinArchiveFormat format)
{
	FormatClass c = format_class (format);

	if (c == CLASS_TAR) {
		return TRUE;
	}
	if (format == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN || nolphin_archive_format_is_single_file (format)) {
		return FALSE;
	}
	return TRUE;
}

gboolean
nolphin_archive_manage_can_modify (NolphinArchiveFormat format)
{
	return format_class (format) != CLASS_NONE;
}

gboolean
nolphin_archive_manage_needs_repack (NolphinArchiveFormat format)
{
	return format_class (format) == CLASS_TAR;
}

/* --- Hilfen im Worker-Thread --------------------------------------------- */

static GQuark
manage_error_quark (void)
{
	return NOLPHIN_ARCHIVE_ERROR;
}

static gboolean
run_sync (const gchar * const *argv, const gchar *cwd, gchar **out_stdout, GError **error)
{
	GSubprocessLauncher *launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
								   G_SUBPROCESS_FLAGS_STDERR_PIPE |
								   G_SUBPROCESS_FLAGS_STDIN_PIPE);
	GSubprocess *proc;
	GBytes *out = NULL, *err = NULL;
	gboolean ok;

	if (cwd != NULL) {
		g_subprocess_launcher_set_cwd (launcher, cwd);
	}
	proc = g_subprocess_launcher_spawnv (launcher, argv, error);
	g_object_unref (launcher);
	if (proc == NULL) {
		return FALSE;
	}

	{
		/* Leere Eingabe: schließt stdin sofort, damit kein Werkzeug auf ein Passwort wartet. */
		GBytes *empty = g_bytes_new_static ("", 0);
		NolphinToolProgress *progress = nolphin_tool_progress_get_thread ();
		gboolean done;

		/* Anhalten/Abbrechen im Fenster „Dateivorgänge“ wirkt auf diesen Prozess. */
		nolphin_tool_progress_set_subprocess (progress, proc);
		done = g_subprocess_communicate (proc, empty, NULL, &out, &err, error);
		nolphin_tool_progress_set_subprocess (progress, NULL);

		g_bytes_unref (empty);
		if (!done) {
			g_object_unref (proc);
			return FALSE;
		}
		if (nolphin_tool_progress_is_cancelled (progress)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, _("Vom Benutzer abgebrochen."));
			g_clear_pointer (&out, g_bytes_unref);
			g_clear_pointer (&err, g_bytes_unref);
			g_object_unref (proc);
			return FALSE;
		}
	}

	ok = g_subprocess_get_successful (proc);
	if (!ok) {
		gsize len = 0;
		const gchar *e = err != NULL ? g_bytes_get_data (err, &len) : NULL;
		const gchar *o = out != NULL ? g_bytes_get_data (out, &len) : NULL;
		gchar *msg = g_utf8_make_valid ((e != NULL && e[0] != '\0') ? e : (o != NULL ? o : ""), -1);

		g_strstrip (msg);
		g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED, "%s",
			     msg[0] != '\0' ? msg : _("Das Archivierungswerkzeug wurde mit einem Fehler beendet."));
		g_free (msg);
	} else if (out_stdout != NULL) {
		gsize len = 0;
		const gchar *data = out != NULL ? g_bytes_get_data (out, &len) : NULL;

		*out_stdout = g_utf8_make_valid (data != NULL ? data : "", len);
	}

	g_clear_pointer (&out, g_bytes_unref);
	g_clear_pointer (&err, g_bytes_unref);
	g_object_unref (proc);
	return ok;
}

static gboolean
need_tool (const gchar *tool, GError **error)
{
	gchar *p = g_find_program_in_path (tool);

	if (p != NULL) {
		g_free (p);
		return TRUE;
	}
	g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
		     _("Das benötigte Werkzeug »%s« ist nicht installiert."), tool);
	return FALSE;
}

/* Pfade aus dem Archiv dürfen nie aus dem Zielordner ausbrechen. */
static gboolean
entry_path_is_safe (const gchar *path)
{
	gchar **parts;
	guint i;
	gboolean safe = TRUE;

	if (path == NULL || path[0] == '\0' || path[0] == '/' || path[0] == '-') {
		return FALSE;
	}
	parts = g_strsplit (path, "/", -1);
	for (i = 0; parts[i] != NULL; i++) {
		if (g_strcmp0 (parts[i], "..") == 0) {
			safe = FALSE;
		}
	}
	g_strfreev (parts);
	return safe;
}

/* Wildcard-Zeichen für zip/unzip maskieren, damit Namen wörtlich gelten. */
static gchar *
escape_glob (const gchar *name)
{
	GString *s = g_string_new (NULL);
	const gchar *p;

	for (p = name; *p != '\0'; p++) {
		if (strchr ("*?[]\\", *p) != NULL) {
			g_string_append_c (s, '\\');
		}
		g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

static gchar *
strip_trailing_slashes (const gchar *name)
{
	gchar *copy = g_strdup (name);
	gsize len = strlen (copy);

	while (len > 1 && copy[len - 1] == '/') {
		copy[--len] = '\0';
	}
	return copy;
}

/* --- Auflisten ------------------------------------------------------------ */

static NolphinArchiveEntry *
entry_new (const gchar *path, goffset size, gboolean is_dir, const gchar *modified)
{
	NolphinArchiveEntry *e = g_new0 (NolphinArchiveEntry, 1);
	gchar *clean = strip_trailing_slashes (path);

	if (g_str_has_prefix (clean, "./") && clean[2] != '\0') {
		e->path = g_strdup (clean + 2);
	} else {
		e->path = g_strdup (clean);
	}
	g_free (clean);
	e->size = size;
	e->is_dir = is_dir;
	e->modified = g_strdup (modified);
	return e;
}

/* tar -tvf: "-rw-r--r-- user/group  1234 2024-01-01 12:00 pfad/zur datei -> ziel" */
static GPtrArray *
parse_tar_listing (const gchar *text)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func ((GDestroyNotify) nolphin_archive_entry_free);
	gchar **lines = g_strsplit (text, "\n", -1);
	guint i;

	for (i = 0; lines[i] != NULL; i++) {
		const gchar *p = lines[i];
		gchar perm[16], owner[128], date[32], time_[32];
		gint64 size = 0;
		gint consumed = 0;
		gchar *name, *arrow, *modified;
		gboolean is_dir;

		if (lines[i][0] == '\0') {
			continue;
		}
		if (sscanf (p, "%15s %127s %" G_GINT64_FORMAT " %31s %31s %n", perm, owner, &size, date, time_, &consumed) < 5 ||
		    consumed <= 0) {
			continue;
		}
		name = g_strdup (p + consumed);
		/* Symlink-Ziel abschneiden */
		arrow = g_strstr_len (name, -1, " -> ");
		if (arrow != NULL && perm[0] == 'l') {
			*arrow = '\0';
		}
		is_dir = perm[0] == 'd';
		modified = g_strdup_printf ("%s %s", date, time_);
		if (name[0] != '\0' && g_strcmp0 (name, "./") != 0 && g_strcmp0 (name, ".") != 0) {
			g_ptr_array_add (entries, entry_new (name, size, is_dir, modified));
		}
		g_free (modified);
		g_free (name);
	}
	g_strfreev (lines);
	return entries;
}

/* 7z l -slt -ba: Blöcke "Schlüssel = Wert", getrennt durch Leerzeilen */
static GPtrArray *
parse_7z_listing (const gchar *text)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func ((GDestroyNotify) nolphin_archive_entry_free);
	gchar **lines = g_strsplit (text, "\n", -1);
	gchar *path = NULL, *modified = NULL;
	goffset size = -1;
	gboolean is_dir = FALSE;
	guint i;

	for (i = 0; ; i++) {
		const gchar *line = lines[i];
		gboolean end = (line == NULL);
		gchar *trimmed = end ? NULL : g_strstrip (g_strdup (line));

		if (end || trimmed[0] == '\0') {
			if (path != NULL) {
				g_ptr_array_add (entries, entry_new (path, size, is_dir, modified));
			}
			g_clear_pointer (&path, g_free);
			g_clear_pointer (&modified, g_free);
			size = -1;
			is_dir = FALSE;
			g_free (trimmed);
			if (end) {
				break;
			}
			continue;
		}

		if (g_str_has_prefix (trimmed, "Path = ")) {
			g_free (path);
			path = g_strdup (trimmed + 7);
		} else if (g_str_has_prefix (trimmed, "Size = ")) {
			size = g_ascii_strtoll (trimmed + 7, NULL, 10);
		} else if (g_str_has_prefix (trimmed, "Folder = ")) {
			is_dir = trimmed[9] == '+';
		} else if (g_str_has_prefix (trimmed, "Attributes = ")) {
			if (trimmed[13] == 'D') {
				is_dir = TRUE;
			}
		} else if (g_str_has_prefix (trimmed, "Modified = ")) {
			g_free (modified);
			modified = g_strdup (trimmed + 11);
		}
		g_free (trimmed);
	}

	g_strfreev (lines);
	return entries;
}

static GPtrArray *
do_list (Job *job, GError **error)
{
	gchar *out = NULL;
	GPtrArray *entries;

	if (format_class (job->format) == CLASS_TAR) {
		const gchar *argv[] = { "tar", "-tvf", job->archive_path, NULL };

		if (!need_tool ("tar", error) || !run_sync (argv, NULL, &out, error)) {
			return NULL;
		}
		entries = parse_tar_listing (out);
	} else {
		gchar *seven = find_7z ();
		const gchar *argv[] = { seven, "l", "-slt", "-ba", job->archive_path, NULL };

		if (seven == NULL) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
				     _("Zum Anzeigen des Inhalts wird »7z« (p7zip) benötigt, es ist nicht installiert."));
			return NULL;
		}
		if (!run_sync (argv, NULL, &out, error)) {
			g_free (seven);
			return NULL;
		}
		g_free (seven);
		entries = parse_7z_listing (out);
	}
	g_free (out);
	return entries;
}

/* --- Staging und Neupacken ------------------------------------------------ */

static gboolean
copy_recursive (GFile *src, GFile *dst, GError **error)
{
	GFileType type = g_file_query_file_type (src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);

	if (type == G_FILE_TYPE_DIRECTORY) {
		GFileEnumerator *en;
		GFileInfo *info;

		if (!g_file_make_directory_with_parents (dst, NULL, error) && !g_file_query_exists (dst, NULL)) {
			return FALSE;
		}
		g_clear_error (error);
		en = g_file_enumerate_children (src, G_FILE_ATTRIBUTE_STANDARD_NAME,
						G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
		if (en == NULL) {
			return FALSE;
		}
		while ((info = g_file_enumerator_next_file (en, NULL, NULL)) != NULL) {
			GFile *s = g_file_get_child (src, g_file_info_get_name (info));
			GFile *d = g_file_get_child (dst, g_file_info_get_name (info));
			gboolean ok = copy_recursive (s, d, error);

			g_object_unref (s);
			g_object_unref (d);
			g_object_unref (info);
			if (!ok) {
				g_object_unref (en);
				return FALSE;
			}
		}
		g_object_unref (en);
		return TRUE;
	}

	{
		GFile *parent = g_file_get_parent (dst);

		if (parent != NULL) {
			g_file_make_directory_with_parents (parent, NULL, NULL);
			g_object_unref (parent);
		}
	}
	return g_file_copy (src, dst, G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA,
			    NULL, NULL, NULL, error);
}

/* Legt alle hinzuzufügenden Dateien unter ihrem Zielpfad in @stage ab. */
static gboolean
stage_items (const gchar *stage, GList *items, GError **error)
{
	GFile *root = g_file_new_for_path (stage);
	GList *l;
	gboolean ok = TRUE;

	for (l = items; l != NULL && ok; l = l->next) {
		StageItem *item = l->data;
		GFile *dst;

		if (!entry_path_is_safe (item->rel_path)) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
				     _("Ungültiger Zielpfad im Archiv: %s"), item->rel_path);
			ok = FALSE;
			break;
		}
		dst = g_file_resolve_relative_path (root, item->rel_path);
		ok = copy_recursive (item->source, dst, error);
		g_object_unref (dst);
	}
	g_object_unref (root);
	return ok;
}

/* Oberste Pfadbestandteile der Staging-Einträge (für zip/7z/tar-Argumente). */
static GPtrArray *
top_level_names (const gchar *dir)
{
	GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
	GDir *d = g_dir_open (dir, 0, NULL);
	const gchar *n;

	if (d != NULL) {
		while ((n = g_dir_read_name (d)) != NULL) {
			g_ptr_array_add (names, g_strdup (n));
		}
		g_dir_close (d);
	}
	g_ptr_array_sort (names, (GCompareFunc) g_strcmp0);
	return names;
}

static void
remove_tree (const gchar *path)
{
	const gchar *argv[] = { "rm", "-rf", "--", path, NULL };

	run_sync (argv, NULL, NULL, NULL);
}

static const gchar *
tar_create_flags (NolphinArchiveFormat f, const gchar **extra)
{
	*extra = NULL;
	switch (f) {
	case NOLPHIN_ARCHIVE_FORMAT_TAR_GZ:   return "-czf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2:  return "-cjf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_XZ:   return "-cJf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_Z:    return "-cZf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_ZST:  *extra = "--zstd"; return "-cf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4:  *extra = "--use-compress-program=lz4"; return "-cf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ:   *extra = "--lzip"; return "-cf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA: *extra = "--lzma"; return "-cf";
	case NOLPHIN_ARCHIVE_FORMAT_TAR_LZO:  *extra = "--lzop"; return "-cf";
	default: return "-cf";
	}
}

/* TAR: entpacken → ändern → neu packen → Original ersetzen. */
static gboolean
tar_modify (Job *job, GError **error)
{
	gchar *work = g_dir_make_tmp ("nolphin-archive-XXXXXX", error);
	gchar *tree, *stage = NULL, *new_archive;
	gboolean ok = FALSE;
	GPtrArray *names = NULL;
	GPtrArray *argv;
	const gchar *flags, *extra;
	GFile *src_file, *dst_file;
	GList *l;

	if (work == NULL) {
		return FALSE;
	}
	tree = g_build_filename (work, "tree", NULL);
	new_archive = g_build_filename (work, "new-archive", NULL);
	g_mkdir (tree, 0700);

	if (!need_tool ("tar", error)) {
		goto out;
	}
	{
		const gchar *x[] = { "tar", "-xf", job->archive_path, "-C", tree, NULL };

		if (!run_sync (x, NULL, NULL, error)) {
			goto out;
		}
	}

	if (job->op == OP_ADD) {
		stage = g_build_filename (work, "stage", NULL);
		g_mkdir (stage, 0700);
		if (!stage_items (stage, job->stage_items, error)) {
			goto out;
		}
		{
			gchar *from = g_strconcat (stage, "/.", NULL);
			const gchar *cp[] = { "cp", "-a", "--", from, tree, NULL };
			gboolean r = run_sync (cp, NULL, NULL, error);

			g_free (from);
			if (!r) {
				goto out;
			}
		}
	} else if (job->op == OP_REMOVE) {
		for (l = job->names; l != NULL; l = l->next) {
			gchar *clean = strip_trailing_slashes (l->data);

			if (!entry_path_is_safe (clean)) {
				g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
					     _("Ungültiger Pfad im Archiv: %s"), (const gchar *) l->data);
				g_free (clean);
				goto out;
			}
			{
				gchar *full = g_build_filename (tree, clean, NULL);

				remove_tree (full);
				g_free (full);
			}
			g_free (clean);
		}
	}

	names = top_level_names (tree);
	if (names->len == 0) {
		g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
			     _("Das Archiv wäre danach leer. Zum Löschen die Datei selbst in den Papierkorb verschieben."));
		goto out;
	}

	flags = tar_create_flags (job->format, &extra);
	argv = g_ptr_array_new ();
	g_ptr_array_add (argv, (gpointer) "tar");
	if (extra != NULL) {
		g_ptr_array_add (argv, (gpointer) extra);
	}
	g_ptr_array_add (argv, (gpointer) flags);
	g_ptr_array_add (argv, new_archive);
	g_ptr_array_add (argv, (gpointer) "-C");
	g_ptr_array_add (argv, tree);
	g_ptr_array_add (argv, (gpointer) "--");
	{
		guint i;

		for (i = 0; i < names->len; i++) {
			g_ptr_array_add (argv, names->pdata[i]);
		}
	}
	g_ptr_array_add (argv, NULL);
	ok = run_sync ((const gchar * const *) argv->pdata, NULL, NULL, error);
	g_ptr_array_free (argv, TRUE);
	if (!ok) {
		goto out;
	}

	src_file = g_file_new_for_path (new_archive);
	dst_file = g_file_new_for_path (job->archive_path);
	ok = g_file_copy (src_file, dst_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, error);
	g_object_unref (src_file);
	g_object_unref (dst_file);

out:
	if (names != NULL) {
		g_ptr_array_free (names, TRUE);
	}
	remove_tree (work);
	g_free (stage);
	g_free (tree);
	g_free (new_archive);
	g_free (work);
	return ok;
}

/* --- Ändern (zip / 7z) ---------------------------------------------------- */

static gboolean
zip_or_7z_add (Job *job, GError **error)
{
	gchar *stage = g_dir_make_tmp ("nolphin-archive-XXXXXX", error);
	GPtrArray *names, *argv;
	gboolean ok = FALSE;
	gchar *tool = NULL;
	guint i;

	if (stage == NULL) {
		return FALSE;
	}
	if (!stage_items (stage, job->stage_items, error)) {
		goto out;
	}
	names = top_level_names (stage);

	argv = g_ptr_array_new ();
	if (format_class (job->format) == CLASS_ZIP) {
		if (!need_tool ("zip", error)) {
			g_ptr_array_free (argv, TRUE);
			g_ptr_array_free (names, TRUE);
			goto out;
		}
		tool = g_strdup ("zip");
		g_ptr_array_add (argv, tool);
		g_ptr_array_add (argv, (gpointer) "-q");
		g_ptr_array_add (argv, (gpointer) "-r");
		g_ptr_array_add (argv, job->archive_path);
	} else {
		tool = find_7z ();
		if (tool == NULL) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
				     _("Das benötigte Werkzeug »7z« ist nicht installiert."));
			g_ptr_array_free (argv, TRUE);
			g_ptr_array_free (names, TRUE);
			goto out;
		}
		g_ptr_array_add (argv, tool);
		g_ptr_array_add (argv, (gpointer) "a");
		g_ptr_array_add (argv, (gpointer) "-y");
		g_ptr_array_add (argv, job->archive_path);
	}
	for (i = 0; i < names->len; i++) {
		g_ptr_array_add (argv, names->pdata[i]);
	}
	g_ptr_array_add (argv, NULL);
	ok = run_sync ((const gchar * const *) argv->pdata, stage, NULL, error);
	g_ptr_array_free (argv, TRUE);
	g_ptr_array_free (names, TRUE);
	g_free (tool);

out:
	remove_tree (stage);
	g_free (stage);
	return ok;
}

static gboolean
zip_or_7z_remove (Job *job, GError **error)
{
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	GList *l;
	gboolean ok;
	gchar *seven = NULL;

	if (format_class (job->format) == CLASS_ZIP) {
		if (!need_tool ("zip", error)) {
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		g_ptr_array_add (argv, g_strdup ("zip"));
		g_ptr_array_add (argv, g_strdup ("-q"));
		g_ptr_array_add (argv, g_strdup ("-d"));
		g_ptr_array_add (argv, g_strdup (job->archive_path));
	} else {
		seven = find_7z ();
		if (seven == NULL) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
				     _("Das benötigte Werkzeug »7z« ist nicht installiert."));
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		g_ptr_array_add (argv, seven);
		g_ptr_array_add (argv, g_strdup ("d"));
		g_ptr_array_add (argv, g_strdup ("-y"));
		g_ptr_array_add (argv, g_strdup ("-spd"));
		g_ptr_array_add (argv, g_strdup (job->archive_path));
	}

	for (l = job->names; l != NULL; l = l->next) {
		const gchar *name = l->data;
		gboolean is_dir = g_str_has_suffix (name, "/");
		gchar *clean = strip_trailing_slashes (name);

		if (!entry_path_is_safe (clean)) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
				     _("Ungültiger Pfad im Archiv: %s"), name);
			g_free (clean);
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		if (format_class (job->format) == CLASS_ZIP) {
			gchar *esc = escape_glob (clean);

			if (is_dir) {
				/* Ordner: Inhalt ("dir/" plus Stern) und der Ordnereintrag "dir/" selbst */
				g_ptr_array_add (argv, g_strconcat (esc, "/*", NULL));
				g_ptr_array_add (argv, g_strconcat (esc, "/", NULL));
				g_free (esc);
			} else {
				g_ptr_array_add (argv, esc);
			}
		} else {
			g_ptr_array_add (argv, g_strdup (clean));
		}
		g_free (clean);
	}
	g_ptr_array_add (argv, NULL);
	ok = run_sync ((const gchar * const *) argv->pdata, NULL, NULL, error);
	g_ptr_array_free (argv, TRUE);
	return ok;
}

/* --- Einzelextraktion ----------------------------------------------------- */

static gboolean
do_extract (Job *job, GError **error)
{
	GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
	FormatClass c = format_class (job->format);
	GList *l;
	gboolean ok;
	gchar *seven = NULL;

	g_mkdir_with_parents (job->destination, 0755);

	if (c == CLASS_ZIP) {
		if (!need_tool ("unzip", error)) {
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		g_ptr_array_add (argv, g_strdup ("unzip"));
		g_ptr_array_add (argv, g_strdup ("-o"));
		g_ptr_array_add (argv, g_strdup ("-q"));
		g_ptr_array_add (argv, g_strdup (job->archive_path));
	} else if (c == CLASS_TAR) {
		if (!need_tool ("tar", error)) {
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		g_ptr_array_add (argv, g_strdup ("tar"));
		g_ptr_array_add (argv, g_strdup ("--no-wildcards"));
		g_ptr_array_add (argv, g_strdup ("-xf"));
		g_ptr_array_add (argv, g_strdup (job->archive_path));
		g_ptr_array_add (argv, g_strdup ("-C"));
		g_ptr_array_add (argv, g_strdup (job->destination));
		g_ptr_array_add (argv, g_strdup ("--"));
	} else {
		seven = find_7z ();
		if (seven == NULL) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
				     _("Das benötigte Werkzeug »7z« ist nicht installiert."));
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		g_ptr_array_add (argv, seven);
		g_ptr_array_add (argv, g_strdup ("x"));
		g_ptr_array_add (argv, g_strdup ("-y"));
		g_ptr_array_add (argv, g_strdup ("-spd"));
		g_ptr_array_add (argv, g_strdup_printf ("-o%s", job->destination));
		g_ptr_array_add (argv, g_strdup (job->archive_path));
	}

	for (l = job->names; l != NULL; l = l->next) {
		const gchar *name = l->data;
		gboolean is_dir = g_str_has_suffix (name, "/");
		gchar *clean = strip_trailing_slashes (name);

		if (!entry_path_is_safe (clean)) {
			g_set_error (error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
				     _("Ungültiger Pfad im Archiv: %s"), name);
			g_free (clean);
			g_ptr_array_free (argv, TRUE);
			return FALSE;
		}
		if (c == CLASS_ZIP) {
			gchar *esc = escape_glob (clean);

			if (is_dir) {
				g_ptr_array_add (argv, g_strconcat (esc, "/*", NULL));
				g_free (esc);
			} else {
				g_ptr_array_add (argv, esc);
			}
		} else {
			g_ptr_array_add (argv, g_strdup (clean));
		}
		g_free (clean);
	}

	if (c == CLASS_ZIP) {
		g_ptr_array_add (argv, g_strdup ("-d"));
		g_ptr_array_add (argv, g_strdup (job->destination));
	}
	g_ptr_array_add (argv, NULL);
	ok = run_sync ((const gchar * const *) argv->pdata, NULL, NULL, error);
	g_ptr_array_free (argv, TRUE);
	return ok;
}

/* --- Thread-Einstieg und öffentliche API ---------------------------------- */

static void
job_run (GTask *task, Job *job)
{
	GError *error = NULL;
	FormatClass c = format_class (job->format);

	switch (job->op) {
	case OP_LIST: {
		GPtrArray *entries = do_list (job, &error);

		if (entries != NULL) {
			g_task_return_pointer (task, entries, (GDestroyNotify) g_ptr_array_unref);
			return;
		}
		break;
	}
	case OP_ADD:
	case OP_REMOVE:
		if (c == CLASS_NONE) {
			g_set_error (&error, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
				     _("Dieses Archivformat kann nur gelesen, aber nicht geändert werden."));
		} else if (c == CLASS_TAR) {
			if (tar_modify (job, &error)) {
				g_task_return_boolean (task, TRUE);
				return;
			}
		} else if (job->op == OP_ADD ? zip_or_7z_add (job, &error) : zip_or_7z_remove (job, &error)) {
			g_task_return_boolean (task, TRUE);
			return;
		}
		break;
	case OP_EXTRACT:
		if (do_extract (job, &error)) {
			g_task_return_boolean (task, TRUE);
			return;
		}
		break;
	}

	if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		nolphin_tool_progress_set_had_error (nolphin_tool_progress_get (task));
	}
	g_task_return_error (task, error);
}

static void
job_thread (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	/* run_sync() findet die Fortschrittsanzeige über den Thread. */
	nolphin_tool_progress_set_thread (nolphin_tool_progress_get (task));
	job_run (task, task_data);
	nolphin_tool_progress_set_thread (NULL);
}

/* Ändernde Vorgänge erscheinen bei längerer Dauer im Fenster
 * „Dateivorgänge“; das Auflisten bleibt still, es füllt nur die
 * Ansicht des Archiv-Managers. */
static void
run_job_with_progress (GTask *task, Job *job, GCancellable *cancellable)
{
	gchar *archive_name = g_path_get_basename (job->archive_path);
	gchar *status;
	gchar *details = NULL;

	switch (job->op) {
	case OP_ADD:
		status = g_strdup_printf (_("Dateien werden zu „%s“ hinzugefügt"), archive_name);
		break;
	case OP_REMOVE:
		status = g_strdup_printf (_("Einträge werden aus „%s“ entfernt"), archive_name);
		break;
	case OP_EXTRACT:
	default:
		status = g_strdup_printf (_("Einträge aus „%s“ werden entpackt"), archive_name);
		details = g_strdup_printf (_("nach %s"), job->destination);
		break;
	}

	nolphin_tool_progress_attach (task, cancellable, status, details);
	g_task_run_in_thread (task, job_thread);

	g_free (details);
	g_free (status);
	g_free (archive_name);
}

static GTask *
new_job_task (GFile *archive, Operation op, GCancellable *cancellable,
	      GAsyncReadyCallback callback, gpointer user_data, Job **out_job)
{
	GTask *task = g_task_new (NULL, cancellable, callback, user_data);
	Job *job = g_new0 (Job, 1);

	job->op = op;
	job->format = nolphin_archive_detect_format (archive);
	job->archive_path = g_file_get_path (archive);
	g_task_set_task_data (task, job, (GDestroyNotify) job_free);
	*out_job = job;

	if (job->archive_path == NULL) {
		g_task_return_new_error (task, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
					 _("Entfernte Orte werden für das Archivieren noch nicht unterstützt."));
		g_object_unref (task);
		return NULL;
	}
	if (job->format == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
		g_task_return_new_error (task, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
					 _("Nicht erkannter Archivtyp."));
		g_object_unref (task);
		return NULL;
	}
	return task;
}

void
nolphin_archive_list_async (GFile *archive, GCancellable *cancellable,
			    GAsyncReadyCallback callback, gpointer user_data)
{
	Job *job;
	GTask *task = new_job_task (archive, OP_LIST, cancellable, callback, user_data, &job);

	if (task == NULL) {
		return;
	}
	if (!nolphin_archive_manage_can_list (job->format)) {
		g_task_return_new_error (task, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
					 _("Dieses Format enthält nur eine einzelne Datei, es gibt keinen Inhalt anzuzeigen."));
		g_object_unref (task);
		return;
	}
	g_task_run_in_thread (task, job_thread);
	g_object_unref (task);
}

GPtrArray *
nolphin_archive_list_finish (GAsyncResult *result, GError **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
add_stage_item (Job *job, GFile *source, const gchar *target_dir, const gchar *name_override)
{
	StageItem *item = g_new0 (StageItem, 1);
	gchar *name = name_override != NULL ? g_strdup (name_override) : g_file_get_basename (source);
	gchar *dir = (target_dir != NULL && target_dir[0] != '\0') ? strip_trailing_slashes (target_dir) : NULL;

	item->rel_path = dir != NULL ? g_strconcat (dir, "/", name, NULL) : g_strdup (name);
	item->source = g_object_ref (source);
	job->stage_items = g_list_append (job->stage_items, item);
	g_free (name);
	g_free (dir);
}

void
nolphin_archive_add_files_async (GFile *archive, GList *sources, const gchar *target_dir,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback, gpointer user_data)
{
	Job *job;
	GTask *task = new_job_task (archive, OP_ADD, cancellable, callback, user_data, &job);
	GList *l;

	if (task == NULL) {
		return;
	}
	for (l = sources; l != NULL; l = l->next) {
		add_stage_item (job, l->data, target_dir, NULL);
	}
	run_job_with_progress (task, job, cancellable);
	g_object_unref (task);
}

void
nolphin_archive_replace_entry_async (GFile *archive, const gchar *entry_path, GFile *source,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback, gpointer user_data)
{
	Job *job;
	GTask *task = new_job_task (archive, OP_ADD, cancellable, callback, user_data, &job);
	gchar *dir, *base;

	if (task == NULL) {
		return;
	}
	dir = g_path_get_dirname (entry_path);
	base = g_path_get_basename (entry_path);
	add_stage_item (job, source, g_strcmp0 (dir, ".") == 0 ? NULL : dir, base);
	g_free (dir);
	g_free (base);
	run_job_with_progress (task, job, cancellable);
	g_object_unref (task);
}

static GList *
copy_names (GList *names)
{
	GList *copy = NULL, *l;

	for (l = names; l != NULL; l = l->next) {
		copy = g_list_append (copy, g_strdup (l->data));
	}
	return copy;
}

void
nolphin_archive_remove_entries_async (GFile *archive, GList *entry_paths,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback, gpointer user_data)
{
	Job *job;
	GTask *task = new_job_task (archive, OP_REMOVE, cancellable, callback, user_data, &job);

	if (task == NULL) {
		return;
	}
	job->names = copy_names (entry_paths);
	run_job_with_progress (task, job, cancellable);
	g_object_unref (task);
}

void
nolphin_archive_extract_entries_async (GFile *archive, GList *entry_paths, GFile *destination,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback, gpointer user_data)
{
	Job *job;
	GTask *task = new_job_task (archive, OP_EXTRACT, cancellable, callback, user_data, &job);

	if (task == NULL) {
		return;
	}
	job->names = copy_names (entry_paths);
	job->destination = g_file_get_path (destination);
	if (job->destination == NULL) {
		g_task_return_new_error (task, manage_error_quark (), NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
					 _("Das Ziel muss ein lokaler Ordner sein."));
		g_object_unref (task);
		return;
	}
	run_job_with_progress (task, job, cancellable);
	g_object_unref (task);
}

gboolean
nolphin_archive_manage_finish (GAsyncResult *result, GError **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}
