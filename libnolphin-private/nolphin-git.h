/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-git.h: git integration via the system's git tool
 *
 * Status, add, commit, pull, push, log, diff - all through GSubprocess
 * (argv arrays, never a shell string). Every operation is async;
 * pull/push touch the network and must never block the GUI, and the
 * others follow the same pattern for consistency.
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

#ifndef NOLPHIN_GIT_H
#define NOLPHIN_GIT_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define NOLPHIN_GIT_ERROR (nolphin_git_error_quark ())
GQuark nolphin_git_error_quark (void);

typedef enum {
    NOLPHIN_GIT_ERROR_TOOL_NOT_FOUND,
    NOLPHIN_GIT_ERROR_REMOTE_FILE,
    NOLPHIN_GIT_ERROR_NOT_A_REPOSITORY,
    NOLPHIN_GIT_ERROR_TOOL_FAILED
} NolphinGitError;

typedef enum {
    NOLPHIN_GIT_STATUS_UNMODIFIED,
    NOLPHIN_GIT_STATUS_MODIFIED,
    NOLPHIN_GIT_STATUS_STAGED,
    NOLPHIN_GIT_STATUS_ADDED,
    NOLPHIN_GIT_STATUS_DELETED,
    NOLPHIN_GIT_STATUS_RENAMED,
    NOLPHIN_GIT_STATUS_UNTRACKED,
    NOLPHIN_GIT_STATUS_IGNORED,
    NOLPHIN_GIT_STATUS_CONFLICTED
} NolphinGitFileStatus;

const gchar *nolphin_git_status_get_label (NolphinGitFileStatus status);

gboolean nolphin_git_is_available (void);

/* Fast, synchronous, filesystem-only check (just walks up looking for
 * a ".git" entry - no subprocess) for whether @file is plausibly
 * inside a git repository. Meant for menu-visibility decisions in the
 * UI's synchronous menu-update path, NOT as a substitute for
 * nolphin_git_find_repository_root_async() before actually running a
 * git command - a corrupt/unusual repo could pass this heuristic and
 * still fail the real git call, which is handled normally there. */
gboolean nolphin_git_directory_looks_like_repository (GFile *file);

/* Finds the top-level directory of the git repository @file is in
 * (or is), walking upward - NULL (no error) if it simply isn't
 * inside a repository, which is the ordinary, expected case for most
 * folders and not treated as a failure. */
void   nolphin_git_find_repository_root_async  (GFile               *file,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data);
GFile *nolphin_git_find_repository_root_finish  (GAsyncResult *result, GError **error);

/* Status of every changed/untracked/ignored path in @repo_root,
 * relative to it. Returns a newly-allocated GHashTable mapping a
 * newly-allocated relative path (gchar*, owned by the table) to a
 * NolphinGitFileStatus (GINT_TO_POINTER) - paths not present in the
 * table are unmodified/up to date. Free with g_hash_table_unref(). */
void          nolphin_git_get_status_async  (GFile               *repo_root,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data);
GHashTable   *nolphin_git_get_status_finish (GAsyncResult *result, GError **error);

/* `git add -- <paths>` (paths relative to nothing in particular -
 * absolute or repo-root-relative both work since git resolves them
 * itself; pass real GFile paths). */
void     nolphin_git_add_async      (GFile *repo_root, GList *files,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback, gpointer user_data);
gboolean nolphin_git_add_finish     (GAsyncResult *result, GError **error);

void     nolphin_git_commit_async   (GFile *repo_root, const gchar *message,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback, gpointer user_data);
gboolean nolphin_git_commit_finish  (GAsyncResult *result, GError **error);

/* Wie commit, merkt aber alle Änderungen selbst vor, wenn noch nichts
 * vorgemerkt ist. Liefert eine Zusammenfassung (Hash, Titel, Dateien). */
void     nolphin_git_commit_full_async  (GFile *repo_root, const gchar *message,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback, gpointer user_data);
gchar   *nolphin_git_commit_full_finish (GAsyncResult *result, GError **error);

void     nolphin_git_pull_async     (GFile *repo_root,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback, gpointer user_data);
/* Returns git's own combined output (stdout+stderr) on success too -
 * pull/push are usually silent on real success, and any real message
 * (fast-forward summary, "Already up to date.", conflicts, ...) is
 * worth showing rather than just a generic "done". */
gchar   *nolphin_git_pull_finish    (GAsyncResult *result, GError **error);

void     nolphin_git_push_async     (GFile *repo_root,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback, gpointer user_data);
gchar   *nolphin_git_push_finish    (GAsyncResult *result, GError **error);

/* `git remote add <name> <url>` - fails with git's own error text if a
 * remote by that name already exists (e.g. "remote origin already
 * exists."), which is surfaced as-is rather than silently overwritten. */
void     nolphin_git_remote_add_async   (GFile *repo_root, const gchar *name, const gchar *url,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback, gpointer user_data);
gboolean nolphin_git_remote_add_finish  (GAsyncResult *result, GError **error);

/* Liest die eingetragenen Remotes (`git remote -v`, jeweils die Fetch-
 * Adresse). Ergebnis: GHashTable Name -> URL (beides gchar*, von der
 * Tabelle besessen, mit g_hash_table_unref() freigeben). Leere Tabelle,
 * wenn noch kein Remote eingetragen ist. */
void        nolphin_git_remote_list_async  (GFile *repo_root,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback, gpointer user_data);
GHashTable *nolphin_git_remote_list_finish (GAsyncResult *result, GError **error);

/* `git remote set-url <name> <url>` - ändert die Adresse eines bereits
 * vorhandenen Remotes (schlägt mit git's Meldung fehl, wenn es ihn nicht gibt). */
void     nolphin_git_remote_set_url_async   (GFile *repo_root, const gchar *name, const gchar *url,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback, gpointer user_data);
gboolean nolphin_git_remote_set_url_finish  (GAsyncResult *result, GError **error);

/* Abgleichen (@apply = FALSE): `git fetch`, danach Vergleich mit dem
 * Server ("x lokal neu, y auf dem Server neu"), ohne etwas zu verändern.
 * Synchronisieren (@apply = TRUE): fetch, Pull und anschließend Push.
 * Liefert in beiden Fällen einen verständlichen Ergebnistext. */
void     nolphin_git_sync_async    (GFile *repo_root, gboolean apply,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback, gpointer user_data);
gchar   *nolphin_git_sync_finish   (GAsyncResult *result, GError **error);

/* `git clone <url>` in @parent_dir; git legt den Zielordner selbst nach
 * dem Repository-Namen an. Fragt nie nach Zugangsdaten (siehe spawn_git()),
 * private Repositories brauchen daher hinterlegte Zugangsdaten oder SSH. */
void     nolphin_git_clone_async   (GFile *parent_dir, const gchar *url,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback, gpointer user_data);
gboolean nolphin_git_clone_finish  (GAsyncResult *result, GError **error);

/* @path: NULL for the whole repository, or a single file/folder to
 * restrict to. Returns the formatted log text. */
void   nolphin_git_log_async   (GFile *repo_root, GFile *path, guint max_count,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback, gpointer user_data);
gchar *nolphin_git_log_finish  (GAsyncResult *result, GError **error);

/* @path: NULL for the whole repository, or a single file/folder. */
void   nolphin_git_diff_async  (GFile *repo_root, GFile *path,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback, gpointer user_data);
gchar *nolphin_git_diff_finish (GAsyncResult *result, GError **error);

G_END_DECLS

#endif /* NOLPHIN_GIT_H */
