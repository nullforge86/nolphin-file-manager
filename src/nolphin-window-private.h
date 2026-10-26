/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
 *  Nolphin
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 1999, 2000, 2001 Eazel, Inc.
 *
 *  Nolphin is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Nolphin is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>
 *           Darin Adler <darin@bentspoon.com>
 *
 */

#ifndef NOLPHIN_WINDOW_PRIVATE_H
#define NOLPHIN_WINDOW_PRIVATE_H

#include "nolphin-window.h"
#include "nolphin-window-slot.h"
#include "nolphin-window-pane.h"
#include "nolphin-navigation-state.h"
#include "nolphin-bookmark-list.h"

#include <libnolphin-private/nolphin-directory.h>

/* FIXME bugzilla.gnome.org 42575: Migrate more fields into here. */
struct NolphinWindowDetails
{
        GtkWidget *statusbar;
        GtkWidget *menubar;

        GtkWidget *nolphin_status_bar;
        GtkWidget *statusbar_separator;

        GtkUIManager *ui_manager;
        GtkActionGroup *main_action_group; /* owned by ui_manager */
        guint help_message_cid;

        /* Menus. */
        guint extensions_menu_merge_id;
        GtkActionGroup *extensions_menu_action_group;

        GtkActionGroup *bookmarks_action_group;
        GtkActionGroup *toolbar_action_group;
        guint bookmarks_merge_id;
        NolphinBookmarkList *bookmark_list;

	NolphinWindowShowHiddenFilesMode show_hidden_files_mode;

	/* Ensures that we do not react on signals of a
	 * view that is re-used as new view when its loading
	 * is cancelled
	 */
	gboolean temporarily_ignore_view_signals;

        /* available panes, and active pane.
         * Both of them may never be NULL.
         */
        GList *panes;
        NolphinWindowPane *active_pane;
        NolphinWindowPane *previous_pane;
        guint pane_numbers_timeout;

        GtkWidget *content_paned;
        NolphinNavigationState *nav_state;
        
        /* Side Pane */
        int side_pane_width;
        gboolean side_pane_clamped;  /* Seitenleiste wegen Platzmangel schmaler als gespeichert */
        GtkWidget *sidebar;
        gchar *sidebar_id;

        gboolean show_sidebar;

        /* Toolbar */
        GtkWidget *toolbar;

        /* Toolbar holder */
        GtkWidget *toolbar_holder;

        guint extensions_toolbar_merge_id;
        GtkActionGroup *extensions_toolbar_action_group;

        guint menu_hide_delay_id;

        /* split view */
        GtkWidget *split_view_hpane;

        /* integrated terminal (F4) - lebt als Seite im workspace_panel
         * (rechte Leiste), nicht mehr in einem eigenen unteren Bereich. */
        GtkWidget *terminal;
        gboolean show_terminal;

        /* info/preview panel (F11) - "workspace_panel" is the tabbed
         * container (Vorschau, Eigenschaften, Archiv, Terminal,
         * Suche/Aktionen, Git, .deb-Paket); "preview" is still just the
         * NolphinPreview widget nested in its first tab. */
        GtkWidget *preview_hpaned;
        GtkWidget *workspace_panel;
        GtkWidget *preview;
        gboolean show_preview;
        guint preview_width_handler_id;

        /* stack of recently-closed tab locations (most recent first),
         * for restoring the last closed tab. Owns a ref on each GFile. */
        GList *closed_tab_locations;

        // A closed pane's location, valid until the remaining pane
        // location changes.
        GFile *secondary_pane_last_location;

        gboolean disable_chrome;

        guint sidebar_width_handler_id;

        guint menu_state_changed_id;

        gboolean menu_skip_release;
        gboolean menu_show_queued;

        gchar *ignore_meta_view_id;
        gint ignore_meta_zoom_level;
        GList *ignore_meta_visible_columns;
        GList *ignore_meta_column_order;
        gchar *ignore_meta_sort_column;
        gint ignore_meta_sort_direction;

        gboolean dynamic_menu_entries_current;
};

/* window geometry */
/* Min values are very small, and a Nolphin window at this tiny size is *almost*
 * completely unusable. However, if all the extra bits (sidebar, location bar, etc)
 * are turned off, you can see an icon or two at this size. See bug 5946.
 */

#define NOLPHIN_WINDOW_MIN_WIDTH		200
#define NOLPHIN_WINDOW_MIN_HEIGHT		200
#define NOLPHIN_WINDOW_DEFAULT_WIDTH		800
#define NOLPHIN_WINDOW_DEFAULT_HEIGHT		550

typedef void (*NolphinBookmarkFailedCallback) (NolphinWindow *window,
                                                NolphinBookmark *bookmark);

void               nolphin_window_sync_view_type                    (NolphinWindow    *window);
void               nolphin_window_load_extension_menus                  (NolphinWindow    *window);
NolphinWindowPane *nolphin_window_get_next_pane                        (NolphinWindow *window);
void               nolphin_menus_append_bookmark_to_menu                (NolphinWindow    *window, 
                                                                          NolphinBookmark  *bookmark, 
                                                                          const char        *parent_path,
                                                                          const char        *parent_id,
                                                                          guint              index_in_parent,
                                                                          GtkActionGroup    *action_group,
                                                                          guint              merge_id,
                                                                          GCallback          refresh_callback,
                                                                          NolphinBookmarkFailedCallback failed_callback);

NolphinWindowSlot *nolphin_window_get_slot_for_view                    (NolphinWindow *window,
									  NolphinView   *view);

void                 nolphin_window_set_active_slot                     (NolphinWindow    *window,
									  NolphinWindowSlot *slot);
void                 nolphin_window_set_active_pane                     (NolphinWindow *window,
                                                                          NolphinWindowPane *new_pane);
NolphinWindowPane * nolphin_window_get_active_pane                     (NolphinWindow *window);

gboolean nolphin_window_restore_saved_tabs                              (NolphinWindow *window);
void nolphin_window_save_session_state                                  (NolphinWindow *window);


/* sync window GUI with current slot. Used when changing slots,
 * and when updating the slot state.
 */
void nolphin_window_sync_allow_stop       (NolphinWindow *window,
					    NolphinWindowSlot *slot);
void nolphin_window_sync_title            (NolphinWindow *window,
					    NolphinWindowSlot *slot);
void nolphin_window_sync_zoom_widgets     (NolphinWindow *window);
void nolphin_window_sync_menu_bar         (NolphinWindow *window);
void nolphin_window_sync_bookmark_action  (NolphinWindow *window);
void nolphin_window_sync_thumbnail_action (NolphinWindow *window);

/* window menus */
GtkActionGroup *nolphin_window_create_toolbar_action_group (NolphinWindow *window);
void               nolphin_window_initialize_actions                    (NolphinWindow    *window);
void               nolphin_window_initialize_menus                      (NolphinWindow    *window);
void               nolphin_window_finalize_menus                        (NolphinWindow    *window);

void               nolphin_window_update_show_hide_ui_elements           (NolphinWindow     *window);

/* window toolbar */
void               nolphin_window_close_pane                            (NolphinWindow    *window,
                                                                          NolphinWindowPane *pane);
void               nolphin_window_show_location_entry                   (NolphinWindow    *window);

#endif /* NOLPHIN_WINDOW_PRIVATE_H */
