/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-terminal.h: integrated VTE terminal panel (F4)
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

#ifndef NOLPHIN_TERMINAL_H
#define NOLPHIN_TERMINAL_H

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define NOLPHIN_TYPE_TERMINAL (nolphin_terminal_get_type ())
G_DECLARE_FINAL_TYPE (NolphinTerminal, nolphin_terminal, NOLPHIN, TERMINAL, GtkBox)

GtkWidget *nolphin_terminal_new (void);
GtkWidget *nolphin_terminal_new_at (const gchar *directory);
const gchar *nolphin_terminal_get_current_directory (NolphinTerminal *terminal);
void nolphin_terminal_clear (NolphinTerminal *terminal);

/* Changes the shell's working directory to match the file manager's
 * current location. If the shell is currently running a foreground
 * command, the change is skipped rather than interrupting it - the
 * next navigation (or an explicit call once the shell is idle again)
 * will catch up. Locations without a local path (e.g. a remote URI
 * that isn't FUSE-mounted) are silently ignored, since a local shell
 * has no path to cd into. */
void nolphin_terminal_set_location (NolphinTerminal *terminal,
                                     GFile           *location);

gboolean nolphin_terminal_is_shell_busy (NolphinTerminal *terminal);

void nolphin_terminal_grab_focus (NolphinTerminal *terminal);

/* Für die Menüleiste über dem Terminal-Panel (Bearbeiten/Ansicht/Terminal). */
void nolphin_terminal_copy (NolphinTerminal *terminal);
void nolphin_terminal_copy_html (NolphinTerminal *terminal);
void nolphin_terminal_paste (NolphinTerminal *terminal);
void nolphin_terminal_select_all (NolphinTerminal *terminal);
void nolphin_terminal_zoom_in (NolphinTerminal *terminal);
void nolphin_terminal_zoom_out (NolphinTerminal *terminal);
void nolphin_terminal_zoom_reset (NolphinTerminal *terminal);
void nolphin_terminal_reset (NolphinTerminal *terminal);

/* Ab hier: echte VTE-Einstellungen (libvte-2.91, GTK3) - jede Funktion
 * ruft eine tatsächlich vorhandene vte_terminal_*()-API auf, keine
 * Attrappen mehr. */

double   nolphin_terminal_get_font_size (NolphinTerminal *terminal);
void     nolphin_terminal_set_font_size (NolphinTerminal *terminal, double size);

const gchar *nolphin_terminal_get_font_family (NolphinTerminal *terminal);
void         nolphin_terminal_set_font_family (NolphinTerminal *terminal, const gchar *family);

typedef enum {
    NOLPHIN_CURSOR_SHAPE_BLOCK = 0,
    NOLPHIN_CURSOR_SHAPE_UNDERLINE = 1,
    NOLPHIN_CURSOR_SHAPE_IBEAM = 2
} NolphinCursorShape;

NolphinCursorShape nolphin_terminal_get_cursor_shape (NolphinTerminal *terminal);
void               nolphin_terminal_set_cursor_shape (NolphinTerminal *terminal, NolphinCursorShape shape);

/* VTE kennt fuer die Eingabemarke wirklich drei Zustaende (SYSTEM = der
 * GTK-Systemeinstellung folgen) - "Vorgabe" in der Referenz ist also kein
 * erfundener Alias mehr, sondern ein echter dritter Zustand. */
typedef enum {
    NOLPHIN_CURSOR_BLINK_SYSTEM = 0,
    NOLPHIN_CURSOR_BLINK_ALWAYS = 1,
    NOLPHIN_CURSOR_BLINK_NEVER = 2
} NolphinCursorBlinkMode;

NolphinCursorBlinkMode nolphin_terminal_get_cursor_blink_mode (NolphinTerminal *terminal);
void                   nolphin_terminal_set_cursor_blink_mode (NolphinTerminal *terminal, NolphinCursorBlinkMode mode);

/* Zwei kompakte Vorgabe-Farbschemata (Standard/Solarisiert) - setzen
 * Vorder-/Hintergrund und die 8-Farb-Palette auf einen Schlag
 * (vte_terminal_set_colors()). Die einzelnen Overrides unten wirken
 * unabhaengig davon zusaetzlich. */
gboolean nolphin_terminal_get_solarized (NolphinTerminal *terminal);
void     nolphin_terminal_set_solarized (NolphinTerminal *terminal, gboolean solarized);

gboolean nolphin_terminal_get_custom_default_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg);
void     nolphin_terminal_set_custom_default_colors (NolphinTerminal *terminal, gboolean enabled,
                                                      const GdkRGBA *fg, const GdkRGBA *bg);

/* "Farben vom System-Thema verwenden": liest Vorder-/Hintergrund aus dem
 * aktiven GTK-Theme (GtkStyleContext des Terminal-Widgets) statt einer
 * der beiden Paletten - hat Vorrang vor der eigenen Standardfarbe oben. */
gboolean nolphin_terminal_get_use_system_colors (NolphinTerminal *terminal);
void     nolphin_terminal_set_use_system_colors (NolphinTerminal *terminal, gboolean use_system);

/* "Fetten Text auch in helleren Farben darstellen" - echter VTE-Schalter. */
gboolean nolphin_terminal_get_bold_is_bright (NolphinTerminal *terminal);
void     nolphin_terminal_set_bold_is_bright (NolphinTerminal *terminal, gboolean bright);

/* Frei editierbare 8er-Palette (Index 0-7): startet mit den Werten des
 * gewaehlten Schemas (Standard/Solarisiert), bleibt nach einer Aenderung
 * bis zum naechsten Schemawechsel eigenstaendig bestehen. */
void nolphin_terminal_get_palette_color (NolphinTerminal *terminal, int index, GdkRGBA *color);
void nolphin_terminal_set_palette_color (NolphinTerminal *terminal, int index, const GdkRGBA *color);

gboolean nolphin_terminal_get_custom_bold_color (NolphinTerminal *terminal, GdkRGBA *fg);
void     nolphin_terminal_set_custom_bold_color (NolphinTerminal *terminal, gboolean enabled, const GdkRGBA *fg);

gboolean nolphin_terminal_get_custom_cursor_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg);
void     nolphin_terminal_set_custom_cursor_colors (NolphinTerminal *terminal, gboolean enabled,
                                                     const GdkRGBA *fg, const GdkRGBA *bg);

gboolean nolphin_terminal_get_custom_highlight_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg);
void     nolphin_terminal_set_custom_highlight_colors (NolphinTerminal *terminal, gboolean enabled,
                                                        const GdkRGBA *fg, const GdkRGBA *bg);

/* Ruecktaste/Entfernen-Taste: alle fuenf echten VTE-Bindungen
 * (vte_terminal_set_backspace_binding()/set_delete_binding()). */
typedef enum {
    NOLPHIN_ERASE_AUTO = 0,
    NOLPHIN_ERASE_ASCII_BACKSPACE = 1,
    NOLPHIN_ERASE_ASCII_DELETE = 2,
    NOLPHIN_ERASE_DELETE_SEQUENCE = 3,
    NOLPHIN_ERASE_TTY = 4
} NolphinEraseBinding;

NolphinEraseBinding nolphin_terminal_get_backspace_binding (NolphinTerminal *terminal);
void                nolphin_terminal_set_backspace_binding (NolphinTerminal *terminal, NolphinEraseBinding binding);
NolphinEraseBinding nolphin_terminal_get_delete_binding (NolphinTerminal *terminal);
void                nolphin_terminal_set_delete_binding (NolphinTerminal *terminal, NolphinEraseBinding binding);

/* Zeichen mit unbekannter Breite (CJK Ambiguous Width): Schmal (Vorgabe)
 * oder Breit - vte_terminal_set_cjk_ambiguous_width(). */
gboolean nolphin_terminal_get_ambiguous_width_wide (NolphinTerminal *terminal);
void     nolphin_terminal_set_ambiguous_width_wide (NolphinTerminal *terminal, gboolean wide);

/* Zellenabstand (x Breite / x Hoehe) - vte_terminal_set_cell_*_scale(). */
double   nolphin_terminal_get_cell_width_scale (NolphinTerminal *terminal);
double   nolphin_terminal_get_cell_height_scale (NolphinTerminal *terminal);
void     nolphin_terminal_set_cell_spacing (NolphinTerminal *terminal, double width_scale, double height_scale);

/* Bildlauf: wirkt sofort auf die laufende Sitzung. */
void     nolphin_terminal_set_scrollback_lines (NolphinTerminal *terminal, int lines);
gboolean nolphin_terminal_get_scroll_on_output (NolphinTerminal *terminal);
void     nolphin_terminal_set_scroll_on_output (NolphinTerminal *terminal, gboolean scroll);
gboolean nolphin_terminal_get_scroll_on_keystroke (NolphinTerminal *terminal);
void     nolphin_terminal_set_scroll_on_keystroke (NolphinTerminal *terminal, gboolean scroll);
gboolean nolphin_terminal_get_scroll_on_paste (NolphinTerminal *terminal);
void     nolphin_terminal_set_scroll_on_paste (NolphinTerminal *terminal, gboolean scroll);
gboolean nolphin_terminal_get_show_scrollbar (NolphinTerminal *terminal);
void     nolphin_terminal_set_show_scrollbar (NolphinTerminal *terminal, gboolean show);

/* Befehl: wirkt erst auf die naechste gestartete Shell (nach einem
 * Neustart ueber "Wenn Befehl beendet", nicht rueckwirkend). */
gboolean nolphin_terminal_get_login_shell (NolphinTerminal *terminal);
void     nolphin_terminal_set_login_shell (NolphinTerminal *terminal, gboolean login_shell);
gboolean nolphin_terminal_get_use_custom_command (NolphinTerminal *terminal);
const gchar *nolphin_terminal_get_custom_command (NolphinTerminal *terminal);
void     nolphin_terminal_set_custom_command (NolphinTerminal *terminal, gboolean use_custom, const gchar *command);

/* Blinkenden Text erlauben. */
gboolean nolphin_terminal_get_text_blink (NolphinTerminal *terminal);
void     nolphin_terminal_set_text_blink (NolphinTerminal *terminal, gboolean enabled);

/* Terminalglocke. */
gboolean nolphin_terminal_get_bell_enabled (NolphinTerminal *terminal);
void     nolphin_terminal_set_bell_enabled (NolphinTerminal *terminal, gboolean enabled);

/* Anfaengliche Groesse (Spalten/Zeilen) - wirkt sofort. */
void     nolphin_terminal_set_initial_size (NolphinTerminal *terminal, int cols, int rows);

/* "Arbeitsordner beibehalten": wenn FALSE, ignoriert
 * nolphin_terminal_set_location() Ordnerwechsel im Dateimanager (kein
 * automatisches "cd" mehr). Vorgabe TRUE. */
gboolean nolphin_terminal_get_follow_location (NolphinTerminal *terminal);
void     nolphin_terminal_set_follow_location (NolphinTerminal *terminal, gboolean follow);

/* "Wenn Befehl beendet": HOLD (Vorgabe, Bildschirm bleibt stehen) oder
 * RESTART (echter Neustart der Shell/des Befehls). */
typedef enum {
    NOLPHIN_TERMINAL_EXIT_HOLD = 0,
    NOLPHIN_TERMINAL_EXIT_RESTART = 1
} NolphinTerminalExitAction;

NolphinTerminalExitAction nolphin_terminal_get_exit_action (NolphinTerminal *terminal);
void                      nolphin_terminal_set_exit_action (NolphinTerminal *terminal, NolphinTerminalExitAction action);

/* "Nur lesen": schaltet Tastatureingaben fuer die Shell ab (vte_terminal_
 * set_input_enabled()) - Rechtsklick-Kontextmenue-Eintrag, wie im
 * Referenz-Terminal. */
gboolean nolphin_terminal_get_read_only (NolphinTerminal *terminal);
void     nolphin_terminal_set_read_only (NolphinTerminal *terminal, gboolean read_only);

/* Signal "settings-requested": wird ausgeloest, wenn der Benutzer im
 * Rechtsklick-Kontextmenue "Einstellungen …" waehlt. Der aufrufende Code
 * (nolphin-workspace-panel.c) verbindet sich darauf, um denselben Dialog
 * wie aus der Bearbeiten-Menueleiste zu oeffnen - das Terminal-Widget
 * selbst kennt den Dialog nicht (saubere Schichtentrennung). */

G_END_DECLS

#endif /* NOLPHIN_TERMINAL_H */
