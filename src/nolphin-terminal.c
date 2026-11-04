/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-terminal.c: integrated VTE terminal panel (F4)
 *
 * Nutzt die echte VTE-Bibliothek (libvte-2.91, GTK3-Variante) statt eines
 * selbstgebauten Terminal-Kerns - robuste, ausgereifte Terminal-Emulation
 * statt einer Eigenentwicklung. Alle Einstellungen werden ueber GSettings
 * (Schema org.nolphin.terminal) dauerhaft gespeichert und beim naechsten
 * Start wieder geladen.
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

#include "nolphin-terminal.h"

#include <vte/vte.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>

struct _NolphinTerminal
{
    GtkBox parent_instance;

    GtkWidget *vte;        /* VteTerminal* */
    GSettings *settings;   /* org.nolphin.terminal - persistiert alles unten */
    gboolean loading;      /* TRUE waehrend load_settings(): Setter schreiben dann nicht zurueck */
    GPid child_pid;
    gchar *cwd;
    gchar *last_cwd;

    gboolean login_shell;
    gboolean use_custom_command;
    gchar *custom_command;
    gboolean follow_location;
    NolphinTerminalExitAction exit_action;

    gboolean solarized;
    gboolean custom_default_colors_enabled;
    GdkRGBA custom_default_fg;
    GdkRGBA custom_default_bg;
    gboolean use_system_colors;

    gboolean custom_bold_color_enabled;
    GdkRGBA custom_bold_fg;
    gboolean custom_cursor_colors_enabled;
    GdkRGBA custom_cursor_fg;
    GdkRGBA custom_cursor_bg;
    gboolean custom_highlight_colors_enabled;
    GdkRGBA custom_highlight_fg;
    GdkRGBA custom_highlight_bg;

    NolphinEraseBinding backspace_binding;
    NolphinEraseBinding delete_binding;

    gboolean palette_customized;
    GdkRGBA custom_palette[16];
};

enum {
    SIGNAL_SETTINGS_REQUESTED,
    N_SIGNALS
};
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (NolphinTerminal, nolphin_terminal, GTK_TYPE_BOX)

/* Zwei echte 16-Farb-Paletten (8 Standard- + 8 "helle" ANSI-Farben) fuer
 * "Farbschema: Standard/Solarisiert" - dieselben Werte, die GNOME Terminal
 * als "GNOME dark" bzw. "Solarized dark" verwendet. */
static const GdkRGBA palette_standard[16] = {
    {0.118,0.118,0.118,1.0},{0.753,0.110,0.157,1.0},{0.149,0.635,0.412,1.0},{0.635,0.451,0.298,1.0},
    {0.071,0.282,0.545,1.0},{0.639,0.278,0.729,1.0},{0.165,0.631,0.702,1.0},{0.812,0.812,0.812,1.0},
    {0.365,0.365,0.365,1.0},{0.965,0.380,0.318,1.0},{0.200,0.820,0.478,1.0},{0.914,0.678,0.047,1.0},
    {0.165,0.482,0.871,1.0},{0.753,0.380,0.796,1.0},{0.200,0.780,0.871,1.0},{1.000,1.000,1.000,1.0}
};
static const GdkRGBA palette_solarized[16] = {
    {0.027,0.212,0.259,1.0},{0.863,0.196,0.184,1.0},{0.522,0.600,0.000,1.0},{0.710,0.537,0.000,1.0},
    {0.149,0.545,0.824,1.0},{0.827,0.212,0.510,1.0},{0.165,0.631,0.596,1.0},{0.933,0.910,0.835,1.0},
    {0.000,0.169,0.212,1.0},{0.796,0.294,0.086,1.0},{0.345,0.431,0.459,1.0},{0.396,0.482,0.514,1.0},
    {0.514,0.580,0.588,1.0},{0.424,0.443,0.769,1.0},{0.576,0.631,0.631,1.0},{0.992,0.965,0.890,1.0}
};
static const GdkRGBA solarized_bg = {0.000,0.169,0.212,1.0};
static const GdkRGBA solarized_fg = {0.514,0.580,0.588,1.0};
static const GdkRGBA standard_bg = {0.118,0.118,0.118,1.0};
static const GdkRGBA standard_fg = {0.812,0.812,0.812,1.0};

/* ---- kleine Helfer fuer GSettings <-> GdkRGBA (als "#rrggbb"-String) ---- */

static void
settings_set_rgba (GSettings *settings, const gchar *key, const GdkRGBA *rgba)
{
    gchar *str = gdk_rgba_to_string (rgba);
    g_settings_set_string (settings, key, str);
    g_free (str);
}

static void
settings_get_rgba (GSettings *settings, const gchar *key, GdkRGBA *rgba, const GdkRGBA *fallback)
{
    gchar *str = g_settings_get_string (settings, key);
    if (str == NULL || !gdk_rgba_parse (rgba, str)) {
        *rgba = *fallback;
    }
    g_free (str);
}

/* Setzt Vorder-/Hintergrund + Palette passend zum gewaehlten Schema neu -
 * eine eigene Standardfarbe oder Systemthema-Farbe (falls aktiv)
 * ueberschreibt danach wieder Vorder-/Hintergrund; eine individuell
 * bearbeitete Palette ersetzt die Schema-Palette, bis ein Schemawechsel
 * sie zuruecksetzt. Reihenfolge der Prioritaet: Systemthema > eigene
 * Standardfarbe > Schema-Vorgabe. */
static void
apply_colors (NolphinTerminal *terminal)
{
    VteTerminal *vte = VTE_TERMINAL (terminal->vte);
    const GdkRGBA *pal = terminal->palette_customized ?
        terminal->custom_palette : (terminal->solarized ? palette_solarized : palette_standard);
    GdkRGBA fg = terminal->solarized ? solarized_fg : standard_fg;
    GdkRGBA bg = terminal->solarized ? solarized_bg : standard_bg;

    vte_terminal_set_colors (vte, &fg, &bg, pal, 16);

    if (terminal->custom_default_colors_enabled) {
        vte_terminal_set_color_foreground (vte, &terminal->custom_default_fg);
        vte_terminal_set_color_background (vte, &terminal->custom_default_bg);
    }

    if (terminal->use_system_colors) {
        GtkStyleContext *style = gtk_widget_get_style_context (terminal->vte);
        GdkRGBA theme_fg, theme_bg;

        gtk_style_context_get_color (style, GTK_STATE_FLAG_NORMAL, &theme_fg);
        if (!gtk_style_context_lookup_color (style, "theme_bg_color", &theme_bg)) {
            gtk_style_context_get_background_color (style, GTK_STATE_FLAG_NORMAL, &theme_bg);
        }
        vte_terminal_set_color_foreground (vte, &theme_fg);
        vte_terminal_set_color_background (vte, &theme_bg);
    }
}

gboolean
nolphin_terminal_is_shell_busy (NolphinTerminal *terminal)
{
    VtePty *pty;
    int fd;
    pid_t fg_pgid;

    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);

    pty = vte_terminal_get_pty (VTE_TERMINAL (terminal->vte));
    if (pty == NULL) {
        return FALSE;
    }

    fd = vte_pty_get_fd (pty);
    if (fd < 0) {
        return FALSE;
    }

    fg_pgid = tcgetpgrp (fd);
    if (fg_pgid == (pid_t) -1) {
        /* Can't tell - better to sync than to get permanently stuck
         * never syncing again. */
        return FALSE;
    }

    return fg_pgid != (pid_t) terminal->child_pid;
}

void
nolphin_terminal_set_location (NolphinTerminal *terminal,
                                GFile           *location)
{
    gchar *path;
    gchar *quoted;
    gchar *cmd;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));

    if (location == NULL) {
        return;
    }

    if (!terminal->follow_location) {
        return;
    }

    path = g_file_get_path (location);
    if (path == NULL) {
        /* Remote/virtual location with no local path - a local shell
         * has nowhere to cd into, so leave the terminal as it is. */
        return;
    }

    /* Ohne diesen Vergleich schickt jedes erneute Zeigen des Terminals
     * (z. B. jeder F4-Druck) ein frisches "cd" hinein, selbst wenn sich
     * der Ordner gar nicht geaendert hat. */
    if (g_strcmp0 (path, terminal->last_cwd) == 0) {
        g_free (path);
        return;
    }

    if (nolphin_terminal_is_shell_busy (terminal)) {
        g_free (path);
        return;
    }

    quoted = g_shell_quote (path);
    cmd = g_strdup_printf ("cd %s\n", quoted);
    vte_terminal_feed_child (VTE_TERMINAL (terminal->vte), cmd, -1);
    g_free (cmd);
    g_free (quoted);

    g_free (terminal->last_cwd);
    terminal->last_cwd = path;
}

void
nolphin_terminal_grab_focus (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    gtk_widget_grab_focus (terminal->vte);
}

void
nolphin_terminal_copy (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_copy_clipboard_format (VTE_TERMINAL (terminal->vte), VTE_FORMAT_TEXT);
}

void
nolphin_terminal_copy_html (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_copy_clipboard_format (VTE_TERMINAL (terminal->vte), VTE_FORMAT_HTML);
}

void
nolphin_terminal_paste (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_paste_clipboard (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_select_all (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_select_all (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_zoom_in (NolphinTerminal *terminal)
{
    VteTerminal *vte;
    gdouble scale;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte = VTE_TERMINAL (terminal->vte);
    scale = vte_terminal_get_font_scale (vte) * 1.1;
    if (scale > 4.0) scale = 4.0;
    vte_terminal_set_font_scale (vte, scale);
}

void
nolphin_terminal_zoom_out (NolphinTerminal *terminal)
{
    VteTerminal *vte;
    gdouble scale;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte = VTE_TERMINAL (terminal->vte);
    scale = vte_terminal_get_font_scale (vte) / 1.1;
    if (scale < 0.25) scale = 0.25;
    vte_terminal_set_font_scale (vte, scale);
}

void
nolphin_terminal_zoom_reset (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_font_scale (VTE_TERMINAL (terminal->vte), 1.0);
}

/* Nach dem Leeren zeichnet die Shell ihre Eingabeaufforderung nicht von
 * selbst neu - der Bildschirm waere komplett leer (ohne
 * "nutzer@rechner:~$ "). Strg+L (Form Feed) laesst die Shell (readline)
 * die Zeile neu aufbauen. Laeuft gerade ein Programm im Vordergrund,
 * wuerde Strg+L dort ankommen - dann bleibt es beim leeren Bildschirm. */
static void
redraw_prompt (NolphinTerminal *terminal)
{
    if (terminal->child_pid > 0 && !nolphin_terminal_is_shell_busy (terminal)) {
        vte_terminal_feed_child (VTE_TERMINAL (terminal->vte), "\014", 1);
    }
}

void
nolphin_terminal_reset (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_reset (VTE_TERMINAL (terminal->vte), TRUE, TRUE);
    redraw_prompt (terminal);
}

void
nolphin_terminal_clear (NolphinTerminal *terminal)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_reset (VTE_TERMINAL (terminal->vte), FALSE, TRUE);
    redraw_prompt (terminal);
}

const gchar *
nolphin_terminal_get_current_directory (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NULL);
    return terminal->last_cwd;
}

double
nolphin_terminal_get_font_size (NolphinTerminal *terminal)
{
    const PangoFontDescription *desc;

    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), 10.0);
    desc = vte_terminal_get_font (VTE_TERMINAL (terminal->vte));
    return desc != NULL ? pango_font_description_get_size (desc) / (double) PANGO_SCALE : 10.0;
}

void
nolphin_terminal_set_font_size (NolphinTerminal *terminal, double size)
{
    const PangoFontDescription *current;
    PangoFontDescription *desc;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    current = vte_terminal_get_font (VTE_TERMINAL (terminal->vte));
    desc = current != NULL ? pango_font_description_copy (current) : pango_font_description_new ();
    pango_font_description_set_size (desc, (gint) (size * PANGO_SCALE));
    vte_terminal_set_font (VTE_TERMINAL (terminal->vte), desc);
    pango_font_description_free (desc);

    if (!terminal->loading) g_settings_set_double (terminal->settings, "font-size", size);
}

const gchar *
nolphin_terminal_get_font_family (NolphinTerminal *terminal)
{
    const PangoFontDescription *desc;

    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), "Monospace");
    desc = vte_terminal_get_font (VTE_TERMINAL (terminal->vte));
    return desc != NULL ? pango_font_description_get_family (desc) : "Monospace";
}

void
nolphin_terminal_set_font_family (NolphinTerminal *terminal, const gchar *family)
{
    const PangoFontDescription *current;
    PangoFontDescription *desc;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    if (family == NULL || *family == '\0') return;

    current = vte_terminal_get_font (VTE_TERMINAL (terminal->vte));
    desc = current != NULL ? pango_font_description_copy (current) : pango_font_description_new ();
    pango_font_description_set_family (desc, family);
    vte_terminal_set_font (VTE_TERMINAL (terminal->vte), desc);
    pango_font_description_free (desc);

    if (!terminal->loading) g_settings_set_string (terminal->settings, "font-family", family);
}

NolphinCursorShape
nolphin_terminal_get_cursor_shape (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NOLPHIN_CURSOR_SHAPE_BLOCK);
    switch (vte_terminal_get_cursor_shape (VTE_TERMINAL (terminal->vte))) {
    case VTE_CURSOR_SHAPE_IBEAM: return NOLPHIN_CURSOR_SHAPE_IBEAM;
    case VTE_CURSOR_SHAPE_UNDERLINE: return NOLPHIN_CURSOR_SHAPE_UNDERLINE;
    case VTE_CURSOR_SHAPE_BLOCK:
    default: return NOLPHIN_CURSOR_SHAPE_BLOCK;
    }
}

void
nolphin_terminal_set_cursor_shape (NolphinTerminal *terminal, NolphinCursorShape shape)
{
    VteCursorShape vte_shape;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    switch (shape) {
    case NOLPHIN_CURSOR_SHAPE_IBEAM: vte_shape = VTE_CURSOR_SHAPE_IBEAM; break;
    case NOLPHIN_CURSOR_SHAPE_UNDERLINE: vte_shape = VTE_CURSOR_SHAPE_UNDERLINE; break;
    case NOLPHIN_CURSOR_SHAPE_BLOCK:
    default: vte_shape = VTE_CURSOR_SHAPE_BLOCK; break;
    }
    vte_terminal_set_cursor_shape (VTE_TERMINAL (terminal->vte), vte_shape);

    if (!terminal->loading) g_settings_set_int (terminal->settings, "cursor-shape", (gint) shape);
}

NolphinCursorBlinkMode
nolphin_terminal_get_cursor_blink_mode (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NOLPHIN_CURSOR_BLINK_SYSTEM);
    switch (vte_terminal_get_cursor_blink_mode (VTE_TERMINAL (terminal->vte))) {
    case VTE_CURSOR_BLINK_ON: return NOLPHIN_CURSOR_BLINK_ALWAYS;
    case VTE_CURSOR_BLINK_OFF: return NOLPHIN_CURSOR_BLINK_NEVER;
    case VTE_CURSOR_BLINK_SYSTEM:
    default: return NOLPHIN_CURSOR_BLINK_SYSTEM;
    }
}

void
nolphin_terminal_set_cursor_blink_mode (NolphinTerminal *terminal, NolphinCursorBlinkMode mode)
{
    VteCursorBlinkMode vte_mode;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    switch (mode) {
    case NOLPHIN_CURSOR_BLINK_ALWAYS: vte_mode = VTE_CURSOR_BLINK_ON; break;
    case NOLPHIN_CURSOR_BLINK_NEVER: vte_mode = VTE_CURSOR_BLINK_OFF; break;
    case NOLPHIN_CURSOR_BLINK_SYSTEM:
    default: vte_mode = VTE_CURSOR_BLINK_SYSTEM; break;
    }
    vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (terminal->vte), vte_mode);

    if (!terminal->loading) g_settings_set_int (terminal->settings, "cursor-blink-mode", (gint) mode);
}

gboolean
nolphin_terminal_get_solarized (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return terminal->solarized;
}

void
nolphin_terminal_set_solarized (NolphinTerminal *terminal, gboolean solarized)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->solarized = solarized;
    terminal->palette_customized = FALSE;
    apply_colors (terminal);

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "solarized", solarized);
        g_settings_set_boolean (terminal->settings, "palette-customized", FALSE);
    }
}

gboolean
nolphin_terminal_get_custom_default_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    if (fg) *fg = terminal->custom_default_fg;
    if (bg) *bg = terminal->custom_default_bg;
    return terminal->custom_default_colors_enabled;
}

void
nolphin_terminal_set_custom_default_colors (NolphinTerminal *terminal, gboolean enabled,
                                             const GdkRGBA *fg, const GdkRGBA *bg)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->custom_default_colors_enabled = enabled;
    if (fg) terminal->custom_default_fg = *fg;
    if (bg) terminal->custom_default_bg = *bg;
    apply_colors (terminal);

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "custom-default-colors-enabled", enabled);
        settings_set_rgba (terminal->settings, "custom-default-fg", &terminal->custom_default_fg);
        settings_set_rgba (terminal->settings, "custom-default-bg", &terminal->custom_default_bg);
    }
}

gboolean
nolphin_terminal_get_use_system_colors (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return terminal->use_system_colors;
}

void
nolphin_terminal_set_use_system_colors (NolphinTerminal *terminal, gboolean use_system)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->use_system_colors = use_system;
    apply_colors (terminal);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "use-system-colors", use_system);
}

gboolean
nolphin_terminal_get_bold_is_bright (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), TRUE);
    return vte_terminal_get_bold_is_bright (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_bold_is_bright (NolphinTerminal *terminal, gboolean bright)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_bold_is_bright (VTE_TERMINAL (terminal->vte), bright);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "bold-is-bright", bright);
}

void
nolphin_terminal_get_palette_color (NolphinTerminal *terminal, int index, GdkRGBA *color)
{
    const GdkRGBA *pal;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    g_return_if_fail (index >= 0 && index < 16);
    if (color == NULL) return;

    pal = terminal->palette_customized ?
        terminal->custom_palette : (terminal->solarized ? palette_solarized : palette_standard);
    *color = pal[index];
}

static void
persist_palette (NolphinTerminal *terminal)
{
    GPtrArray *arr;
    guint i;

    if (terminal->loading) return;

    arr = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < 16; ++i) {
        g_ptr_array_add (arr, gdk_rgba_to_string (&terminal->custom_palette[i]));
    }
    g_ptr_array_add (arr, NULL);

    g_settings_set_boolean (terminal->settings, "palette-customized", TRUE);
    g_settings_set_strv (terminal->settings, "palette", (const gchar * const *) arr->pdata);
    g_ptr_array_unref (arr);
}

void
nolphin_terminal_set_palette_color (NolphinTerminal *terminal, int index, const GdkRGBA *color)
{
    const GdkRGBA *base;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    g_return_if_fail (index >= 0 && index < 16);
    if (color == NULL) return;

    if (!terminal->palette_customized) {
        /* Beim ersten Bearbeiten eines Swatches: restliche 15 Farben vom
         * aktuellen Schema uebernehmen, nicht nur die eine geaenderte. */
        base = terminal->solarized ? palette_solarized : palette_standard;
        memcpy (terminal->custom_palette, base, sizeof (terminal->custom_palette));
        terminal->palette_customized = TRUE;
    }
    terminal->custom_palette[index] = *color;
    apply_colors (terminal);
    persist_palette (terminal);
}

gboolean
nolphin_terminal_get_custom_bold_color (NolphinTerminal *terminal, GdkRGBA *fg)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    if (fg) *fg = terminal->custom_bold_fg;
    return terminal->custom_bold_color_enabled;
}

void
nolphin_terminal_set_custom_bold_color (NolphinTerminal *terminal, gboolean enabled, const GdkRGBA *fg)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->custom_bold_color_enabled = enabled;
    if (fg) terminal->custom_bold_fg = *fg;
    vte_terminal_set_color_bold (VTE_TERMINAL (terminal->vte), enabled ? &terminal->custom_bold_fg : NULL);

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "custom-bold-color-enabled", enabled);
        settings_set_rgba (terminal->settings, "custom-bold-color", &terminal->custom_bold_fg);
    }
}

gboolean
nolphin_terminal_get_custom_cursor_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    if (fg) *fg = terminal->custom_cursor_fg;
    if (bg) *bg = terminal->custom_cursor_bg;
    return terminal->custom_cursor_colors_enabled;
}

void
nolphin_terminal_set_custom_cursor_colors (NolphinTerminal *terminal, gboolean enabled,
                                            const GdkRGBA *fg, const GdkRGBA *bg)
{
    VteTerminal *vte;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->custom_cursor_colors_enabled = enabled;
    if (fg) terminal->custom_cursor_fg = *fg;
    if (bg) terminal->custom_cursor_bg = *bg;

    vte = VTE_TERMINAL (terminal->vte);
    vte_terminal_set_color_cursor (vte, enabled ? &terminal->custom_cursor_bg : NULL);
    vte_terminal_set_color_cursor_foreground (vte, enabled ? &terminal->custom_cursor_fg : NULL);

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "custom-cursor-colors-enabled", enabled);
        settings_set_rgba (terminal->settings, "custom-cursor-fg", &terminal->custom_cursor_fg);
        settings_set_rgba (terminal->settings, "custom-cursor-bg", &terminal->custom_cursor_bg);
    }
}

gboolean
nolphin_terminal_get_custom_highlight_colors (NolphinTerminal *terminal, GdkRGBA *fg, GdkRGBA *bg)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    if (fg) *fg = terminal->custom_highlight_fg;
    if (bg) *bg = terminal->custom_highlight_bg;
    return terminal->custom_highlight_colors_enabled;
}

void
nolphin_terminal_set_custom_highlight_colors (NolphinTerminal *terminal, gboolean enabled,
                                               const GdkRGBA *fg, const GdkRGBA *bg)
{
    VteTerminal *vte;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->custom_highlight_colors_enabled = enabled;
    if (fg) terminal->custom_highlight_fg = *fg;
    if (bg) terminal->custom_highlight_bg = *bg;

    vte = VTE_TERMINAL (terminal->vte);
    vte_terminal_set_color_highlight (vte, enabled ? &terminal->custom_highlight_bg : NULL);
    vte_terminal_set_color_highlight_foreground (vte, enabled ? &terminal->custom_highlight_fg : NULL);

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "custom-highlight-colors-enabled", enabled);
        settings_set_rgba (terminal->settings, "custom-highlight-fg", &terminal->custom_highlight_fg);
        settings_set_rgba (terminal->settings, "custom-highlight-bg", &terminal->custom_highlight_bg);
    }
}

static VteEraseBinding
erase_binding_to_vte (NolphinEraseBinding binding)
{
    switch (binding) {
    case NOLPHIN_ERASE_ASCII_BACKSPACE: return VTE_ERASE_ASCII_BACKSPACE;
    case NOLPHIN_ERASE_ASCII_DELETE: return VTE_ERASE_ASCII_DELETE;
    case NOLPHIN_ERASE_DELETE_SEQUENCE: return VTE_ERASE_DELETE_SEQUENCE;
    case NOLPHIN_ERASE_TTY: return VTE_ERASE_TTY;
    case NOLPHIN_ERASE_AUTO:
    default: return VTE_ERASE_AUTO;
    }
}

NolphinEraseBinding
nolphin_terminal_get_backspace_binding (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NOLPHIN_ERASE_AUTO);
    return terminal->backspace_binding;
}

void
nolphin_terminal_set_backspace_binding (NolphinTerminal *terminal, NolphinEraseBinding binding)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->backspace_binding = binding;
    vte_terminal_set_backspace_binding (VTE_TERMINAL (terminal->vte), erase_binding_to_vte (binding));

    if (!terminal->loading) g_settings_set_int (terminal->settings, "backspace-binding", (gint) binding);
}

NolphinEraseBinding
nolphin_terminal_get_delete_binding (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NOLPHIN_ERASE_DELETE_SEQUENCE);
    return terminal->delete_binding;
}

void
nolphin_terminal_set_delete_binding (NolphinTerminal *terminal, NolphinEraseBinding binding)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->delete_binding = binding;
    vte_terminal_set_delete_binding (VTE_TERMINAL (terminal->vte), erase_binding_to_vte (binding));

    if (!terminal->loading) g_settings_set_int (terminal->settings, "delete-binding", (gint) binding);
}

gboolean
nolphin_terminal_get_ambiguous_width_wide (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_cjk_ambiguous_width (VTE_TERMINAL (terminal->vte)) == 2;
}

void
nolphin_terminal_set_ambiguous_width_wide (NolphinTerminal *terminal, gboolean wide)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_cjk_ambiguous_width (VTE_TERMINAL (terminal->vte), wide ? 2 : 1);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "ambiguous-width-wide", wide);
}

double
nolphin_terminal_get_cell_width_scale (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), 1.0);
    return vte_terminal_get_cell_width_scale (VTE_TERMINAL (terminal->vte));
}

double
nolphin_terminal_get_cell_height_scale (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), 1.0);
    return vte_terminal_get_cell_height_scale (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_cell_spacing (NolphinTerminal *terminal, double width_scale, double height_scale)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_cell_width_scale (VTE_TERMINAL (terminal->vte), width_scale);
    vte_terminal_set_cell_height_scale (VTE_TERMINAL (terminal->vte), height_scale);

    if (!terminal->loading) {
        g_settings_set_double (terminal->settings, "cell-width-scale", width_scale);
        g_settings_set_double (terminal->settings, "cell-height-scale", height_scale);
    }
}

void
nolphin_terminal_set_scrollback_lines (NolphinTerminal *terminal, int lines)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_scrollback_lines (VTE_TERMINAL (terminal->vte), lines);

    if (!terminal->loading) g_settings_set_int (terminal->settings, "scrollback-lines", lines);
}

gboolean
nolphin_terminal_get_scroll_on_output (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_scroll_on_output (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_scroll_on_output (NolphinTerminal *terminal, gboolean scroll)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_scroll_on_output (VTE_TERMINAL (terminal->vte), scroll);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "scroll-on-output", scroll);
}

gboolean
nolphin_terminal_get_scroll_on_keystroke (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_scroll_on_keystroke (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_scroll_on_keystroke (NolphinTerminal *terminal, gboolean scroll)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_scroll_on_keystroke (VTE_TERMINAL (terminal->vte), scroll);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "scroll-on-keystroke", scroll);
}

gboolean
nolphin_terminal_get_scroll_on_paste (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_scroll_on_insert (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_scroll_on_paste (NolphinTerminal *terminal, gboolean scroll)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_scroll_on_insert (VTE_TERMINAL (terminal->vte), scroll);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "scroll-on-paste", scroll);
}

gboolean
nolphin_terminal_get_show_scrollbar (NolphinTerminal *terminal)
{
    GtkWidget *scrolled;
    GtkWidget *vscrollbar;

    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), TRUE);
    scrolled = gtk_widget_get_parent (terminal->vte);
    if (!GTK_IS_SCROLLED_WINDOW (scrolled)) return TRUE;

    vscrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (scrolled));
    return vscrollbar == NULL || gtk_widget_get_visible (vscrollbar);
}

void
nolphin_terminal_set_show_scrollbar (NolphinTerminal *terminal, gboolean show)
{
    GtkWidget *scrolled;
    GtkWidget *vscrollbar;

    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    scrolled = gtk_widget_get_parent (terminal->vte);
    if (GTK_IS_SCROLLED_WINDOW (scrolled)) {
        vscrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (scrolled));
        if (vscrollbar != NULL) {
            gtk_widget_set_visible (vscrollbar, show);
        }
    }

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "show-scrollbar", show);
}

gboolean
nolphin_terminal_get_login_shell (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return terminal->login_shell;
}

void
nolphin_terminal_set_login_shell (NolphinTerminal *terminal, gboolean login_shell)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->login_shell = login_shell;

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "login-shell", login_shell);
}

gboolean
nolphin_terminal_get_use_custom_command (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return terminal->use_custom_command;
}

const gchar *
nolphin_terminal_get_custom_command (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NULL);
    return terminal->custom_command;
}

void
nolphin_terminal_set_custom_command (NolphinTerminal *terminal, gboolean use_custom, const gchar *command)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->use_custom_command = use_custom;
    g_free (terminal->custom_command);
    terminal->custom_command = (use_custom && command && *command) ? g_strdup (command) : NULL;

    if (!terminal->loading) {
        g_settings_set_boolean (terminal->settings, "use-custom-command", use_custom);
        g_settings_set_string (terminal->settings, "custom-command", terminal->custom_command ? terminal->custom_command : "");
    }
}

gboolean
nolphin_terminal_get_text_blink (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_text_blink_mode (VTE_TERMINAL (terminal->vte)) != VTE_TEXT_BLINK_NEVER;
}

void
nolphin_terminal_set_text_blink (NolphinTerminal *terminal, gboolean enabled)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_text_blink_mode (VTE_TERMINAL (terminal->vte),
        enabled ? VTE_TEXT_BLINK_ALWAYS : VTE_TEXT_BLINK_NEVER);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "text-blink-enabled", enabled);
}

gboolean
nolphin_terminal_get_bell_enabled (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return vte_terminal_get_audible_bell (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_bell_enabled (NolphinTerminal *terminal, gboolean enabled)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_audible_bell (VTE_TERMINAL (terminal->vte), enabled);

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "bell-enabled", enabled);
}

void
nolphin_terminal_set_initial_size (NolphinTerminal *terminal, int cols, int rows)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    vte_terminal_set_size (VTE_TERMINAL (terminal->vte), cols, rows);

    if (!terminal->loading) {
        g_settings_set_int (terminal->settings, "initial-columns", cols);
        g_settings_set_int (terminal->settings, "initial-rows", rows);
    }
}

gboolean
nolphin_terminal_get_follow_location (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), TRUE);
    return terminal->follow_location;
}

void
nolphin_terminal_set_follow_location (NolphinTerminal *terminal, gboolean follow)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->follow_location = follow;

    if (!terminal->loading) g_settings_set_boolean (terminal->settings, "follow-location", follow);
}

NolphinTerminalExitAction
nolphin_terminal_get_exit_action (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), NOLPHIN_TERMINAL_EXIT_HOLD);
    return terminal->exit_action;
}

void
nolphin_terminal_set_exit_action (NolphinTerminal *terminal, NolphinTerminalExitAction action)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    terminal->exit_action = action;

    if (!terminal->loading) g_settings_set_int (terminal->settings, "exit-action", (gint) action);
}

gboolean
nolphin_terminal_get_read_only (NolphinTerminal *terminal)
{
    g_return_val_if_fail (NOLPHIN_IS_TERMINAL (terminal), FALSE);
    return !vte_terminal_get_input_enabled (VTE_TERMINAL (terminal->vte));
}

void
nolphin_terminal_set_read_only (NolphinTerminal *terminal, gboolean read_only)
{
    g_return_if_fail (NOLPHIN_IS_TERMINAL (terminal));
    vte_terminal_set_input_enabled (VTE_TERMINAL (terminal->vte), !read_only);
}

static void spawn_shell (NolphinTerminal *terminal);

static void
on_spawn_complete (VteTerminal *vte, GPid pid, GError *error, gpointer user_data)
{
    NolphinTerminal *terminal = user_data;

    (void) vte;
    if (error != NULL) {
        g_warning ("Shell konnte nicht gestartet werden: %s", error->message);
        terminal->child_pid = -1;
        return;
    }
    terminal->child_pid = pid;
}

static void
spawn_shell (NolphinTerminal *terminal)
{
    GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
    gchar **envv = g_get_environ ();

    if (terminal->use_custom_command && terminal->custom_command != NULL) {
        g_ptr_array_add (argv, g_strdup ("/bin/sh"));
        g_ptr_array_add (argv, g_strdup ("-c"));
        g_ptr_array_add (argv, g_strdup (terminal->custom_command));
    } else {
        const gchar *shell = g_environ_getenv (envv, "SHELL");
        if (shell == NULL || *shell == '\0') shell = "/bin/bash";
        g_ptr_array_add (argv, g_strdup (shell));
        if (terminal->login_shell) {
            g_ptr_array_add (argv, g_strdup ("--login"));
        }
    }
    g_ptr_array_add (argv, NULL);

    vte_terminal_spawn_async (VTE_TERMINAL (terminal->vte),
                               VTE_PTY_DEFAULT,
                               terminal->cwd,
                               (char **) argv->pdata,
                               envv,
                               G_SPAWN_DEFAULT,
                               NULL, NULL, NULL,
                               -1, NULL,
                               on_spawn_complete, terminal);

    g_ptr_array_unref (argv);
    g_strfreev (envv);
}

static void
on_child_exited (VteTerminal *vte, int status, gpointer user_data)
{
    NolphinTerminal *terminal = user_data;

    (void) vte; (void) status;
    terminal->child_pid = -1;

    if (terminal->exit_action == NOLPHIN_TERMINAL_EXIT_RESTART) {
        vte_terminal_reset (VTE_TERMINAL (terminal->vte), TRUE, TRUE);
        spawn_shell (terminal);
    }
    /* HOLD (Vorgabe): Bildschirminhalt bleibt einfach stehen. */
}

/* Laedt alle gespeicherten Einstellungen aus GSettings und wendet sie auf
 * das frisch erzeugte VTE-Widget an. terminal->loading haelt waehrenddessen
 * die Setter davon ab, dieselben Werte gleich wieder zurueckzuschreiben. */
static void
load_settings (NolphinTerminal *terminal)
{
    GSettings *s = terminal->settings;
    GdkRGBA rgba, rgba2;
    gchar *str;
    gchar **palette_strv;

    terminal->loading = TRUE;

    str = g_settings_get_string (s, "font-family");
    nolphin_terminal_set_font_family (terminal, str);
    g_free (str);
    nolphin_terminal_set_font_size (terminal, g_settings_get_double (s, "font-size"));

    nolphin_terminal_set_cursor_shape (terminal, (NolphinCursorShape) g_settings_get_int (s, "cursor-shape"));
    nolphin_terminal_set_cursor_blink_mode (terminal, (NolphinCursorBlinkMode) g_settings_get_int (s, "cursor-blink-mode"));

    terminal->solarized = g_settings_get_boolean (s, "solarized");

    palette_strv = g_settings_get_strv (s, "palette");
    if (g_settings_get_boolean (s, "palette-customized") && g_strv_length (palette_strv) == 16) {
        guint i;
        for (i = 0; i < 16; ++i) {
            if (!gdk_rgba_parse (&terminal->custom_palette[i], palette_strv[i])) {
                terminal->custom_palette[i] = terminal->solarized ? palette_solarized[i] : palette_standard[i];
            }
        }
        terminal->palette_customized = TRUE;
    }
    g_strfreev (palette_strv);

    settings_get_rgba (s, "custom-default-fg", &rgba, &standard_fg);
    settings_get_rgba (s, "custom-default-bg", &rgba2, &standard_bg);
    nolphin_terminal_set_custom_default_colors (terminal,
        g_settings_get_boolean (s, "custom-default-colors-enabled"), &rgba, &rgba2);

    nolphin_terminal_set_use_system_colors (terminal, g_settings_get_boolean (s, "use-system-colors"));
    nolphin_terminal_set_bold_is_bright (terminal, g_settings_get_boolean (s, "bold-is-bright"));

    settings_get_rgba (s, "custom-bold-color", &rgba, &standard_fg);
    nolphin_terminal_set_custom_bold_color (terminal,
        g_settings_get_boolean (s, "custom-bold-color-enabled"), &rgba);

    settings_get_rgba (s, "custom-cursor-fg", &rgba, &standard_bg);
    settings_get_rgba (s, "custom-cursor-bg", &rgba2, &standard_fg);
    nolphin_terminal_set_custom_cursor_colors (terminal,
        g_settings_get_boolean (s, "custom-cursor-colors-enabled"), &rgba, &rgba2);

    settings_get_rgba (s, "custom-highlight-fg", &rgba, &standard_fg);
    settings_get_rgba (s, "custom-highlight-bg", &rgba2, &solarized_bg);
    nolphin_terminal_set_custom_highlight_colors (terminal,
        g_settings_get_boolean (s, "custom-highlight-colors-enabled"), &rgba, &rgba2);

    nolphin_terminal_set_backspace_binding (terminal, (NolphinEraseBinding) g_settings_get_int (s, "backspace-binding"));
    nolphin_terminal_set_delete_binding (terminal, (NolphinEraseBinding) g_settings_get_int (s, "delete-binding"));
    nolphin_terminal_set_ambiguous_width_wide (terminal, g_settings_get_boolean (s, "ambiguous-width-wide"));

    nolphin_terminal_set_cell_spacing (terminal,
        g_settings_get_double (s, "cell-width-scale"), g_settings_get_double (s, "cell-height-scale"));

    nolphin_terminal_set_scrollback_lines (terminal, g_settings_get_int (s, "scrollback-lines"));
    nolphin_terminal_set_scroll_on_output (terminal, g_settings_get_boolean (s, "scroll-on-output"));
    nolphin_terminal_set_scroll_on_keystroke (terminal, g_settings_get_boolean (s, "scroll-on-keystroke"));
    nolphin_terminal_set_scroll_on_paste (terminal, g_settings_get_boolean (s, "scroll-on-paste"));
    nolphin_terminal_set_show_scrollbar (terminal, g_settings_get_boolean (s, "show-scrollbar"));

    terminal->login_shell = g_settings_get_boolean (s, "login-shell");
    str = g_settings_get_string (s, "custom-command");
    terminal->use_custom_command = g_settings_get_boolean (s, "use-custom-command");
    terminal->custom_command = (terminal->use_custom_command && str && *str) ? g_strdup (str) : NULL;
    g_free (str);

    nolphin_terminal_set_text_blink (terminal, g_settings_get_boolean (s, "text-blink-enabled"));
    nolphin_terminal_set_bell_enabled (terminal, g_settings_get_boolean (s, "bell-enabled"));
    terminal->follow_location = g_settings_get_boolean (s, "follow-location");
    terminal->exit_action = (NolphinTerminalExitAction) g_settings_get_int (s, "exit-action");

    nolphin_terminal_set_initial_size (terminal,
        g_settings_get_int (s, "initial-columns"), g_settings_get_int (s, "initial-rows"));

    apply_colors (terminal);

    terminal->loading = FALSE;
}

static void
on_context_menu_copy (GtkMenuItem *item, gpointer user_data)
{
    (void) item;
    nolphin_terminal_copy (NOLPHIN_TERMINAL (user_data));
}

static void
on_context_menu_paste (GtkMenuItem *item, gpointer user_data)
{
    (void) item;
    nolphin_terminal_paste (NOLPHIN_TERMINAL (user_data));
}

static void
on_context_menu_select_all (GtkMenuItem *item, gpointer user_data)
{
    (void) item;
    nolphin_terminal_select_all (NOLPHIN_TERMINAL (user_data));
}

static void
on_context_menu_copy_html (GtkMenuItem *item, gpointer user_data)
{
    (void) item;
    nolphin_terminal_copy_html (NOLPHIN_TERMINAL (user_data));
}

static void
on_context_menu_read_only_toggled (GtkCheckMenuItem *item, gpointer user_data)
{
    nolphin_terminal_set_read_only (NOLPHIN_TERMINAL (user_data),
                                     gtk_check_menu_item_get_active (item));
}

static void
on_context_menu_settings (GtkMenuItem *item, gpointer user_data)
{
    (void) item;
    g_signal_emit (NOLPHIN_TERMINAL (user_data), signals[SIGNAL_SETTINGS_REQUESTED], 0);
}

/* VTE zeigt von sich aus kein Rechtsklick-Menue. vte_terminal_set_
 * context_menu() uebernimmt zwar das Oeffnen, positioniert das Menue
 * dabei aber nicht bildschirmrand-bewusst - am rechten Bildschirmrand
 * (wo das Terminal-Panel typischerweise sitzt) wurde es deshalb
 * abgeschnitten statt sich nach links umzuklappen. gtk_menu_popup_at_
 * pointer() (von uns selbst in button_press_cb() aufgerufen) macht
 * genau diese Randerkennung zuverlaessig - derselbe Mechanismus, den
 * GTK fuer jedes andere Kontextmenue in Nolphin verwendet.
 * "Neues Fenster"/"Neuer Reiter" aus dem Referenz-Kontextmenue fehlen
 * bewusst - Nolphins Terminal-Panel kennt kein eigenes Fenster und
 * (noch) keine mehreren Reiter. "Menüleiste anzeigen" fehlt ebenfalls
 * bewusst: das ist eine Eigenschaft des umgebenden Panels
 * (nolphin-workspace-panel.c), nicht dieses Widgets. */
static GtkWidget *
build_context_menu (NolphinTerminal *terminal)
{
    GtkWidget *menu = gtk_menu_new ();
    GtkWidget *item;

    item = gtk_menu_item_new_with_label (_("Kopieren"));
    g_signal_connect (item, "activate", G_CALLBACK (on_context_menu_copy), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Als HTML kopieren"));
    g_signal_connect (item, "activate", G_CALLBACK (on_context_menu_copy_html), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Einfügen"));
    g_signal_connect (item, "activate", G_CALLBACK (on_context_menu_paste), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());

    item = gtk_menu_item_new_with_label (_("Alles auswählen"));
    g_signal_connect (item, "activate", G_CALLBACK (on_context_menu_select_all), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());

    item = gtk_check_menu_item_new_with_label (_("Nur lesen"));
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), nolphin_terminal_get_read_only (terminal));
    g_signal_connect (item, "toggled", G_CALLBACK (on_context_menu_read_only_toggled), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Einstellungen …"));
    g_signal_connect (item, "activate", G_CALLBACK (on_context_menu_settings), terminal);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_widget_show_all (menu);
    return menu;
}

static gboolean
on_vte_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    NolphinTerminal *terminal = user_data;
    GtkWidget *menu;

    (void) widget;
    if (event->button != GDK_BUTTON_SECONDARY) {
        return FALSE;
    }

    menu = g_object_get_data (G_OBJECT (terminal), "nolphin-context-menu");
    /* Haelt den "Nur lesen"-Haken aktuell, falls er sich seit dem letzten
     * Rechtsklick geaendert hat (z. B. ueber einen zukuenftigen zweiten
     * Zugang zu dieser Einstellung). */
    {
        GList *children = gtk_container_get_children (GTK_CONTAINER (menu));
        GList *l;
        for (l = children; l != NULL; l = l->next) {
            if (GTK_IS_CHECK_MENU_ITEM (l->data)) {
                g_signal_handlers_block_by_func (l->data, on_context_menu_read_only_toggled, terminal);
                gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (l->data),
                                                 nolphin_terminal_get_read_only (terminal));
                g_signal_handlers_unblock_by_func (l->data, on_context_menu_read_only_toggled, terminal);
            }
        }
        g_list_free (children);
    }

    gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
    return TRUE;
}

static void
nolphin_terminal_destroy (GtkWidget *widget)
{
    NolphinTerminal *terminal = NOLPHIN_TERMINAL (widget);

    g_clear_object (&terminal->settings);
    g_free (terminal->cwd);
    terminal->cwd = NULL;
    g_free (terminal->last_cwd);
    terminal->last_cwd = NULL;
    g_free (terminal->custom_command);
    terminal->custom_command = NULL;

    GTK_WIDGET_CLASS (nolphin_terminal_parent_class)->destroy (widget);
}

static void
nolphin_terminal_class_init (NolphinTerminalClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    widget_class->destroy = nolphin_terminal_destroy;

    signals[SIGNAL_SETTINGS_REQUESTED] =
        g_signal_new ("settings-requested",
                       G_TYPE_FROM_CLASS (klass),
                       G_SIGNAL_RUN_LAST,
                       0, NULL, NULL, NULL,
                       G_TYPE_NONE, 0);
}

static void
nolphin_terminal_init (NolphinTerminal *terminal)
{
    GtkWidget *scrolled;

    gtk_orientable_set_orientation (GTK_ORIENTABLE (terminal), GTK_ORIENTATION_VERTICAL);

    terminal->settings = g_settings_new ("org.nolphin.terminal");
    terminal->cwd = g_strdup (g_get_home_dir ());
    terminal->last_cwd = g_strdup (terminal->cwd);
    terminal->child_pid = -1;

    terminal->vte = vte_terminal_new ();
    g_signal_connect_swapped (terminal->vte, "style-updated", G_CALLBACK (apply_colors), terminal);
    g_signal_connect (terminal->vte, "child-exited", G_CALLBACK (on_child_exited), terminal);
    g_object_set_data_full (G_OBJECT (terminal), "nolphin-context-menu",
                             build_context_menu (terminal), (GDestroyNotify) gtk_widget_destroy);
    g_signal_connect (terminal->vte, "button-press-event", G_CALLBACK (on_vte_button_press), terminal);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                     GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scrolled), terminal->vte);

    gtk_box_pack_start (GTK_BOX (terminal), scrolled, TRUE, TRUE, 0);
    gtk_widget_show (terminal->vte);
    gtk_widget_show (scrolled);

    load_settings (terminal);
    /* Die Shell startet erst in nolphin_terminal_new*(), damit ein neuer
     * Reiter gleich im gewuenschten Ordner beginnt. */
}

GtkWidget *
nolphin_terminal_new (void)
{
    return nolphin_terminal_new_at (NULL);
}

GtkWidget *
nolphin_terminal_new_at (const gchar *directory)
{
    NolphinTerminal *terminal = g_object_new (NOLPHIN_TYPE_TERMINAL, NULL);

    if (directory != NULL && g_file_test (directory, G_FILE_TEST_IS_DIR)) {
        g_free (terminal->cwd);
        terminal->cwd = g_strdup (directory);
        g_free (terminal->last_cwd);
        terminal->last_cwd = g_strdup (directory);
    }
    spawn_shell (terminal);
    return GTK_WIDGET (terminal);
}
