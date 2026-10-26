/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-tool-progress.c: Fortschrittsanzeige für externe Werkzeuge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <config.h>

#include "nolphin-tool-progress.h"
#include "nolphin-progress-info.h"

#include <signal.h>

#define TASK_DATA_KEY "nolphin-tool-progress"
#define PULSE_INTERVAL_MS 250

struct _NolphinToolProgress {
    gint ref_count;
    GMutex lock;

    NolphinProgressInfo *info;
    GCancellable *cancellable;          /* das des NolphinProgressInfo */
    gulong cancel_id;
    GCancellable *caller_cancellable;
    gulong caller_cancel_id;

    /* geschützt durch lock */
    GSubprocess *subprocess;
    gboolean stopped;                   /* SIGSTOP geschickt */
    gboolean finished;
};

static GPrivate thread_progress;

static NolphinToolProgress *
tool_progress_ref (NolphinToolProgress *progress)
{
    g_atomic_int_inc (&progress->ref_count);
    return progress;
}

static void
tool_progress_unref (NolphinToolProgress *progress)
{
    if (!g_atomic_int_dec_and_test (&progress->ref_count)) {
        return;
    }

    g_clear_object (&progress->subprocess);
    g_clear_object (&progress->caller_cancellable);
    g_clear_object (&progress->cancellable);
    g_clear_object (&progress->info);
    g_mutex_clear (&progress->lock);
    g_free (progress);
}

/* Läuft in dem Thread, der abbricht - bei nolphin_progress_info_cancel()
 * mit gehaltener Sperre des NolphinProgressInfo, darum hier nur den
 * Kindprozess anfassen. */
static void
on_cancelled (GCancellable *cancellable, NolphinToolProgress *progress)
{
    g_mutex_lock (&progress->lock);
    if (progress->subprocess != NULL) {
        g_subprocess_force_exit (progress->subprocess);
    }
    g_mutex_unlock (&progress->lock);
}

static void
on_caller_cancelled (GCancellable *cancellable, NolphinToolProgress *progress)
{
    nolphin_progress_info_cancel (progress->info);
}

static gboolean
pulse_cb (gpointer user_data)
{
    NolphinToolProgress *progress = user_data;
    gboolean paused;

    g_mutex_lock (&progress->lock);
    if (progress->finished) {
        g_mutex_unlock (&progress->lock);
        return G_SOURCE_REMOVE;
    }

    paused = nolphin_progress_info_get_is_paused (progress->info);
    if (progress->subprocess != NULL && paused != progress->stopped) {
        g_subprocess_send_signal (progress->subprocess, paused ? SIGSTOP : SIGCONT);
        progress->stopped = paused;
    }
    g_mutex_unlock (&progress->lock);

    /* Nicht im pausierten Zustand pulsen: pulse_progress() wartet dann,
     * bis fortgesetzt wird, und würde die Oberfläche blockieren. */
    if (!paused) {
        nolphin_progress_info_pulse_progress (progress->info);
    }

    return G_SOURCE_CONTINUE;
}

/* Wird beim Freigeben des GTask aufgerufen - in dem Thread, der die
 * letzte Referenz abgibt. */
static void
tool_progress_finish (NolphinToolProgress *progress)
{
    g_mutex_lock (&progress->lock);
    progress->finished = TRUE;
    g_clear_object (&progress->subprocess);
    g_mutex_unlock (&progress->lock);

    g_cancellable_disconnect (progress->cancellable, progress->cancel_id);
    if (progress->caller_cancellable != NULL) {
        g_cancellable_disconnect (progress->caller_cancellable, progress->caller_cancel_id);
    }

    nolphin_progress_info_finish (progress->info);
    tool_progress_unref (progress);
}

NolphinToolProgress *
nolphin_tool_progress_attach (GTask        *task,
                              GCancellable *cancellable,
                              const gchar  *status,
                              const gchar  *details)
{
    NolphinToolProgress *progress;

    g_return_val_if_fail (G_IS_TASK (task), NULL);

    progress = g_new0 (NolphinToolProgress, 1);
    progress->ref_count = 1;
    g_mutex_init (&progress->lock);

    progress->info = nolphin_progress_info_new ();
    nolphin_progress_info_set_status (progress->info, status);
    nolphin_progress_info_set_details (progress->info, details != NULL ? details : "");
    progress->cancellable = nolphin_progress_info_get_cancellable (progress->info);
    progress->cancel_id = g_cancellable_connect (progress->cancellable,
                                                 G_CALLBACK (on_cancelled), progress, NULL);

    if (cancellable != NULL) {
        progress->caller_cancellable = g_object_ref (cancellable);
        progress->caller_cancel_id = g_cancellable_connect (cancellable,
                                                            G_CALLBACK (on_caller_cancelled),
                                                            progress, NULL);
    }

    g_object_set_data_full (G_OBJECT (task), TASK_DATA_KEY, progress,
                            (GDestroyNotify) tool_progress_finish);

    /* Externe Werkzeuge warten nicht in der Warteschlange der
     * Dateivorgänge, sie laufen sofort. */
    nolphin_progress_info_queue (progress->info);
    nolphin_progress_info_start (progress->info);

    g_timeout_add_full (G_PRIORITY_DEFAULT, PULSE_INTERVAL_MS, pulse_cb,
                        tool_progress_ref (progress), (GDestroyNotify) tool_progress_unref);

    return progress;
}

NolphinToolProgress *
nolphin_tool_progress_get (GTask *task)
{
    return g_object_get_data (G_OBJECT (task), TASK_DATA_KEY);
}

GCancellable *
nolphin_tool_progress_get_cancellable (NolphinToolProgress *progress)
{
    return progress != NULL ? progress->cancellable : NULL;
}

gboolean
nolphin_tool_progress_is_cancelled (NolphinToolProgress *progress)
{
    return progress != NULL && g_cancellable_is_cancelled (progress->cancellable);
}

void
nolphin_tool_progress_set_subprocess (NolphinToolProgress *progress,
                                      GSubprocess         *subprocess)
{
    if (progress == NULL) {
        return;
    }

    g_mutex_lock (&progress->lock);
    g_clear_object (&progress->subprocess);
    progress->stopped = FALSE;
    if (subprocess != NULL && !progress->finished) {
        progress->subprocess = g_object_ref (subprocess);
        if (g_cancellable_is_cancelled (progress->cancellable)) {
            g_subprocess_force_exit (subprocess);
        }
    }
    g_mutex_unlock (&progress->lock);
}

void
nolphin_tool_progress_set_had_error (NolphinToolProgress *progress)
{
    if (progress != NULL) {
        nolphin_progress_info_set_had_error (progress->info);
    }
}

void
nolphin_tool_progress_set_thread (NolphinToolProgress *progress)
{
    g_private_set (&thread_progress, progress);
}

NolphinToolProgress *
nolphin_tool_progress_get_thread (void)
{
    return g_private_get (&thread_progress);
}
