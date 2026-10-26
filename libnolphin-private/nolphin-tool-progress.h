/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-tool-progress.h: Fortschrittsanzeige für externe Werkzeuge
 *
 * Archiv- und Git-Vorgänge laufen über externe Programme (tar, zip, 7z,
 * git …) statt über nolphin-file-operations.c. Damit sie trotzdem im
 * Fenster „Dateivorgänge“ (nolphin-progress-ui-handler.c) samt
 * Statusleisten-Symbol und Abschlussmeldung erscheinen, legt dieses Modul
 * für einen GTask ein NolphinProgressInfo an:
 *
 *   - Anzeige: Pulsbalken (die Werkzeuge melden keinen verlässlichen
 *     Prozentwert); wie bei Kopiervorgängen erscheint das Fenster erst,
 *     wenn der Vorgang länger als zwei Sekunden dauert.
 *   - Pause: hält den angemeldeten Kindprozess mit SIGSTOP an, „Fortsetzen“
 *     schickt SIGCONT.
 *   - Abbrechen: beendet den angemeldeten Kindprozess (SIGKILL).
 *
 * Der Vorgang gilt als beendet, sobald der GTask freigegeben wird.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef NOLPHIN_TOOL_PROGRESS_H
#define NOLPHIN_TOOL_PROGRESS_H

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _NolphinToolProgress NolphinToolProgress;

/* Nur im Hauptthread aufrufen. @cancellable ist das des Aufrufers (darf
 * NULL sein); wird es abgebrochen, bricht auch der Vorgang ab. Gibt eine
 * geliehene Referenz zurück, die so lange gilt wie @task. */
NolphinToolProgress *nolphin_tool_progress_attach          (GTask        *task,
                                                            GCancellable *cancellable,
                                                            const gchar  *status,
                                                            const gchar  *details);

/* NULL, wenn an @task keine Fortschrittsanzeige hängt. */
NolphinToolProgress *nolphin_tool_progress_get             (GTask        *task);

/* Alle folgenden Funktionen sind threadsicher und akzeptieren NULL. */

/* Abbrechen-Knopf im Fenster oder das Cancellable des Aufrufers. */
GCancellable        *nolphin_tool_progress_get_cancellable (NolphinToolProgress *progress);
gboolean             nolphin_tool_progress_is_cancelled    (NolphinToolProgress *progress);

/* Meldet den gerade laufenden Kindprozess an (NULL meldet ihn ab). Wurde
 * der Vorgang schon abgebrochen, wird der Prozess sofort beendet. */
void                 nolphin_tool_progress_set_subprocess  (NolphinToolProgress *progress,
                                                            GSubprocess         *subprocess);

/* Für die Abschlussmeldung „Nicht alle Dateioperationen …“. */
void                 nolphin_tool_progress_set_had_error   (NolphinToolProgress *progress);

/* Fortschrittsanzeige des Vorgangs, der im aktuellen Worker-Thread läuft
 * (für synchrone Hilfsfunktionen, die den GTask nicht kennen). Am Ende
 * des Threads mit NULL zurücksetzen. */
void                 nolphin_tool_progress_set_thread      (NolphinToolProgress *progress);
NolphinToolProgress *nolphin_tool_progress_get_thread      (void);

G_END_DECLS

#endif /* NOLPHIN_TOOL_PROGRESS_H */
