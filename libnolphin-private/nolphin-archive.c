/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-archive.c: abstracted archive backend (create/extract/test)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#include <config.h>

#include "nolphin-archive.h"
#include "nolphin-tool-progress.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>

typedef struct {
    const gchar *label;
    const gchar *extension;
    /* zip/unzip are separate binaries; tar and 7z use the same binary
     * for both directions, so create_tool == extract_tool for those.
     * NULL create_tool = "Entpacken / Nur Lesen" - kein Erstellen
     * vortaeuschen, wenn das Werkzeug es nicht anbietet. */
    const gchar *create_tool;
    const gchar *extract_tool;
} FormatInfo;

static const FormatInfo format_info[] = {
    [NOLPHIN_ARCHIVE_FORMAT_ZIP]       = { "ZIP",       ".zip",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR]       = { "TAR",       ".tar",     "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_GZ]    = { "TAR.GZ",    ".tar.gz",  "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2]   = { "TAR.BZ2",   ".tar.bz2", "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_XZ]    = { "TAR.XZ",    ".tar.xz",  "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_ZST]   = { "TAR.ZST",   ".tar.zst", "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4]   = { "TAR.LZ4",   ".tar.lz4", "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP] = { "7-Zip",     ".7z",      "7z",    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_GZ]        = { "GZ",        ".gz",      "gzip",  "gzip" },
    [NOLPHIN_ARCHIVE_FORMAT_BZ2]       = { "BZ2",       ".bz2",     "bzip2", "bzip2" },
    [NOLPHIN_ARCHIVE_FORMAT_XZ]        = { "XZ",        ".xz",      "xz",    "xz" },
    [NOLPHIN_ARCHIVE_FORMAT_ZST]       = { "ZST",       ".zst",     "zstd",  "zstd" },
    [NOLPHIN_ARCHIVE_FORMAT_LZ4]       = { "LZ4",       ".lz4",     "lz4",   "lz4" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_LZ]    = { "TAR.LZ",    ".tar.lz",  "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA]  = { "TAR.LZMA",  ".tar.lzma","tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_LZO]   = { "TAR.LZO",   ".tar.lzo", "tar",   "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_Z]     = { "TAR.Z",     ".tar.Z",   "compress", "tar" },
    [NOLPHIN_ARCHIVE_FORMAT_LZ]        = { "LZ",        ".lz",      "lzip",  "lzip" },
    [NOLPHIN_ARCHIVE_FORMAT_LZMA]      = { "LZMA",      ".lzma",    "xz",    "xz" },
    [NOLPHIN_ARCHIVE_FORMAT_LZO]       = { "LZO",       ".lzo",     "lzop",  "lzop" },
    [NOLPHIN_ARCHIVE_FORMAT_JAR]       = { "JAR",       ".jar",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_WAR]       = { "WAR",       ".war",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_EAR]       = { "EAR",       ".ear",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_EPUB]      = { "EPUB",      ".epub",    "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_CBZ]       = { "CBZ",       ".cbz",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_AR]        = { "AR",        ".ar",      "ar",    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_TAR_7Z]    = { "TAR.7Z",    ".tar.7z",  "7z",    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_EXE]       = { "EXE",       ".exe",     "7z",    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_CRX]       = { "CRX",       ".crx",     "zip",   "unzip" },
    [NOLPHIN_ARCHIVE_FORMAT_RAR]       = { "RAR",       ".rar",     NULL,    "unrar" },
    [NOLPHIN_ARCHIVE_FORMAT_CAB]       = { "CAB",       ".cab",     NULL,    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_ARJ]       = { "ARJ",       ".arj",     NULL,    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_LZH]       = { "LZH",       ".lzh",     NULL,    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_ISO]       = { "ISO",       ".iso",     "genisoimage", "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_CPIO]      = { "CPIO",      ".cpio",    "cpio",  "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_RPM]       = { "RPM",       ".rpm",     NULL,    "7z" },
    [NOLPHIN_ARCHIVE_FORMAT_DEB]       = { "DEB",       ".deb",     NULL,    "7z" },
};

GQuark
nolphin_archive_error_quark (void)
{
    return g_quark_from_static_string ("nolphin-archive-error-quark");
}

const gchar *
nolphin_archive_format_get_label (NolphinArchiveFormat format)
{
    if (format < 0 || format >= G_N_ELEMENTS (format_info)) {
        return _("Unbekannt");
    }
    return format_info[format].label;
}

const gchar *
nolphin_archive_format_get_extension (NolphinArchiveFormat format)
{
    if (format < 0 || format >= G_N_ELEMENTS (format_info)) {
        return "";
    }
    return format_info[format].extension;
}

static gboolean
tool_is_available (const gchar *tool)
{
    gchar *path = g_find_program_in_path (tool);
    gboolean available = (path != NULL);
    g_free (path);
    return available;
}

gboolean
nolphin_archive_format_can_create (NolphinArchiveFormat format)
{
    if (format < 0 || format >= G_N_ELEMENTS (format_info)) {
        return FALSE;
    }
    return format_info[format].create_tool != NULL;
}

gboolean
nolphin_archive_format_is_single_file (NolphinArchiveFormat format)
{
    return format == NOLPHIN_ARCHIVE_FORMAT_GZ || format == NOLPHIN_ARCHIVE_FORMAT_BZ2 ||
           format == NOLPHIN_ARCHIVE_FORMAT_XZ || format == NOLPHIN_ARCHIVE_FORMAT_ZST ||
           format == NOLPHIN_ARCHIVE_FORMAT_LZ4 || format == NOLPHIN_ARCHIVE_FORMAT_LZ ||
           format == NOLPHIN_ARCHIVE_FORMAT_LZMA || format == NOLPHIN_ARCHIVE_FORMAT_LZO;
}

gboolean
nolphin_archive_format_is_available (NolphinArchiveFormat format)
{
    if (format < 0 || format >= G_N_ELEMENTS (format_info)) {
        return FALSE;
    }

    /* Nur-Lesen-Formate haben kein create_tool - nur die Entpackrichtung
     * muss verfuegbar sein. Fuer erstellbare Formate wie zip/unzip
     * muessen echt beide Richtungen funktionieren (zwei verschiedene
     * Pakete). */
    if (format_info[format].create_tool == NULL) {
        return tool_is_available (format_info[format].extract_tool);
    }

    return tool_is_available (format_info[format].create_tool) &&
           tool_is_available (format_info[format].extract_tool);
}

gboolean
nolphin_archive_format_supports_password (NolphinArchiveFormat format)
{
    return format == NOLPHIN_ARCHIVE_FORMAT_ZIP || format == NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP;
}

gboolean
nolphin_archive_format_supports_split (NolphinArchiveFormat format)
{
    return format == NOLPHIN_ARCHIVE_FORMAT_ZIP || format == NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP;
}

NolphinArchiveFormat
nolphin_archive_detect_format (GFile *archive_file)
{
    gchar *name;
    gchar *lower;
    NolphinArchiveFormat result = NOLPHIN_ARCHIVE_FORMAT_UNKNOWN;

    name = g_file_get_basename (archive_file);
    if (name == NULL) {
        return NOLPHIN_ARCHIVE_FORMAT_UNKNOWN;
    }
    lower = g_ascii_strdown (name, -1);
    g_free (name);

    /* Longest/most specific suffixes first, so ".tar.gz" isn't
     * mis-detected as plain ".gz"/".tar". */
    if (g_str_has_suffix (lower, ".tar.gz") || g_str_has_suffix (lower, ".tgz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_GZ;
    } else if (g_str_has_suffix (lower, ".tar.bz2") || g_str_has_suffix (lower, ".tbz2")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2;
    } else if (g_str_has_suffix (lower, ".tar.xz") || g_str_has_suffix (lower, ".txz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_XZ;
    } else if (g_str_has_suffix (lower, ".tar.zst") || g_str_has_suffix (lower, ".tzst")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_ZST;
    } else if (g_str_has_suffix (lower, ".tar.lz4")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4;
    } else if (g_str_has_suffix (lower, ".tar.7z")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_7Z;
    } else if (g_str_has_suffix (lower, ".tar.lzma")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA;
    } else if (g_str_has_suffix (lower, ".tar.lzo")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_LZO;
    } else if (g_str_has_suffix (lower, ".tar.lz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_LZ;
    } else if (g_str_has_suffix (lower, ".tar.z")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR_Z;
    } else if (g_str_has_suffix (lower, ".tar")) {
        result = NOLPHIN_ARCHIVE_FORMAT_TAR;
    } else if (g_str_has_suffix (lower, ".zip")) {
        result = NOLPHIN_ARCHIVE_FORMAT_ZIP;
    } else if (g_str_has_suffix (lower, ".7z")) {
        result = NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP;
    } else if (g_str_has_suffix (lower, ".gz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_GZ;
    } else if (g_str_has_suffix (lower, ".bz2")) {
        result = NOLPHIN_ARCHIVE_FORMAT_BZ2;
    } else if (g_str_has_suffix (lower, ".xz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_XZ;
    } else if (g_str_has_suffix (lower, ".zst")) {
        result = NOLPHIN_ARCHIVE_FORMAT_ZST;
    } else if (g_str_has_suffix (lower, ".lz4")) {
        result = NOLPHIN_ARCHIVE_FORMAT_LZ4;
    } else if (g_str_has_suffix (lower, ".lzma")) {
        result = NOLPHIN_ARCHIVE_FORMAT_LZMA;
    } else if (g_str_has_suffix (lower, ".lzo")) {
        result = NOLPHIN_ARCHIVE_FORMAT_LZO;
    } else if (g_str_has_suffix (lower, ".lz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_LZ;
    } else if (g_str_has_suffix (lower, ".jar")) {
        result = NOLPHIN_ARCHIVE_FORMAT_JAR;
    } else if (g_str_has_suffix (lower, ".war")) {
        result = NOLPHIN_ARCHIVE_FORMAT_WAR;
    } else if (g_str_has_suffix (lower, ".ear")) {
        result = NOLPHIN_ARCHIVE_FORMAT_EAR;
    } else if (g_str_has_suffix (lower, ".epub")) {
        result = NOLPHIN_ARCHIVE_FORMAT_EPUB;
    } else if (g_str_has_suffix (lower, ".cbz")) {
        result = NOLPHIN_ARCHIVE_FORMAT_CBZ;
    } else if (g_str_has_suffix (lower, ".exe")) {
        result = NOLPHIN_ARCHIVE_FORMAT_EXE;
    } else if (g_str_has_suffix (lower, ".crx")) {
        result = NOLPHIN_ARCHIVE_FORMAT_CRX;
    } else if (g_str_has_suffix (lower, ".ar")) {
        result = NOLPHIN_ARCHIVE_FORMAT_AR;
    } else if (g_str_has_suffix (lower, ".rar")) {
        result = NOLPHIN_ARCHIVE_FORMAT_RAR;
    } else if (g_str_has_suffix (lower, ".cab")) {
        result = NOLPHIN_ARCHIVE_FORMAT_CAB;
    } else if (g_str_has_suffix (lower, ".arj")) {
        result = NOLPHIN_ARCHIVE_FORMAT_ARJ;
    } else if (g_str_has_suffix (lower, ".lzh") || g_str_has_suffix (lower, ".lha")) {
        result = NOLPHIN_ARCHIVE_FORMAT_LZH;
    } else if (g_str_has_suffix (lower, ".iso")) {
        result = NOLPHIN_ARCHIVE_FORMAT_ISO;
    } else if (g_str_has_suffix (lower, ".cpio")) {
        result = NOLPHIN_ARCHIVE_FORMAT_CPIO;
    } else if (g_str_has_suffix (lower, ".rpm")) {
        result = NOLPHIN_ARCHIVE_FORMAT_RPM;
    } else if (g_str_has_suffix (lower, ".deb")) {
        result = NOLPHIN_ARCHIVE_FORMAT_DEB;
    }

    g_free (lower);
    return result;
}

/* --- shared subprocess runner --------------------------------------- */

static void
subprocess_wait_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GSubprocess *subprocess = G_SUBPROCESS (source);
    GTask *task = G_TASK (user_data);
    NolphinToolProgress *progress = nolphin_tool_progress_get (task);
    GError *error = NULL;

    nolphin_tool_progress_set_subprocess (progress, NULL);

    if (!g_subprocess_wait_finish (subprocess, result, &error)) {
        nolphin_tool_progress_set_had_error (progress);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (nolphin_tool_progress_is_cancelled (progress)) {
        /* Abgebrochen: ein halb geschriebenes Archiv nicht liegen lassen
         * (nur wenn es vorher nicht existierte, siehe run_tool_async()). */
        const gchar *cleanup_path = g_object_get_data (G_OBJECT (task), "cleanup-path");

        if (cleanup_path != NULL) {
            g_unlink (cleanup_path);
        }
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                 "%s", _("Vom Benutzer abgebrochen."));
    } else if (g_subprocess_get_successful (subprocess)) {
        g_task_return_boolean (task, TRUE);
    } else {
        gchar *stderr_buf = NULL;
        GInputStream *err_stream = g_subprocess_get_stderr_pipe (subprocess);

        if (err_stream != NULL) {
            /* Best-effort: the pipe was fully drained by the time
             * wait_finish() returned (GSubprocess reads it internally
             * to avoid deadlocks), so this is just picking up what's
             * buffered - may be empty, that's fine. */
            GBytes *bytes = g_input_stream_read_bytes (err_stream, 4096, NULL, NULL);
            if (bytes != NULL) {
                gsize len;
                const gchar *data = g_bytes_get_data (bytes, &len);
                if (len > 0) {
                    stderr_buf = g_strndup (data, len);
                }
                g_bytes_unref (bytes);
            }
        }

        nolphin_tool_progress_set_had_error (progress);
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_TOOL_FAILED,
                                 "%s",
                                 (stderr_buf != NULL && stderr_buf[0] != '\0') ?
                                 stderr_buf : _("Das Archivierungswerkzeug wurde mit einem Fehler beendet."));
        g_free (stderr_buf);
    }

    g_object_unref (task);
}

/* Runs argv[0] (an ABSOLUTE path, already resolved by the caller via
 * g_find_program_in_path so this never silently falls through to a
 * shell PATH lookup) with the given working directory, and reports
 * completion through @task. Takes ownership of @task's one reference.
 *
 * @stdout_path: NULL for the normal case (tool writes its own output
 * file, e.g. tar/zip/7z); otherwise the tool's stdout is redirected
 * into this path - used for the single-file gzip/bzip2/xz/zstd/lz4
 * tools, which write the compressed/decompressed stream to stdout
 * rather than taking a destination-path argument.
 *
 * @status/@details: Text im Fenster „Dateivorgänge“, das bei längeren
 * Vorgängen erscheint (siehe nolphin-tool-progress.h). Dort lässt sich
 * der Vorgang auch anhalten und abbrechen.
 *
 * @output_path: NULL oder die Datei, die der Vorgang neu anlegt; wird
 * beim Abbrechen gelöscht, falls sie vorher noch nicht existierte. */
static void
run_tool_async (gchar        **argv,
                const gchar   *working_directory,
                const gchar   *stdout_path,
                const gchar   *output_path,
                const gchar   *status,
                const gchar   *details,
                GCancellable  *cancellable,
                GTask         *task)
{
    GSubprocessLauncher *launcher;
    GSubprocessFlags flags;
    GSubprocess *subprocess;
    NolphinToolProgress *progress;
    GError *error = NULL;

    if (output_path != NULL && !g_file_test (output_path, G_FILE_TEST_EXISTS)) {
        g_object_set_data_full (G_OBJECT (task), "cleanup-path", g_strdup (output_path), g_free);
    }
    progress = nolphin_tool_progress_attach (task, cancellable, status, details);

    flags = G_SUBPROCESS_FLAGS_STDERR_PIPE;
    flags |= (stdout_path != NULL) ? 0 : G_SUBPROCESS_FLAGS_STDOUT_SILENCE;

    launcher = g_subprocess_launcher_new (flags);
    if (working_directory != NULL) {
        g_subprocess_launcher_set_cwd (launcher, working_directory);
    }
    if (stdout_path != NULL) {
        g_subprocess_launcher_set_stdout_file_path (launcher, stdout_path);
    }

    subprocess = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv, &error);
    g_object_unref (launcher);

    if (subprocess == NULL) {
        nolphin_tool_progress_set_had_error (progress);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Kein Cancellable: beim Abbrechen beendet nolphin-tool-progress.c
     * den Prozess, und erst nach dessen Ende wird der Task beendet. */
    nolphin_tool_progress_set_subprocess (progress, subprocess);
    g_subprocess_wait_async (subprocess, NULL, subprocess_wait_cb, task);
    g_object_unref (subprocess);
}

static gboolean
check_tool_available (NolphinArchiveFormat format, const gchar *tool, GTask *task)
{
    if (tool_is_available (tool)) {
        return TRUE;
    }

    g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_TOOL_NOT_FOUND,
                             _("Das für %s-Archive benötigte Werkzeug »%s« ist nicht installiert."),
                             format_info[format].label, tool);
    g_object_unref (task);
    return FALSE;
}

/* --- compress --------------------------------------------------------- */

void
nolphin_archive_compress_async (GList                *sources,
                                GFile                *destination,
                                NolphinArchiveFormat   format,
                                const gchar           *password,
                                guint                  split_size_mb,
                                GCancellable          *cancellable,
                                GAsyncReadyCallback    callback,
                                gpointer               user_data)
{
    GTask *task;
    GPtrArray *argv;
    GFile *parent;
    gchar *parent_path, *dest_path, *tool_path;
    GList *l;
    gboolean want_password = (password != NULL && *password != '\0');

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, nolphin_archive_compress_async);

    if (sources == NULL) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Nichts zu komprimieren."));
        g_object_unref (task);
        return;
    }

    if (format < 0 || format >= G_N_ELEMENTS (format_info)) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Unbekanntes Archivformat."));
        g_object_unref (task);
        return;
    }

    if (!nolphin_archive_format_can_create (format)) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("%s-Archive können nur entpackt/gelesen werden, nicht erstellt."),
                                 format_info[format].label);
        g_object_unref (task);
        return;
    }

    if (nolphin_archive_format_is_single_file (format) && sources->next != NULL) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("%s komprimiert immer nur genau eine Datei, kein Tar-Container."),
                                 format_info[format].label);
        g_object_unref (task);
        return;
    }

    if (want_password && !nolphin_archive_format_supports_password (format)) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("%s-Archive unterstützen kein Passwort."), format_info[format].label);
        g_object_unref (task);
        return;
    }

    if (split_size_mb > 0 && !nolphin_archive_format_supports_split (format)) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("%s-Archive unterstützen keine Teilarchive."), format_info[format].label);
        g_object_unref (task);
        return;
    }

    if (!check_tool_available (format, format_info[format].create_tool, task)) {
        return;
    }

    /* tar ruft für diese Varianten ein eigenes Kompressionsprogramm auf. */
    if (format == NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4 && !check_tool_available (format, "lz4", task)) {
        return;
    }
    if (format == NOLPHIN_ARCHIVE_FORMAT_TAR_LZ && !check_tool_available (format, "lzip", task)) {
        return;
    }
    if (format == NOLPHIN_ARCHIVE_FORMAT_TAR_LZO && !check_tool_available (format, "lzop", task)) {
        return;
    }

    /* All sources must be siblings: they're passed to the archiver as
     * plain basenames with the shared parent as the working
     * directory, so the archive stores relative paths instead of this
     * machine's absolute filesystem layout. */
    parent = g_file_get_parent (G_FILE (sources->data));
    if (parent == NULL) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Ein Objekt ohne übergeordneten Ordner kann nicht komprimiert werden."));
        g_object_unref (task);
        return;
    }
    for (l = sources->next; l != NULL; l = l->next) {
        GFile *this_parent = g_file_get_parent (G_FILE (l->data));
        gboolean same = (this_parent != NULL && g_file_equal (parent, this_parent));
        g_clear_object (&this_parent);
        if (!same) {
            g_clear_object (&parent);
            g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                     _("Alle ausgewählten Objekte müssen sich im selben Ordner befinden, um gemeinsam komprimiert zu werden."));
            g_object_unref (task);
            return;
        }
    }

    parent_path = g_file_get_path (parent);
    dest_path = g_file_get_path (destination);
    g_clear_object (&parent);

    if (parent_path == NULL || dest_path == NULL) {
        g_free (parent_path);
        g_free (dest_path);
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Entfernte Orte werden für das Archivieren noch nicht unterstützt."));
        g_object_unref (task);
        return;
    }

    tool_path = g_find_program_in_path (format_info[format].create_tool);
    argv = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (argv, tool_path); /* ownership moves to argv */

    switch (format) {
        case NOLPHIN_ARCHIVE_FORMAT_ZIP:
        case NOLPHIN_ARCHIVE_FORMAT_JAR:
        case NOLPHIN_ARCHIVE_FORMAT_WAR:
        case NOLPHIN_ARCHIVE_FORMAT_EAR:
        case NOLPHIN_ARCHIVE_FORMAT_EPUB:
        case NOLPHIN_ARCHIVE_FORMAT_CBZ:
        case NOLPHIN_ARCHIVE_FORMAT_CRX:
            g_ptr_array_add (argv, g_strdup ("-r"));
            if (want_password) {
                g_ptr_array_add (argv, g_strdup ("-P"));
                g_ptr_array_add (argv, g_strdup (password));
            }
            if (split_size_mb > 0) {
                g_ptr_array_add (argv, g_strdup_printf ("-s%um", split_size_mb));
            }
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR:
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_GZ:
            g_ptr_array_add (argv, g_strdup ("-czf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2:
            g_ptr_array_add (argv, g_strdup ("-cjf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_XZ:
            g_ptr_array_add (argv, g_strdup ("-cJf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_ZST:
            /* GNU tar >= 1.31 kennt --zstd direkt, wie -z fuer gzip. */
            g_ptr_array_add (argv, g_strdup ("--zstd"));
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4:
            /* -I <prog> laesst tar ein beliebiges (de)Kompressionsprogramm
             * aufrufen - hier lz4, das eigene "tar.lz4"-Unterstuetzung
             * fehlt tar von Haus aus. */
            g_ptr_array_add (argv, g_strdup ("-I"));
            g_ptr_array_add (argv, g_strdup ("lz4"));
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ:
            g_ptr_array_add (argv, g_strdup ("--lzip"));
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA:
            g_ptr_array_add (argv, g_strdup ("--lzma"));
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZO:
            g_ptr_array_add (argv, g_strdup ("--lzop"));
            g_ptr_array_add (argv, g_strdup ("-cf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_Z:
            g_ptr_array_add (argv, g_strdup ("-Zcf"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_EXE:
            /* 7z-Archiv mit vorangestelltem Selbstentpacker-Modul. */
            g_ptr_array_add (argv, g_strdup ("a"));
            g_ptr_array_add (argv, g_strdup ("-sfx"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_CPIO:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_7Z:
            /* Zwei Programme hintereinander: eine kleine Shell-Pipeline.
             * Zielpfad und Namen werden als Argumente übergeben (nie in den
             * Skripttext eingesetzt), daher keine Shell-Injektion. */
            g_free (g_ptr_array_index (argv, 0));
            g_ptr_array_index (argv, 0) = g_strdup ("/bin/sh");
            g_ptr_array_add (argv, g_strdup ("-c"));
            if (format == NOLPHIN_ARCHIVE_FORMAT_CPIO) {
                g_ptr_array_add (argv, g_strdup ("d=$1; shift; find \"$@\" | cpio -o -H newc -F \"$d\""));
            } else {
                g_ptr_array_add (argv, g_strdup ("d=$1; shift; tar -cf - \"$@\" | 7z a -si -t7z \"$d\" >/dev/null"));
            }
            g_ptr_array_add (argv, g_strdup ("sh"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_AR:
            g_ptr_array_add (argv, g_strdup ("rc"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_ISO:
            g_ptr_array_add (argv, g_strdup ("-r"));
            g_ptr_array_add (argv, g_strdup ("-J"));
            g_ptr_array_add (argv, g_strdup ("-o"));
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP:
            g_ptr_array_add (argv, g_strdup ("a"));
            if (want_password) {
                /* Kein Leerzeichen zwischen -p und Passwort (7z-Syntax).
                 * -mhe=on verschlüsselt zusätzlich die Dateinamen im
                 * Archiv-Header, nicht nur den Inhalt. */
                g_ptr_array_add (argv, g_strconcat ("-p", password, NULL));
                g_ptr_array_add (argv, g_strdup ("-mhe=on"));
            }
            if (split_size_mb > 0) {
                g_ptr_array_add (argv, g_strdup_printf ("-v%um", split_size_mb));
            }
            g_ptr_array_add (argv, g_strdup (dest_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_GZ:
        case NOLPHIN_ARCHIVE_FORMAT_BZ2:
        case NOLPHIN_ARCHIVE_FORMAT_XZ:
        case NOLPHIN_ARCHIVE_FORMAT_ZST:
        case NOLPHIN_ARCHIVE_FORMAT_LZ4:
        case NOLPHIN_ARCHIVE_FORMAT_LZ:
        case NOLPHIN_ARCHIVE_FORMAT_LZMA:
        case NOLPHIN_ARCHIVE_FORMAT_LZO:
            if (format == NOLPHIN_ARCHIVE_FORMAT_LZMA) {
                g_ptr_array_add (argv, g_strdup ("--format=lzma"));
            }
            /* Kein Tar-Container: gzip/bzip2/xz/zstd/lz4 komprimieren
             * genau eine Datei nach stdout (-c), statt selbst eine
             * Zieldatei anzulegen - die Quelldatei bleibt dabei
             * unangetastet erhalten. run_tool_async() unten leitet
             * stdout in den vom Benutzer gewaehlten Zielpfad um. */
            g_ptr_array_add (argv, g_strdup ("-c"));
            break;
        default:
            g_assert_not_reached ();
    }

    for (l = sources; l != NULL; l = l->next) {
        gchar *basename = g_file_get_basename (G_FILE (l->data));
        g_ptr_array_add (argv, basename); /* ownership moves to argv */
    }
    g_ptr_array_add (argv, NULL);

    {
        gchar *dest_name = g_path_get_basename (dest_path);
        gchar *status = g_strdup_printf (_("„%s“ wird erstellt"), dest_name);
        gchar *details = g_strdup_printf (ngettext ("%u Objekt wird als %s komprimiert",
                                                    "%u Objekte werden als %s komprimiert",
                                                    g_list_length (sources)),
                                          g_list_length (sources), format_info[format].label);

        /* Teilarchive (Name.001 …) legt das Werkzeug unter anderen Namen
         * an, darum dort kein Aufräumen über den Zielpfad. */
        run_tool_async ((gchar **) argv->pdata, parent_path,
                        nolphin_archive_format_is_single_file (format) ? dest_path : NULL,
                        split_size_mb > 0 ? NULL : dest_path,
                        status, details, cancellable, task);
        g_free (details);
        g_free (status);
        g_free (dest_name);
    }

    g_ptr_array_free (argv, TRUE);
    g_free (parent_path);
    g_free (dest_path);
}

gboolean
nolphin_archive_compress_finish (GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

/* --- extract ------------------------------------------------------------ */

void
nolphin_archive_extract_async (GFile               *archive_file,
                               GFile               *destination_dir,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
    GTask *task;
    NolphinArchiveFormat format;
    GPtrArray *argv;
    gchar *archive_path, *dest_path, *tool_path, *o_arg;

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, nolphin_archive_extract_async);

    format = nolphin_archive_detect_format (archive_file);
    if (format == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Nicht erkannter Archivtyp."));
        g_object_unref (task);
        return;
    }

    if (!check_tool_available (format, format_info[format].extract_tool, task)) {
        return;
    }

    archive_path = g_file_get_path (archive_file);
    dest_path = g_file_get_path (destination_dir);
    if (archive_path == NULL || dest_path == NULL) {
        g_free (archive_path);
        g_free (dest_path);
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Entfernte Orte werden für das Archivieren noch nicht unterstützt."));
        g_object_unref (task);
        return;
    }

    tool_path = g_find_program_in_path (format_info[format].extract_tool);
    argv = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (argv, tool_path);

    {
        gchar *single_file_out_path = NULL;

        switch (format) {
            case NOLPHIN_ARCHIVE_FORMAT_ZIP:
            case NOLPHIN_ARCHIVE_FORMAT_JAR:
            case NOLPHIN_ARCHIVE_FORMAT_WAR:
            case NOLPHIN_ARCHIVE_FORMAT_EAR:
            case NOLPHIN_ARCHIVE_FORMAT_EPUB:
            case NOLPHIN_ARCHIVE_FORMAT_CBZ:
            case NOLPHIN_ARCHIVE_FORMAT_CRX:
                g_ptr_array_add (argv, g_strdup ("-o")); /* overwrite without prompting */
                g_ptr_array_add (argv, g_strdup (archive_path));
                g_ptr_array_add (argv, g_strdup ("-d"));
                g_ptr_array_add (argv, g_strdup (dest_path));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_TAR:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_GZ:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_XZ:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_LZO:
            case NOLPHIN_ARCHIVE_FORMAT_TAR_Z:
                /* Plain -xf: GNU tar auto-detects the compression, no
                 * need to pick z/j/J again on the way out. */
                g_ptr_array_add (argv, g_strdup ("-xf"));
                g_ptr_array_add (argv, g_strdup (archive_path));
                g_ptr_array_add (argv, g_strdup ("-C"));
                g_ptr_array_add (argv, g_strdup (dest_path));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_TAR_ZST:
                g_ptr_array_add (argv, g_strdup ("--zstd"));
                g_ptr_array_add (argv, g_strdup ("-xf"));
                g_ptr_array_add (argv, g_strdup (archive_path));
                g_ptr_array_add (argv, g_strdup ("-C"));
                g_ptr_array_add (argv, g_strdup (dest_path));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4:
                g_ptr_array_add (argv, g_strdup ("-I"));
                g_ptr_array_add (argv, g_strdup ("lz4"));
                g_ptr_array_add (argv, g_strdup ("-xf"));
                g_ptr_array_add (argv, g_strdup (archive_path));
                g_ptr_array_add (argv, g_strdup ("-C"));
                g_ptr_array_add (argv, g_strdup (dest_path));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP:
            /* "Entpacken / Nur Lesen": 7z kann diese Containerformate
             * auch ohne eigenes Erstellen-Kommando lesen/entpacken - ein
             * einziges, bereits freigegebenes Werkzeug statt sechs neuer
             * Abhängigkeiten (cabextract/arj/lha/...). */
            case NOLPHIN_ARCHIVE_FORMAT_CAB:
            case NOLPHIN_ARCHIVE_FORMAT_ARJ:
            case NOLPHIN_ARCHIVE_FORMAT_LZH:
            case NOLPHIN_ARCHIVE_FORMAT_ISO:
            case NOLPHIN_ARCHIVE_FORMAT_CPIO:
            case NOLPHIN_ARCHIVE_FORMAT_RPM:
            case NOLPHIN_ARCHIVE_FORMAT_DEB:
            case NOLPHIN_ARCHIVE_FORMAT_AR:
            case NOLPHIN_ARCHIVE_FORMAT_EXE:
                g_ptr_array_add (argv, g_strdup ("x"));
                g_ptr_array_add (argv, g_strdup (archive_path));
                o_arg = g_strconcat ("-o", dest_path, NULL); /* 7z requires no space after -o */
                g_ptr_array_add (argv, o_arg);
                g_ptr_array_add (argv, g_strdup ("-y"));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_TAR_7Z:
                g_free (g_ptr_array_index (argv, 0));
                g_ptr_array_index (argv, 0) = g_strdup ("/bin/sh");
                g_ptr_array_add (argv, g_strdup ("-c"));
                g_ptr_array_add (argv, g_strdup ("7z x -so \"$1\" | tar -xf - -C \"$2\""));
                g_ptr_array_add (argv, g_strdup ("sh"));
                g_ptr_array_add (argv, g_strdup (archive_path));
                g_ptr_array_add (argv, g_strdup (dest_path));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_RAR:
                g_ptr_array_add (argv, g_strdup ("x"));
                g_ptr_array_add (argv, g_strdup ("-o+")); /* overwrite without prompting */
                g_ptr_array_add (argv, g_strdup (archive_path));
                /* unrar braucht den abschließenden Trenner, um den
                 * Zielpfad sicher als Verzeichnis zu erkennen. */
                g_ptr_array_add (argv, g_strconcat (dest_path, "/", NULL));
                break;
            case NOLPHIN_ARCHIVE_FORMAT_GZ:
            case NOLPHIN_ARCHIVE_FORMAT_BZ2:
            case NOLPHIN_ARCHIVE_FORMAT_XZ:
            case NOLPHIN_ARCHIVE_FORMAT_ZST:
            case NOLPHIN_ARCHIVE_FORMAT_LZ4:
            case NOLPHIN_ARCHIVE_FORMAT_LZ:
            case NOLPHIN_ARCHIVE_FORMAT_LZMA:
            case NOLPHIN_ARCHIVE_FORMAT_LZO: {
                /* Kein Container: entpackt auf stdout (-dc), Ergebnis
                 * ist genau eine Datei mit demselben Namen ohne die
                 * Formatendung, im Zielordner. */
                gchar *archive_basename = g_path_get_basename (archive_path);
                gchar *out_basename;
                gsize ext_len = strlen (format_info[format].extension);

                if (g_str_has_suffix (archive_basename, format_info[format].extension)) {
                    out_basename = g_strndup (archive_basename, strlen (archive_basename) - ext_len);
                } else {
                    out_basename = g_strdup_printf ("%s.entpackt", archive_basename);
                }
                single_file_out_path = g_build_filename (dest_path, out_basename, NULL);

                g_ptr_array_add (argv, g_strdup ("-dc"));
                g_ptr_array_add (argv, g_strdup (archive_path));

                g_free (out_basename);
                g_free (archive_basename);
                break;
            }
            default:
                g_assert_not_reached ();
        }
        g_ptr_array_add (argv, NULL);

        {
            gchar *archive_name = g_path_get_basename (archive_path);
            gchar *status = g_strdup_printf (_("„%s“ wird entpackt"), archive_name);
            gchar *details = g_strdup_printf (_("nach %s"), dest_path);

            run_tool_async ((gchar **) argv->pdata, NULL, single_file_out_path, single_file_out_path,
                            status, details, cancellable, task);
            g_free (details);
            g_free (status);
            g_free (archive_name);
        }

        g_free (single_file_out_path);
    }

    g_ptr_array_free (argv, TRUE);
    g_free (archive_path);
    g_free (dest_path);
}

gboolean
nolphin_archive_extract_finish (GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

/* --- test ---------------------------------------------------------------- */

void
nolphin_archive_test_async (GFile               *archive_file,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback   callback,
                            gpointer              user_data)
{
    GTask *task;
    NolphinArchiveFormat format;
    GPtrArray *argv;
    gchar *archive_path, *tool_path;

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, nolphin_archive_test_async);

    format = nolphin_archive_detect_format (archive_file);
    if (format == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Nicht erkannter Archivtyp."));
        g_object_unref (task);
        return;
    }

    if (!check_tool_available (format, format_info[format].extract_tool, task)) {
        return;
    }

    archive_path = g_file_get_path (archive_file);
    if (archive_path == NULL) {
        g_task_return_new_error (task, NOLPHIN_ARCHIVE_ERROR, NOLPHIN_ARCHIVE_ERROR_UNKNOWN_FORMAT,
                                 _("Entfernte Orte werden für das Archivieren noch nicht unterstützt."));
        g_object_unref (task);
        return;
    }

    tool_path = g_find_program_in_path (format_info[format].extract_tool);
    argv = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (argv, tool_path);

    switch (format) {
        case NOLPHIN_ARCHIVE_FORMAT_ZIP:
        case NOLPHIN_ARCHIVE_FORMAT_JAR:
        case NOLPHIN_ARCHIVE_FORMAT_WAR:
        case NOLPHIN_ARCHIVE_FORMAT_EAR:
        case NOLPHIN_ARCHIVE_FORMAT_EPUB:
        case NOLPHIN_ARCHIVE_FORMAT_CBZ:
        case NOLPHIN_ARCHIVE_FORMAT_CRX:
            g_ptr_array_add (argv, g_strdup ("-t"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_GZ:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_BZ2:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_XZ:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZMA:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZO:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_Z:
            /* tar has no dedicated integrity-check mode; listing the
             * contents at least fails loudly if the archive is
             * truncated or not actually a tar stream. */
            g_ptr_array_add (argv, g_strdup ("-tf"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_ZST:
            g_ptr_array_add (argv, g_strdup ("--zstd"));
            g_ptr_array_add (argv, g_strdup ("-tf"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_TAR_LZ4:
            g_ptr_array_add (argv, g_strdup ("-I"));
            g_ptr_array_add (argv, g_strdup ("lz4"));
            g_ptr_array_add (argv, g_strdup ("-tf"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_SEVEN_ZIP:
        case NOLPHIN_ARCHIVE_FORMAT_CAB:
        case NOLPHIN_ARCHIVE_FORMAT_ARJ:
        case NOLPHIN_ARCHIVE_FORMAT_LZH:
        case NOLPHIN_ARCHIVE_FORMAT_ISO:
        case NOLPHIN_ARCHIVE_FORMAT_CPIO:
        case NOLPHIN_ARCHIVE_FORMAT_RPM:
        case NOLPHIN_ARCHIVE_FORMAT_DEB:
        case NOLPHIN_ARCHIVE_FORMAT_AR:
        case NOLPHIN_ARCHIVE_FORMAT_TAR_7Z:
        case NOLPHIN_ARCHIVE_FORMAT_EXE:
            g_ptr_array_add (argv, g_strdup ("t"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_RAR:
            g_ptr_array_add (argv, g_strdup ("t"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        case NOLPHIN_ARCHIVE_FORMAT_GZ:
        case NOLPHIN_ARCHIVE_FORMAT_BZ2:
        case NOLPHIN_ARCHIVE_FORMAT_XZ:
        case NOLPHIN_ARCHIVE_FORMAT_ZST:
        case NOLPHIN_ARCHIVE_FORMAT_LZ4:
        case NOLPHIN_ARCHIVE_FORMAT_LZ:
        case NOLPHIN_ARCHIVE_FORMAT_LZMA:
        case NOLPHIN_ARCHIVE_FORMAT_LZO:
            g_ptr_array_add (argv, g_strdup ("-t"));
            g_ptr_array_add (argv, g_strdup (archive_path));
            break;
        default:
            g_assert_not_reached ();
    }
    g_ptr_array_add (argv, NULL);

    {
        gchar *archive_name = g_path_get_basename (archive_path);
        gchar *status = g_strdup_printf (_("„%s“ wird geprüft"), archive_name);

        run_tool_async ((gchar **) argv->pdata, NULL, NULL, NULL,
                        status, format_info[format].label, cancellable, task);
        g_free (status);
        g_free (archive_name);
    }

    g_ptr_array_free (argv, TRUE);
    g_free (archive_path);
}

gboolean
nolphin_archive_test_finish (GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}
