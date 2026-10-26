/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nolphin
 *
 *  Copyright (C) 1999, 2000, 2004 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *
 *  Nolphin is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  Nolphin is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>
 *  	     John Sullivan <sullivan@eazel.com>
 *           Alexander Larsson <alexl@redhat.com>
 */

/* nolphin-window.c: Implementation of the main window object */

#include <config.h>

#include "nolphin-window-private.h"
#include <glib/gstdio.h>

#include "nolphin-actions.h"
#include "nolphin-application.h"
#include "nolphin-bookmarks-window.h"
#include "nolphin-desktop-window.h"
#include "nolphin-gid-menu.h"
#include "nolphin-gid-sidebar.h"
#include "nolphin-location-bar.h"
#include "nolphin-mime-actions.h"
#include "nolphin-notebook.h"
#include "nolphin-places-sidebar.h"
#include "nolphin-tree-sidebar.h"
#include "nolphin-view-factory.h"
#include "nolphin-window-manage-views.h"
#include "nolphin-window-bookmarks.h"
#include "nolphin-window-slot.h"
#include "nolphin-window-menus.h"
#include "nolphin-terminal.h"
#include "nolphin-preview.h"
#include "nolphin-workspace-panel.h"
#include "nolphin-icon-view.h"
#include "nolphin-list-view.h"
#include "nolphin-statusbar.h"

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-string.h>
#include <eel/eel-vfs-extensions.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_X11_XF86KEYSYM_H
#include <X11/XF86keysym.h>
#endif
#include <libnolphin-private/nolphin-file-utilities.h>
#include <libnolphin-private/nolphin-file-attributes.h>
#include <libnolphin-private/nolphin-global-preferences.h>
#include <libnolphin-private/nolphin-metadata.h>
#include <libnolphin-private/nolphin-clipboard.h>
#include <libnolphin-private/nolphin-undo.h>
#include <libnolphin-private/nolphin-search-directory.h>
#include <libnolphin-private/nolphin-signaller.h>

#define DEBUG_FLAG NOLPHIN_DEBUG_WINDOW
#include <libnolphin-private/nolphin-debug.h>
#include "nolphin-diagnostics.h"

#include <math.h>
#include <sys/time.h>

#define MAX_TITLE_LENGTH 180

/* Forward and back buttons on the mouse */
static gboolean mouse_extra_buttons = TRUE;
static guint mouse_forward_button = 9;
static guint mouse_back_button = 8;

static void mouse_back_button_changed		     (gpointer                  callback_data);
static void mouse_forward_button_changed	     (gpointer                  callback_data);
static void use_extra_mouse_buttons_changed          (gpointer              callback_data);
static void side_pane_id_changed                    (NolphinWindow            *window);
static void toggle_menubar                          (NolphinWindow            *window,
                                                     gint                   action);
static void nolphin_window_reload                      (NolphinWindow            *window);

/* Sanity check: highest mouse button value I could find was 14. 5 is our
 * lower threshold (well-documented to be the one of the button events for the
 * scrollwheel), so it's hardcoded in the functions below. However, if you have
 * a button that registers higher and want to map it, file a bug and
 * we'll move the bar. Makes you wonder why the X guys don't have
 * defined values for these like the XKB stuff, huh?
 */
#define UPPER_MOUSE_LIMIT 14

enum {
	PROP_DISABLE_CHROME = 1,
    PROP_SIDEBAR_VIEW_TYPE,
    PROP_SHOW_SIDEBAR,
	NUM_PROPERTIES,
};

enum {
	GO_UP,
	RELOAD,
	PROMPT_FOR_LOCATION,
	LOADING_URI,
	HIDDEN_FILES_MODE_CHANGED,
	SLOT_ADDED,
	SLOT_REMOVED,
	LAST_SIGNAL
};

enum {
    MENU_HIDE,
    MENU_SHOW,
    MENU_TOGGLE
};

static guint signals[LAST_SIGNAL] = { 0 };
static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NolphinWindow, nolphin_window, GTK_TYPE_APPLICATION_WINDOW);

static const struct {
	unsigned int keyval;
	const char *action;
} extra_window_keybindings [] = {
#ifdef HAVE_X11_XF86KEYSYM_H
	{ XF86XK_AddFavorite,	NOLPHIN_ACTION_ADD_BOOKMARK },
	{ XF86XK_Favorites,	NOLPHIN_ACTION_EDIT_BOOKMARKS },
	{ XF86XK_Go,		NOLPHIN_ACTION_EDIT_LOCATION },
	{ XF86XK_HomePage,      NOLPHIN_ACTION_GO_HOME },
	{ XF86XK_OpenURL,	NOLPHIN_ACTION_EDIT_LOCATION },
	{ XF86XK_Refresh,	NOLPHIN_ACTION_RELOAD },
	{ XF86XK_Reload,	NOLPHIN_ACTION_RELOAD },
	{ XF86XK_Search,	NOLPHIN_ACTION_SEARCH },
	{ XF86XK_Start,		NOLPHIN_ACTION_GO_HOME },
	{ XF86XK_Stop,		NOLPHIN_ACTION_STOP },
	{ XF86XK_ZoomIn,	NOLPHIN_ACTION_ZOOM_IN },
	{ XF86XK_ZoomOut,	NOLPHIN_ACTION_ZOOM_OUT },
	{ XF86XK_Back,		NOLPHIN_ACTION_BACK },
	{ XF86XK_Forward,	NOLPHIN_ACTION_FORWARD }

#endif
};

void
nolphin_window_push_status (NolphinWindow *window,
			     const char *text)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	/* clear any previous message, underflow is allowed */
	gtk_statusbar_pop (GTK_STATUSBAR (window->details->statusbar), 0);

	if (text != NULL && text[0] != '\0') {
		gtk_statusbar_push (GTK_STATUSBAR (window->details->statusbar), 0, text);
	}
}

void
nolphin_window_go_to (NolphinWindow *window, GFile *location)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	nolphin_window_slot_open_location (nolphin_window_get_active_slot (window),
					    location, 0);
}

void
nolphin_window_go_to_tab (NolphinWindow *window, GFile *location)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	nolphin_window_slot_open_location (nolphin_window_get_active_slot (window),
					    location, NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB);
}

void
nolphin_window_go_to_full (NolphinWindow *window,
			    GFile          *location,
			    NolphinWindowGoToCallback callback,
			    gpointer        user_data)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	nolphin_window_slot_open_location_full (nolphin_window_get_active_slot (window),
						 location, 0, NULL, callback, user_data);
}

static void
nolphin_window_go_up_signal (NolphinWindow *window)
{
	nolphin_window_slot_go_up (nolphin_window_get_active_slot (window), 0);
}

void
nolphin_window_slot_removed (NolphinWindow *window,  NolphinWindowSlot *slot)
{
	g_signal_emit (window, signals[SLOT_REMOVED], 0, slot);
}

void
nolphin_window_slot_added (NolphinWindow *window,  NolphinWindowSlot *slot)
{
    g_signal_emit (window, signals[SLOT_ADDED], 0, slot);
}

void
nolphin_window_new_tab (NolphinWindow *window)
{
	NolphinWindowSlot *current_slot;
	NolphinWindowSlot *new_slot;
	NolphinWindowOpenFlags flags;
	GFile *location;
	int new_slot_position;
	char *scheme;

	current_slot = nolphin_window_get_active_slot (window);
	location = nolphin_window_slot_get_location (current_slot);

	if (location != NULL) {
		flags = 0;

		new_slot_position = g_settings_get_enum (nolphin_preferences, NOLPHIN_PREFERENCES_NEW_TAB_POSITION);
		if (new_slot_position == NOLPHIN_NEW_TAB_POSITION_END) {
			flags = NOLPHIN_WINDOW_OPEN_SLOT_APPEND;
		}

		scheme = g_file_get_uri_scheme (location);
		if (!strcmp (scheme, "x-nolphin-search")) {
			g_object_unref (location);
			location = g_file_new_for_path (g_get_home_dir ());
		}
		g_free (scheme);

		new_slot = nolphin_window_pane_open_slot (current_slot->pane, flags);
		nolphin_window_set_active_slot (window, new_slot);
		nolphin_window_slot_open_location (new_slot, location, 0);
		g_object_unref (location);
	}
}

/* "Tab duplizieren": currently identical to New Tab, since New Tab
 * already opens at the current tab's location (see above). Kept as
 * its own, separately-labeled action for discoverability, and as a
 * home for future per-tab state cloning (selection, sort, view mode)
 * that New Tab intentionally doesn't carry over. */
void
nolphin_window_duplicate_tab (NolphinWindow *window)
{
	nolphin_window_new_tab (window);
}

void
nolphin_window_close_all_tabs (NolphinWindow *window)
{
	NolphinWindowPane *pane;
	GList *slots_snapshot, *l;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	pane = nolphin_window_get_active_pane (window);
	if (pane == NULL) {
		return;
	}

	/* nolphin_window_pane_close_slot() mutates pane->slots as it goes
	 * (and may close the pane/window once the last one is gone), so
	 * iterate over a snapshot rather than the live list. */
	slots_snapshot = g_list_copy (pane->slots);
	for (l = slots_snapshot; l != NULL; l = l->next) {
		nolphin_window_pane_close_slot (pane, NOLPHIN_WINDOW_SLOT (l->data));
	}
	g_list_free (slots_snapshot);
}

/* Beschriftung der Sperren-Aktion an den Zustand des aktiven Reiters anpassen. */
void
nolphin_window_sync_tab_actions (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	GtkAction *action;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (window->details->main_action_group == NULL) {
		return;
	}

	slot = nolphin_window_get_active_slot (window);
	action = gtk_action_group_get_action (window->details->main_action_group, "Lock Tab");
	if (action != NULL) {
		g_object_set (action, "label",
			      (slot != NULL && slot->locked) ? _("Reiter ent_sperren") : _("Reiter _sperren"),
			      NULL);
	}

	/* Bereichsaktionen: Duplizieren bis vier Bereiche, Maximieren/Schließen nur bei mehreren */
	action = gtk_action_group_get_action (window->details->main_action_group, "Duplicate Pane");
	if (action != NULL) {
		gtk_action_set_sensitive (action, g_list_length (window->details->panes) < 4);
	}
	action = gtk_action_group_get_action (window->details->main_action_group, "Maximize Pane");
	if (action != NULL) {
		gtk_action_set_sensitive (action, g_list_length (window->details->panes) > 1);
		g_object_set (action, "label",
			      GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "nolphin-pane-maximized"))
			      ? _("Bereich _wiederherstellen") : _("Bereich _maximieren"),
			      NULL);
	}
	action = gtk_action_group_get_action (window->details->main_action_group, "Close Pane");
	if (action != NULL) {
		gtk_action_set_sensitive (action, g_list_length (window->details->panes) > 1);
	}
}

void
nolphin_window_toggle_lock_tab (NolphinWindow *window)
{
	NolphinWindowSlot *slot;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	slot = nolphin_window_get_active_slot (window);
	if (slot == NULL) {
		return;
	}

	slot->locked = !slot->locked;
	nolphin_notebook_sync_tab_label (NOLPHIN_NOTEBOOK (slot->pane->notebook), slot);
	nolphin_notebook_update_tabs_visibility (NOLPHIN_NOTEBOOK (slot->pane->notebook));
	nolphin_window_sync_tab_actions (window);
}

/* Ein Popover, das direkt aus einem Menüeintrag geöffnet wird, würde beim
 * Schließen des Menüs sofort wieder verschwinden: deshalb leicht verzögert. */
static gboolean
popover_popup_later_cb (gpointer data)
{
	GtkWidget *popover = GTK_WIDGET (data);
	GtkWidget *focus = g_object_get_data (G_OBJECT (popover), "popup-focus");

	if (gtk_widget_get_parent (popover) != NULL) {
		gtk_popover_popup (GTK_POPOVER (popover));
		if (focus != NULL) {
			gtk_widget_grab_focus (focus);
		}
	}
	g_object_unref (popover);
	return G_SOURCE_REMOVE;
}

static void
popover_popup_later (GtkWidget *popover, GtkWidget *focus)
{
	g_object_set_data (G_OBJECT (popover), "popup-focus", focus);
	g_timeout_add (150, popover_popup_later_cb, g_object_ref (popover));
}

static void
tab_rename_apply (GtkWidget *popover)
{
	GtkWidget *entry = g_object_get_data (G_OBJECT (popover), "rename-entry");
	NolphinWindowSlot *slot = g_object_get_data (G_OBJECT (popover), "rename-slot");

	if (slot != NULL && slot->pane != NULL) {
		gchar *text = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (entry))));

		g_free (slot->custom_title);
		slot->custom_title = (text[0] != '\0' && g_strcmp0 (text, slot->title) != 0) ? text : NULL;
		if (slot->custom_title == NULL) {
			g_free (text);
		}
		nolphin_notebook_sync_tab_label (NOLPHIN_NOTEBOOK (slot->pane->notebook), slot);
	}
	gtk_widget_destroy (popover);
}

static void
tab_rename_entry_activate_cb (GtkEntry *entry, gpointer popover)
{
	tab_rename_apply (GTK_WIDGET (popover));
}

static void
tab_rename_ok_cb (GtkButton *button, gpointer popover)
{
	tab_rename_apply (GTK_WIDGET (popover));
}

/* Reiter umbenennen: kleines Popover am Reiterbereich statt eines Dialogs. */
void
nolphin_window_rename_tab (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	GtkWidget *popover, *box, *entry, *ok;
	GdkRectangle anchor = { 24, 4, 1, 1 };

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	slot = nolphin_window_get_active_slot (window);
	if (slot == NULL || slot->pane == NULL) {
		return;
	}

	popover = gtk_popover_new (slot->pane->notebook);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &anchor);
	gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_BOTTOM);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 8);
	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), slot->custom_title != NULL ? slot->custom_title : slot->title);
	gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("Leer lassen für den automatischen Namen"));
	gtk_entry_set_width_chars (GTK_ENTRY (entry), 26);
	ok = gtk_button_new_with_label (_("Umbenennen"));
	gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), ok, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (popover), box);

	g_object_set_data (G_OBJECT (popover), "rename-entry", entry);
	g_object_set_data_full (G_OBJECT (popover), "rename-slot", g_object_ref (slot), g_object_unref);
	g_signal_connect (entry, "activate", G_CALLBACK (tab_rename_entry_activate_cb), popover);
	g_signal_connect (ok, "clicked", G_CALLBACK (tab_rename_ok_cb), popover);

	gtk_widget_show_all (box);
	popover_popup_later (popover, entry);
}

gboolean
nolphin_window_has_closed_tab_history (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	return window->details->closed_tab_locations != NULL;
}

void
nolphin_window_restore_closed_tab (NolphinWindow *window)
{
	NolphinWindowSlot *current_slot, *new_slot;
	GFile *location;
	NolphinWindowOpenFlags flags;
	gint new_slot_position;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (window->details->closed_tab_locations == NULL) {
		return;
	}

	location = G_FILE (window->details->closed_tab_locations->data);
	window->details->closed_tab_locations =
		g_list_delete_link (window->details->closed_tab_locations,
				    window->details->closed_tab_locations);

	current_slot = nolphin_window_get_active_slot (window);
	if (current_slot == NULL) {
		g_object_unref (location);
		return;
	}

	flags = 0;
	new_slot_position = g_settings_get_enum (nolphin_preferences, NOLPHIN_PREFERENCES_NEW_TAB_POSITION);
	if (new_slot_position == NOLPHIN_NEW_TAB_POSITION_END) {
		flags = NOLPHIN_WINDOW_OPEN_SLOT_APPEND;
	}

	new_slot = nolphin_window_pane_open_slot (current_slot->pane, flags);
	nolphin_window_set_active_slot (window, new_slot);
	nolphin_window_slot_open_location (new_slot, location, 0);
	g_object_unref (location);
}

static void
update_cursor (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	GdkCursor *cursor;

	slot = nolphin_window_get_active_slot (window);

	if (slot && slot->allow_stop) {
		cursor = gdk_cursor_new (GDK_WATCH);
                gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (window)), cursor);
		g_object_unref (cursor);
	} else {
                gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET (window)), NULL);
        }
}

void
nolphin_window_sync_allow_stop (NolphinWindow *window,
				 NolphinWindowSlot *slot)
{
	GtkAction *stop_action;
	GtkAction *reload_action;
	gboolean allow_stop, slot_is_active;
	NolphinNotebook *notebook;

	stop_action = gtk_action_group_get_action (nolphin_window_get_main_action_group (window),
						   NOLPHIN_ACTION_STOP);
	reload_action = gtk_action_group_get_action (nolphin_window_get_main_action_group (window),
						     NOLPHIN_ACTION_RELOAD);
	allow_stop = gtk_action_get_sensitive (stop_action);

	slot_is_active = (slot == nolphin_window_get_active_slot (window));

	if (!slot_is_active ||
	    allow_stop != slot->allow_stop) {
		if (slot_is_active) {
			gtk_action_set_visible (stop_action, slot->allow_stop);
			gtk_action_set_visible (reload_action, !slot->allow_stop);
		}

		if (gtk_widget_get_realized (GTK_WIDGET (window))) {
			update_cursor (window);
		}


		notebook = NOLPHIN_NOTEBOOK (slot->pane->notebook);
		nolphin_notebook_sync_loading (notebook, slot);
	}
}

static void
nolphin_window_prompt_for_location (NolphinWindow *window,
                                 const char *initial)
{
    NolphinWindowPane *pane;

    g_return_if_fail (NOLPHIN_IS_WINDOW (window));

    if (!NOLPHIN_IS_DESKTOP_WINDOW (window)) {
        if (initial) {
            NolphinEntry *entry;
            nolphin_window_show_location_entry(window);
            pane = window->details->active_pane;
            entry = nolphin_location_bar_get_entry (NOLPHIN_LOCATION_BAR (pane->location_bar));
            nolphin_entry_set_text (entry, initial);
            gtk_editable_set_position (GTK_EDITABLE (entry), -1);
        }
    }
}

/* Code should never force the window taller than this size.
 * (The user can still stretch the window taller if desired).
 */
static guint
get_max_forced_height (GdkScreen *screen)
{
	return (gdk_screen_get_height (screen) * 90) / 100;
}

/* Code should never force the window wider than this size.
 * (The user can still stretch the window wider if desired).
 */
static guint
get_max_forced_width (GdkScreen *screen)
{
	return (gdk_screen_get_width (screen) * 90) / 100;
}

/* This must be called when construction of NolphinWindow is finished,
 * since it depends on the type of the argument, which isn't decided at
 * construction time.
 */
static void
nolphin_window_set_initial_window_geometry (NolphinWindow *window)
{
	GdkScreen *screen;
	guint max_width_for_screen, max_height_for_screen;
	guint default_width, default_height;

	screen = gtk_window_get_screen (GTK_WINDOW (window));

	max_width_for_screen = get_max_forced_width (screen);
	max_height_for_screen = get_max_forced_height (screen);

	default_width = NOLPHIN_WINDOW_DEFAULT_WIDTH;
	default_height = NOLPHIN_WINDOW_DEFAULT_HEIGHT;

	gtk_window_set_default_size (GTK_WINDOW (window),
				     MIN (default_width,
				          max_width_for_screen),
				     MIN (default_height,
				          max_height_for_screen));
}

static gboolean
save_sidebar_width_cb (gpointer user_data)
{
	NolphinWindow *window = user_data;

	window->details->sidebar_width_handler_id = 0;

	DEBUG ("Saving sidebar width: %d", window->details->side_pane_width);

	g_settings_set_int (nolphin_window_state,
			    NOLPHIN_WINDOW_STATE_SIDEBAR_WIDTH,
			    window->details->side_pane_width);

	return FALSE;
}

#define NOLPHIN_PREVIEW_MIN_WIDTH 400

static gboolean
save_preview_width_cb (gpointer user_data)
{
	NolphinWindow *window = user_data;
	gint total, position, width;

	window->details->preview_width_handler_id = 0;


	/* Mirrors save_terminal_height_cb: "position" is pack1's (the
	 * file-view side's) extent, so the preview panel's own width is
	 * total - position. */
	total = gtk_widget_get_allocated_width (window->details->preview_hpaned);
	position = gtk_paned_get_position (GTK_PANED (window->details->preview_hpaned));
	width = total - position;

	/* Zu schmale Werte stammen von noch nicht fertigem Layout beim
	 * Start und dürfen den gespeicherten Wert nicht überschreiben. */
	if (width < NOLPHIN_PREVIEW_MIN_WIDTH || total <= 1) {
		return FALSE;
	}

	DEBUG ("Saving preview panel width: %d", width);

	g_settings_set_int (nolphin_window_state,
			    NOLPHIN_WINDOW_STATE_PREVIEW_WIDTH,
			    width);

	return FALSE;
}

static void
preview_size_allocate_callback (GtkWidget *widget,
				GtkAllocation *allocation,
				gpointer user_data)
{
	NolphinWindow *window = user_data;

	if (!gtk_widget_get_visible (widget)) {
		return;
	}

	if (window->details->preview_width_handler_id != 0) {
		g_source_remove (window->details->preview_width_handler_id);
		window->details->preview_width_handler_id = 0;
	}

	window->details->preview_width_handler_id =
		g_timeout_add (100, save_preview_width_cb, window);
}

/* Die gespeicherte Breite des rechten Bereichs gilt bei jeder Änderung der
 * Fensterbreite (auch beim Maximieren nach dem Start); eine vom Benutzer mit
 * dem Teiler gewählte Breite wird gespeichert und danach so beibehalten. */
static void
preview_hpaned_allocate_callback (GtkWidget     *widget,
				  GtkAllocation *allocation,
				  gpointer       user_data)
{
	NolphinWindow *window = user_data;
	gint wanted_width, last_width;

	if (allocation->width <= 1 || !window->details->show_preview) {
		return;
	}

	last_width = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "nolphin-last-width"));
	if (last_width == allocation->width) {
		return;
	}
	g_object_set_data (G_OBJECT (widget), "nolphin-last-width", GINT_TO_POINTER (allocation->width));

	wanted_width = MAX (g_settings_get_int (nolphin_window_state,
						NOLPHIN_WINDOW_STATE_PREVIEW_WIDTH),
			    NOLPHIN_PREVIEW_MIN_WIDTH);
	gtk_paned_set_position (GTK_PANED (widget),
				MAX (allocation->width - wanted_width, 1));
}

/* side pane helpers */
static void
side_pane_size_allocate_callback (GtkWidget *widget,
				  GtkAllocation *allocation,
				  gpointer user_data)
{
	NolphinWindow *window;

	window = user_data;

	if (window->details->sidebar_width_handler_id != 0) {
		g_source_remove (window->details->sidebar_width_handler_id);
		window->details->sidebar_width_handler_id = 0;
	}

	/* Rechts liegt anders als bei Nemo noch der Arbeitsbereich. Ist das Fenster zu
	 * schmal, drückt GtkPaned die Seitenleiste bis an ihre größte mögliche Position.
	 * Diese erzwungene Breite wird nicht gespeichert; sobald wieder Platz ist, kehrt
	 * die Seitenleiste zur gemerkten Breite zurück. */
	if (allocation->width > 1 && window->details->content_paned != NULL) {
		GtkPaned *paned = GTK_PANED (window->details->content_paned);
		gint max_position;

		g_object_get (paned, "max-position", &max_position, NULL);

		if (allocation->width < window->details->side_pane_width &&
		    (window->details->side_pane_clamped ||
		     gtk_paned_get_position (paned) >= max_position)) {
			window->details->side_pane_clamped = TRUE;
			return;
		}

		window->details->side_pane_clamped = FALSE;
	}

	if (allocation->width != window->details->side_pane_width &&
	    allocation->width > 1) {
		window->details->side_pane_width = allocation->width;

		window->details->sidebar_width_handler_id =
			g_timeout_add (100, save_sidebar_width_cb, window);
	}
}

/* Zieht der Benutzer selbst am Teiler, gilt ab dann seine Breite. */
static gboolean
content_paned_button_press_callback (GtkWidget      *widget,
				     GdkEventButton *event,
				     gpointer        user_data)
{
	NOLPHIN_WINDOW (user_data)->details->side_pane_clamped = FALSE;
	return GDK_EVENT_PROPAGATE;
}

static gboolean
restore_side_pane_width_idle (gpointer user_data)
{
	NolphinWindow *window = user_data;
	gint max_position;

	if (window->details == NULL || window->details->content_paned == NULL ||
	    !window->details->side_pane_clamped) {
		return G_SOURCE_REMOVE;
	}

	g_object_get (window->details->content_paned, "max-position", &max_position, NULL);
	gtk_paned_set_position (GTK_PANED (window->details->content_paned),
				MIN (window->details->side_pane_width, max_position));
	return G_SOURCE_REMOVE;
}

/* Wird das Fenster wieder breiter, bekommt eine zuvor erzwungen schmalere
 * Seitenleiste ihre gemerkte Breite zurück (soweit Platz ist). */
static void
content_paned_size_allocate_callback (GtkWidget     *widget,
				      GtkAllocation *allocation,
				      gpointer       user_data)
{
	NolphinWindow *window = user_data;
	gint max_position, position;

	if (!window->details->side_pane_clamped || window->details->sidebar == NULL) {
		return;
	}

	g_object_get (widget, "max-position", &max_position, NULL);
	position = gtk_paned_get_position (GTK_PANED (widget));

	if (position < max_position && position < window->details->side_pane_width) {
		/* Nicht mitten in der Größenzuteilung setzen, sonst verwirft GTK den Wert */
		g_idle_add_full (G_PRIORITY_HIGH_IDLE, restore_side_pane_width_idle,
				 g_object_ref (window), g_object_unref);
	}
}

static void
setup_side_pane_width (NolphinWindow *window)
{
	g_return_if_fail (window->details->sidebar != NULL);

	window->details->side_pane_width =
		g_settings_get_int (nolphin_window_state,
				    NOLPHIN_WINDOW_STATE_SIDEBAR_WIDTH);

	gtk_paned_set_position (GTK_PANED (window->details->content_paned),
				window->details->side_pane_width);
}

static void
nolphin_window_set_up_sidebar (NolphinWindow *window)
{
	GtkWidget *sidebar;

	DEBUG ("Setting up sidebar id %s", window->details->sidebar_id);

	window->details->sidebar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class (gtk_widget_get_style_context (window->details->sidebar),
				     GTK_STYLE_CLASS_SIDEBAR);

	gtk_paned_pack1 (GTK_PANED (window->details->content_paned),
			 GTK_WIDGET (window->details->sidebar),
			 FALSE, FALSE);

	setup_side_pane_width (window);
	g_signal_connect (window->details->sidebar,
			  "size_allocate",
			  G_CALLBACK (side_pane_size_allocate_callback),
			  window);
	if (!g_signal_handler_find (window->details->content_paned, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
				    0, 0, NULL, content_paned_size_allocate_callback, window)) {
		g_signal_connect (window->details->content_paned,
				  "size-allocate",
				  G_CALLBACK (content_paned_size_allocate_callback),
				  window);
		g_signal_connect (window->details->content_paned,
				  "button-press-event",
				  G_CALLBACK (content_paned_button_press_callback),
				  window);
	}

    g_signal_connect_object (NOLPHIN_WINDOW (window), "notify::sidebar-view-id",
                             G_CALLBACK (side_pane_id_changed), window, 0);

    if (g_strcmp0 (window->details->sidebar_id, NOLPHIN_WINDOW_SIDEBAR_PLACES) == 0) {
        sidebar = nolphin_places_sidebar_new (window);
    } else if (g_strcmp0 (window->details->sidebar_id, NOLPHIN_WINDOW_SIDEBAR_TREE) == 0) {
        sidebar = nolphin_tree_sidebar_new (window);
    } else {
        g_assert_not_reached ();
    }

	gtk_box_pack_start (GTK_BOX (window->details->sidebar), sidebar, TRUE, TRUE, 0);
	gtk_widget_show (sidebar);

	/* GID-Projekte: zusätzlicher Bereich unter Orte/Ordnerbaum. */
	gtk_box_pack_end (GTK_BOX (window->details->sidebar), nolphin_gid_sidebar_new (window), FALSE, FALSE, 0);
	gtk_widget_show (GTK_WIDGET (window->details->sidebar));
}

static void
nolphin_window_tear_down_sidebar (NolphinWindow *window)
{
	DEBUG ("Destroying sidebar");

    g_signal_handlers_disconnect_by_func (NOLPHIN_WINDOW (window), side_pane_id_changed, window);

	if (window->details->sidebar != NULL) {
		gtk_widget_destroy (GTK_WIDGET (window->details->sidebar));
		window->details->sidebar = NULL;
	}
}

void
nolphin_window_hide_sidebar (NolphinWindow *window)
{
	DEBUG ("Called hide_sidebar()");

	if (window->details->sidebar == NULL) {
		return;
	}

	nolphin_window_tear_down_sidebar (window);
	nolphin_window_update_show_hide_ui_elements (window);

    nolphin_window_set_show_sidebar (window, FALSE);
}

void
nolphin_window_show_sidebar (NolphinWindow *window)
{
	DEBUG ("Called show_sidebar()");

	if (window->details->sidebar != NULL) {
		return;
	}

	if (window->details->disable_chrome) {
		return;
	}

	nolphin_window_set_up_sidebar (window);
	nolphin_window_update_show_hide_ui_elements (window);

    nolphin_window_set_show_sidebar (window, TRUE);
}

static gboolean
sidebar_id_is_valid (const gchar *sidebar_id)
{
    return (g_strcmp0 (sidebar_id, NOLPHIN_WINDOW_SIDEBAR_PLACES) == 0 ||
            g_strcmp0 (sidebar_id, NOLPHIN_WINDOW_SIDEBAR_TREE) == 0);
}

static void
side_pane_id_changed (NolphinWindow *window)
{

    if (!sidebar_id_is_valid (window->details->sidebar_id)) {
        return;
    }

    /* refresh the sidebar setting */
    nolphin_window_tear_down_sidebar (window);
    nolphin_window_set_up_sidebar (window);
}

gboolean
nolphin_window_disable_chrome_mapping (GValue *value,
					GVariant *variant,
					gpointer user_data)
{
	NolphinWindow *window = user_data;

	g_value_set_boolean (value,
			     g_variant_get_boolean (variant) &&
			     !window->details->disable_chrome);

	return TRUE;
}

static gboolean
on_button_press_callback (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    NolphinWindow *window = NOLPHIN_WINDOW (user_data);

    if (event->button == 3) {
        toggle_menubar (window, MENU_TOGGLE);
    }

    return GDK_EVENT_STOP;
}

static void
clear_menu_hide_delay (NolphinWindow *window)
{
    if (window->details->menu_hide_delay_id > 0) {
        g_source_remove (window->details->menu_hide_delay_id);
    }

    window->details->menu_hide_delay_id = 0;
}

static gboolean
hide_menu_on_delay (NolphinWindow *window)
{
    toggle_menubar (window, MENU_HIDE);

    window->details->menu_hide_delay_id = 0;
    return FALSE;
}

static gboolean
on_menu_focus_out (GtkMenuShell *widget,
                   GdkEvent  *event,
                   gpointer   user_data)
{
    NolphinWindow *window = NOLPHIN_WINDOW (user_data);

    /* The menu, when visible on demand, gets the keyboard grab.
     * If the user clicks on some element in the window,, we want the menu
     * to disappear, but if it's done immediately, everything shifts up the
     * height of the menu, and the user will more than likely end up clicking
     * in the wrong spot.  Delay the hide momentarily, to allow the user to
     * complete their click action. */
    clear_menu_hide_delay (window);

    /* When a submenu pops-up, the menu loses focus. The menu should disappear
     * only when none of its elements is selected. */
    if (!gtk_menu_shell_get_selected_item (widget)) {
        window->details->menu_hide_delay_id = g_timeout_add (200, (GSourceFunc) hide_menu_on_delay, window);
    }

    return GDK_EVENT_PROPAGATE;
}

void
on_menu_selection_done (GtkMenuShell *menushell,
                        gpointer      user_data)
{
	NolphinWindow *window = NOLPHIN_WINDOW (user_data);

	/* Remove the menu inmediately after selecting an item. */
	clear_menu_hide_delay (window);
	window->details->menu_hide_delay_id = g_timeout_add (0, (GSourceFunc) hide_menu_on_delay, window);
}

static void
nolphin_window_constructed (GObject *self)
{
	NolphinWindow *window;
	GtkWidget *grid;
	GtkWidget *menu;
	GtkWidget *hpaned;
	GtkWidget *vbox;
	GtkWidget *content_stack;
	GtkWidget *toolbar_holder;
    GtkWidget *nolphin_statusbar;
	NolphinWindowPane *pane;
	NolphinWindowSlot *slot;
	NolphinApplication *application;

	window = NOLPHIN_WINDOW (self);
	application = nolphin_application_get_singleton ();

	G_OBJECT_CLASS (nolphin_window_parent_class)->constructed (self);
	gtk_window_set_application (GTK_WINDOW (window), GTK_APPLICATION (application));

	/* disable automatic menubar handling, since we show our regular
	 * menubar together with the app menu.
	 */
	gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (self), FALSE);

	grid = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
	gtk_widget_show (grid);
	gtk_container_add (GTK_CONTAINER (window), grid);

	/* Statusbar is packed in the subclasses */

	nolphin_window_initialize_menus (window);
	nolphin_window_initialize_actions (window);

	menu = gtk_ui_manager_get_widget (window->details->ui_manager, "/MenuBar");
	window->details->menubar = menu;

    gtk_widget_set_can_focus (menu, TRUE);
	gtk_widget_set_hexpand (menu, TRUE);

    g_signal_connect_object (menu,
                             "focus-out-event",
                             G_CALLBACK (on_menu_focus_out),
                             window,
                             0);

    g_signal_connect_object (menu,
                             "selection-done",
                             G_CALLBACK (on_menu_selection_done),
                             window,
                             0);

	if (g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_START_WITH_MENU_BAR)){
		gtk_widget_show (menu);
	} else {
		gtk_widget_hide (menu);
	}

    g_settings_bind_with_mapping (nolphin_window_state,
                      NOLPHIN_WINDOW_STATE_START_WITH_MENU_BAR,
                      window->details->menubar,
                      "visible",
                      G_SETTINGS_BIND_GET,
                      nolphin_window_disable_chrome_mapping, NULL,
                      window, NULL);

	gtk_container_add (GTK_CONTAINER (grid), menu);

	/* Set up the toolbar place holder */
	toolbar_holder = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (grid), toolbar_holder);
	gtk_widget_show (toolbar_holder);

    g_signal_connect_object (toolbar_holder, "button-press-event",
                             G_CALLBACK (on_button_press_callback), window, 0);

	window->details->toolbar_holder = toolbar_holder;

	/* Register to menu provider extension signal managing menu updates */
	g_signal_connect_object (nolphin_signaller_get_current (), "popup_menu_changed",
			 G_CALLBACK (nolphin_window_load_extension_menus), window, G_CONNECT_SWAPPED);

	window->details->content_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand (window->details->content_paned, TRUE);
	gtk_widget_set_vexpand (window->details->content_paned, TRUE);

	/* The stack lets full-page views (e.g. the preferences) replace the
	 * file area while menu bar and toolbar stay in place. */
	content_stack = gtk_stack_new ();
	gtk_widget_set_hexpand (content_stack, TRUE);
	gtk_widget_set_vexpand (content_stack, TRUE);
	gtk_stack_add_named (GTK_STACK (content_stack), window->details->content_paned, "files");
	g_object_set_data (G_OBJECT (window), "nolphin-content-stack", content_stack);
	gtk_container_add (GTK_CONTAINER (grid), content_stack);
	gtk_widget_show (content_stack);
	gtk_widget_show (window->details->content_paned);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_paned_pack2 (GTK_PANED (window->details->content_paned), vbox,
			 TRUE, FALSE);
	gtk_widget_show (vbox);

	hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_style_context_add_class (gtk_widget_get_style_context (hpaned), "nolphin-split-paned");
	gtk_widget_show (hpaned);
	window->details->split_view_hpane = hpaned;

	/* info/preview panel (F11): file view (incl. split view) on the
	 * left, Arbeitsbereich-Leiste (Vorschau/Eigenschaften/Archiv/
	 * Terminal/Git/.deb-Paket) auf der rechten Seite. Das integrierte
	 * Terminal (F4) lebt als Seite in dieser Leiste (siehe
	 * nolphin-workspace-panel.c) statt in einem eigenen unteren
	 * Bereich - spart Bildschirmplatz gegenüber einer zusätzlichen
	 * horizontalen Teilung. */
	window->details->preview_hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (vbox), window->details->preview_hpaned, TRUE, TRUE, 0);
	gtk_widget_show (window->details->preview_hpaned);
	gtk_paned_pack1 (GTK_PANED (window->details->preview_hpaned), hpaned, TRUE, FALSE);

	window->details->preview = nolphin_preview_new ();
	window->details->terminal = nolphin_terminal_new ();
	window->details->workspace_panel = nolphin_workspace_panel_new (window,
									 window->details->preview,
									 window->details->terminal);
	/* Nicht schrumpfbar: die Leiste behält ihre Mindestbreite, das Fenster
	 * wird dafür notfalls breiter, statt den Inhalt abzuschneiden. */
	gtk_paned_pack2 (GTK_PANED (window->details->preview_hpaned), window->details->workspace_panel, FALSE, FALSE);
	window->details->show_preview = FALSE;
	window->details->show_terminal = FALSE;

	g_signal_connect (window->details->workspace_panel, "size-allocate",
			  G_CALLBACK (preview_size_allocate_callback), window);
	g_signal_connect (window->details->preview_hpaned, "size-allocate",
			  G_CALLBACK (preview_hpaned_allocate_callback), window);

	pane = nolphin_window_pane_new (window);
	window->details->panes = g_list_prepend (window->details->panes, pane);

	gtk_paned_pack1 (GTK_PANED (hpaned), GTK_WIDGET (pane), TRUE, FALSE);


    nolphin_statusbar = nolphin_status_bar_new (window);
    window->details->nolphin_status_bar = nolphin_statusbar;

    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add (GTK_CONTAINER (grid), sep);
    gtk_widget_show (sep);
    window->details->statusbar_separator = sep;

    GtkWidget *eb;

    eb = gtk_event_box_new ();
    gtk_container_add (GTK_CONTAINER (eb), nolphin_statusbar);
    gtk_container_add (GTK_CONTAINER (grid), eb);
    gtk_widget_show (eb);

    window->details->statusbar = nolphin_status_bar_get_real_statusbar (NOLPHIN_STATUS_BAR (nolphin_statusbar));
    window->details->help_message_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->details->statusbar),
                                                                      "help_message");

    gtk_widget_add_events (GTK_WIDGET (eb), GDK_BUTTON_PRESS_MASK);

    g_signal_connect_object (GTK_WIDGET (eb), "button-press-event",
                             G_CALLBACK (on_button_press_callback), window, 0);

    g_settings_bind_with_mapping (nolphin_window_state,
                      NOLPHIN_WINDOW_STATE_START_WITH_STATUS_BAR,
                      window->details->nolphin_status_bar,
                      "visible",
                      G_SETTINGS_BIND_DEFAULT,
                      nolphin_window_disable_chrome_mapping, NULL,
                      window, NULL);

    g_object_bind_property (window->details->nolphin_status_bar, "visible",
                            sep, "visible",
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

	/* this has to be done after the location bar has been set up,
	 * but before menu stuff is being called */
	nolphin_window_set_active_pane (window, pane);

	/* Das Terminal startet bewusst NIE automatisch mit - anders als bei
	 * der Vorschau (unten) gibt es dafuer keinen "Merk dir das"-Nutzen:
	 * die Vorschau ist immer die Ruhelage des Arbeitsbereichs, das
	 * Terminal ist eine bewusst ausgeloeste Funktion (F4/Menue). */

	/* Die Arbeitsbereich-Leiste gehört nur ins normale Datei-Fenster -
	 * das Desktop-Fenster (NolphinDesktopWindow erbt von NolphinWindow)
	 * zeigt sonst Vorschau/Eigenschaften eines "x-nolphin-desktop"-
	 * Objekts über den eigentlichen Desktop-Icons an. */
	if (!NOLPHIN_IS_DESKTOP_WINDOW (window)) {
		nolphin_window_set_show_preview (window,
						 g_settings_get_boolean (nolphin_window_state,
									  NOLPHIN_WINDOW_STATE_START_WITH_PREVIEW));
	}

	side_pane_id_changed (window);

	nolphin_window_initialize_bookmarks_menu (window);
	if (!NOLPHIN_IS_DESKTOP_WINDOW (window))
		nolphin_gid_menu_initialize (window);
	nolphin_window_set_initial_window_geometry (window);

	slot = nolphin_window_pane_open_slot (window->details->active_pane, 0);
	nolphin_window_set_active_slot (window, slot);

    if (g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_START_WITH_DUAL_PANE) &&
        !window->details->disable_chrome)
        nolphin_window_split_view_on (window);

    g_signal_connect_swapped (GTK_WINDOW (window),
                              "notify::scale-factor",
                              G_CALLBACK (nolphin_window_reload),
                              window);
}

static void
nolphin_window_set_property (GObject *object,
			      guint arg_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	NolphinWindow *window;

	window = NOLPHIN_WINDOW (object);

	switch (arg_id) {
	case PROP_DISABLE_CHROME:
		window->details->disable_chrome = g_value_get_boolean (value);
		break;
    case PROP_SIDEBAR_VIEW_TYPE:
        window->details->sidebar_id = g_strdup (g_value_get_string (value));
        break;
    case PROP_SHOW_SIDEBAR:
        nolphin_window_set_show_sidebar (window, g_value_get_boolean (value));
        break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
		break;
	}
}

static void
nolphin_window_get_property (GObject *object,
			      guint arg_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	NolphinWindow *window;

	window = NOLPHIN_WINDOW (object);

	switch (arg_id) {
        case PROP_DISABLE_CHROME:
            g_value_set_boolean (value, window->details->disable_chrome);
            break;
        case PROP_SIDEBAR_VIEW_TYPE:
            g_value_set_string (value, window->details->sidebar_id);
            break;
        case PROP_SHOW_SIDEBAR:
            g_value_set_boolean (value, window->details->show_sidebar);
            break;
        default:
        	g_assert_not_reached ();
        	break;
	}
}

static void
destroy_panes_foreach (gpointer data,
		       gpointer user_data)
{
	NolphinWindowPane *pane = data;
	NolphinWindow *window = user_data;

	nolphin_window_close_pane (window, pane);
}

static void
nolphin_window_destroy (GtkWidget *object)
{
	NolphinWindow *window;
	GList *panes_copy;

	window = NOLPHIN_WINDOW (object);

	DEBUG ("Destroying window");

	/* close the sidebar first */
	nolphin_window_tear_down_sidebar (window);

	/* close all panes safely */
	panes_copy = g_list_copy (window->details->panes);
	g_list_foreach (panes_copy, (GFunc) destroy_panes_foreach, window);
	g_list_free (panes_copy);

	/* the panes list should now be empty */
	g_assert (window->details->panes == NULL);
	g_assert (window->details->active_pane == NULL);

	GTK_WIDGET_CLASS (nolphin_window_parent_class)->destroy (object);
}

static void
nolphin_window_finalize (GObject *object)
{
	NolphinWindow *window;

	window = NOLPHIN_WINDOW (object);

	if (window->details->sidebar_width_handler_id != 0) {
		g_source_remove (window->details->sidebar_width_handler_id);
		window->details->sidebar_width_handler_id = 0;
	}

	g_list_free_full (window->details->closed_tab_locations, g_object_unref);
	window->details->closed_tab_locations = NULL;

    g_signal_handlers_disconnect_by_func (nolphin_preferences,
                                          nolphin_window_sync_thumbnail_action,
                                          window);

    clear_menu_hide_delay (window);

	nolphin_window_finalize_menus (window);

	g_clear_object (&window->details->nav_state);
    g_clear_object (&window->details->secondary_pane_last_location);

	g_clear_object (&window->details->ui_manager);

	g_free (window->details->sidebar_id);

	/* nolphin_window_close() should have run */
	g_assert (window->details->panes == NULL);

	G_OBJECT_CLASS (nolphin_window_parent_class)->finalize (object);
}

void
nolphin_window_view_visible (NolphinWindow *window,
			      NolphinView *view)
{
	NolphinWindowSlot *slot;
	NolphinWindowPane *pane;
	GList *l, *walk;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	slot = nolphin_window_get_slot_for_view (window, view);

	if (slot->visible) {
		return;
	}

	slot->visible = TRUE;
	pane = slot->pane;

	if (gtk_widget_get_visible (GTK_WIDGET (pane))) {
		return;
	}

	/* Look for other non-visible slots */
	for (l = pane->slots; l != NULL; l = l->next) {
		slot = l->data;

		if (!slot->visible) {
			return;
		}
	}

	/* None, this pane is visible */
	gtk_widget_show (GTK_WIDGET (pane));

	/* Look for other non-visible panes */
	for (walk = window->details->panes; walk; walk = walk->next) {
		pane = walk->data;

		if (!gtk_widget_get_visible (GTK_WIDGET (pane))) {
			return;
		}

		for (l = pane->slots; l != NULL; l = l->next) {
			slot = l->data;

			nolphin_window_slot_update_title (slot);
			nolphin_window_slot_update_icon (slot);
		}
	}

	nolphin_window_pane_grab_focus (window->details->active_pane);

	/* All slots and panes visible, show window */
	gtk_widget_show (GTK_WIDGET (window));
}

static gboolean
nolphin_window_is_desktop (NolphinWindow *window)
{
    return window->details->disable_chrome;
}

static void
nolphin_window_save_geometry (NolphinWindow *window)
{
	char *geometry_string;
	gboolean is_maximized;

	g_assert (NOLPHIN_IS_WINDOW (window));

	if (gtk_widget_get_window (GTK_WIDGET (window)) && !nolphin_window_is_desktop (window)) {
        GdkWindowState state = gdk_window_get_state (gtk_widget_get_window (GTK_WIDGET (window)));

        if (state & GDK_WINDOW_STATE_TILED) {
            return;
        }

        geometry_string = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));

		is_maximized = state & GDK_WINDOW_STATE_MAXIMIZED;

		if (!is_maximized) {
			g_settings_set_string
				(nolphin_window_state, NOLPHIN_WINDOW_STATE_GEOMETRY,
				 geometry_string);
		}
		g_free (geometry_string);

		g_settings_set_boolean
			(nolphin_window_state, NOLPHIN_WINDOW_STATE_MAXIMIZED,
			 is_maximized);
	}
}

void
nolphin_window_close (NolphinWindow *window)
{
	NOLPHIN_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->close (window);
}

static void set_pane_maximized (NolphinWindow *window, gboolean maximized);

/* Hat ein verschachtelter GtkPaned (bei 3-4 Bereichen) nur noch ein Kind,
 * wird er aufgelöst: das Kind rückt an seine Stelle im übergeordneten Paned. */
static void
collapse_nested_paned (NolphinWindow *window, GtkPaned *paned)
{
	GtkWidget *remaining;
	GtkWidget *grandparent_widget;
	GtkPaned *grandparent;
	gboolean was_child1;

	if (GTK_WIDGET (paned) == window->details->split_view_hpane) {
		return;
	}
	if (gtk_paned_get_child1 (paned) != NULL && gtk_paned_get_child2 (paned) != NULL) {
		return;
	}

	remaining = gtk_paned_get_child1 (paned) != NULL ? gtk_paned_get_child1 (paned)
							  : gtk_paned_get_child2 (paned);
	grandparent_widget = gtk_widget_get_parent (GTK_WIDGET (paned));
	if (remaining == NULL || !GTK_IS_PANED (grandparent_widget)) {
		return;
	}
	grandparent = GTK_PANED (grandparent_widget);
	was_child1 = gtk_paned_get_child1 (grandparent) == GTK_WIDGET (paned);

	g_object_ref (remaining);
	gtk_container_remove (GTK_CONTAINER (paned), remaining);
	gtk_container_remove (GTK_CONTAINER (grandparent), GTK_WIDGET (paned));
	if (was_child1) {
		gtk_paned_pack1 (grandparent, remaining, TRUE, FALSE);
	} else {
		gtk_paned_pack2 (grandparent, remaining, TRUE, FALSE);
	}
	g_object_unref (remaining);
}

void
nolphin_window_close_pane (NolphinWindow *window,
			    NolphinWindowPane *pane)
{
	GtkWidget *pane_parent;

	g_assert (NOLPHIN_IS_WINDOW_PANE (pane));

	/* Ein geschlossener Bereich beendet auch den maximierten Zustand. */
	set_pane_maximized (window, FALSE);
	pane_parent = gtk_widget_get_parent (GTK_WIDGET (pane));

	while (pane->slots != NULL) {
		NolphinWindowSlot *slot = pane->slots->data;

		nolphin_window_pane_remove_slot_unsafe (pane, slot);
	}

	/* If the pane was active, set it to NULL. The caller is responsible
	 * for setting a new active pane with nolphin_window_set_active_pane()
	 * if it wants to continue using the window. */
	if (window->details->active_pane == pane) {
		window->details->active_pane = NULL;
	}
	if (window->details->previous_pane == pane) {
		window->details->previous_pane = NULL;
	}

	/* Required really. Destroying the NolphinWindowPane still leaves behind the toolbar.
	 * This kills it off. Do it before we call gtk_widget_destroy for safety. */
	gtk_container_remove (GTK_CONTAINER (window->details->toolbar_holder), GTK_WIDGET (pane->tool_bar));

	window->details->panes = g_list_remove (window->details->panes, pane);

	gtk_widget_destroy (GTK_WIDGET (pane));

	if (pane_parent != NULL && GTK_IS_PANED (pane_parent)) {
		collapse_nested_paned (window, GTK_PANED (pane_parent));
	}
}

NolphinWindowPane*
nolphin_window_get_active_pane (NolphinWindow *window)
{
	g_assert (NOLPHIN_IS_WINDOW (window));
	return window->details->active_pane;
}

static void
real_set_active_pane (NolphinWindow *window, NolphinWindowPane *new_pane)
{
	/* make old pane inactive, and new one active.
	 * Currently active pane may be NULL (after init). */
	if (window->details->active_pane &&
	    window->details->active_pane != new_pane) {
		nolphin_window_pane_set_active (window->details->active_pane, FALSE);
		window->details->previous_pane = window->details->active_pane;
	}
	nolphin_window_pane_set_active (new_pane, TRUE);

	window->details->active_pane = new_pane;
}

/* Make the given pane the active pane of its associated window. This
 * always implies making the containing active slot the active slot of
 * the window. */
void
nolphin_window_set_active_pane (NolphinWindow *window,
				 NolphinWindowPane *new_pane)
{
	g_assert (NOLPHIN_IS_WINDOW_PANE (new_pane));

	DEBUG ("Setting new pane %p as active", new_pane);

	if (new_pane->active_slot) {
		nolphin_window_set_active_slot (window, new_pane->active_slot);
	} else if (new_pane != window->details->active_pane) {
		real_set_active_pane (window, new_pane);
	}
}

/* Make both, the given slot the active slot and its corresponding
 * pane the active pane of the associated window.
 * new_slot may be NULL. */
void
nolphin_window_set_active_slot (NolphinWindow *window, NolphinWindowSlot *new_slot)
{
	NolphinWindowSlot *old_slot;
	g_assert (NOLPHIN_IS_WINDOW (window));

	DEBUG ("Setting new slot %p as active", new_slot);

	if (new_slot) {
		g_assert ((window == nolphin_window_slot_get_window (new_slot)));
		g_assert (NOLPHIN_IS_WINDOW_PANE (new_slot->pane));
		g_assert (g_list_find (new_slot->pane->slots, new_slot) != NULL);
	}

	old_slot = nolphin_window_get_active_slot (window);

	if (old_slot == new_slot) {
		return;
	}

	/* make old slot inactive if it exists (may be NULL after init, for example) */
	if (old_slot != NULL) {
		/* inform window */
		if (old_slot->content_view != NULL) {
			nolphin_window_disconnect_content_view (window, old_slot->content_view);
		}
		gtk_widget_hide (GTK_WIDGET (old_slot->pane->tool_bar));
		/* inform slot & view */
		g_signal_emit_by_name (old_slot, "inactive");
	}

	/* deal with panes */
	if (new_slot &&
	    new_slot->pane != window->details->active_pane) {
		real_set_active_pane (window, new_slot->pane);
	}

	window->details->active_pane->active_slot = new_slot;

	/* make new slot active, if it exists */
	if (new_slot) {
		/* inform sidebar panels */
                nolphin_window_report_location_change (window);
		/* TODO decide whether "selection-changed" should be emitted */

		if (new_slot->content_view != NULL) {
                        /* inform window */
                        nolphin_window_connect_content_view (window, new_slot->content_view);
                }

		// Show active toolbar
		gboolean show_toolbar;
		show_toolbar = g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_START_WITH_TOOLBAR);

		if ( show_toolbar) {
			gtk_widget_show (GTK_WIDGET (new_slot->pane->tool_bar));
		}

		/* inform slot & view */
                g_signal_emit_by_name (new_slot, "active");
	}

	nolphin_window_sync_tab_actions (window);
}

static void
nolphin_window_realize (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (nolphin_window_parent_class)->realize (widget);
	update_cursor (NOLPHIN_WINDOW (widget));
}

static void
toggle_menubar (NolphinWindow *window, gint action)
{
    GtkWidget *menu;
    gboolean default_visible;

    default_visible = g_settings_get_boolean (nolphin_window_state,
                                              NOLPHIN_WINDOW_STATE_START_WITH_MENU_BAR);

    if (default_visible || window->details->disable_chrome) {
        return;
    }

    menu = window->details->menubar;

    if (action == MENU_TOGGLE) {
        action = gtk_widget_get_visible (menu) ? MENU_HIDE : MENU_SHOW;
    }

    if (action == MENU_HIDE) {
        gtk_widget_hide (menu);
    } else {
        gtk_widget_show (menu);

        /* When the menu is normally hidden, have an activation of it trigger a key grab.
         * For keyboard users, this is a natural progression, that they will type a mnemonic
         * next to open a menu.  Any loss of focus or click elsewhere will re-hide the menu
         * and cancel focus.
         */
        gtk_widget_grab_focus (menu);
        gtk_window_set_mnemonics_visible (GTK_WINDOW (window), TRUE);
    }

    return;
}

static gboolean
is_alt_key_event (GdkEventKey *event)
{
    GdkModifierType nominal_state;
    gboolean state_ok;

    nominal_state = event->state & gtk_accelerator_get_default_mod_mask();

    /* A key press of alt will show just the alt keyval (GDK_KEY_Alt_L/R).  A key release
     * of a single modifier is always modified by itself.  So a valid press state is 0 and
     * a valid release state is GDK_MOD1_MASK (alt modifier).
     */
    state_ok = (event->type == GDK_KEY_PRESS && nominal_state == 0) ||
               (event->type == GDK_KEY_RELEASE && nominal_state == GDK_MOD1_MASK);

    if (state_ok && (event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
nolphin_window_key_press_event (GtkWidget *widget,
				 GdkEventKey *event)
{
	NolphinWindow *window;
	NolphinWindowSlot *active_slot;
	NolphinView *view;
	GtkWidget *focus_widget;
	size_t i;

	window = NOLPHIN_WINDOW (widget);

	active_slot = nolphin_window_get_active_slot (window);
	view = active_slot->content_view;

      /**
       * Disable the GTK Emoji Chooser
       */
      if ((event->keyval == GDK_KEY_semicolon || event->keyval == GDK_KEY_period) && (event->state & GDK_CONTROL_MASK)) {
          return FALSE;
      }

	if (view != NULL && nolphin_view_get_is_renaming (view) && event->keyval != GDK_KEY_F2) {
		/* if we're renaming, just forward the event to the
		 * focused widget and return. We don't want to process the window
		 * accelerator bindings, as they might conflict with the
		 * editable widget bindings.
		 */
		if (gtk_window_propagate_key_event (GTK_WINDOW (window), event)) {
			return TRUE;
		}

               /* Do not allow for other accelerator bindings to fire off while
                *  renaming is in progress
                */
               return FALSE;
	}

	/* F4 wird VOR der Weiterleitung an ein fokussiertes GtkEditable
	 * behandelt: ein fokussiertes GtkEntry (Adressleiste, Suchfeld, …)
	 * markiert in GTK praktisch jeden Tastendruck als "verarbeitet" -
	 * auch Tasten, mit denen es gar nichts anfängt - wodurch F4 sonst
	 * lautlos verschluckt würde, sobald irgendein Eingabefeld den Fokus
	 * hat. Ob das Terminal gerade sichtbar ist, wird direkt an der
	 * tatsächlich sichtbaren Stack-Seite abgelesen statt am
	 * show_terminal-Flag, das durch andere Panel-Seiten (Archiv, Suche, …)
	 * veralten kann, ohne zurückgesetzt zu werden. */
	if (event->keyval == GDK_KEY_F4 && (event->state & gtk_accelerator_get_default_mod_mask ()) == 0) {
		const gchar *visible_page = gtk_stack_get_visible_child_name (GTK_STACK (window->details->workspace_panel));
		gboolean terminal_currently_shown = window->details->show_preview &&
			g_strcmp0 (visible_page, "terminal") == 0;

		nolphin_window_set_show_terminal (window, !terminal_currently_shown);
		return TRUE;
	}

	focus_widget = gtk_window_get_focus (GTK_WINDOW (window));
	if (view != NULL && focus_widget != NULL &&
	    GTK_IS_EDITABLE (focus_widget)) {
		/* if we have input focus on a GtkEditable (e.g. a GtkEntry), forward
		 * the event to it before activating accelerator bindings too.
		 */
		if (gtk_window_propagate_key_event (GTK_WINDOW (window), event)) {
			return TRUE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (extra_window_keybindings); i++) {
		if (extra_window_keybindings[i].keyval == event->keyval) {
			const GList *action_groups;
			GtkAction *action;

			action = NULL;

			action_groups = gtk_ui_manager_get_action_groups (window->details->ui_manager);
			while (action_groups != NULL && action == NULL) {
				action = gtk_action_group_get_action (action_groups->data, extra_window_keybindings[i].action);
				action_groups = action_groups->next;
			}

			g_assert (action != NULL);
			if (gtk_action_is_sensitive (action)) {
				gtk_action_activate (action);
				return TRUE;
			}

			break;
		}
	}

    /* An alt key press by itself will always hide the menu if it's visible.  We set a flag
     * to skip the subsequent release, otherwise we'll show the menu again.
     *
     * When alt is pressed and the menu is NOT visible, we flag that on release we'll show the
     * menu.  If any other keys are pressed between alt being pressed and released, we clear that
     * flag, because it was more than likely part of some other shortcut, and otherwise, depending
     * on the order the keys are released, if the alt key is last to be released, we don't want to
     * show the menu, as that was not the original intent.
     */

    if (is_alt_key_event (event)) {
        if (gtk_widget_get_visible (window->details->menubar)) {
            toggle_menubar (window, MENU_HIDE);
            window->details->menu_skip_release = TRUE;
        } else {
            window->details->menu_show_queued = TRUE;
        }
    } else {
        window->details->menu_show_queued = FALSE;
    }

	return GTK_WIDGET_CLASS (nolphin_window_parent_class)->key_press_event (widget, event);
}

static gboolean
nolphin_window_key_release_event (GtkWidget *widget,
                             GdkEventKey *event)
{
    NolphinWindow *window = NOLPHIN_WINDOW (widget);

    /* Conditions to show the menu via the alt key is that it must have been pressed and
     * released without any other key events in between, and we must not have hidden the
     * menu on the alt key press event.  Show we check both flags here, for opposing states.
     */

    if (is_alt_key_event (event)) {
        if (!window->details->menu_skip_release && window->details->menu_show_queued) {
            toggle_menubar (window, MENU_SHOW);
        }
    }

    window->details->menu_skip_release = FALSE;
    window->details->menu_show_queued = FALSE;

    return GTK_WIDGET_CLASS (nolphin_window_parent_class)->key_release_event (widget, event);
}

/*
 * Main API
 */

static void
sync_view_type_callback (NolphinFile *file,
                         gpointer callback_data)
{
    NolphinWindow *window;
    NolphinWindowSlot *slot;

    slot = callback_data;
    window = nolphin_window_slot_get_window (slot);

    if (slot == nolphin_window_get_active_slot (window)) {
        const gchar *view_id;

        if (slot->content_view == NULL) {
            return;
        }

        view_id = nolphin_window_slot_get_content_view_id (slot);

        toolbar_set_view_button (action_for_view_id (view_id), window);
        menu_set_view_selection (action_for_view_id (view_id), window);
    }
}

static void
cancel_sync_view_type_callback (NolphinWindowSlot *slot)
{
	nolphin_file_cancel_call_when_ready (slot->viewed_file,
					      sync_view_type_callback,
					      slot);
}

void
nolphin_window_sync_view_type (NolphinWindow *window)
{
    NolphinWindowSlot *slot;
    NolphinFileAttributes attributes;

    g_return_if_fail (NOLPHIN_IS_WINDOW (window));

    attributes = nolphin_mime_actions_get_required_file_attributes ();

    slot = nolphin_window_get_active_slot (window);

    cancel_sync_view_type_callback (slot);
    nolphin_file_call_when_ready (slot->viewed_file,
                               attributes,
                               sync_view_type_callback,
                               slot);
}

void
nolphin_window_sync_menu_bar (NolphinWindow *window)
{
    GtkWidget *menu = window->details->menubar;

    if (g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_START_WITH_MENU_BAR) &&
                                !window->details->disable_chrome) {
        gtk_widget_show (menu);
    } else {
        gtk_widget_hide (menu);
    }
}

void
nolphin_window_sync_title (NolphinWindow *window,
			    NolphinWindowSlot *slot)
{
	NolphinWindowPane *pane;
	NolphinNotebook *notebook;
	char *full_title;
	char *window_title;

	if (NOLPHIN_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->sync_title != NULL) {
		NOLPHIN_WINDOW_CLASS (G_OBJECT_GET_CLASS (window))->sync_title (window, slot);

		return;
	}

	if (slot == nolphin_window_get_active_slot (window)) {
		/* if spatial mode is default, we keep "File Browser" in the window title
		 * to recognize browser windows. Otherwise, we default to the directory name.
		 */
		if (!g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER)) {
			full_title = g_strdup_printf (_("%s - Dateiverwaltung"), slot->title);
			window_title = eel_str_middle_truncate (full_title, MAX_TITLE_LENGTH);
			g_free (full_title);
		} else {
			window_title = eel_str_middle_truncate (slot->title, MAX_TITLE_LENGTH);
		}

		if (nolphin_is_development_build (NULL)) {
			gchar *dev_title = g_strdup_printf (_("%s  [Entwicklung]"), window_title);

			g_free (window_title);
			window_title = dev_title;
		}
		gtk_window_set_title (GTK_WINDOW (window), window_title);
		g_free (window_title);
	}

	pane = slot->pane;
	notebook = NOLPHIN_NOTEBOOK (pane->notebook);
	nolphin_notebook_sync_tab_label (notebook, slot);
}

void
nolphin_window_sync_zoom_widgets (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	NolphinView *view;
	GtkActionGroup *action_group;
	GtkAction *action;
	gboolean supports_zooming;
	gboolean can_zoom, can_zoom_in, can_zoom_out;
	NolphinZoomLevel zoom_level;

	slot = nolphin_window_get_active_slot (window);
	view = slot->content_view;

	if (view != NULL) {
		supports_zooming = nolphin_view_supports_zooming (view);
		zoom_level = nolphin_view_get_zoom_level (view);
		can_zoom = supports_zooming &&
			   zoom_level >= NOLPHIN_ZOOM_LEVEL_SMALLEST &&
			   zoom_level <= NOLPHIN_ZOOM_LEVEL_LARGEST;
		can_zoom_in = can_zoom && nolphin_view_can_zoom_in (view);
		can_zoom_out = can_zoom && nolphin_view_can_zoom_out (view);
	} else {
		supports_zooming = FALSE;
		can_zoom = FALSE;
		can_zoom_in = FALSE;
		can_zoom_out = FALSE;
	}

	action_group = nolphin_window_get_main_action_group (window);

	action = gtk_action_group_get_action (action_group,
					      NOLPHIN_ACTION_ZOOM_IN);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom_in);

	action = gtk_action_group_get_action (action_group,
					      NOLPHIN_ACTION_ZOOM_OUT);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom_out);

	action = gtk_action_group_get_action (action_group,
					      NOLPHIN_ACTION_ZOOM_NORMAL);
	gtk_action_set_visible (action, supports_zooming);
	gtk_action_set_sensitive (action, can_zoom);

    nolphin_status_bar_sync_zoom_widgets (NOLPHIN_STATUS_BAR (window->details->nolphin_status_bar));
}

void
nolphin_window_sync_bookmark_action (NolphinWindow *window)
{
    NolphinWindowSlot *slot;
    GFile *location;
    GtkAction *action;
    gchar *uri;
    slot = nolphin_window_get_active_slot (window);
    location = nolphin_window_slot_get_location (slot);

    if (!location) {
        return;
    }

    uri = g_file_get_uri (location);

    action = gtk_action_group_get_action (nolphin_window_get_main_action_group (window),
                                          NOLPHIN_ACTION_ADD_BOOKMARK);

    gtk_action_set_sensitive (action, !eel_uri_is_search (uri));

    g_free (uri);
    g_object_unref (location);
}

void
sync_thumbnail_action_callback (NolphinFile *file,
                       gpointer callback_data)
{
    NolphinWindow *window;
    NolphinWindowSlot *slot;

    slot = callback_data;
    window = nolphin_window_slot_get_window (slot);

    if (slot == nolphin_window_get_active_slot (window)) {
        NolphinWindowPane *pane;
        gboolean show_thumbnails;

        pane = nolphin_window_get_active_pane(window);
        show_thumbnails = nolphin_file_should_show_thumbnail (file);

        toolbar_set_show_thumbnails_button (show_thumbnails, pane);
        menu_set_show_thumbnails_action (show_thumbnails, window);
    }
}

static void
cancel_sync_show_thumbnail_callback (NolphinWindowSlot *slot)
{
	nolphin_file_cancel_call_when_ready (slot->viewed_file,
					      sync_thumbnail_action_callback,
					      slot);
}

void
nolphin_window_sync_thumbnail_action (NolphinWindow *window)
{
    NolphinWindowSlot *slot;
    NolphinFileAttributes attributes;

    g_return_if_fail (NOLPHIN_IS_WINDOW (window));

    attributes = nolphin_mime_actions_get_required_file_attributes ();

    slot = nolphin_window_get_active_slot (window);

    cancel_sync_show_thumbnail_callback (slot);
    nolphin_file_call_when_ready (slot->viewed_file,
                               attributes,
                               sync_thumbnail_action_callback,
                               slot);
}

void
nolphin_window_sync_create_folder_button (NolphinWindow *window)
{
    NolphinWindowSlot *slot;
    gboolean allow;

    slot = nolphin_window_get_active_slot (window);

    allow = nolphin_file_can_write (slot->viewed_file) &&
            !nolphin_file_is_in_favorites (slot->viewed_file) &&
            !nolphin_file_is_in_trash (slot->viewed_file);

    toolbar_set_create_folder_button (allow, slot->pane);
}

static void
zoom_level_changed_callback (NolphinView *view,
                             NolphinWindow *window)
{
	g_assert (NOLPHIN_IS_WINDOW (window));

	/* This is called each time the component in
	 * the active slot successfully completed
	 * a zooming operation.
	 */
	nolphin_window_sync_zoom_widgets (window);
}


/* These are called
 *   A) when switching the view within the active slot
 *   B) when switching the active slot
 *   C) when closing the active slot (disconnect)
*/
void
nolphin_window_connect_content_view (NolphinWindow *window,
				      NolphinView *view)
{
	NolphinWindowSlot *slot;

	g_assert (NOLPHIN_IS_WINDOW (window));
	g_assert (NOLPHIN_IS_VIEW (view));

	slot = nolphin_window_get_slot_for_view (window, view);

	if (slot != nolphin_window_get_active_slot (window)) {
		return;
	}

	g_signal_connect (view, "zoom-level-changed",
			  G_CALLBACK (zoom_level_changed_callback),
			  window);

	g_signal_connect_swapped (view, "selection-changed",
				  G_CALLBACK (nolphin_window_sync_preview_selection),
				  window);

    /* Update displayed the selected view type in the toolbar and menu. */
    if (slot->pending_location == NULL) {
        nolphin_window_sync_view_type (window);
    }

	nolphin_window_sync_preview_selection (window);

	nolphin_view_grab_focus (view);
}

void
nolphin_window_disconnect_content_view (NolphinWindow *window,
					 NolphinView *view)
{
	NolphinWindowSlot *slot;

	g_assert (NOLPHIN_IS_WINDOW (window));
	g_assert (NOLPHIN_IS_VIEW (view));

	slot = nolphin_window_get_slot_for_view (window, view);

	if (slot != nolphin_window_get_active_slot (window)) {
		return;
	}

	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (zoom_level_changed_callback), window);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (nolphin_window_sync_preview_selection), window);
}

/**
 * nolphin_window_show:
 * @widget:	GtkWidget
 *
 * Call parent and then show/hide window items
 * base on user prefs.
 */
static void
nolphin_window_show (GtkWidget *widget)
{
	NolphinWindow *window;

	window = NOLPHIN_WINDOW (widget);

    g_free (window->details->sidebar_id);
    window->details->sidebar_id = g_settings_get_string (nolphin_window_state,
                                                         NOLPHIN_WINDOW_STATE_SIDE_PANE_VIEW);

	if (g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_START_WITH_SIDEBAR)) {
		nolphin_window_show_sidebar (window);
	} else {
		nolphin_window_hide_sidebar (window);
	}

	GTK_WIDGET_CLASS (nolphin_window_parent_class)->show (widget);

	gtk_ui_manager_ensure_update (window->details->ui_manager);
}

GtkUIManager *
nolphin_window_get_ui_manager (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);

	return window->details->ui_manager;
}

GtkActionGroup *
nolphin_window_get_main_action_group (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);

	return window->details->main_action_group;
}

GtkWidget *
nolphin_window_get_workspace_panel (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);

	return window->details->workspace_panel;
}

GtkWidget *
nolphin_window_get_terminal (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);

	return window->details->terminal;
}

NolphinNavigationState *
nolphin_window_get_navigation_state (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);

	return window->details->nav_state;
}

NolphinWindowPane *
nolphin_window_get_next_pane (NolphinWindow *window)
{
       NolphinWindowPane *next_pane;
       GList *node;

       /* return NULL if there is only one pane */
       if (!window->details->panes || !window->details->panes->next) {
	       return NULL;
       }

       /* get next pane in the (wrapped around) list */
       node = g_list_find (window->details->panes, window->details->active_pane);
       g_return_val_if_fail (node, NULL);
       if (node->next) {
	       next_pane = node->next->data;
       } else {
	       next_pane =  window->details->panes->data;
       }

       return next_pane;
}


void
nolphin_window_slot_set_viewed_file (NolphinWindowSlot *slot,
				      NolphinFile *file)
{
	NolphinFileAttributes attributes;

	if (slot->viewed_file == file) {
		return;
	}

	nolphin_file_ref (file);

	cancel_sync_view_type_callback (slot);
    cancel_sync_show_thumbnail_callback (slot);

	if (slot->viewed_file != NULL) {
		nolphin_file_monitor_remove (slot->viewed_file,
					      slot);
	}

	if (file != NULL) {
		attributes =
			NOLPHIN_FILE_ATTRIBUTE_INFO |
			NOLPHIN_FILE_ATTRIBUTE_LINK_INFO;
		nolphin_file_monitor_add (file, slot, attributes);
	}

	nolphin_file_unref (slot->viewed_file);
	slot->viewed_file = file;
}

NolphinWindowSlot *
nolphin_window_get_slot_for_view (NolphinWindow *window,
				   NolphinView *view)
{
	NolphinWindowSlot *slot;
	GList *l, *walk;

	for (walk = window->details->panes; walk; walk = walk->next) {
		NolphinWindowPane *pane = walk->data;

		for (l = pane->slots; l != NULL; l = l->next) {
			slot = l->data;
			if (slot->content_view == view ||
			    slot->new_content_view == view) {
				return slot;
			}
		}
	}

	return NULL;
}

NolphinWindowShowHiddenFilesMode
nolphin_window_get_hidden_files_mode (NolphinWindow *window)
{
	return window->details->show_hidden_files_mode;
}

void
nolphin_window_set_hidden_files_mode (NolphinWindow *window,
				       NolphinWindowShowHiddenFilesMode  mode)
{
	window->details->show_hidden_files_mode = mode;
    g_settings_set_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SHOW_HIDDEN_FILES,
                            mode == NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_ENABLE);
	g_signal_emit_by_name (window, "hidden_files_mode_changed");
}

NolphinWindowSlot *
nolphin_window_get_active_slot (NolphinWindow *window)
{
	g_assert (NOLPHIN_IS_WINDOW (window));

	if (window->details->active_pane == NULL) {
		return NULL;
	}

	return window->details->active_pane->active_slot;
}

NolphinWindowSlot *
nolphin_window_get_extra_slot (NolphinWindow *window)
{
	NolphinWindowPane *extra_pane;
	GList *node;

	g_assert (NOLPHIN_IS_WINDOW (window));


	/* return NULL if there is only one pane */
	if (window->details->panes == NULL ||
	    window->details->panes->next == NULL) {
		return NULL;
	}

	/* get next pane in the (wrapped around) list */
	node = g_list_find (window->details->panes,
			    window->details->active_pane);
	g_return_val_if_fail (node, FALSE);

	if (node->next) {
		extra_pane = node->next->data;
	}
	else {
		extra_pane =  window->details->panes->data;
	}

	return extra_pane->active_slot;
}

GList *
nolphin_window_get_panes (NolphinWindow *window)
{
	g_assert (NOLPHIN_IS_WINDOW (window));

	return window->details->panes;
}

static void
window_set_search_action_text (NolphinWindow *window,
			       gboolean setting)
{
	GtkAction *action;
	NolphinWindowPane *pane;
	GList *l;

	for (l = window->details->panes; l != NULL; l = l->next) {
		pane = l->data;
		action = gtk_action_group_get_action (pane->action_group,
						      NOLPHIN_ACTION_SEARCH);

		gtk_action_set_is_important (action, setting);
	}
}

static void
center_pane_divider (GtkWidget  *paned,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
    /* Make the paned think it's been manually resized, otherwise
     * things like the trash bar will force unwanted resizes */

    g_object_set (G_OBJECT (paned),
                  "position", gtk_widget_get_allocated_width (paned) / 2,
                  NULL);

    g_signal_handlers_disconnect_by_func (G_OBJECT (paned), center_pane_divider, NULL);
}

/* Bei verschachtelten Paneds (3-4 Bereiche) ist ein Kind des Haupt-Paneds
 * kein Bereich, sondern wieder ein Paned: dann gilt der erste Bereich darin. */
static NolphinWindowPane *
first_pane_in_widget (GtkWidget *widget)
{
	if (widget == NULL) {
		return NULL;
	}
	if (NOLPHIN_IS_WINDOW_PANE (widget)) {
		return NOLPHIN_WINDOW_PANE (widget);
	}
	if (GTK_IS_PANED (widget)) {
		NolphinWindowPane *pane = first_pane_in_widget (gtk_paned_get_child1 (GTK_PANED (widget)));

		return pane != NULL ? pane : first_pane_in_widget (gtk_paned_get_child2 (GTK_PANED (widget)));
	}
	return NULL;
}

static NolphinWindowSlot *
create_extra_pane (NolphinWindow *window)
{
	NolphinWindowPane *pane;
	NolphinWindowSlot *slot;
	GtkPaned *paned;

	/* New pane */
	pane = nolphin_window_pane_new (window);
	window->details->panes = g_list_append (window->details->panes, pane);

	paned = GTK_PANED (window->details->split_view_hpane);

    g_signal_connect_after (paned,
                            "notify::position",
                            G_CALLBACK(center_pane_divider),
                            NULL);

	if (gtk_paned_get_child1 (paned) == NULL) {
		gtk_paned_pack1 (paned, GTK_WIDGET (pane), TRUE, FALSE);
	} else {
		gtk_paned_pack2 (paned, GTK_WIDGET (pane), TRUE, FALSE);
	}

	/* Ensure the toolbar doesn't pop itself into existence (double toolbars suck.) */
	gtk_widget_hide (pane->tool_bar);

	/* slot */
	slot = nolphin_window_pane_open_slot (NOLPHIN_WINDOW_PANE (pane),
					       NOLPHIN_WINDOW_OPEN_SLOT_APPEND);
	pane->active_slot = slot;

	return slot;
}

static void
nolphin_window_reload (NolphinWindow *window)
{
	NolphinWindowSlot *active_slot;
	active_slot = nolphin_window_get_active_slot (window);
	nolphin_window_slot_queue_reload (active_slot, TRUE);
}

static gboolean
nolphin_window_state_event (GtkWidget *widget,
			     GdkEventWindowState *event)
{
	if ((event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) && !nolphin_window_is_desktop (NOLPHIN_WINDOW (widget))) {
		g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_MAXIMIZED,
					event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED);
	}

	if (GTK_WIDGET_CLASS (nolphin_window_parent_class)->window_state_event != NULL) {
		return GTK_WIDGET_CLASS (nolphin_window_parent_class)->window_state_event (widget, event);
	}

	return FALSE;
}

static gboolean
nolphin_window_delete_event (GtkWidget *widget,
			      GdkEventAny *event)
{
	nolphin_window_close (NOLPHIN_WINDOW (widget));
	return FALSE;
}

static gboolean
nolphin_window_button_press_event (GtkWidget *widget,
				    GdkEventButton *event)
{
	NolphinWindow *window;
	gboolean handled;

	window = NOLPHIN_WINDOW (widget);

	if (mouse_extra_buttons && (event->button == mouse_back_button)) {
		nolphin_window_back_or_forward (window, TRUE, 0, 0);
		handled = TRUE;
	} else if (mouse_extra_buttons && (event->button == mouse_forward_button)) {
		nolphin_window_back_or_forward (window, FALSE, 0, 0);
		handled = TRUE;
	} else if (GTK_WIDGET_CLASS (nolphin_window_parent_class)->button_press_event) {
		handled = GTK_WIDGET_CLASS (nolphin_window_parent_class)->button_press_event (widget, event);
	} else {
		handled = FALSE;
	}
	return handled;
}

static void
mouse_back_button_changed (gpointer callback_data)
{
	int new_back_button;

	new_back_button = g_settings_get_int (nolphin_preferences, NOLPHIN_PREFERENCES_MOUSE_BACK_BUTTON);

	/* Bounds checking */
	if (new_back_button < 6 || new_back_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_back_button = new_back_button;
}

static void
mouse_forward_button_changed (gpointer callback_data)
{
	int new_forward_button;

	new_forward_button = g_settings_get_int (nolphin_preferences, NOLPHIN_PREFERENCES_MOUSE_FORWARD_BUTTON);

	/* Bounds checking */
	if (new_forward_button < 6 || new_forward_button > UPPER_MOUSE_LIMIT)
		return;

	mouse_forward_button = new_forward_button;
}

static void
use_extra_mouse_buttons_changed (gpointer callback_data)
{
	mouse_extra_buttons = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS);
}


/*
 * Main API
 */

static void
nolphin_window_init (NolphinWindow *window)
{
    GtkWindowGroup *window_group;

	window->details = G_TYPE_INSTANCE_GET_PRIVATE (window, NOLPHIN_TYPE_WINDOW, NolphinWindowDetails);

	window->details->panes = NULL;
	window->details->active_pane = NULL;

    gboolean show_hidden = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SHOW_HIDDEN_FILES);

    window->details->show_hidden_files_mode = show_hidden ? NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_ENABLE :
                                                            NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_DISABLE;

    window->details->show_sidebar = g_settings_get_boolean (nolphin_window_state,
                                                            NOLPHIN_WINDOW_STATE_START_WITH_SIDEBAR);

    window->details->menu_skip_release = FALSE;
    window->details->menu_show_queued = FALSE;

    window->details->ignore_meta_view_id = NULL;
    window->details->ignore_meta_zoom_level = -1;
    window->details->ignore_meta_visible_columns = NULL;
    window->details->ignore_meta_column_order = NULL;
    window->details->ignore_meta_sort_column = NULL;
    window->details->ignore_meta_sort_direction = SORT_NULL;

	/* This makes it possible for GTK+ themes to apply styling that is specific to Nolphin
	 * without affecting other GTK+ applications.
	 */
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), "nolphin-window");
	/* Nemo-Klasse zusätzlich, damit Themes (Mint-Y, Mint-X …) ihre Nemo-Regeln
	 * für Seitenleiste und Bereiche auch auf Nolphin anwenden. */
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), "nemo-window");

	window_group = gtk_window_group_new ();
	gtk_window_group_add_window (window_group, GTK_WINDOW (window));
	g_object_unref (window_group);

	/* Set initial window title */
	gtk_window_set_title (GTK_WINDOW (window), _("Nolphin"));

    g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS,
				  G_CALLBACK(nolphin_window_sync_thumbnail_action),
				  window);
    g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_INHERIT_SHOW_THUMBNAILS,
				  G_CALLBACK(nolphin_window_sync_thumbnail_action),
				  window);
}

static NolphinIconInfo *
real_get_icon (NolphinWindow *window,
               NolphinWindowSlot *slot)
{
        return nolphin_file_get_icon (slot->viewed_file, 48, 0,
                       gtk_widget_get_scale_factor (GTK_WIDGET (window)),
				       NOLPHIN_FILE_ICON_FLAGS_IGNORE_VISITING |
				       NOLPHIN_FILE_ICON_FLAGS_USE_MOUNT_ICON);
}

static gboolean
uri_is_native_session_uri (const char *uri)
{
	GFile *file;
	gboolean is_native;

	if (uri == NULL || uri[0] == '\0') {
		return FALSE;
	}

	file = g_file_new_for_uri (uri);

	/* skip searches and non-native locations for this simple session restore */
	if (g_file_has_uri_scheme (file, "x-nolphin-search")) {
		g_object_unref (file);
		return FALSE;
	}

	is_native = g_file_is_native (file);
	g_object_unref (file);

	return is_native;
}

static char **
collect_pane_saved_tab_uris (NolphinWindowPane *pane, gint *active_index_out)
{
	GtkNotebook *notebook;
	int n_pages, i;
	int current_page;
	int saved_index = 0;
	int saved_active_index = 0;
	GPtrArray *arr;

	if (active_index_out != NULL) {
		*active_index_out = 0;
	}

	if (pane == NULL || pane->notebook == NULL) {
		return g_new0 (char *, 1);
	}

	notebook = GTK_NOTEBOOK (pane->notebook);
	n_pages = gtk_notebook_get_n_pages (notebook);
	current_page = gtk_notebook_get_current_page (notebook);

	arr = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < n_pages; i++) {
		GtkWidget *page;
		NolphinWindowSlot *slot;
		char *uri;

		page = gtk_notebook_get_nth_page (notebook, i);
		if (page == NULL) {
			continue;
		}

		slot = NOLPHIN_WINDOW_SLOT (page);
		uri = nolphin_window_slot_get_location_uri (slot);

		if (uri_is_native_session_uri (uri)) {
			if (i == current_page) {
				saved_active_index = saved_index;
			}
			g_ptr_array_add (arr, uri);
			saved_index++;
		} else {
			g_free (uri);
		}
	}

	g_ptr_array_add (arr, NULL);

	if (active_index_out != NULL) {
		*active_index_out = saved_active_index;
	}

	return (char **) g_ptr_array_free (arr, FALSE);
}

void
nolphin_window_save_session_state (NolphinWindow *window)
{
	NolphinWindowPane *left_pane;
	NolphinWindowPane *right_pane;
	char **left_uris;
	char **right_uris;
	gint left_active = 0;
	gint right_active = 0;
	gboolean split_view;
	GtkPaned *paned;
	GtkWidget *child1;
	GtkWidget *child2;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	/* Do not store session state for the desktop window */
	if (nolphin_window_is_desktop (window)) {
		return;
	}

	paned = GTK_PANED (window->details->split_view_hpane);
	child1 = gtk_paned_get_child1 (paned);
	child2 = gtk_paned_get_child2 (paned);

	left_pane = first_pane_in_widget (child1);
	right_pane = first_pane_in_widget (child2);

	/* Nach dem Schließen von Bereichen kann der einzige verbliebene in
	 * child2 liegen: das ist keine Teilung. */
	if (g_list_length (window->details->panes) <= 1) {
		if (left_pane == NULL) {
			left_pane = right_pane;
		}
		right_pane = NULL;
	}

	left_uris = collect_pane_saved_tab_uris (left_pane, &left_active);
	right_uris = collect_pane_saved_tab_uris (right_pane, &right_active);
	split_view = (right_pane != NULL);

	g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_SPLIT_VIEW, split_view);
	g_settings_set_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_LEFT, (const gchar * const *) left_uris);
	g_settings_set_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_RIGHT, (const gchar * const *) right_uris);
	g_settings_set_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_LEFT, left_active);
	g_settings_set_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_RIGHT, right_active);

	g_strfreev (left_uris);
	g_strfreev (right_uris);
}

static void
clear_pane_to_single_slot (NolphinWindowPane *pane)
{
	GtkNotebook *notebook;
	int n_pages;

	if (pane == NULL || pane->notebook == NULL) {
		return;
	}

	notebook = GTK_NOTEBOOK (pane->notebook);
	n_pages = gtk_notebook_get_n_pages (notebook);

	/* Ensure there is a predictable active tab */
	if (n_pages > 0) {
		gtk_notebook_set_current_page (notebook, 0);
	}

	/* Close all tabs except the first one */
	while (gtk_notebook_get_n_pages (notebook) > 1) {
		GtkWidget *page;
		NolphinWindowSlot *slot;
		int last = gtk_notebook_get_n_pages (notebook) - 1;

		page = gtk_notebook_get_nth_page (notebook, last);
		if (page == NULL) {
			break;
		}

		slot = NOLPHIN_WINDOW_SLOT (page);
		nolphin_window_pane_close_slot (pane, slot);
	}
}

static void
open_uri_list_in_pane (NolphinWindowPane *pane, char **uris)
{
	int i;

	if (pane == NULL) {
		return;
	}

	/* If no URIs were saved for this pane, leave its first tab alone */
	if (uris == NULL || uris[0] == NULL) {
		return;
	}

	for (i = 0; uris[i] != NULL; i++) {
		NolphinWindowSlot *slot;
		GFile *location;

		if (!uri_is_native_session_uri (uris[i])) {
			continue;
		}

		if (i == 0) {
			/* Reuse the existing first tab */
			slot = pane->active_slot;
			if (slot == NULL && pane->notebook != NULL) {
				GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->notebook), 0);
				if (page != NULL) {
					slot = NOLPHIN_WINDOW_SLOT (page);
				}
			}
		} else {
			slot = nolphin_window_pane_open_slot (pane, NOLPHIN_WINDOW_OPEN_SLOT_APPEND);
		}

		if (slot == NULL) {
			continue;
		}

		location = g_file_new_for_uri (uris[i]);
		nolphin_window_slot_open_location (slot, location, 0);
		g_object_unref (location);
	}
}

#define WS_GROUP "Workspace"
#define WS_MAX_PANES 4

static NolphinWindowPane *split_pane_nested (NolphinWindow *window, NolphinWindowPane *active, GtkOrientation orientation);

/* Sperre und eigener Name der Reiter eines Bereichs: parallel zu den
 * gespeicherten Reiter-Adressen (nur lokale Orte, gleiche Reihenfolge wie
 * collect_pane_saved_tab_uris()). */
static void
workspace_capture_tab_extras (NolphinWindowPane *pane, GKeyFile *kf, const char *group,
			      const char *lock_key, const char *title_key)
{
	GtkNotebook *nb;
	gint n, i;
	GArray *locks = g_array_new (FALSE, FALSE, sizeof (gint));
	GPtrArray *titles = g_ptr_array_new_with_free_func (g_free);

	if (pane != NULL && pane->notebook != NULL) {
		nb = GTK_NOTEBOOK (pane->notebook);
		n = gtk_notebook_get_n_pages (nb);
		for (i = 0; i < n; i++) {
			NolphinWindowSlot *slot = NOLPHIN_WINDOW_SLOT (gtk_notebook_get_nth_page (nb, i));
			char *uri = nolphin_window_slot_get_location_uri (slot);
			gint locked = slot->locked ? 1 : 0;

			if (uri_is_native_session_uri (uri)) {
				g_array_append_val (locks, locked);
				g_ptr_array_add (titles, g_strdup (slot->custom_title != NULL ? slot->custom_title : ""));
			}
			g_free (uri);
		}
	}

	if (locks->len > 0) {
		g_key_file_set_integer_list (kf, group, lock_key, (gint *) locks->data, locks->len);
		g_key_file_set_string_list (kf, group, title_key, (const gchar * const *) titles->pdata, titles->len);
	}
	g_array_free (locks, TRUE);
	g_ptr_array_free (titles, TRUE);
}

static void
workspace_apply_tab_extras (NolphinWindowPane *pane, GKeyFile *kf, const char *group,
			    const char *lock_key, const char *title_key)
{
	gsize n_locks = 0, n_titles = 0, i;
	gint *locks = g_key_file_get_integer_list (kf, group, lock_key, &n_locks, NULL);
	gchar **titles = g_key_file_get_string_list (kf, group, title_key, &n_titles, NULL);

	if (pane != NULL && pane->notebook != NULL) {
		GtkNotebook *nb = GTK_NOTEBOOK (pane->notebook);
		gint pages = gtk_notebook_get_n_pages (nb);

		for (i = 0; i < n_locks && (gint) i < pages; i++) {
			NolphinWindowSlot *slot = NOLPHIN_WINDOW_SLOT (gtk_notebook_get_nth_page (nb, i));

			slot->locked = locks[i] != 0;
			g_free (slot->custom_title);
			slot->custom_title = (titles != NULL && i < n_titles && titles[i][0] != '\0') ? g_strdup (titles[i]) : NULL;
			nolphin_notebook_sync_tab_label (NOLPHIN_NOTEBOOK (pane->notebook), slot);
		}
		nolphin_notebook_update_tabs_visibility (NOLPHIN_NOTEBOOK (pane->notebook));
	}
	g_free (locks);
	g_strfreev (titles);
}

/* --- Layout als Baum: "h(p0,v(p1,p2))" --------------------------------------
 * p<n> ist ein Bereich (Gruppe "Pane<n>" mit Reitern), h/v eine Teilung
 * nebeneinander bzw. untereinander mit zwei Kindern. Funktioniert für bis zu
 * vier Bereiche. */

typedef struct WsNode {
	gchar kind;     /* 'p', 'h' oder 'v' */
	gint idx;       /* nur bei 'p' */
	struct WsNode *a, *b;
} WsNode;

static void
ws_node_free (WsNode *n)
{
	if (n == NULL) {
		return;
	}
	ws_node_free (n->a);
	ws_node_free (n->b);
	g_free (n);
}

static gint
ws_node_leaves (WsNode *n)
{
	return n == NULL ? 0 : (n->kind == 'p' ? 1 : ws_node_leaves (n->a) + ws_node_leaves (n->b));
}

static WsNode *
ws_parse (const gchar **s)
{
	WsNode *n = NULL;

	if (**s == 'p') {
		gchar *end;

		(*s)++;
		n = g_new0 (WsNode, 1);
		n->kind = 'p';
		n->idx = (gint) g_ascii_strtoll (*s, &end, 10);
		if (end == *s) {
			ws_node_free (n);
			return NULL;
		}
		*s = end;
	} else if (**s == 'h' || **s == 'v') {
		n = g_new0 (WsNode, 1);
		n->kind = **s;
		(*s)++;
		if (**s != '(') {
			ws_node_free (n);
			return NULL;
		}
		(*s)++;
		n->a = ws_parse (s);
		if (n->a == NULL || **s != ',') {
			ws_node_free (n);
			return NULL;
		}
		(*s)++;
		n->b = ws_parse (s);
		if (n->b == NULL || **s != ')') {
			ws_node_free (n);
			return NULL;
		}
		(*s)++;
	}
	return n;
}

static gchar *
ws_serialize_node (GtkWidget *w, GKeyFile *kf, gint *counter)
{
	if (w == NULL) {
		return NULL;
	}
	if (NOLPHIN_IS_WINDOW_PANE (w)) {
		NolphinWindowPane *pane = NOLPHIN_WINDOW_PANE (w);
		gint idx = (*counter)++, active = 0;
		gchar **uris = collect_pane_saved_tab_uris (pane, &active);
		gchar *group = g_strdup_printf ("Pane%d", idx);

		g_key_file_set_string_list (kf, group, "tabs", (const gchar * const *) uris, g_strv_length (uris));
		g_key_file_set_integer (kf, group, "active", active);
		workspace_capture_tab_extras (pane, kf, group, "locked", "titles");
		g_free (group);
		g_strfreev (uris);
		return g_strdup_printf ("p%d", idx);
	}
	if (GTK_IS_PANED (w)) {
		gchar *a = ws_serialize_node (gtk_paned_get_child1 (GTK_PANED (w)), kf, counter);
		gchar *b = ws_serialize_node (gtk_paned_get_child2 (GTK_PANED (w)), kf, counter);
		gchar *result;

		if (a != NULL && b != NULL) {
			result = g_strdup_printf ("%c(%s,%s)",
						  gtk_orientable_get_orientation (GTK_ORIENTABLE (w)) == GTK_ORIENTATION_HORIZONTAL ? 'h' : 'v',
						  a, b);
			g_free (a);
			g_free (b);
		} else {
			result = a != NULL ? a : b;
		}
		return result;
	}
	return NULL;
}

/* Schreibt Layout und Bereiche des Fensters in @kf. Rückgabe: Zahl der Bereiche. */
static gint
workspace_capture_layout (NolphinWindow *window, GKeyFile *kf)
{
	gint counter = 0;
	gchar *layout = ws_serialize_node (window->details->split_view_hpane, kf, &counter);

	if (layout != NULL) {
		g_key_file_set_string (kf, WS_GROUP, "layout", layout);
	}
	g_free (layout);
	return counter;
}

static void
ws_restore_leaf (NolphinWindowPane *pane, GKeyFile *kf, gint idx)
{
	gchar *group = g_strdup_printf ("Pane%d", idx);
	gchar **uris = g_key_file_get_string_list (kf, group, "tabs", NULL, NULL);
	gint active = g_key_file_get_integer (kf, group, "active", NULL);

	if (pane != NULL && uris != NULL && uris[0] != NULL) {
		clear_pane_to_single_slot (pane);
		open_uri_list_in_pane (pane, uris);
		if (pane->notebook != NULL) {
			GtkNotebook *nb = GTK_NOTEBOOK (pane->notebook);
			gint n = gtk_notebook_get_n_pages (nb);

			if (n > 0) {
				gtk_notebook_set_current_page (nb, CLAMP (active, 0, n - 1));
			}
		}
		workspace_apply_tab_extras (pane, kf, group, "locked", "titles");
	}
	g_strfreev (uris);
	g_free (group);
}

static void
ws_restore_node (NolphinWindow *window, WsNode *n, NolphinWindowPane *pane, GKeyFile *kf, gboolean is_root)
{
	GtkOrientation o;
	NolphinWindowPane *pa, *pb;

	if (n->kind == 'p') {
		ws_restore_leaf (pane, kf, n->idx);
		return;
	}

	o = n->kind == 'h' ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
	if (is_root) {
		GtkPaned *root = GTK_PANED (window->details->split_view_hpane);

		gtk_orientable_set_orientation (GTK_ORIENTABLE (root), o);
		nolphin_window_split_view_on (window);
		pa = first_pane_in_widget (gtk_paned_get_child1 (root));
		pb = first_pane_in_widget (gtk_paned_get_child2 (root));
	} else {
		pa = pane;
		pb = split_pane_nested (window, pane, o);
	}
	ws_restore_node (window, n->a, pa, kf, FALSE);
	ws_restore_node (window, n->b, pb, kf, FALSE);
}

/* Baut Teilung und Reiter nach dem Layout in @kf neu auf. FALSE, wenn @kf
 * kein brauchbares Layout enthält (dann bleibt das Fenster unverändert). */
static gboolean
workspace_apply_layout (NolphinWindow *window, GKeyFile *kf)
{
	gchar *layout = g_key_file_get_string (kf, WS_GROUP, "layout", NULL);
	const gchar *p = layout;
	WsNode *root = layout != NULL ? ws_parse (&p) : NULL;
	NolphinWindowPane *start;
	GtkPaned *rootp;

	g_free (layout);
	if (root == NULL || ws_node_leaves (root) > WS_MAX_PANES) {
		ws_node_free (root);
		return FALSE;
	}

	/* Auf einen einzigen Bereich zurückführen und von dort neu aufbauen */
	if (nolphin_window_split_view_showing (window)) {
		nolphin_window_split_view_off (window);
	}
	start = nolphin_window_get_active_pane (window);
	ws_restore_node (window, root, start, kf, TRUE);
	ws_node_free (root);

	rootp = GTK_PANED (window->details->split_view_hpane);
	start = first_pane_in_widget (gtk_paned_get_child1 (rootp));
	if (start == NULL) {
		start = first_pane_in_widget (gtk_paned_get_child2 (rootp));
	}
	if (start != NULL) {
		nolphin_window_set_active_pane (window, start);
	}
	nolphin_window_update_show_hide_ui_elements (window);
	return TRUE;
}

/* Automatische Sitzung mit mehr als zwei Bereichen: GSettings kennt nur links
 * und rechts, daher liegt das volle Layout in dieser Datei. */
static gchar *
last_session_path (void)
{
	return g_build_filename (g_get_user_config_dir (), "nolphin", "last-session.ini", NULL);
}

static gboolean
restore_saved_tabs_from_settings (NolphinWindow *window)
{
	NolphinWindowPane *left_pane;
	NolphinWindowPane *right_pane;
	char **left_uris;
	char **right_uris;
	gint left_active;
	gint right_active;
	gboolean want_split;
	gboolean saved_split;

	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	/* Never restore tabs for the desktop window */
	if (nolphin_window_is_desktop (window)) {
		return FALSE;
	}

	left_uris = g_settings_get_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_LEFT);
	right_uris = g_settings_get_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_RIGHT);
	left_active = g_settings_get_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_LEFT);
	right_active = g_settings_get_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_RIGHT);
	saved_split = g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_SPLIT_VIEW);

	if ((left_uris == NULL || left_uris[0] == NULL) &&
	    (right_uris == NULL || right_uris[0] == NULL)) {
		g_strfreev (left_uris);
		g_strfreev (right_uris);
		return FALSE;
	}

	/* Only create the extra pane if we actually have tabs to restore there */
	want_split = saved_split && (right_uris != NULL && right_uris[0] != NULL);

	if (want_split && !nolphin_window_split_view_showing (window)) {
		nolphin_window_split_view_on (window);
	} else if (!want_split && nolphin_window_split_view_showing (window)) {
		nolphin_window_split_view_off (window);
	}

	{
		GtkPaned *paned = GTK_PANED (window->details->split_view_hpane);
		GtkWidget *child1 = gtk_paned_get_child1 (paned);
		GtkWidget *child2 = gtk_paned_get_child2 (paned);

		left_pane = first_pane_in_widget (child1);
		right_pane = want_split ? first_pane_in_widget (child2) : NULL;
	}

	/* Reset panes to one tab each, then rebuild tabs in saved order */
	clear_pane_to_single_slot (left_pane);
	clear_pane_to_single_slot (right_pane);

	/* If nothing saved for the left pane, open Home as a minimal fallback */
	if (left_uris == NULL || left_uris[0] == NULL) {
		GFile *home = g_file_new_for_path (g_get_home_dir ());
		if (left_pane != NULL && left_pane->active_slot != NULL) {
			nolphin_window_slot_open_location (left_pane->active_slot, home, 0);
		}
		g_object_unref (home);
	} else {
		open_uri_list_in_pane (left_pane, left_uris);
	}

	open_uri_list_in_pane (right_pane, right_uris);

	/* Restore active tabs (clamp indices) */
	if (left_pane != NULL && left_pane->notebook != NULL) {
		GtkNotebook *nb = GTK_NOTEBOOK (left_pane->notebook);
		int n = gtk_notebook_get_n_pages (nb);
		if (n > 0) {
			gtk_notebook_set_current_page (nb, CLAMP (left_active, 0, n - 1));
		}
	}

	if (right_pane != NULL && right_pane->notebook != NULL) {
		GtkNotebook *nb = GTK_NOTEBOOK (right_pane->notebook);
		int n = gtk_notebook_get_n_pages (nb);
		if (n > 0) {
			gtk_notebook_set_current_page (nb, CLAMP (right_active, 0, n - 1));
		}
	}

	/* Make the left pane active for a predictable starting point */
	if (left_pane != NULL) {
		nolphin_window_set_active_pane (window, left_pane);
	}

	g_strfreev (left_uris);
	g_strfreev (right_uris);

	return TRUE;
}

/* --- Benannte Arbeitsbereiche --------------------------------------
 * Ein Arbeitsbereich ist ein Schnappschuss der Sitzung: Reiter beider Bereiche,
 * Teilung samt Ausrichtung, Fenstergröße und die Sichtbarkeit von
 * Seitenleiste, Vorschau und Terminal. Er nutzt dieselben Werte wie die
 * automatische Sitzungswiederherstellung. */
void
nolphin_window_workspace_capture (NolphinWindow *window, GKeyFile *kf)
{
	gchar **left, **right;
	gint width = 0, height = 0;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	nolphin_window_save_session_state (window);

	left = g_settings_get_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_LEFT);
	right = g_settings_get_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_RIGHT);

	g_key_file_set_boolean (kf, WS_GROUP, "split", g_settings_get_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_SPLIT_VIEW));
	g_key_file_set_string_list (kf, WS_GROUP, "tabs-left", (const gchar * const *) left, g_strv_length (left));
	g_key_file_set_string_list (kf, WS_GROUP, "tabs-right", (const gchar * const *) right, g_strv_length (right));
	g_key_file_set_integer (kf, WS_GROUP, "active-left", g_settings_get_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_LEFT));
	g_key_file_set_integer (kf, WS_GROUP, "active-right", g_settings_get_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_RIGHT));
	g_key_file_set_boolean (kf, WS_GROUP, "split-vertical",
				gtk_orientable_get_orientation (GTK_ORIENTABLE (window->details->split_view_hpane)) == GTK_ORIENTATION_VERTICAL);

	{
		GtkWidget *c1 = gtk_paned_get_child1 (GTK_PANED (window->details->split_view_hpane));
		GtkWidget *c2 = gtk_paned_get_child2 (GTK_PANED (window->details->split_view_hpane));
		NolphinWindowPane *lp = first_pane_in_widget (c1), *rp = first_pane_in_widget (c2);

		if (g_list_length (window->details->panes) <= 1) {
			if (lp == NULL) {
				lp = rp;
			}
			rp = NULL;
		}
		workspace_capture_tab_extras (lp, kf, WS_GROUP, "locked-left", "titles-left");
		workspace_capture_tab_extras (rp, kf, WS_GROUP, "locked-right", "titles-right");
	}

	workspace_capture_layout (window, kf);

	gtk_window_get_size (GTK_WINDOW (window), &width, &height);
	g_key_file_set_integer (kf, WS_GROUP, "width", width);
	g_key_file_set_integer (kf, WS_GROUP, "height", height);

	g_key_file_set_boolean (kf, WS_GROUP, "sidebar", nolphin_window_get_show_sidebar (window));
	g_key_file_set_boolean (kf, WS_GROUP, "preview", nolphin_window_preview_showing (window));
	g_key_file_set_boolean (kf, WS_GROUP, "terminal", window->details->show_terminal);

	g_strfreev (left);
	g_strfreev (right);
}

gboolean
nolphin_window_workspace_apply (NolphinWindow *window, GKeyFile *kf)
{
	gchar **left, **right;
	gboolean restored;

	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	if (!g_key_file_has_group (kf, WS_GROUP) || nolphin_window_is_desktop (window)) {
		return FALSE;
	}

	left = g_key_file_get_string_list (kf, WS_GROUP, "tabs-left", NULL, NULL);
	right = g_key_file_get_string_list (kf, WS_GROUP, "tabs-right", NULL, NULL);
	if (left == NULL) {
		left = g_new0 (gchar *, 1);
	}
	if (right == NULL) {
		right = g_new0 (gchar *, 1);
	}

	if (workspace_apply_layout (window, kf)) {
		restored = TRUE;
		g_strfreev (left);
		g_strfreev (right);
	} else {
		/* Werte in die Sitzungsschlüssel schreiben und die bewährte
		 * Wiederherstellung der Sitzung verwenden. */
		g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_SPLIT_VIEW, g_key_file_get_boolean (kf, WS_GROUP, "split", NULL));
		g_settings_set_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_LEFT, (const gchar * const *) left);
		g_settings_set_strv (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_TABS_RIGHT, (const gchar * const *) right);
		g_settings_set_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_LEFT, g_key_file_get_integer (kf, WS_GROUP, "active-left", NULL));
		g_settings_set_int (nolphin_window_state, NOLPHIN_WINDOW_STATE_SAVED_ACTIVE_TAB_RIGHT, g_key_file_get_integer (kf, WS_GROUP, "active-right", NULL));
		g_strfreev (left);
		g_strfreev (right);

		gtk_orientable_set_orientation (GTK_ORIENTABLE (window->details->split_view_hpane),
						g_key_file_get_boolean (kf, WS_GROUP, "split-vertical", NULL)
						? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
		restored = restore_saved_tabs_from_settings (window);
		{
			GtkWidget *c1 = gtk_paned_get_child1 (GTK_PANED (window->details->split_view_hpane));
			GtkWidget *c2 = gtk_paned_get_child2 (GTK_PANED (window->details->split_view_hpane));
			NolphinWindowPane *lp = first_pane_in_widget (c1), *rp = first_pane_in_widget (c2);

			if (g_list_length (window->details->panes) <= 1) {
				if (lp == NULL) {
					lp = rp;
				}
				rp = NULL;
			}
			workspace_apply_tab_extras (lp, kf, WS_GROUP, "locked-left", "titles-left");
			workspace_apply_tab_extras (rp, kf, WS_GROUP, "locked-right", "titles-right");
		}


	}

	if (g_key_file_has_key (kf, WS_GROUP, "sidebar", NULL)) {
		if (g_key_file_get_boolean (kf, WS_GROUP, "sidebar", NULL)) {
			nolphin_window_show_sidebar (window);
		} else {
			nolphin_window_hide_sidebar (window);
		}
	}
	if (g_key_file_has_key (kf, WS_GROUP, "preview", NULL)) {
		nolphin_window_set_show_preview (window, g_key_file_get_boolean (kf, WS_GROUP, "preview", NULL));
	}
	if (g_key_file_has_key (kf, WS_GROUP, "terminal", NULL)) {
		nolphin_window_set_show_terminal (window, g_key_file_get_boolean (kf, WS_GROUP, "terminal", NULL));
	}
	{
		gint w = g_key_file_get_integer (kf, WS_GROUP, "width", NULL);
		gint h = g_key_file_get_integer (kf, WS_GROUP, "height", NULL);

		if (w > 200 && h > 150 && !gtk_window_is_maximized (GTK_WINDOW (window))) {
			gtk_window_resize (GTK_WINDOW (window), w, h);
		}
	}

	nolphin_window_update_show_hide_ui_elements (window);
	return restored;
}

static gint split_layout_zones (NolphinSplitLayout layout);

/* Sitzung beim Start wiederherstellen: ein gespeichertes Layout aus
 * mindestens zwei Bereichen hat Vorrang, sonst die bewährte Wiederherstellung
 * aus GSettings (links/rechts). Wurde zuletzt ein Layout aus der Auswahl
 * gewählt, wird es mit seinen Größenverhältnissen wieder angewendet. */
gboolean
nolphin_window_restore_saved_tabs (NolphinWindow *window)
{
	gchar *path;
	GKeyFile *kf;
	gboolean ok = FALSE;

	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	if (nolphin_window_is_desktop (window)) {
		return FALSE;
	}

	path = last_session_path ();
	kf = g_key_file_new ();
	if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
		ok = workspace_apply_layout (window, kf);
		if (ok && g_key_file_has_key (kf, WS_GROUP, "preset", NULL)) {
			gint preset = g_key_file_get_integer (kf, WS_GROUP, "preset", NULL);

			if (preset >= NOLPHIN_SPLIT_LAYOUT_TWO_COLUMNS && preset <= NOLPHIN_SPLIT_LAYOUT_TWO_ROWS &&
			    split_layout_zones (preset) == (gint) g_list_length (window->details->panes)) {
				nolphin_window_apply_split_layout (window, preset);
			}
		}
	}
	g_key_file_free (kf);
	g_free (path);

	return ok || restore_saved_tabs_from_settings (window);
}

static void
real_window_close (NolphinWindow *window)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	nolphin_window_save_session_state (window);

	/* Mehr als zwei Bereiche passen nicht in die GSettings-Schlüssel: dann das
	 * volle Layout in die Sitzungsdatei, sonst eine veraltete Datei entfernen. */
	if (!nolphin_window_is_desktop (window)) {
		gchar *path = last_session_path ();

		if (g_list_length (window->details->panes) > 1) {
			GKeyFile *kf = g_key_file_new ();
			gchar *dir = g_path_get_dirname (path), *data;
			gint preset = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "nolphin-split-preset")) - 1;

			workspace_capture_layout (window, kf);
			if (preset >= 0 && split_layout_zones (preset) == (gint) g_list_length (window->details->panes)) {
				g_key_file_set_integer (kf, WS_GROUP, "preset", preset);
			}
			g_mkdir_with_parents (dir, 0700);
			data = g_key_file_to_data (kf, NULL, NULL);
			g_file_set_contents (path, data, -1, NULL);
			g_free (data);
			g_free (dir);
			g_key_file_free (kf);
		} else {
			g_remove (path);
		}
		g_free (path);
	}

	nolphin_window_save_geometry (window);

	gtk_widget_destroy (GTK_WIDGET (window));
}

static void
nolphin_window_class_init (NolphinWindowClass *class)
{
	GtkBindingSet *binding_set;
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS (class);

	oclass->finalize = nolphin_window_finalize;
	oclass->constructed = nolphin_window_constructed;
	oclass->get_property = nolphin_window_get_property;
	oclass->set_property = nolphin_window_set_property;

	wclass->destroy = nolphin_window_destroy;
	wclass->show = nolphin_window_show;
	wclass->realize = nolphin_window_realize;
	wclass->key_press_event = nolphin_window_key_press_event;
    wclass->key_release_event = nolphin_window_key_release_event;
	wclass->window_state_event = nolphin_window_state_event;
	wclass->button_press_event = nolphin_window_button_press_event;
	wclass->delete_event = nolphin_window_delete_event;

	class->get_icon = real_get_icon;
	class->close = real_window_close;

	properties[PROP_DISABLE_CHROME] =
		g_param_spec_boolean ("disable-chrome",
				      "Disable chrome",
				      "Disable window chrome, for the desktop",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS);

    properties[PROP_SIDEBAR_VIEW_TYPE] =
        g_param_spec_string ("sidebar-view-id",
                      "Sidebar view type",
                      "Sidebar view type",
                      NULL,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_SIDEBAR] =
        g_param_spec_boolean ("show-sidebar",
                              "Show the sidebar",
                              "Show the sidebar",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	signals[GO_UP] =
		g_signal_new ("go-up",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NolphinWindowClass, go_up),
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);
	signals[RELOAD] =
		g_signal_new ("reload",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NolphinWindowClass, reload),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[PROMPT_FOR_LOCATION] =
		g_signal_new ("prompt-for-location",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NolphinWindowClass, prompt_for_location),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[HIDDEN_FILES_MODE_CHANGED] =
		g_signal_new ("hidden_files_mode_changed",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[LOADING_URI] =
		g_signal_new ("loading_uri",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	signals[SLOT_ADDED] =
		g_signal_new ("slot-added",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, NOLPHIN_TYPE_WINDOW_SLOT);
	signals[SLOT_REMOVED] =
		g_signal_new ("slot-removed",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, NOLPHIN_TYPE_WINDOW_SLOT);

	binding_set = gtk_binding_set_by_class (class);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_BackSpace, 0,
				      "go-up", 0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_F5, 0,
				      "reload", 0);
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_slash, 0,
				      "prompt-for-location", 1,
				      G_TYPE_STRING, "/");
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Divide, 0,
				      "prompt-for-location", 1,
				      G_TYPE_STRING, "/");
	gtk_binding_entry_add_signal (binding_set, GDK_KEY_asciitilde, 0,
				      "prompt-for-location", 1,
				      G_TYPE_STRING, "~");

	class->reload = nolphin_window_reload;
	class->go_up = nolphin_window_go_up_signal;
	class->prompt_for_location = nolphin_window_prompt_for_location;

	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_MOUSE_BACK_BUTTON,
				  G_CALLBACK(mouse_back_button_changed),
				  NULL);

	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_MOUSE_FORWARD_BUTTON,
				  G_CALLBACK(mouse_forward_button_changed),
				  NULL);

	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS,
				  G_CALLBACK(use_extra_mouse_buttons_changed),
				  NULL);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
	g_type_class_add_private (oclass, sizeof (NolphinWindowDetails));
}

NolphinWindow *
nolphin_window_new (GtkApplication *application,
                 GdkScreen *screen)
{
	return g_object_new (NOLPHIN_TYPE_WINDOW,
			     "application", application,
			     "screen", screen,
			     NULL);
}

void
nolphin_window_split_view_on (NolphinWindow *window)
{
	NolphinWindowSlot *slot, *old_active_slot;
	GFile *location;

	old_active_slot = nolphin_window_get_active_slot (window);
	slot = create_extra_pane (window);

    location = window->details->secondary_pane_last_location;

	if (location == NULL && old_active_slot != NULL) {
		location = nolphin_window_slot_get_location (old_active_slot);
		if (location != NULL) {
			if (g_file_has_uri_scheme (location, "x-nolphin-search")) {
				g_object_unref (location);
				location = NULL;
			}
		}
	}
	if (location == NULL) {
		location = g_file_new_for_path (g_get_home_dir ());
	}

	nolphin_window_slot_open_location (slot, location, 0);
	g_object_unref (location);

	window_set_search_action_text (window, FALSE);
}

void
nolphin_window_split_view_off (NolphinWindow *window)
{
	NolphinWindowPane *pane, *active_pane;
	GList *l, *next;

	active_pane = nolphin_window_get_active_pane (window);

	/* delete all panes except the first (main) pane */
	for (l = window->details->panes; l != NULL; l = next) {
		next = l->next;
		pane = l->data;
		if (pane != active_pane) {
            g_clear_object (&window->details->secondary_pane_last_location);
            window->details->secondary_pane_last_location = nolphin_window_slot_get_location (pane->active_slot);
			nolphin_window_close_pane (window, pane);
		}
	}

    /* Reset split view pane's position so the position can be
     * caught again later */
    g_object_set (G_OBJECT (window->details->split_view_hpane),
                  "position", 0,
                  "position-set", FALSE,
                  NULL);

	nolphin_window_set_active_pane (window, active_pane);
	nolphin_navigation_state_set_master (window->details->nav_state,
					      active_pane->action_group);

	nolphin_window_update_show_hide_ui_elements (window);
}

#define MAX_SPLIT_PANES 4

static void
center_nested_paned (GtkWidget *paned, GdkRectangle *allocation, gpointer user_data)
{
	gint size = gtk_orientable_get_orientation (GTK_ORIENTABLE (paned)) == GTK_ORIENTATION_HORIZONTAL
		    ? allocation->width : allocation->height;

	if (size <= 1) {
		return;
	}
	gtk_paned_set_position (GTK_PANED (paned), size / 2);
	g_signal_handlers_disconnect_by_func (paned, center_nested_paned, user_data);
}

/* Setzt @active in einen eigenen, verschachtelten GtkPaned der Ausrichtung
 * @orientation und legt daneben einen neuen Bereich an (zweites Kind).
 * Rückgabe: der neue Bereich, mit einem Reiter, aber noch ohne Ort. */
static NolphinWindowPane *
split_pane_nested (NolphinWindow *window, NolphinWindowPane *active, GtkOrientation orientation)
{
	NolphinWindowPane *pane;
	GtkPaned *parent, *nested;
	gboolean was_child1;

	parent = GTK_PANED (gtk_widget_get_parent (GTK_WIDGET (active)));
	was_child1 = gtk_paned_get_child1 (parent) == GTK_WIDGET (active);

	pane = nolphin_window_pane_new (window);
	window->details->panes = g_list_append (window->details->panes, pane);

	nested = GTK_PANED (gtk_paned_new (orientation));
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (nested)), "nolphin-split-paned");
	g_object_ref (active);
	gtk_container_remove (GTK_CONTAINER (parent), GTK_WIDGET (active));
	gtk_paned_pack1 (nested, GTK_WIDGET (active), TRUE, FALSE);
	gtk_paned_pack2 (nested, GTK_WIDGET (pane), TRUE, FALSE);
	g_object_unref (active);
	if (was_child1) {
		gtk_paned_pack1 (parent, GTK_WIDGET (nested), TRUE, FALSE);
	} else {
		gtk_paned_pack2 (parent, GTK_WIDGET (nested), TRUE, FALSE);
	}
	g_signal_connect (nested, "size-allocate", G_CALLBACK (center_nested_paned), NULL);
	gtk_widget_show (GTK_WIDGET (nested));

	gtk_widget_hide (pane->tool_bar);
	pane->active_slot = nolphin_window_pane_open_slot (NOLPHIN_WINDOW_PANE (pane), NOLPHIN_WINDOW_OPEN_SLOT_APPEND);

	return pane;
}

/* Bereich duplizieren: öffnet einen weiteren Bereich (bis zu vier) am
 * Ort des aktiven Bereichs. Ab dem dritten Bereich wird der aktive Bereich
 * in einen eigenen, verschachtelten GtkPaned mit umgekehrter Ausrichtung
 * gesetzt. */
void
nolphin_window_split_view_add_pane (NolphinWindow *window)
{
	NolphinWindowPane *active, *pane;
	NolphinWindowSlot *slot, *old_slot;
	GtkPaned *parent;
	GFile *location = NULL;
	GtkOrientation orientation;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!nolphin_window_split_view_showing (window)) {
		nolphin_window_split_view_on (window);
		nolphin_window_update_show_hide_ui_elements (window);
		return;
	}

	if (g_list_length (window->details->panes) >= MAX_SPLIT_PANES) {
		return;
	}

	active = nolphin_window_get_active_pane (window);
	if (active == NULL || !GTK_IS_PANED (gtk_widget_get_parent (GTK_WIDGET (active)))) {
		return;
	}
	old_slot = active->active_slot;
	parent = GTK_PANED (gtk_widget_get_parent (GTK_WIDGET (active)));
	orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (parent)) == GTK_ORIENTATION_HORIZONTAL
		      ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;

	pane = split_pane_nested (window, active, orientation);
	slot = pane->active_slot;

	if (old_slot != NULL) {
		location = nolphin_window_slot_get_location (old_slot);
		if (location != NULL && g_file_has_uri_scheme (location, "x-nolphin-search")) {
			g_object_unref (location);
			location = NULL;
		}
	}
	if (location == NULL) {
		location = g_file_new_for_path (g_get_home_dir ());
	}
	nolphin_window_slot_open_location (slot, location, 0);
	g_object_unref (location);

	nolphin_window_update_show_hide_ui_elements (window);
	nolphin_window_sync_tab_actions (window);
}

/* --- Layouts der geteilten Ansicht ----------------------------------
 * Die vorhandenen Bereiche werden in den Baum des gewählten Layouts umgesetzt.
 * Sie bleiben dabei unverändert (Reiter, Auswahl, Ort). Gibt es mehr Bereiche
 * als Zonen, wandern die Orte der überzähligen als Reiter in die letzte Zone;
 * fehlende Zonen bekommen einen neuen Bereich am Ort des aktiven Bereichs. */

static gint
split_layout_zones (NolphinSplitLayout layout)
{
	switch (layout) {
	case NOLPHIN_SPLIT_LAYOUT_TWO_COLUMNS:
	case NOLPHIN_SPLIT_LAYOUT_TWO_ROWS:
		return 2;
	case NOLPHIN_SPLIT_LAYOUT_THREE_COLUMNS:
	case NOLPHIN_SPLIT_LAYOUT_BIG_PLUS_TWO:
		return 3;
	case NOLPHIN_SPLIT_LAYOUT_GRID:
	default:
		return 4;
	}
}

static void
collect_panes_in_tree (GtkWidget *widget, GList **out)
{
	if (widget == NULL) {
		return;
	}
	if (NOLPHIN_IS_WINDOW_PANE (widget)) {
		*out = g_list_append (*out, widget);
	} else if (GTK_IS_PANED (widget)) {
		collect_panes_in_tree (gtk_paned_get_child1 (GTK_PANED (widget)), out);
		collect_panes_in_tree (gtk_paned_get_child2 (GTK_PANED (widget)), out);
	}
}

/* Setzt die Trennlinie einmalig auf @user_data Promille der Größe. */
static void
layout_set_divider (GtkWidget *paned, GdkRectangle *allocation, gpointer user_data)
{
	gint size = gtk_orientable_get_orientation (GTK_ORIENTABLE (paned)) == GTK_ORIENTATION_HORIZONTAL
		    ? allocation->width : allocation->height;

	if (size <= 1) {
		return;
	}
	gtk_paned_set_position (GTK_PANED (paned), size * GPOINTER_TO_INT (user_data) / 1000);
	g_signal_handlers_disconnect_by_func (paned, layout_set_divider, user_data);
}

static void
layout_fill_paned (GtkPaned *paned, GtkOrientation orientation,
		   GtkWidget *a, GtkWidget *b, gint permille)
{
	gtk_orientable_set_orientation (GTK_ORIENTABLE (paned), orientation);
	gtk_paned_pack1 (paned, a, TRUE, FALSE);
	gtk_paned_pack2 (paned, b, TRUE, FALSE);
	g_signal_connect (paned, "size-allocate", G_CALLBACK (layout_set_divider), GINT_TO_POINTER (permille));
	gtk_widget_show (GTK_WIDGET (paned));
}

static GtkWidget *
layout_new_paned (GtkOrientation orientation, GtkWidget *a, GtkWidget *b, gint permille)
{
	GtkWidget *paned = gtk_paned_new (orientation);

	gtk_style_context_add_class (gtk_widget_get_style_context (paned), "nolphin-split-paned");
	layout_fill_paned (GTK_PANED (paned), orientation, a, b, permille);
	return paned;
}

void
nolphin_window_apply_split_layout (NolphinWindow *window, NolphinSplitLayout layout)
{
	GList *tree = NULL, *l;
	GPtrArray *keep;
	NolphinWindowPane *active, *target;
	GtkPaned *root;
	GtkWidget *child;
	gint zones, count, i;
	GtkWidget **p;
	gboolean *was_visible;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (nolphin_window_is_desktop (window)) {
		return;
	}

	zones = split_layout_zones (layout);
	root = GTK_PANED (window->details->split_view_hpane);
	set_pane_maximized (window, FALSE);
	active = nolphin_window_get_active_pane (window);

	collect_panes_in_tree (GTK_WIDGET (root), &tree);

	/* Überzählige Bereiche: Orte als Reiter in die letzte Zone übernehmen */
	if ((gint) g_list_length (tree) > zones) {
		NolphinWindowPane *last = g_list_nth_data (tree, zones - 1);

		for (l = g_list_nth (tree, zones); l != NULL; l = l->next) {
			NolphinWindowPane *extra = l->data;
			GList *s;

			for (s = extra->slots; s != NULL; s = s->next) {
				GFile *loc = nolphin_window_slot_get_location (s->data);

				if (loc != NULL && !g_file_has_uri_scheme (loc, "x-nolphin-search")) {
					NolphinWindowSlot *slot = nolphin_window_pane_open_slot (last, NOLPHIN_WINDOW_OPEN_SLOT_APPEND);

					nolphin_window_slot_open_location (slot, loc, 0);
				}
				g_clear_object (&loc);
			}
		}
		for (l = g_list_nth (tree, zones); l != NULL; l = l->next) {
			if (l->data == active) {
				active = last;
			}
			nolphin_window_close_pane (window, l->data);
		}
		g_list_free (tree);
		tree = NULL;
		collect_panes_in_tree (GTK_WIDGET (root), &tree);
	}

	/* Fehlende Zonen: neue Bereiche am Ort des aktiven Bereichs */
	count = g_list_length (tree);
	while (count < zones) {
		NolphinWindowPane *pane = nolphin_window_pane_new (window);
		GFile *location = NULL;

		window->details->panes = g_list_append (window->details->panes, pane);
		gtk_widget_hide (pane->tool_bar);
		pane->active_slot = nolphin_window_pane_open_slot (pane, NOLPHIN_WINDOW_OPEN_SLOT_APPEND);

		if (active != NULL && active->active_slot != NULL) {
			location = nolphin_window_slot_get_location (active->active_slot);
			if (location != NULL && g_file_has_uri_scheme (location, "x-nolphin-search")) {
				g_clear_object (&location);
			}
		}
		if (location == NULL) {
			location = g_file_new_for_path (g_get_home_dir ());
		}
		nolphin_window_slot_open_location (pane->active_slot, location, 0);
		g_object_unref (location);

		tree = g_list_append (tree, pane);
		count++;
	}

	/* Alle Bereiche aus dem alten Baum lösen, danach den Rest abbauen */
	keep = g_ptr_array_new ();
	was_visible = g_new0 (gboolean, g_list_length (tree));
	for (l = tree, i = 0; l != NULL; l = l->next, i++) {
		GtkWidget *w = l->data;
		GtkWidget *parent = gtk_widget_get_parent (w);

		/* Noch nicht geladene Bereiche bleiben unsichtbar: das Fenster
		 * zeigt sich erst, wenn alle Bereiche von selbst sichtbar werden. */
		was_visible[i] = gtk_widget_get_visible (w);
		g_ptr_array_add (keep, g_object_ref (w));
		if (parent != NULL) {
			gtk_container_remove (GTK_CONTAINER (parent), w);
		}
	}
	while ((child = gtk_paned_get_child1 (root)) != NULL) {
		gtk_container_remove (GTK_CONTAINER (root), child);
	}
	while ((child = gtk_paned_get_child2 (root)) != NULL) {
		gtk_container_remove (GTK_CONTAINER (root), child);
	}
	g_signal_handlers_disconnect_by_func (root, center_pane_divider, NULL);
	g_object_set (G_OBJECT (root), "position", 0, "position-set", FALSE, NULL);

	p = (GtkWidget **) keep->pdata;
	switch (layout) {
	case NOLPHIN_SPLIT_LAYOUT_TWO_COLUMNS:
		layout_fill_paned (root, GTK_ORIENTATION_HORIZONTAL, p[0], p[1], 500);
		break;
	case NOLPHIN_SPLIT_LAYOUT_THREE_COLUMNS:
		layout_fill_paned (root, GTK_ORIENTATION_HORIZONTAL, p[0],
				   layout_new_paned (GTK_ORIENTATION_HORIZONTAL, p[1], p[2], 500), 333);
		break;
	case NOLPHIN_SPLIT_LAYOUT_GRID:
		layout_fill_paned (root, GTK_ORIENTATION_VERTICAL,
				   layout_new_paned (GTK_ORIENTATION_HORIZONTAL, p[0], p[1], 500),
				   layout_new_paned (GTK_ORIENTATION_HORIZONTAL, p[2], p[3], 500), 500);
		break;
	case NOLPHIN_SPLIT_LAYOUT_BIG_PLUS_TWO:
		layout_fill_paned (root, GTK_ORIENTATION_HORIZONTAL, p[0],
				   layout_new_paned (GTK_ORIENTATION_VERTICAL, p[1], p[2], 500), 667);
		break;
	case NOLPHIN_SPLIT_LAYOUT_TWO_ROWS:
	default:
		layout_fill_paned (root, GTK_ORIENTATION_VERTICAL, p[0], p[1], 500);
		break;
	}
	for (i = 0; i < (gint) keep->len; i++) {
		gtk_widget_set_visible (p[i], was_visible[i]);
		g_object_unref (p[i]);
	}
	g_ptr_array_free (keep, TRUE);
	g_free (was_visible);

	/* Reihenfolge der Bereichsliste = Reihenfolge im Baum */
	g_list_free (window->details->panes);
	window->details->panes = g_list_copy (tree);

	target = (active != NULL && g_list_find (tree, active) != NULL) ? active : tree->data;
	g_list_free (tree);

	g_object_set_data (G_OBJECT (window), "nolphin-split-preset", GINT_TO_POINTER ((gint) layout + 1));

	nolphin_window_set_active_pane (window, target);
	nolphin_navigation_state_set_master (window->details->nav_state, target->action_group);
	nolphin_window_update_show_hide_ui_elements (window);
	nolphin_window_sync_tab_actions (window);
}

/* Tauscht den aktiven Bereich mit dem nächsten in der Reihenfolge des Baums.
 * Die Zonen (Größen, Teilungen) bleiben, nur die Inhalte wechseln den Platz. */
void
nolphin_window_swap_active_pane (NolphinWindow *window)
{
	GList *tree = NULL;
	GtkWidget *a, *b, *pa, *pb;
	gboolean a_first, b_first;
	gint idx, n;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	collect_panes_in_tree (GTK_WIDGET (window->details->split_view_hpane), &tree);
	n = g_list_length (tree);
	idx = g_list_index (tree, window->details->active_pane);
	if (n < 2 || idx < 0) {
		g_list_free (tree);
		return;
	}
	set_pane_maximized (window, FALSE);
	a = g_list_nth_data (tree, idx);
	b = g_list_nth_data (tree, (idx + 1) % n);
	g_list_free (tree);

	pa = gtk_widget_get_parent (a);
	pb = gtk_widget_get_parent (b);
	a_first = gtk_paned_get_child1 (GTK_PANED (pa)) == a;
	b_first = gtk_paned_get_child1 (GTK_PANED (pb)) == b;

	g_object_ref (a);
	g_object_ref (b);
	gtk_container_remove (GTK_CONTAINER (pa), a);
	gtk_container_remove (GTK_CONTAINER (pb), b);
	if (b_first) {
		gtk_paned_pack1 (GTK_PANED (pb), a, TRUE, FALSE);
	} else {
		gtk_paned_pack2 (GTK_PANED (pb), a, TRUE, FALSE);
	}
	if (a_first) {
		gtk_paned_pack1 (GTK_PANED (pa), b, TRUE, FALSE);
	} else {
		gtk_paned_pack2 (GTK_PANED (pa), b, TRUE, FALSE);
	}
	g_object_unref (a);
	g_object_unref (b);

	tree = NULL;
	collect_panes_in_tree (GTK_WIDGET (window->details->split_view_hpane), &tree);
	g_list_free (window->details->panes);
	window->details->panes = g_list_copy (tree);
	g_list_free (tree);

	nolphin_window_pane_grab_focus (NOLPHIN_WINDOW_PANE (a));
	nolphin_window_update_show_hide_ui_elements (window);
}

/* --- Zwischen Bereichen springen -------------------------------------
 * Bereiche werden in der Reihenfolge des Baums nummeriert (oben/links = 1). */

void
nolphin_window_activate_pane_number (NolphinWindow *window, gint number)
{
	GList *tree = NULL;
	NolphinWindowPane *pane;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	collect_panes_in_tree (window->details->split_view_hpane, &tree);
	pane = g_list_nth_data (tree, number - 1);
	g_list_free (tree);

	if (pane == NULL || pane == window->details->active_pane || !gtk_widget_get_visible (GTK_WIDGET (pane))) {
		return;
	}
	nolphin_window_set_active_pane (window, pane);
	nolphin_navigation_state_set_master (window->details->nav_state, pane->action_group);
	nolphin_window_pane_grab_focus (pane);
	nolphin_window_sync_tab_actions (window);
}

void
nolphin_window_activate_previous_pane (NolphinWindow *window)
{
	NolphinWindowPane *pane;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	pane = window->details->previous_pane;
	if (pane == NULL || pane == window->details->active_pane || !gtk_widget_get_visible (GTK_WIDGET (pane))) {
		return;
	}
	nolphin_window_set_active_pane (window, pane);
	nolphin_navigation_state_set_master (window->details->nav_state, pane->action_group);
	nolphin_window_pane_grab_focus (pane);
	nolphin_window_sync_tab_actions (window);
}

/* Zeichnet die Bereichsnummer über den Inhalt des Bereichs (kein eigenes Fenster). */
static gboolean
pane_number_draw_after (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	gint number = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "nolphin-pane-number"));
	GtkStyleContext *context;
	GdkRGBA fg, bg;
	cairo_text_extents_t ext;
	gchar *text;
	double w, h, r, cx, cy;

	if (number <= 0) {
		return FALSE;
	}
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &fg);
	gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL, GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &bg, NULL);
	if (bg.alpha < 0.5) {
		bg.red = 1 - fg.red;
		bg.green = 1 - fg.green;
		bg.blue = 1 - fg.blue;
	}
	w = gtk_widget_get_allocated_width (widget);
	h = gtk_widget_get_allocated_height (widget);
	r = MIN (w, h) / 6;
	cx = w / 2;
	cy = h / 2;

	cairo_arc (cr, cx, cy, r, 0, 2 * G_PI);
	cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, 0.85);
	cairo_fill_preserve (cr);
	cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.9);
	cairo_set_line_width (cr, 2);
	cairo_stroke (cr);

	text = g_strdup_printf ("%d", number);
	cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size (cr, r * 1.3);
	cairo_text_extents (cr, text, &ext);
	cairo_move_to (cr, cx - ext.width / 2 - ext.x_bearing, cy - ext.height / 2 - ext.y_bearing);
	cairo_show_text (cr, text);
	g_free (text);
	return FALSE;
}

static void
set_pane_numbers (NolphinWindow *window, gboolean show)
{
	GList *tree = NULL, *l;
	gint n = 1;

	collect_panes_in_tree (window->details->split_view_hpane, &tree);
	for (l = tree; l != NULL; l = l->next, n++) {
		GObject *pane = l->data;

		if (g_object_get_data (pane, "nolphin-pane-number-hooked") == NULL) {
			g_signal_connect_after (pane, "draw", G_CALLBACK (pane_number_draw_after), NULL);
			g_object_set_data (pane, "nolphin-pane-number-hooked", GINT_TO_POINTER (1));
		}
		g_object_set_data (pane, "nolphin-pane-number", GINT_TO_POINTER (show ? n : 0));
		gtk_widget_queue_draw (GTK_WIDGET (pane));
	}
	g_list_free (tree);
}

static gboolean
hide_pane_numbers_cb (gpointer user_data)
{
	NolphinWindow *window = NOLPHIN_WINDOW (user_data);

	window->details->pane_numbers_timeout = 0;
	set_pane_numbers (window, FALSE);
	return G_SOURCE_REMOVE;
}

/* Blendet die Bereichsnummern kurz ein (nur bei mehreren Bereichen). */
void
nolphin_window_show_pane_numbers (NolphinWindow *window)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!nolphin_window_split_view_showing (window)) {
		return;
	}
	if (window->details->pane_numbers_timeout != 0) {
		g_source_remove (window->details->pane_numbers_timeout);
	}
	set_pane_numbers (window, TRUE);
	window->details->pane_numbers_timeout = g_timeout_add (1500, hide_pane_numbers_cb, window);
}

/* Bereich schließen: schließt den aktiven Bereich (nur bei mehreren). */
void
nolphin_window_close_active_pane (NolphinWindow *window)
{
	NolphinWindowPane *active, *next;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!nolphin_window_split_view_showing (window)) {
		return;
	}

	active = nolphin_window_get_active_pane (window);
	next = nolphin_window_get_next_pane (window);
	if (active == NULL || next == NULL) {
		return;
	}

	g_clear_object (&window->details->secondary_pane_last_location);
	window->details->secondary_pane_last_location = nolphin_window_slot_get_location (active->active_slot);

	nolphin_window_close_pane (window, active);
	nolphin_window_set_active_pane (window, next);
	nolphin_navigation_state_set_master (window->details->nav_state, next->action_group);

	nolphin_window_update_show_hide_ui_elements (window);
	nolphin_window_sync_tab_actions (window);
}

/* Blendet nach dem (Ein-)Ausblenden von Bereichen auch verschachtelte
 * Paneds aus, in denen kein Kind mehr sichtbar ist (und wieder ein). */
static void
sync_nested_paned_visibility (NolphinWindow *window, GtkWidget *widget)
{
	GtkWidget *parent = gtk_widget_get_parent (widget);

	while (GTK_IS_PANED (parent) && parent != window->details->split_view_hpane) {
		GtkWidget *c1 = gtk_paned_get_child1 (GTK_PANED (parent));
		GtkWidget *c2 = gtk_paned_get_child2 (GTK_PANED (parent));
		gboolean visible = (c1 != NULL && gtk_widget_get_visible (c1)) ||
				   (c2 != NULL && gtk_widget_get_visible (c2));

		gtk_widget_set_visible (parent, visible);
		parent = gtk_widget_get_parent (parent);
	}
}

static void
set_pane_maximized (NolphinWindow *window, gboolean maximized)
{
	NolphinWindowPane *active = window->details->active_pane;
	GList *l;

	if (!!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "nolphin-pane-maximized")) == maximized) {
		return;
	}
	if (maximized && active == NULL) {
		return;
	}

	for (l = window->details->panes; l != NULL; l = l->next) {
		NolphinWindowPane *pane = l->data;

		if (pane != active) {
			gtk_widget_set_visible (GTK_WIDGET (pane), !maximized);
			sync_nested_paned_visibility (window, GTK_WIDGET (pane));
		}
	}
	g_object_set_data (G_OBJECT (window), "nolphin-pane-maximized", GINT_TO_POINTER (maximized));
}

/* Bereich maximieren / wiederherstellen */
void
nolphin_window_toggle_maximize_pane (NolphinWindow *window)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!nolphin_window_split_view_showing (window)) {
		return;
	}

	set_pane_maximized (window, !GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "nolphin-pane-maximized")));
	nolphin_window_sync_tab_actions (window);
}

gboolean
nolphin_window_split_view_showing (NolphinWindow *window)
{
	return g_list_length (NOLPHIN_WINDOW (window)->details->panes) > 1;
}

/* Shift+F3 ("horizontale Teilung" per spec, i.e. panes stacked top/bottom -
 * which is GTK_ORIENTATION_VERTICAL for a GtkPaned, since GTK names paned
 * orientation after the child-arrangement axis, not the divider line).
 * F3 ("vertikale Teilung", panes side-by-side) remains the on/off toggle
 * regardless of orientation; this just ensures the split is showing and
 * stacked, re-orienting an already-open side-by-side split in place rather
 * than opening a third pane - true recursive N-way nesting is not yet
 * implemented. */
void
nolphin_window_split_view_toggle_horizontal (NolphinWindow *window)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!nolphin_window_split_view_showing (window)) {
		gtk_orientable_set_orientation (GTK_ORIENTABLE (window->details->split_view_hpane),
						GTK_ORIENTATION_VERTICAL);
		nolphin_window_split_view_on (window);
	} else {
		gtk_orientable_set_orientation (GTK_ORIENTABLE (window->details->split_view_hpane),
						GTK_ORIENTATION_VERTICAL);
	}

	nolphin_window_update_show_hide_ui_elements (window);
}

void
nolphin_window_set_show_terminal (NolphinWindow *window,
				  gboolean        show)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	/* Bewusst KEIN frühes "return" bei show == show_terminal: das Flag
	 * bildet nur den Terminal/nicht-Terminal-Zustand ab, aber der rechte
	 * Arbeitsbereich hat noch weitere Seiten (Archiv, Suche, Eigenschaften,
	 * Git, …), die über ihre jeweiligen eigenen show_*()-Funktionen direkt
	 * angezeigt werden, OHNE dieses Flag zurückzusetzen. Dadurch kann
	 * show_terminal noch TRUE sein, obwohl gerade eine andere Seite sichtbar
	 * ist - ein frühes "return" würde dann faelschlich gar nichts tun,
	 * wenn man versucht, das Terminal (wieder) zu zeigen. */

	window->details->show_terminal = show;

	if (show) {
		nolphin_workspace_panel_show_terminal (window->details->workspace_panel, window);
		nolphin_window_sync_terminal_location (window);
		nolphin_terminal_grab_focus (NOLPHIN_TERMINAL (window->details->terminal));
	} else {
		nolphin_workspace_panel_show_preview (window->details->workspace_panel);
	}
}

gboolean
nolphin_window_terminal_showing (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	return window->details->show_terminal;
}

void
nolphin_window_sync_terminal_location (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	GFile *location;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!window->details->show_terminal) {
		return;
	}

	slot = nolphin_window_get_active_slot (window);
	if (slot == NULL) {
		return;
	}

	location = nolphin_window_slot_get_location (slot);
	if (location == NULL) {
		return;
	}

	nolphin_terminal_set_location (NOLPHIN_TERMINAL (window->details->terminal), location);
	g_object_unref (location);
}

void
nolphin_window_set_show_preview (NolphinWindow *window,
				 gboolean        show)
{
	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (show == window->details->show_preview) {
		return;
	}

	window->details->show_preview = show;

	if (show) {
		gint total, wanted_width;

		total = gtk_widget_get_allocated_width (window->details->preview_hpaned);
		if (total > 1) {
			wanted_width = MAX (g_settings_get_int (nolphin_window_state,
								NOLPHIN_WINDOW_STATE_PREVIEW_WIDTH),
					    NOLPHIN_PREVIEW_MIN_WIDTH);
			gtk_paned_set_position (GTK_PANED (window->details->preview_hpaned),
						MAX (total - wanted_width, 1));
		}

		g_object_set_data (G_OBJECT (window->details->preview_hpaned), "nolphin-last-width", NULL);
		gtk_widget_show (window->details->workspace_panel);
		nolphin_window_sync_preview_selection (window);
	} else {
		gtk_widget_hide (window->details->workspace_panel);
		nolphin_preview_clear (NOLPHIN_PREVIEW (window->details->preview));
	}

	g_settings_set_boolean (nolphin_window_state,
				NOLPHIN_WINDOW_STATE_START_WITH_PREVIEW,
				show);
}

gboolean
nolphin_window_preview_showing (NolphinWindow *window)
{
	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), FALSE);

	return window->details->show_preview;
}

void
nolphin_window_sync_preview_selection (NolphinWindow *window)
{
	NolphinWindowSlot *slot;
	NolphinView *view;
	GList *selection;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	if (!window->details->show_preview) {
		return;
	}

	slot = nolphin_window_get_active_slot (window);
	if (slot == NULL || slot->content_view == NULL) {
		nolphin_preview_clear (NOLPHIN_PREVIEW (window->details->preview));
		return;
	}

	view = NOLPHIN_VIEW (slot->content_view);
	selection = nolphin_view_get_selection (view);
	nolphin_preview_set_selection (NOLPHIN_PREVIEW (window->details->preview),
				       selection, nolphin_view_get_directory_as_file (view));
	nolphin_workspace_panel_sync_properties (nolphin_window_get_workspace_panel (window),
						 selection, nolphin_view_get_directory_as_file (view));
	nolphin_file_list_free (selection);
}

void
nolphin_window_clear_secondary_pane_location (NolphinWindow *window)
{
    g_return_if_fail (NOLPHIN_IS_WINDOW (window));
    g_clear_object (&window->details->secondary_pane_last_location);
}

void
nolphin_window_set_sidebar_id (NolphinWindow *window,
                            const gchar *id)
{
    if (g_strcmp0 (id, window->details->sidebar_id) != 0) {

        g_settings_set_string (nolphin_window_state,
                               NOLPHIN_WINDOW_STATE_SIDE_PANE_VIEW,
                               id);

        g_free (window->details->sidebar_id);

        window->details->sidebar_id = g_strdup (id);

        g_object_notify_by_pspec (G_OBJECT (window), properties[PROP_SIDEBAR_VIEW_TYPE]);
    }
}

const gchar *
nolphin_window_get_sidebar_id (NolphinWindow *window)
{
    return window->details->sidebar_id;
}

void
nolphin_window_set_show_sidebar (NolphinWindow *window,
                              gboolean show)
{
    if (!NOLPHIN_IS_DESKTOP_WINDOW (window)) {
        window->details->show_sidebar = show;

        g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_START_WITH_SIDEBAR, show);

        g_object_notify_by_pspec (G_OBJECT (window), properties[PROP_SHOW_SIDEBAR]);
    }
}

gboolean
nolphin_window_get_show_sidebar (NolphinWindow *window)
{
    return window->details->show_sidebar;
}

const gchar *
nolphin_window_get_ignore_meta_view_id (NolphinWindow *window)
{
    return window->details->ignore_meta_view_id;
}

void
nolphin_window_set_ignore_meta_view_id (NolphinWindow *window, const gchar *id)
{
    if (id != NULL) {
        gchar *old_id = window->details->ignore_meta_view_id;
        if (g_strcmp0 (old_id, id) != 0) {
            nolphin_window_set_ignore_meta_zoom_level (window, -1);
        }
        window->details->ignore_meta_view_id = g_strdup (id);
        g_free (old_id);
    }
}

gint
nolphin_window_get_ignore_meta_zoom_level (NolphinWindow *window)
{
    return window->details->ignore_meta_zoom_level;
}

void
nolphin_window_set_ignore_meta_zoom_level (NolphinWindow *window, gint level)
{
    window->details->ignore_meta_zoom_level = level;
}

GList *
nolphin_window_get_ignore_meta_visible_columns (NolphinWindow *window)
{
    return g_list_copy_deep (window->details->ignore_meta_visible_columns, (GCopyFunc) g_strdup, NULL);
}

void
nolphin_window_set_ignore_meta_visible_columns (NolphinWindow *window, GList *list)
{
    GList *old = window->details->ignore_meta_visible_columns;
    window->details->ignore_meta_visible_columns = list != NULL ? g_list_copy_deep (list, (GCopyFunc) g_strdup, NULL) :
                                                                  NULL;
    if (old != NULL)
        g_list_free_full (old, g_free);
}

GList *
nolphin_window_get_ignore_meta_column_order (NolphinWindow *window)
{
    return g_list_copy_deep (window->details->ignore_meta_column_order, (GCopyFunc) g_strdup, NULL);
}

void
nolphin_window_set_ignore_meta_column_order (NolphinWindow *window, GList *list)
{
    GList *old = window->details->ignore_meta_column_order;
    window->details->ignore_meta_column_order = list != NULL ? g_list_copy_deep (list, (GCopyFunc) g_strdup, NULL) :
                                                               NULL;
    if (old != NULL)
        g_list_free_full (old, g_free);
}

const gchar *
nolphin_window_get_ignore_meta_sort_column (NolphinWindow *window)
{
    return window->details->ignore_meta_sort_column;
}

void
nolphin_window_set_ignore_meta_sort_column (NolphinWindow *window, const gchar *column)
{
    if (column != NULL) {
        gchar *old_column = window->details->ignore_meta_sort_column;
        window->details->ignore_meta_sort_column = g_strdup (column);
        g_free (old_column);
    }
}

gint
nolphin_window_get_ignore_meta_sort_direction (NolphinWindow *window)
{
    return window->details->ignore_meta_sort_direction;
}

void
nolphin_window_set_ignore_meta_sort_direction (NolphinWindow *window, gint direction)
{
    window->details->ignore_meta_sort_direction = direction;
}

NolphinWindowOpenFlags
nolphin_event_get_window_open_flags (void)
{
	NolphinWindowOpenFlags flags = 0;
	GdkEvent *event;

	event = gtk_get_current_event ();

	if (event == NULL) {
		return flags;
	}

	if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
	    (event->button.button == 2)) {
		flags |= NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB;
	}

	gdk_event_free (event);

	return flags;
}
