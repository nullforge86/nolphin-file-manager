/* Prüft nolphin-tool-progress.c: Archiv- und Git-Vorgänge sollen wie
 * Kopiervorgänge im Fenster „Dateivorgänge“ erscheinen. Getestet wird ohne
 * Fenster, direkt am NolphinProgressInfo, das der Fenster-Handler
 * (src/nolphin-progress-ui-handler.c) über den Manager erhält:
 *
 *   1. Ein angehängter Vorgang meldet sich beim Manager an, ist gestartet
 *      und wird beim Freigeben des Tasks beendet.
 *   2. „Anhalten“ hält den Kindprozess an (SIGSTOP), „Fortsetzen“ lässt
 *      ihn weiterlaufen.
 *   3. „Abbrechen“ beendet den Kindprozess.
 *   4. Das Cancellable des Aufrufers bricht ebenfalls ab.
 *   5. Komprimieren/Entpacken über nolphin-archive.c laufen mit Anzeige;
 *      ein kaputtes Archiv setzt „had_error“.
 *
 * Endet mit 0, wenn alles stimmt, sonst mit 1. */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libnolphin-private/nolphin-archive.h>
#include <libnolphin-private/nolphin-progress-info.h>
#include <libnolphin-private/nolphin-progress-info-manager.h>
#include <libnolphin-private/nolphin-tool-progress.h>

static gint exit_code = 0;
static GPtrArray *seen_infos;

static void
check (gboolean ok, const gchar *what)
{
    if (ok) {
        g_print ("PASS: %s\n", what);
    } else {
        g_printerr ("FAIL: %s\n", what);
        exit_code = 1;
    }
}

static void
on_new_progress_info (NolphinProgressInfoManager *manager, NolphinProgressInfo *info, gpointer user_data)
{
    g_ptr_array_add (seen_infos, g_object_ref (info));
}

static gboolean
quit_cb (gpointer loop)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

/* Lässt die Hauptschleife @ms Millisekunden laufen (Idle-Signale,
 * Pulstimer). */
static void
spin (guint ms)
{
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);

    g_timeout_add (ms, quit_cb, loop);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);
}

/* Zustand aus /proc/<pid>/stat: 'T' = angehalten. */
static gchar
process_state (GSubprocess *proc)
{
    const gchar *id = g_subprocess_get_identifier (proc);
    gchar *path, *contents = NULL, *close_paren;
    gchar state = '?';

    if (id == NULL) {
        return '?';
    }
    path = g_strdup_printf ("/proc/%s/stat", id);
    if (g_file_get_contents (path, &contents, NULL, NULL)) {
        close_paren = strrchr (contents, ')');
        if (close_paren != NULL && close_paren[1] == ' ') {
            state = close_paren[2];
        }
    }
    g_free (contents);
    g_free (path);
    return state;
}

static GSubprocess *
spawn_sleep (void)
{
    return g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, NULL, "sleep", "30", NULL);
}

static gboolean
wait_exited (GSubprocess *proc)
{
    GError *error = NULL;
    gboolean ok = g_subprocess_wait (proc, NULL, &error);

    g_clear_error (&error);
    return ok && g_subprocess_get_if_signaled (proc);
}

static void
test_lifecycle (void)
{
    GTask *task = g_task_new (NULL, NULL, NULL, NULL);
    NolphinToolProgress *progress;
    NolphinProgressInfo *info;
    gchar *status;

    g_ptr_array_set_size (seen_infos, 0);
    progress = nolphin_tool_progress_attach (task, NULL, "„a.zip“ wird erstellt", "2 Objekte");
    check (progress != NULL && nolphin_tool_progress_get (task) == progress, "Anzeige hängt am Task");
    check (seen_infos->len == 1, "Manager meldet den Vorgang (Fenster „Dateivorgänge“)");
    if (seen_infos->len != 1) {
        g_object_unref (task);
        return;
    }
    info = g_object_ref (g_ptr_array_index (seen_infos, 0));

    spin (100);
    status = nolphin_progress_info_get_status (info);
    check (g_strcmp0 (status, "„a.zip“ wird erstellt") == 0, "Statustext übernommen");
    g_free (status);
    check (nolphin_progress_info_get_is_started (info), "Vorgang ist gestartet");
    check (!nolphin_progress_info_get_is_finished (info), "Vorgang läuft noch");

    g_object_unref (task);
    spin (50);
    check (nolphin_progress_info_get_is_finished (info), "Freigeben des Tasks beendet den Vorgang");
    check (!nolphin_progress_info_get_had_error (info), "ohne Fehler kein had_error");
    g_object_unref (info);
}

static void
test_pause_and_cancel (void)
{
    GTask *task = g_task_new (NULL, NULL, NULL, NULL);
    NolphinToolProgress *progress;
    NolphinProgressInfo *info;
    GSubprocess *proc;

    g_ptr_array_set_size (seen_infos, 0);
    progress = nolphin_tool_progress_attach (task, NULL, "Test", NULL);
    info = g_object_ref (g_ptr_array_index (seen_infos, 0));
    proc = spawn_sleep ();
    nolphin_tool_progress_set_subprocess (progress, proc);

    nolphin_progress_info_pause (info);
    spin (600);
    check (process_state (proc) == 'T', "Anhalten stoppt den Kindprozess");

    nolphin_progress_info_resume (info);
    spin (600);
    check (process_state (proc) != 'T' && process_state (proc) != '?', "Fortsetzen lässt ihn weiterlaufen");

    /* Abbrechen auch aus dem angehaltenen Zustand. */
    nolphin_progress_info_pause (info);
    spin (600);
    nolphin_progress_info_cancel (info);
    check (wait_exited (proc), "Abbrechen beendet den Kindprozess");
    check (nolphin_tool_progress_is_cancelled (progress), "Vorgang gilt als abgebrochen");

    g_object_unref (proc);
    g_object_unref (task);
    spin (50);
    check (nolphin_progress_info_get_is_finished (info), "abgebrochener Vorgang wird beendet");
    g_object_unref (info);
}

static void
test_caller_cancellable (void)
{
    GCancellable *cancellable = g_cancellable_new ();
    GTask *task = g_task_new (NULL, cancellable, NULL, NULL);
    NolphinToolProgress *progress = nolphin_tool_progress_attach (task, cancellable, "Test", NULL);
    GSubprocess *proc = spawn_sleep ();

    nolphin_tool_progress_set_subprocess (progress, proc);
    g_cancellable_cancel (cancellable);
    check (wait_exited (proc), "Cancellable des Aufrufers beendet den Kindprozess");
    g_object_unref (proc);

    /* Nach dem Abbrechen angemeldete Prozesse werden sofort beendet. */
    proc = spawn_sleep ();
    nolphin_tool_progress_set_subprocess (progress, proc);
    check (wait_exited (proc), "nach dem Abbrechen gestarteter Prozess wird beendet");
    g_object_unref (proc);

    g_object_unref (task);
    g_object_unref (cancellable);
    spin (50);
}

typedef struct {
    GMainLoop *loop;
    gboolean ok;
    GError *error;
} OpResult;

static void
on_compress_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
    OpResult *r = user_data;

    r->ok = nolphin_archive_compress_finish (result, &r->error);
    g_main_loop_quit (r->loop);
}

static void
on_extract_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
    OpResult *r = user_data;

    r->ok = nolphin_archive_extract_finish (result, &r->error);
    g_main_loop_quit (r->loop);
}

static NolphinProgressInfo *
last_info (void)
{
    return seen_infos->len > 0 ? g_ptr_array_index (seen_infos, seen_infos->len - 1) : NULL;
}

static void
test_archive (void)
{
    gchar *dir = g_dir_make_tmp ("nolphin-tool-progress-XXXXXX", NULL);
    gchar *src_path = g_build_filename (dir, "inhalt.txt", NULL);
    gchar *zip_path = g_build_filename (dir, "inhalt.zip", NULL);
    gchar *bad_path = g_build_filename (dir, "kaputt.zip", NULL);
    gchar *out_path = g_build_filename (dir, "ziel", NULL);
    GFile *src, *zip, *bad, *out;
    GList *sources;
    OpResult r = { 0 };
    gchar *zip_tool = g_find_program_in_path ("zip");
    gchar *unzip_tool = g_find_program_in_path ("unzip");

    if (zip_tool == NULL || unzip_tool == NULL) {
        g_print ("SKIP: zip/unzip nicht installiert, Archivteil übersprungen\n");
        goto out;
    }

    g_file_set_contents (src_path, "Hallo Nolphin", -1, NULL);
    g_file_set_contents (bad_path, "kein Archiv", -1, NULL);
    g_mkdir (out_path, 0700);
    src = g_file_new_for_path (src_path);
    zip = g_file_new_for_path (zip_path);
    bad = g_file_new_for_path (bad_path);
    out = g_file_new_for_path (out_path);
    sources = g_list_prepend (NULL, src);

    r.loop = g_main_loop_new (NULL, FALSE);

    g_ptr_array_set_size (seen_infos, 0);
    nolphin_archive_compress_async (sources, zip, NOLPHIN_ARCHIVE_FORMAT_ZIP, NULL, 0, NULL, on_compress_done, &r);
    g_main_loop_run (r.loop);
    spin (50);
    check (r.ok, "Komprimieren gelingt");
    check (last_info () != NULL, "Komprimieren meldet einen Vorgang an");
    check (last_info () != NULL && nolphin_progress_info_get_is_finished (last_info ())
           && !nolphin_progress_info_get_had_error (last_info ()),
           "Komprimieren-Vorgang endet ohne Fehler");
    g_clear_error (&r.error);

    /* Abbrechen beim Komprimieren: kein halbes Archiv liegen lassen. */
    {
        GCancellable *cancellable = g_cancellable_new ();
        gchar *cancel_path = g_build_filename (dir, "abgebrochen.zip", NULL);
        GFile *cancel_zip = g_file_new_for_path (cancel_path);

        g_cancellable_cancel (cancellable);
        nolphin_archive_compress_async (sources, cancel_zip, NOLPHIN_ARCHIVE_FORMAT_ZIP, NULL, 0,
                                        cancellable, on_compress_done, &r);
        g_main_loop_run (r.loop);
        check (!r.ok && g_error_matches (r.error, G_IO_ERROR, G_IO_ERROR_CANCELLED),
               "abgebrochenes Komprimieren meldet G_IO_ERROR_CANCELLED");
        check (!g_file_test (cancel_path, G_FILE_TEST_EXISTS), "kein halbes Archiv nach dem Abbrechen");
        g_clear_error (&r.error);
        g_unlink (cancel_path);
        g_object_unref (cancel_zip);
        g_free (cancel_path);
        g_object_unref (cancellable);
        spin (50);
    }

    g_ptr_array_set_size (seen_infos, 0);
    nolphin_archive_extract_async (bad, out, NULL, on_extract_done, &r);
    g_main_loop_run (r.loop);
    spin (50);
    check (!r.ok, "kaputtes Archiv schlägt fehl");
    check (last_info () != NULL && nolphin_progress_info_get_is_finished (last_info ())
           && nolphin_progress_info_get_had_error (last_info ()),
           "Fehler wird für die Abschlussmeldung vermerkt");
    g_clear_error (&r.error);

    g_main_loop_unref (r.loop);
    g_list_free (sources);
    g_object_unref (src);
    g_object_unref (zip);
    g_object_unref (bad);
    g_object_unref (out);

    g_unlink (src_path);
    g_unlink (zip_path);
    g_unlink (bad_path);
    g_rmdir (out_path);
    g_rmdir (dir);
out:
    g_free (zip_tool);
    g_free (unzip_tool);
    g_free (src_path);
    g_free (zip_path);
    g_free (bad_path);
    g_free (out_path);
    g_free (dir);
}

int
main (int argc, char **argv)
{
    NolphinProgressInfoManager *manager;

    seen_infos = g_ptr_array_new_with_free_func (g_object_unref);
    manager = nolphin_progress_info_manager_new ();
    g_signal_connect (manager, "new-progress-info", G_CALLBACK (on_new_progress_info), NULL);

    test_lifecycle ();
    test_pause_and_cancel ();
    test_caller_cancellable ();
    test_archive ();

    g_object_unref (manager);
    g_ptr_array_unref (seen_infos);
    return exit_code;
}
