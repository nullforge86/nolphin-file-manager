/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nolphin-view.c
 *
 * Copyright (C) 1999, 2000  Free Software Foundation
 * Copyright (C) 2000, 2001  Eazel, Inc.
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
 *
 * Authors: Ettore Perazzoli,
 *          John Sullivan <sullivan@eazel.com>,
 *          Darin Adler <darin@bentspoon.com>,
 *          Pavel Cisler <pavel@eazel.com>,
 *          David Emory Watson <dwatson@cs.ucr.edu>
 */

#include <config.h>

#include "nolphin-view.h"
#include "nolphin-application.h"
#include "nolphin-window.h"

#include "nolphin-actions.h"
#include "nolphin-gid-projects.h"
#include "nolphin-phone.h"
#include "nolphin-workspace-panel.h"
#include "nolphin-desktop-icon-view.h"
#include "nolphin-error-reporting.h"
#include "nolphin-list-view.h"
#include "nolphin-mime-actions.h"
#include "nolphin-previewer.h"
#include "nolphin-terminal.h"
#include "nolphin-workspace-panel.h"
#include "nolphin-bookmark-list.h"
#include "nolphin-directory-private.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <math.h>

#include <eel/eel-glib-extensions.h>
#include <eel/eel-gnome-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string.h>
#include <eel/eel-vfs-extensions.h>

#include <libnolphin-extension/nolphin-menu-provider.h>
#include <libnolphin-private/nolphin-bookmark.h>
#include <libnolphin-private/nolphin-clipboard.h>
#include <libnolphin-private/nolphin-clipboard-monitor.h>
#include <libnolphin-private/nolphin-desktop-icon-file.h>
#include <libnolphin-private/nolphin-desktop-directory.h>
#include <libnolphin-private/nolphin-search-directory.h>
#include <libnolphin-private/nolphin-directory.h>
#include <libnolphin-private/nolphin-dnd.h>
#include <libnolphin-private/nolphin-file.h>
#include <libnolphin-private/nolphin-file-attributes.h>
#include <libnolphin-private/nolphin-file-changes-queue.h>
#include <libnolphin-private/nolphin-file-dnd.h>
#include <libnolphin-private/nolphin-file-operations.h>
#include <libnolphin-private/nolphin-archive.h>
#include <libnolphin-private/nolphin-checksum.h>
#include <libnolphin-private/nolphin-encryption.h>
#include <libnolphin-private/nolphin-acl.h>
#include <libnolphin-private/nolphin-git.h>
#include <libnolphin-private/nolphin-saved-selections.h>
#include <libnolphin-private/nolphin-file-utilities.h>
#include <libnolphin-private/nolphin-malloc-utils.h>
#include <libnolphin-private/fzy-match.h>
#include <libnolphin-private/nolphin-fzy-utils.h>
#include <libnolphin-private/nolphin-file-private.h>
#include <libnolphin-private/nolphin-global-preferences.h>
#include <libnolphin-private/nolphin-link.h>
#include <libnolphin-private/nolphin-metadata.h>
#include <libnolphin-private/nolphin-recent.h>
#include <libnolphin-private/nolphin-module.h>
#include <libnolphin-private/nolphin-mount-operation.h>
#include <libnolphin-private/nolphin-program-choosing.h>
#include <libnolphin-private/nolphin-trash-monitor.h>
#include <libnolphin-private/nolphin-ui-utilities.h>
#include <libnolphin-private/nolphin-signaller.h>
#include <libnolphin-private/nolphin-icon-names.h>
#include <libnolphin-private/nolphin-file-undo-manager.h>
#include <libnolphin-private/nolphin-action.h>
#include <libnolphin-private/nolphin-widget-action.h>
#include <libnolphin-private/nolphin-separator-action.h>
#include <libnolphin-private/nolphin-action-manager.h>
#include <libnolphin-private/nolphin-mime-application-chooser.h>

#define DEBUG_FLAG NOLPHIN_DEBUG_DIRECTORY_VIEW
#include <libnolphin-private/nolphin-debug.h>

/* Minimum starting update interval for progressive display */
#define UPDATE_INTERVAL_MIN 100
/* One-shot pre-render delay in deferred mode (sort by folder size, deep counts etc.),
 * to give async deep counts a chance to arrive before the first render. */
#define UPDATE_INTERVAL_DEFERRED 50
/* Maximum update interval */
#define UPDATE_INTERVAL_MAX 500
/* Amount of miliseconds the update interval is increased */
#define UPDATE_INTERVAL_INC 200
/* Interval at which the update interval is increased */
#define UPDATE_INTERVAL_TIMEOUT_INTERVAL 500
/* Milliseconds that have to pass without a change to reset the update interval */
#define UPDATE_INTERVAL_RESET 1000

#define SILENT_WINDOW_OPEN_LIMIT 5

#define DUPLICATE_HORIZONTAL_ICON_OFFSET 70
#define DUPLICATE_VERTICAL_ICON_OFFSET   30

#define MAX_QUEUED_UPDATES 250
/* Max files to hold before falling back to progressive display during loading */
#define MAX_LOADING_PENDING_HELD 500

#define SELECTION_CHANGED_UPDATE_INTERVAL 50

#define NOLPHIN_VIEW_MENU_PATH_OPEN_PLACEHOLDER                  "/MenuBar/File/Open Placeholder"
#define NOLPHIN_VIEW_MENU_PATH_APPLICATIONS_SUBMENU_PLACEHOLDER  "/MenuBar/File/Open Placeholder/Open With/Applications Placeholder"
#define NOLPHIN_VIEW_MENU_PATH_APPLICATIONS_PLACEHOLDER    	  "/MenuBar/File/Open Placeholder/Applications Placeholder"
#define NOLPHIN_VIEW_MENU_PATH_SCRIPTS_PLACEHOLDER               "/MenuBar/File/Open Placeholder/Scripts/Scripts Placeholder"
#define NOLPHIN_VIEW_MENU_PATH_ACTIONS_PLACEHOLDER               "/MenuBar/File/Open Placeholder/ActionsPlaceholder"
#define NOLPHIN_VIEW_MENU_PATH_EXTENSION_ACTIONS_PLACEHOLDER     "/MenuBar/Edit/Extension Actions"
#define NOLPHIN_VIEW_MENU_PATH_NEW_DOCUMENTS_PLACEHOLDER  	  "/MenuBar/File/New Items Placeholder/New Documents/New Documents Placeholder"
#define NOLPHIN_VIEW_MENU_PATH_OPEN				  "/MenuBar/File/Open Placeholder/Open"

#define NOLPHIN_VIEW_POPUP_PATH_SELECTION            "/selection"
#define NOLPHIN_VIEW_POPUP_PATH_OPEN_PLACEHOLDER	  "/selection/Open Placeholder"
#define NOLPHIN_VIEW_POPUP_PATH_APPLICATIONS_SUBMENU_PLACEHOLDER "/selection/Open Placeholder/Open With/Applications Placeholder"
#define NOLPHIN_VIEW_POPUP_PATH_APPLICATIONS_PLACEHOLDER    	  "/selection/Open Placeholder/Applications Placeholder"
#define NOLPHIN_VIEW_POPUP_PATH_SCRIPTS_PLACEHOLDER    	  "/selection/Open Placeholder/Scripts/Scripts Placeholder"
#define NOLPHIN_VIEW_POPUP_PATH_ACTIONS_PLACEHOLDER           "/selection/Open Placeholder/ActionsPlaceholder"
#define NOLPHIN_VIEW_POPUP_PATH_EXTENSION_ACTIONS		  "/selection/Extension Actions"
#define NOLPHIN_VIEW_POPUP_PATH_OPEN				  "/selection/Open Placeholder/Open"

#define NOLPHIN_VIEW_POPUP_PATH_BACKGROUND			  "/background"
#define NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_SCRIPTS_PLACEHOLDER	  "/background/Before Zoom Items/New Object Items/Scripts/Scripts Placeholder"
#define NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_ACTIONS_PLACEHOLDER   "/background/Before Zoom Items/New Object Items/ActionsPlaceholder"
#define NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_NEW_DOCUMENTS_PLACEHOLDER "/background/Before Zoom Items/New Object Items/New Documents/New Documents Placeholder"

#define NOLPHIN_VIEW_POPUP_PATH_LOCATION			  "/location"

#define NOLPHIN_VIEW_POPUP_PATH_BOOKMARK_MOVETO_ENTRIES_PLACEHOLDER "/selection/File Actions/MoveToMenu/BookmarkMoveToPlaceHolder"
#define NOLPHIN_VIEW_POPUP_PATH_BOOKMARK_COPYTO_ENTRIES_PLACEHOLDER "/selection/File Actions/CopyToMenu/BookmarkCopyToPlaceHolder"
#define NOLPHIN_VIEW_MENU_PATH_BOOKMARK_MOVETO_ENTRIES_PLACEHOLDER "/MenuBar/Edit/File Items Placeholder/MoveToMenu/BookmarkMoveToPlaceHolder"
#define NOLPHIN_VIEW_MENU_PATH_BOOKMARK_COPYTO_ENTRIES_PLACEHOLDER "/MenuBar/Edit/File Items Placeholder/CopyToMenu/BookmarkCopyToPlaceHolder"

#define NOLPHIN_VIEW_POPUP_PATH_PLACES_MOVETO_ENTRIES_PLACEHOLDER "/selection/File Actions/MoveToMenu/PlacesMoveToPlaceHolder"
#define NOLPHIN_VIEW_POPUP_PATH_PLACES_COPYTO_ENTRIES_PLACEHOLDER "/selection/File Actions/CopyToMenu/PlacesCopyToPlaceHolder"
#define NOLPHIN_VIEW_MENU_PATH_PLACES_MOVETO_ENTRIES_PLACEHOLDER "/MenuBar/Edit/File Items Placeholder/MoveToMenu/PlacesMoveToPlaceHolder"
#define NOLPHIN_VIEW_MENU_PATH_PLACES_COPYTO_ENTRIES_PLACEHOLDER "/MenuBar/Edit/File Items Placeholder/CopyToMenu/PlacesCopyToPlaceHolder"

#define MAX_MENU_LEVELS 5
#define TEMPLATE_LIMIT 30

enum {
	ADD_FILE,
	BEGIN_FILE_CHANGES,
	BEGIN_LOADING,
	CLEAR,
	END_FILE_CHANGES,
	END_LOADING,
	FILE_CHANGED,
	LOAD_ERROR,
	MOVE_COPY_ITEMS,
	REMOVE_FILE,
	ZOOM_LEVEL_CHANGED,
	SELECTION_CHANGED,
	TRASH,
	DELETE,
    ACTIVATE_FILTER,
	LAST_SIGNAL
};

enum {
	PROP_WINDOW_SLOT = 1,
	PROP_SUPPORTS_ZOOMING,
	NUM_PROPERTIES
};

static guint signals[LAST_SIGNAL] = { 0 };
static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

static GdkAtom copied_files_atom;

static char *scripts_directory_uri = NULL;
static int scripts_directory_uri_length;

struct NolphinViewDetails
{
	NolphinWindow *window;
	NolphinWindowSlot *slot;
	NolphinDirectory *model;
	NolphinFile *directory_as_file;
	NolphinFile *location_popup_directory_as_file;
	NolphinBookmarkList *bookmarks;
	GdkEventButton *location_popup_event;
	GtkActionGroup *dir_action_group;
	guint dir_merge_id;

	gboolean supports_zooming;

	GList *scripts_directory_list;
	GtkActionGroup *scripts_action_group;
	guint scripts_merge_id;

    GtkActionGroup *actions_action_group;
    guint actions_merge_id;
    guint action_manager_changed_id;
    NolphinActionManager *action_manager;

	GList *templates_directory_list;
	GtkActionGroup *templates_action_group;
	guint templates_merge_id;

	GtkActionGroup *extensions_menu_action_group;
	guint extensions_menu_merge_id;

	guint display_selection_idle_id;
	guint update_menus_timeout_id;
	guint update_status_idle_id;
	guint reveal_selection_idle_id;

	guint display_pending_source_id;
	guint changes_timeout_id;

	guint update_interval;
 	guint64 last_queued;

	guint files_added_handler_id;
	guint files_changed_handler_id;
	guint load_error_handler_id;
	guint done_loading_handler_id;
	guint file_changed_handler_id;

	guint delayed_rename_file_id;

	GList *new_added_files;
	GList *new_changed_files;

	GHashTable *non_ready_files;

	GList *old_added_files;
	GList *old_changed_files;

	GList *pending_selection;

	/* whether we are in the active slot */
	gboolean active;

	/* loading indicates whether this view has begun loading a directory.
	 * This flag should need not be set inside subclasses. NolphinView automatically
	 * sets 'loading' to TRUE before it begins loading a directory's contents and to FALSE
	 * after it finishes loading the directory and its view.
	 */
	gboolean loading;
	gboolean menu_states_untrustworthy;
	gboolean scripts_invalid;
	gboolean templates_invalid;
    gboolean actions_invalid;
	gboolean reported_load_error;

	/* flag to indicate that no file updates should be dispatched to subclasses.
	 * This is a workaround for bug #87701 that prevents the list view from
	 * losing focus when the underlying GtkTreeView is updated.
	 */
	gboolean updates_frozen;
	guint	 updates_queued;
	gboolean needs_reload;
	guint    loading_pending_held;
	const gchar *display_method;
	gboolean displayed_during_load;
	gdouble  first_render_elapsed;

	gboolean is_renaming;

	gboolean sort_directories_first;
	gboolean sort_favorites_first;

	gboolean show_foreign_files;
	gboolean show_hidden_files;
	gboolean ignore_hidden_file_preferences;

	gboolean batching_selection_level;
	gboolean selection_changed_while_batched;

	gboolean selection_was_removed;

	gboolean metadata_for_directory_as_file_pending;
	gboolean metadata_for_files_in_directory_pending;

	gboolean selection_change_is_due_to_shell;
	gboolean send_selection_change_to_shell;

	GtkActionGroup *open_with_action_group;
	guint open_with_merge_id;

	GList *subdirectory_list;

    guint copy_move_merge_ids[4];
    GtkActionGroup *copy_move_action_groups[4];
    guint bookmarks_changed_id;

	GdkPoint context_menu_position;

    gboolean showing_bookmarks_in_to_menus;
    gboolean showing_places_in_to_menus;

    GVolumeMonitor *volume_monitor;

    GTimer *load_timer;

    char *detail_string;

    /* Type-to-filter */
    gchar *filter_text;
    gchar *filter_text_stripped;
    gboolean filter_active;
    gboolean filter_navigation_blocked;
    guint filter_debounce_id;
    GHashTable *filter_score_cache;
};

typedef struct {
	NolphinFile *file;
	NolphinDirectory *directory;
} FileAndDirectory;

/* forward declarations */

static void     send_archive_notification                     (const gchar          *title,
								gboolean              success,
								const gchar          *detail_on_error);
static gboolean display_selection_info_idle_callback           (gpointer              data);
static void     nolphin_view_duplicate_selection              (NolphinView      *view,
							        GList                *files,
							        GArray               *item_locations);
static void     nolphin_view_create_links_for_files           (NolphinView      *view,
							        GList                *files,
							        GArray               *item_locations);
static void     trash_or_delete_files                          (GtkWindow            *parent_window,
								const GList          *files,
								gboolean              delete_if_all_already_in_trash,
								NolphinView      *view);
static void     load_directory                                 (NolphinView      *view,
								NolphinDirectory    *directory);
static void     nolphin_view_apply_filter                         (NolphinView      *view);
static void     reset_filter_state                             (NolphinView      *view);

static void     nolphin_view_merge_menus                      (NolphinView      *view);
static void     nolphin_view_unmerge_menus                    (NolphinView      *view);
static void     nolphin_view_init_show_hidden_files           (NolphinView      *view);
static void     clipboard_changed_callback                     (NolphinClipboardMonitor *monitor,
								NolphinView      *view);
static void     open_one_in_new_window                         (gpointer              data,
								gpointer              callback_data);
static void     schedule_update_menus                          (NolphinView      *view);
static void     schedule_update_menus_callback                 (gpointer              callback_data);
static void     remove_update_menus_timeout_callback           (NolphinView      *view);
static void     schedule_update_status                          (NolphinView      *view);
static void     remove_update_status_idle_callback             (NolphinView *view);
static void     reset_update_interval                          (NolphinView      *view);
static void     schedule_idle_display_of_pending_files         (NolphinView      *view);
static void     unschedule_display_of_pending_files            (NolphinView      *view);
static void     disconnect_model_handlers                      (NolphinView      *view);
static void     metadata_for_directory_as_file_ready_callback  (NolphinFile         *file,
								gpointer              callback_data);
static void     metadata_for_files_in_directory_ready_callback (NolphinDirectory    *directory,
								GList                *files,
								gpointer              callback_data);
static void     nolphin_view_trash_state_changed_callback     (NolphinTrashMonitor *trash,
							        gboolean              state,
							        gpointer              callback_data);
static void     nolphin_view_select_file                      (NolphinView      *view,
							        NolphinFile         *file);

static void     update_templates_directory                     (NolphinView *view);
static void     user_dirs_changed                              (NolphinView *view);

static gboolean file_list_all_are_folders                      (GList *file_list);

static void unschedule_pop_up_location_context_menu (NolphinView *view);
static void disconnect_bookmark_signals (NolphinView *view);
static void run_action_callback (NolphinAction *action, gpointer callback_data);
static void update_accelerated_actions (NolphinView *view);

G_DEFINE_TYPE (NolphinView, nolphin_view, GTK_TYPE_SCROLLED_WINDOW);
#define parent_class nolphin_view_parent_class

/* virtual methods (public and non-public) */

/**
 * nolphin_view_merge_menus:
 *
 * Add this view's menus to the window's menu bar.
 * @view: NolphinView in question.
 */
static void
nolphin_view_merge_menus (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->merge_menus (view);
}

static void
nolphin_view_unmerge_menus (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->unmerge_menus (view);}

static char *
real_get_backing_uri (NolphinView *view)
{
	NolphinDirectory *directory;
	char *uri;

	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	if (view->details->model == NULL) {
		return NULL;
	}

	directory = view->details->model;

	if (NOLPHIN_IS_DESKTOP_DIRECTORY (directory)) {
		directory = nolphin_desktop_directory_get_real_directory (NOLPHIN_DESKTOP_DIRECTORY (directory));
	} else {
		nolphin_directory_ref (directory);
	}

	uri = nolphin_directory_get_uri (directory);

	nolphin_directory_unref (directory);

	return uri;
}

/**
 *
 * nolphin_view_get_backing_uri:
 *
 * Returns the URI for the target location of new directory, new file, new
 * link and paste operations.
 */

char *
nolphin_view_get_backing_uri (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_backing_uri (view);
}

/**
 * nolphin_view_select_all:
 *
 * select all the items in the view
 *
 **/
static void
nolphin_view_select_all (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->select_all (view);
}

static void
nolphin_view_call_set_selection (NolphinView *view, GList *selection)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->set_selection (view, selection);
}

static GList *
nolphin_view_get_selection_for_file_transfer (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_selection_for_file_transfer (view);
}

/**
 * nolphin_view_get_selected_icon_locations:
 *
 * return an array of locations of selected icons if available
 * Return value: GArray of GdkPoints
 *
 **/
static GArray *
nolphin_view_get_selected_icon_locations (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_selected_icon_locations (view);
}

static void
nolphin_view_invert_selection (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->invert_selection (view);
}

/**
 * nolphin_view_reveal_selection:
 *
 * Scroll as necessary to reveal the selected items.
 **/
static void
nolphin_view_reveal_selection (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->reveal_selection (view);
}

/**
 * nolphin_view_reset_to_defaults:
 *
 * set sorting order, zoom level, etc. to match defaults
 *
 **/
static void
nolphin_view_reset_to_defaults (NolphinView *view)
{
    NolphinFile *file;
    NolphinWindow *window;
    g_return_if_fail (NOLPHIN_IS_VIEW (view));

    NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->reset_to_defaults (view);

    gboolean show_hidden = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SHOW_HIDDEN_FILES);

    window = view->details->window;
    if (show_hidden) {
        nolphin_window_set_hidden_files_mode (window, NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_ENABLE);
    } else {
        nolphin_window_set_hidden_files_mode (window, NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_DISABLE);
    }

    file = view->details->slot->viewed_file;
    nolphin_file_set_metadata(file, NOLPHIN_METADATA_KEY_SHOW_THUMBNAILS, NULL, NULL);
    nolphin_file_set_metadata(file, NOLPHIN_METADATA_KEY_DEFAULT_VIEW, NULL, NULL);
    gtk_action_activate (gtk_action_group_get_action (nolphin_window_get_main_action_group (window), NOLPHIN_ACTION_RELOAD));
}

static gboolean
nolphin_view_using_manual_layout (NolphinView  *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return 	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->using_manual_layout (view);
}

static guint
nolphin_view_get_item_count (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), 0);

	return 	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_item_count (view);
}

/**
 * nolphin_view_can_rename_file
 *
 * Determine whether a file can be renamed.
 * @file: A NolphinFile
 *
 * Return value: TRUE if @file can be renamed, FALSE otherwise.
 *
 **/
static gboolean
nolphin_view_can_rename_file (NolphinView *view, NolphinFile *file)
{
	return 	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->can_rename_file (view, file);
}

static gboolean
nolphin_view_is_read_only (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return 	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->is_read_only (view);
}

static gboolean
showing_trash_directory (NolphinView *view)
{
	NolphinFile *file;

	file = nolphin_view_get_directory_as_file (view);
	if (file != NULL) {
		return nolphin_file_is_in_trash (file);
	}
	return FALSE;
}

static gboolean
showing_recent_directory (NolphinView *view)
{
   NolphinFile *file;

   file = nolphin_view_get_directory_as_file (view);
   if (file != NULL) {
       return nolphin_file_is_in_recent (file);
   }
   return FALSE;
}

static gboolean
showing_favorites_directory (NolphinView *view)
{
   NolphinFile *file;

   file = nolphin_view_get_directory_as_file (view);
   if (file != NULL) {
       return nolphin_file_is_in_favorites (file);
   }
   return FALSE;
}

static gboolean
showing_admin_enabled_directory (NolphinView *view)
{
    NolphinFile *file;

    file = nolphin_view_get_directory_as_file (view);

    if (file != NULL) {
        return nolphin_file_has_uri_scheme (file, "admin");
    }

    return FALSE;
}

static gboolean
nolphin_view_supports_creating_files (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

    return !nolphin_view_is_read_only (view)
           && !showing_trash_directory (view)
           && !showing_recent_directory (view)
           && !showing_favorites_directory (view);
}

static gboolean
nolphin_view_is_empty (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return 	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->is_empty (view);
}

/**
 * nolphin_view_bump_zoom_level:
 *
 * bump the current zoom level by invoking the relevant subclass through the slot
 *
 **/
void
nolphin_view_bump_zoom_level (NolphinView *view,
			       int zoom_increment)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	if (!nolphin_view_supports_zooming (view)) {
		return;
	}

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->bump_zoom_level (view, zoom_increment);
}

/**
 * nolphin_view_zoom_to_level:
 *
 * Set the current zoom level by invoking the relevant subclass through the slot
 *
 **/
void
nolphin_view_zoom_to_level (NolphinView *view,
			     NolphinZoomLevel zoom_level)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	if (!nolphin_view_supports_zooming (view)) {
		return;
	}

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->zoom_to_level (view, zoom_level);
}

NolphinZoomLevel
nolphin_view_get_zoom_level (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NOLPHIN_ZOOM_LEVEL_STANDARD);

	if (!nolphin_view_supports_zooming (view)) {
		return NOLPHIN_ZOOM_LEVEL_STANDARD;
	}

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_zoom_level (view);
}

/**
 * nolphin_view_can_zoom_in:
 *
 * Determine whether the view can be zoomed any closer.
 * @view: The zoomable NolphinView.
 *
 * Return value: TRUE if @view can be zoomed any closer, FALSE otherwise.
 *
 **/
gboolean
nolphin_view_can_zoom_in (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	if (!nolphin_view_supports_zooming (view)) {
		return FALSE;
	}

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->can_zoom_in (view);
}

/**
 * nolphin_view_can_zoom_out:
 *
 * Determine whether the view can be zoomed any further away.
 * @view: The zoomable NolphinView.
 *
 * Return value: TRUE if @view can be zoomed any further away, FALSE otherwise.
 *
 **/
gboolean
nolphin_view_can_zoom_out (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	if (!nolphin_view_supports_zooming (view)) {
		return FALSE;
	}

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->can_zoom_out (view);
}

gboolean
nolphin_view_supports_zooming (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return view->details->supports_zooming;
}

/**
 * nolphin_view_restore_default_zoom_level:
 *
 * restore to the default zoom level by invoking the relevant subclass through the slot
 *
 **/
void
nolphin_view_restore_default_zoom_level (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	if (!nolphin_view_supports_zooming (view)) {
		return;
	}

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->restore_default_zoom_level (view);
}

/*
static NolphinZoomLevel
nolphin_view_get_default_zoom_level (NolphinView *view)
{
    g_return_if_fail (NOLPHIN_IS_VIEW (view));

    if (!nolphin_view_supports_zooming (view)) {
        return -1;
    }

    NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_default_zoom_level (view);
}
*/

const char *
nolphin_view_get_view_id (NolphinView *view)
{
	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_view_id (view);
}

char *
nolphin_view_get_first_visible_file (NolphinView *view)
{
	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_first_visible_file (view);
}

void
nolphin_view_scroll_to_file (NolphinView *view,
			      const char *uri)
{
	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->scroll_to_file (view, uri);
}

/**
 * nolphin_view_get_selection:
 *
 * Get a list of NolphinFile pointers that represents the
 * currently-selected items in this view. Subclasses must override
 * the signal handler for the 'get_selection' signal. Callers are
 * responsible for g_free-ing the list (but not its data).
 * @view: NolphinView whose selected items are of interest.
 *
 * Return value: GList of NolphinFile pointers representing the selection.
 *
 **/
GList *
nolphin_view_get_selection (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_selection (view);
}

/**
 * nolphin_view_peek_selection:
 *
 * Get a list of NolphinFile pointers that represents the
 * currently-selected items in this view. Subclasses must override
 * the signal handler for the 'peek_selection' signal. The returned list
 * is owned by the view's icon container and should not be freed.
 * @view: NolphinView whose selected items are of interest.
 *
 * Return value: GList of NolphinFile pointers representing the selection.
 *
 **/
GList *
nolphin_view_peek_selection (NolphinView *view)
{
    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

    return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->peek_selection (view);
}

// int
// nolphin_view_get_selection_count (NolphinView *view)
// {
//     g_return_val_if_fail (NOLPHIN_IS_VIEW (view), 0);

//     return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->get_selection_count (view);
// }

/**
 * nolphin_view_update_menus:
 *
 * Update the sensitivity and wording of dynamic menu items.
 * @view: NolphinView in question.
 */
void
nolphin_view_update_menus (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	if (!view->details->active) {
		return;
	}

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->update_menus (view);

	view->details->menu_states_untrustworthy = FALSE;
}

typedef struct {
	GAppInfo *application;
	GList *files;
	NolphinView *directory_view;
} ApplicationLaunchParameters;

typedef struct {
	NolphinFile *file;
	NolphinView *directory_view;
} ScriptLaunchParameters;

typedef struct {
	NolphinFile *file;
	NolphinView *directory_view;
} CreateTemplateParameters;

typedef struct {
	NolphinView *view;
	char *dest_uri;
} BookmarkCallbackData;


static BookmarkCallbackData *
bookmark_callback_data_new (NolphinView *view,
							   gchar *uri)
{
    BookmarkCallbackData *result;

    result = g_new0 (BookmarkCallbackData, 1);
    result->view = view;
    result->dest_uri = g_strdup(uri);
    return result;
}

static void
bookmark_callback_data_free (BookmarkCallbackData *data)
{
    g_free ((char *)data->dest_uri);
    g_free (data);
}

static ApplicationLaunchParameters *
application_launch_parameters_new (GAppInfo *application,
			      	   GList *files,
			           NolphinView *directory_view)
{
	ApplicationLaunchParameters *result;

	result = g_new0 (ApplicationLaunchParameters, 1);
	result->application = g_object_ref (application);
	result->files = nolphin_file_list_copy (files);

	if (directory_view != NULL) {
		g_object_ref (directory_view);
		result->directory_view = directory_view;
	}

	return result;
}

static void
application_launch_parameters_free (ApplicationLaunchParameters *parameters)
{
	g_object_unref (parameters->application);
	nolphin_file_list_free (parameters->files);

	if (parameters->directory_view != NULL) {
		g_object_unref (parameters->directory_view);
	}

	g_free (parameters);
}

static GList *
file_and_directory_list_to_files (GList *fad_list)
{
	GList *res, *l;
	FileAndDirectory *fad;

	res = NULL;
	for (l = fad_list; l != NULL; l = l->next) {
		fad = l->data;
		res = g_list_prepend (res, nolphin_file_ref (fad->file));
	}
	return g_list_reverse (res);
}


static GList *
file_and_directory_list_from_files (NolphinDirectory *directory, GList *files)
{
	GList *res, *l;
	FileAndDirectory *fad;

	res = NULL;
	for (l = files; l != NULL; l = l->next) {
		fad = g_new0 (FileAndDirectory, 1);
		fad->directory = nolphin_directory_ref (directory);
		fad->file = nolphin_file_ref (l->data);
		res = g_list_prepend (res, fad);
	}
	return g_list_reverse (res);
}

static void
file_and_directory_free (FileAndDirectory *fad)
{
	nolphin_directory_unref (fad->directory);
	nolphin_file_unref (fad->file);
	g_free (fad);
}


static void
file_and_directory_list_free (GList *list)
{
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		file_and_directory_free (l->data);
	}

	g_list_free (list);
}

static gboolean
file_and_directory_equal (gconstpointer  v1,
			  gconstpointer  v2)
{
	const FileAndDirectory *fad1, *fad2;
	fad1 = v1;
	fad2 = v2;

	return (fad1->file == fad2->file &&
		fad1->directory == fad2->directory);
}

static guint
file_and_directory_hash  (gconstpointer  v)
{
	const FileAndDirectory *fad;

	fad = v;
	return GPOINTER_TO_UINT (fad->file) ^ GPOINTER_TO_UINT (fad->directory);
}




static ScriptLaunchParameters *
script_launch_parameters_new (NolphinFile *file,
			      NolphinView *directory_view)
{
	ScriptLaunchParameters *result;

	result = g_new0 (ScriptLaunchParameters, 1);
	g_object_ref (directory_view);
	result->directory_view = directory_view;
	nolphin_file_ref (file);
	result->file = file;

	return result;
}

static void
script_launch_parameters_free (ScriptLaunchParameters *parameters)
{
	g_object_unref (parameters->directory_view);
	nolphin_file_unref (parameters->file);
	g_free (parameters);
}

static CreateTemplateParameters *
create_template_parameters_new (NolphinFile *file,
				NolphinView *directory_view)
{
	CreateTemplateParameters *result;

	result = g_new0 (CreateTemplateParameters, 1);
	g_object_ref (directory_view);
	result->directory_view = directory_view;
	nolphin_file_ref (file);
	result->file = file;

	return result;
}

static void
create_templates_parameters_free (CreateTemplateParameters *parameters)
{
	g_object_unref (parameters->directory_view);
	nolphin_file_unref (parameters->file);
	g_free (parameters);
}

NolphinWindow *
nolphin_view_get_nolphin_window (NolphinView  *view)
{
	g_assert (view->details->window != NULL);

	return view->details->window;
}

NolphinWindowSlot *
nolphin_view_get_nolphin_window_slot (NolphinView  *view)
{
	g_assert (view->details->slot != NULL);

	return view->details->slot;
}

/* Returns the GtkWindow that this directory view occupies, or NULL
 * if at the moment this directory view is not in a GtkWindow or the
 * GtkWindow cannot be determined. Primarily used for parenting dialogs.
 */
static GtkWindow *
nolphin_view_get_containing_window (NolphinView *view)
{
	GtkWidget *window;

	g_assert (NOLPHIN_IS_VIEW (view));

	window = gtk_widget_get_ancestor (GTK_WIDGET (view), GTK_TYPE_WINDOW);
	if (window == NULL) {
		return NULL;
	}

	return GTK_WINDOW (window);
}

static gboolean
nolphin_view_confirm_multiple (GtkWindow *parent_window,
				int count,
				gboolean tabs)
{
	GtkDialog *dialog;
	char *prompt;
	char *detail;
	int response;

	if (count <= SILENT_WINDOW_OPEN_LIMIT) {
		return TRUE;
	}

	prompt = _("Sind Sie sicher, dass Sie alle Dateien öffnen wollen?");
	if (tabs) {
		detail = g_strdup_printf (ngettext("Dies würde %'d Reiter öffnen.",
						   "Dies würde %'d Reiter öffnen.", count), count);
	} else {
		detail = g_strdup_printf (ngettext("Dies würde %'d Einzelfenster öffnen.",
						   "Dies würde %'d Einzelfenster öffnen.", count), count);
	}
	dialog = eel_show_yes_no_dialog (prompt, detail,
					 GTK_STOCK_OK, GTK_STOCK_CANCEL,
					 parent_window);
	g_free (detail);

	response = gtk_dialog_run (dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));

	return response == GTK_RESPONSE_YES;
}

static gboolean
selection_contains_one_item_in_menu_callback (NolphinView *view, GList *selection)
{
	if (g_list_length (selection) == 1) {
		return TRUE;
	}

	/* If we've requested a menu update that hasn't yet occurred, then
	 * the mismatch here doesn't surprise us, and we won't complain.
	 * Otherwise, we will complain.
	 */
	if (!view->details->menu_states_untrustworthy) {
		g_warning ("Expected one selected item, found %'d. No action will be performed.",
			   g_list_length (selection));
	}

	return FALSE;
}

static gboolean
selection_not_empty_in_menu_callback (NolphinView *view, GList *selection)
{
	if (selection != NULL) {
		return TRUE;
	}

	/* If we've requested a menu update that hasn't yet occurred, then
	 * the mismatch here doesn't surprise us, and we won't complain.
	 * Otherwise, we will complain.
	 */
	if (!view->details->menu_states_untrustworthy) {
		g_warning ("Empty selection found when selection was expected. No action will be performed.");
	}

	return FALSE;
}

static char *
get_view_directory (NolphinView *view)
{
	char *uri, *path;
	GFile *f;

	uri = nolphin_directory_get_uri (view->details->model);
	if (eel_uri_is_desktop (uri)) {
		g_free (uri);
		uri = nolphin_get_desktop_directory_uri ();

	}
	f = g_file_new_for_uri (uri);
	path = g_file_get_path (f);
	g_object_unref (f);
	g_free (uri);

	return path;
}

static gboolean
get_is_desktop_view (NolphinView *view)
{
    gchar *uri;
    gboolean ret;

    uri = nolphin_directory_get_uri (view->details->model);
    ret = eel_uri_is_desktop (uri);
    g_free (uri);

    return ret;
}

void
nolphin_view_preview_files (NolphinView *view,
			     GList *files,
			     GArray *locations)
{
	/* Leertaste blendet die Vorschau der Auswahl im F11-
	 * Informationsbereich ein/aus - nicht mehr den externen D-Bus-
	 * Previewer. Der Panel-Inhalt selbst folgt der Auswahl bereits
	 * automatisch (nolphin_window_sync_preview_selection(), verdrahtet
	 * über das "selection-changed"-Signal), daher genügt hier ein
	 * reines Sichtbarkeits-Toggle. */
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));

	if (NOLPHIN_IS_WINDOW (toplevel)) {
		NolphinWindow *window = NOLPHIN_WINDOW (toplevel);

		nolphin_window_set_show_preview (window, !nolphin_window_preview_showing (window));
	}
}

void
nolphin_view_activate_files (NolphinView *view,
			      GList *files,
			      NolphinWindowOpenFlags flags,
			      gboolean confirm_multiple)
{
	char *path;

	path = get_view_directory (view);
	nolphin_mime_activate_files (nolphin_view_get_containing_window (view),
				      view->details->slot,
				      files,
				      path,
				      flags,
				      confirm_multiple);

	if (get_is_desktop_view(view)) {
		nolphin_view_set_selection(view, NULL);
	}

	g_free (path);
}

void
nolphin_view_activate_file (NolphinView *view,
			     NolphinFile *file,
			     NolphinWindowOpenFlags flags)
{
	char *path;

	path = get_view_directory (view);
	nolphin_mime_activate_file (nolphin_view_get_containing_window (view),
				     view->details->slot,
				     file,
				     path,
				     flags);

	if (get_is_desktop_view(view)) {
		nolphin_view_set_selection(view, NULL);
	}

	g_free (path);
}

static void
action_open_callback (GtkAction *action,
		      gpointer callback_data)
{
	GList *selection;
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	selection = nolphin_view_get_selection (view);
	nolphin_view_activate_files (view,
				      selection,
				      0,
				      TRUE);
	nolphin_file_list_free (selection);
}

static void
action_open_alternate_callback (GtkAction *action,
				gpointer callback_data)
{
	NolphinView *view;
	GList *selection;
	GtkWindow *window;

	view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);

	window = nolphin_view_get_containing_window (view);

	if (nolphin_view_confirm_multiple (window, g_list_length (selection), FALSE)) {
		g_list_foreach (selection, open_one_in_new_window, view);
	}

	nolphin_file_list_free (selection);
}

static void
action_open_new_tab_callback (GtkAction *action,
			      gpointer callback_data)
{
	NolphinView *view;
	GList *selection;
	GtkWindow *window;

	view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);

	window = nolphin_view_get_containing_window (view);

	if (nolphin_view_confirm_multiple (window, g_list_length (selection), TRUE)) {
		nolphin_view_activate_files (view,
					      selection,
					      NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB,
					      FALSE);
	}

	nolphin_file_list_free (selection);
}

static void
app_chooser_dialog_response_cb (GtkDialog *dialog,
				gint response_id,
				gpointer user_data)
{
	GtkWindow *parent_window;
	NolphinFile *file;
	GAppInfo *info;
	GList files;

	parent_window = user_data;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

    GtkWidget *content = gtk_dialog_get_content_area (dialog);
    GList *children = gtk_container_get_children (GTK_CONTAINER (content));

    NolphinMimeApplicationChooser *chooser = children->data;

    g_list_free (children);

	info = nolphin_mime_application_chooser_get_info (chooser);
	file = nolphin_file_get_by_uri (nolphin_mime_application_chooser_get_uri (chooser));

	g_signal_emit_by_name (nolphin_signaller_get_current (), "mime_data_changed");

	files.next = NULL;
	files.prev = NULL;
	files.data = file;
	nolphin_launch_application (info, &files, parent_window);

    nolphin_file_unref (file);

	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (info);
}

static void
choose_program (NolphinView *view,
		NolphinFile *file)
{
	GtkWidget *dialog;
    GtkWidget *ok_button;

    char *mime_type;
    char *uri = NULL;
    GList *uris = NULL;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (NOLPHIN_IS_FILE (file));

    mime_type = nolphin_file_get_mime_type (file);
    uri = nolphin_file_get_uri (file);

    dialog = gtk_dialog_new_with_buttons (_("Öffnen mit"),
                          nolphin_view_get_containing_window (view),
                          GTK_DIALOG_DESTROY_WITH_PARENT,
                          GTK_STOCK_CANCEL,
                          GTK_RESPONSE_CANCEL,
                          NULL);
    ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                       GTK_STOCK_OK,
                                       GTK_RESPONSE_OK);

    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

    GtkWidget *chooser = nolphin_mime_application_chooser_new (uri, uris, mime_type, ok_button);

    g_free (mime_type);
    g_free (uri);

    GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    gtk_box_pack_start (GTK_BOX (content), chooser, TRUE, TRUE, 0);

    gtk_widget_show_all (dialog);

	g_signal_connect_object (dialog, "response",
				 G_CALLBACK (app_chooser_dialog_response_cb),
				 nolphin_view_get_containing_window (view), 0);
}

static void
open_with_other_program (NolphinView *view)
{
        GList *selection;

	g_assert (NOLPHIN_IS_VIEW (view));

       	selection = nolphin_view_get_selection (view);

	if (selection_contains_one_item_in_menu_callback (view, selection)) {
		choose_program (view, NOLPHIN_FILE (selection->data));
	}

	nolphin_file_list_free (selection);
}

static void
action_other_application_callback (GtkAction *action,
				   gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	open_with_other_program (NOLPHIN_VIEW (callback_data));
}

static void
trash_or_delete_selected_files (NolphinView *view)
{
        GList *selection;

	/* This might be rapidly called multiple times for the same selection
	 * when using keybindings. So we remember if the current selection
	 * was already removed (but the view doesn't know about it yet).
	 */
	if (!view->details->selection_was_removed) {
		selection = nolphin_view_get_selection_for_file_transfer (view);

        if (selection == NULL) {
            return;
        }

		trash_or_delete_files (nolphin_view_get_containing_window (view),
				       selection, TRUE,
				       view);
		nolphin_file_list_free (selection);
		view->details->selection_was_removed = TRUE;
	}
}

static gboolean
real_trash (NolphinView *view)
{
	GtkAction *action;

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_TRASH);
	if ((gtk_action_get_sensitive (action) && gtk_action_get_visible (action)) ||
        showing_recent_directory (view)) {
		trash_or_delete_selected_files (view);
		return TRUE;
	}
	return FALSE;
}

static void
action_trash_callback (GtkAction *action,
		       gpointer callback_data)
{
        trash_or_delete_selected_files (NOLPHIN_VIEW (callback_data));
}

static void
delete_selected_files (NolphinView *view)
{
        GList *selection;
	GList *node;
	GList *locations;

	selection = nolphin_view_get_selection_for_file_transfer (view);
	if (selection == NULL) {
		return;
	}

	locations = NULL;
	for (node = selection; node != NULL; node = node->next) {
		locations = g_list_prepend (locations,
					    nolphin_file_get_location ((NolphinFile *) node->data));
	}
	locations = g_list_reverse (locations);

	nolphin_file_operations_delete (locations, nolphin_view_get_containing_window (view), NULL, NULL);

	g_list_free_full (locations, g_object_unref);
        nolphin_file_list_free (selection);
}

static void
action_delete_callback (GtkAction *action,
			gpointer callback_data)
{
        delete_selected_files (NOLPHIN_VIEW (callback_data));
}

static void
action_restore_from_trash_callback (GtkAction *action,
				    gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);

	selection = nolphin_view_get_selection_for_file_transfer (view);
	nolphin_restore_files_from_trash (selection,
					   nolphin_view_get_containing_window (view));

	nolphin_file_list_free (selection);

}

static gboolean
real_delete (NolphinView *view)
{
	GtkAction *action;

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_DELETE);
	if (gtk_action_get_sensitive (action) &&
	    gtk_action_get_visible (action)) {
		delete_selected_files (view);
		return TRUE;
	}
	return FALSE;
}

static void
action_duplicate_callback (GtkAction *action,
			   gpointer callback_data)
{
        NolphinView *view;
        GList *selection;
        GArray *selected_item_locations;

        view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection_for_file_transfer (view);
	if (selection_not_empty_in_menu_callback (view, selection)) {
		/* FIXME bugzilla.gnome.org 45061:
		 * should change things here so that we use a get_icon_locations (view, selection).
		 * Not a problem in this case but in other places the selection may change by
		 * the time we go and retrieve the icon positions, relying on the selection
		 * staying intact to ensure the right sequence and count of positions is fragile.
		 */
		selected_item_locations = nolphin_view_get_selected_icon_locations (view);
	        nolphin_view_duplicate_selection (view, selection, selected_item_locations);
	        g_array_free (selected_item_locations, TRUE);
	}

        nolphin_file_list_free (selection);
}

static void
action_create_link_callback (GtkAction *action,
			     gpointer callback_data)
{
        NolphinView *view;
        GList *selection;
        GArray *selected_item_locations;

        g_assert (NOLPHIN_IS_VIEW (callback_data));

        view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);
	if (selection_not_empty_in_menu_callback (view, selection)) {
		selected_item_locations = nolphin_view_get_selected_icon_locations (view);
	        nolphin_view_create_links_for_files (view, selection, selected_item_locations);
	        g_array_free (selected_item_locations, TRUE);
	}

        nolphin_file_list_free (selection);
}

static void
action_pin_unpin_file_callback (GtkAction *action,
                                gpointer   callback_data)
{
    NolphinView *view;
    GList *selection;
    gboolean to_pin;

    g_assert (NOLPHIN_IS_VIEW (callback_data));

    view = NOLPHIN_VIEW (callback_data);

    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) < 1) {
        g_warning ("No selection to pin - why?");
        return;
    }

    /* Apply pinning according to the current state of the first
     * selected file. */
    to_pin = !nolphin_file_get_pinning (NOLPHIN_FILE (selection->data));

    if (selection_not_empty_in_menu_callback (view, selection)) {
        NolphinFile *file;
        GList *iter;

        for (iter = selection; iter != NULL; iter = iter->next) {
            file = NOLPHIN_FILE (iter->data);

            nolphin_file_set_pinning (file, to_pin);
        }
    }

    nolphin_file_list_free (selection);
}

static void
action_favorite_unfavorite_file_callback (GtkAction *action,
                                          gpointer   callback_data)
{
    NolphinView *view;
    GList *selection;
    gboolean to_favorite;

    g_assert (NOLPHIN_IS_VIEW (callback_data));

    view = NOLPHIN_VIEW (callback_data);

    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) < 1) {
        g_warning ("Favorites - no selection - why?");
        return;
    }

    /* Apply favorite according to the current state of the first
     * selected file. */
    to_favorite = !nolphin_file_get_is_favorite (NOLPHIN_FILE (selection->data));

    if (selection_not_empty_in_menu_callback (view, selection)) {
        NolphinFile *file;
        GList *iter;

        for (iter = selection; iter != NULL; iter = iter->next) {
            file = NOLPHIN_FILE (iter->data);

            nolphin_file_set_is_favorite (file, to_favorite);
        }
    }

    nolphin_file_list_free (selection);
}

static void
action_select_all_callback (GtkAction *action,
			    gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_select_all (callback_data);
}

static void
action_invert_selection_callback (GtkAction *action,
				  gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_invert_selection (callback_data);
}

static void
pattern_select_response_cb (GtkWidget *dialog, int response, gpointer user_data)
{
	NolphinView *view;
	NolphinDirectory *directory;
	GtkWidget *entry;
	GList *selection;
	GError *error;

	view = NOLPHIN_VIEW (user_data);

	switch (response) {
	case GTK_RESPONSE_OK :
		entry = g_object_get_data (G_OBJECT (dialog), "entry");
		directory = nolphin_view_get_model (view);
		selection = nolphin_directory_match_pattern (directory,
							      gtk_entry_get_text (GTK_ENTRY (entry)));

		if (selection) {
			nolphin_view_call_set_selection (view, selection);
			nolphin_file_list_free (selection);

			nolphin_view_reveal_selection(view);
		}
		/* fall through */
	case GTK_RESPONSE_NONE :
	case GTK_RESPONSE_DELETE_EVENT :
	case GTK_RESPONSE_CANCEL :
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_HELP :
		error = NULL;
		gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (dialog)),
			      "help:gnome-help/files-select",
			      gtk_get_current_event_time (), &error);
		if (error) {
			eel_show_error_dialog (_("Beim Anzeigen der Hilfe ist ein Fehler aufgetreten."), error->message,
					       GTK_WINDOW (dialog));
			g_error_free (error);
		}
		break;
	default :
		g_assert_not_reached ();
	}
}

static void
select_pattern (NolphinView *view)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *example;
	GtkWidget *grid;
	GtkWidget *entry;
	char *example_pattern;

	dialog = gtk_dialog_new_with_buttons (_("Passende Objekte auswählen"),
					      nolphin_view_get_containing_window (view),
					      GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_HELP,
					      GTK_RESPONSE_HELP,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 2);

	label = gtk_label_new_with_mnemonic (_("_Muster:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);

	example = gtk_label_new (NULL);
	gtk_widget_set_halign (example, GTK_ALIGN_START);
	example_pattern = g_strdup_printf ("<b>%s</b><i>%s</i> ",
					   _("Beispiele: "),
					   "*.png, file\?\?.txt, pict*.\?\?\?");
	gtk_label_set_markup (GTK_LABEL (example), example_pattern);
	g_free (example_pattern);

	entry = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_hexpand (entry, TRUE);

	grid = gtk_grid_new ();
	g_object_set (grid,
		      "orientation", GTK_ORIENTATION_VERTICAL,
		      "border-width", 6,
		      "row-spacing", 6,
		      "column-spacing", 12,
		      NULL);

	gtk_container_add (GTK_CONTAINER (grid), label);
	gtk_grid_attach_next_to (GTK_GRID (grid), entry, label,
				 GTK_POS_RIGHT, 1, 1);
	gtk_grid_attach_next_to (GTK_GRID (grid), example, entry,
				 GTK_POS_BOTTOM, 1, 1);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
	gtk_widget_show_all (grid);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
	g_object_set_data (G_OBJECT (dialog), "entry", entry);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (pattern_select_response_cb),
			  view);
	gtk_widget_show_all (dialog);
}

static void
action_select_pattern_callback (GtkAction *action,
				gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	select_pattern(callback_data);
}

/* "Auswahl nach Dateityp" */

static const NolphinFileTypeCategory select_type_categories[] = {
	NOLPHIN_FILE_TYPE_CATEGORY_FOLDER,
	NOLPHIN_FILE_TYPE_CATEGORY_IMAGE,
	NOLPHIN_FILE_TYPE_CATEGORY_VIDEO,
	NOLPHIN_FILE_TYPE_CATEGORY_AUDIO,
	NOLPHIN_FILE_TYPE_CATEGORY_TEXT,
	NOLPHIN_FILE_TYPE_CATEGORY_ARCHIVE,
	NOLPHIN_FILE_TYPE_CATEGORY_OTHER
};

static void
type_select_response_cb (GtkWidget *dialog, int response, gpointer user_data)
{
	NolphinView *view;
	NolphinDirectory *directory;
	GtkWidget *combo;
	GList *selection;
	int active;

	view = NOLPHIN_VIEW (user_data);

	if (response == GTK_RESPONSE_OK) {
		combo = g_object_get_data (G_OBJECT (dialog), "combo");
		active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));

		if (active >= 0 && active < (int) G_N_ELEMENTS (select_type_categories)) {
			directory = nolphin_view_get_model (view);
			selection = nolphin_directory_match_type_category (directory,
									     select_type_categories[active]);

			if (selection) {
				nolphin_view_call_set_selection (view, selection);
				nolphin_file_list_free (selection);

				nolphin_view_reveal_selection (view);
			}
		}
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
select_type (NolphinView *view)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *combo;
	GtkWidget *grid;
	guint i;

	dialog = gtk_dialog_new_with_buttons (_("Nach Dateityp auswählen"),
					      nolphin_view_get_containing_window (view),
					      GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

	label = gtk_label_new_with_mnemonic (_("_Typ:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new ();
	for (i = 0; i < G_N_ELEMENTS (select_type_categories); i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo),
						nolphin_file_type_category_get_label (select_type_categories[i]));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	gtk_widget_set_hexpand (combo, TRUE);

	grid = gtk_grid_new ();
	g_object_set (grid,
		      "orientation", GTK_ORIENTATION_VERTICAL,
		      "border-width", 6,
		      "row-spacing", 6,
		      "column-spacing", 12,
		      NULL);

	gtk_container_add (GTK_CONTAINER (grid), label);
	gtk_grid_attach_next_to (GTK_GRID (grid), combo, label,
				 GTK_POS_RIGHT, 1, 1);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
	gtk_widget_show_all (grid);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
	g_object_set_data (G_OBJECT (dialog), "combo", combo);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (type_select_response_cb),
			  view);
	gtk_widget_show_all (dialog);
}

static void
action_select_type_callback (GtkAction *action,
			     gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	select_type (callback_data);
}

/* Auswahl speichern/wiederherstellen - merkt sich die aktuelle
 * Auswahl unter einem Namen, gebunden an den aktuellen Ordner, und
 * kann sie später dort wiederherstellen. */

static void
save_selection_response_cb (GtkWidget *dialog, int response, gpointer user_data)
{
	NolphinView *view = NOLPHIN_VIEW (user_data);

	if (response == GTK_RESPONSE_OK) {
		GtkWidget *entry = g_object_get_data (G_OBJECT (dialog), "entry");
		const gchar *name = gtk_entry_get_text (GTK_ENTRY (entry));

		if (name[0] != '\0') {
			GList *selection = nolphin_view_get_selection (view);

			if (selection == NULL) {
				GtkWidget *info = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
									   GTK_DIALOG_DESTROY_WITH_PARENT,
									   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
									   "%s", _("Keine Objekte ausgewählt."));
				gtk_dialog_run (GTK_DIALOG (info));
				gtk_widget_destroy (info);
			} else {
				GList *l, *basenames = NULL;
				gchar *folder_uri;
				GError *error = NULL;

				for (l = selection; l != NULL; l = l->next) {
					basenames = g_list_prepend (basenames, nolphin_file_get_name (NOLPHIN_FILE (l->data)));
				}

				folder_uri = nolphin_directory_get_uri (nolphin_view_get_model (view));

				if (!nolphin_saved_selections_save (folder_uri, name, basenames, &error)) {
					GtkWidget *err_dialog = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
											 GTK_DIALOG_DESTROY_WITH_PARENT,
											 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
											 "%s", error->message);
					gtk_dialog_run (GTK_DIALOG (err_dialog));
					gtk_widget_destroy (err_dialog);
					g_clear_error (&error);
				} else {
					send_archive_notification (_("Auswahl speichern"), TRUE, NULL);
				}

				g_free (folder_uri);
				g_list_free_full (basenames, g_free);
				nolphin_file_list_free (selection);
			}
		}
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
action_save_selection_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GtkWidget *dialog, *grid, *label, *entry;

	dialog = gtk_dialog_new_with_buttons (_("Auswahl speichern"),
					      nolphin_view_get_containing_window (view),
					      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK, GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	grid = gtk_grid_new ();
	g_object_set (grid, "border-width", 12, "row-spacing", 8, "column-spacing", 12, NULL);
	label = gtk_label_new (_("Name:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
	entry = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_widget_set_size_request (entry, 320, -1);
	gtk_grid_attach (GTK_GRID (grid), entry, 1, 0, 1, 1);
	gtk_widget_show_all (grid);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
	g_object_set_data (G_OBJECT (dialog), "entry", entry);

	g_signal_connect (dialog, "response", G_CALLBACK (save_selection_response_cb), view);
	gtk_widget_show_all (dialog);
}

static void
restore_selection_response_cb (GtkWidget *dialog, int response, gpointer user_data)
{
	NolphinView *view = NOLPHIN_VIEW (user_data);

	if (response == GTK_RESPONSE_OK) {
		GtkWidget *combo = g_object_get_data (G_OBJECT (dialog), "combo");
		gchar *chosen_name = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));

		if (chosen_name != NULL) {
			gchar *folder_uri = nolphin_directory_get_uri (nolphin_view_get_model (view));
			GList *basenames = nolphin_saved_selections_restore (folder_uri, chosen_name);

			if (basenames == NULL) {
				GtkWidget *info = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
									   GTK_DIALOG_DESTROY_WITH_PARENT,
									   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
									   "%s", _("Diese gespeicherte Auswahl konnte nicht gelesen werden."));
				gtk_dialog_run (GTK_DIALOG (info));
				gtk_widget_destroy (info);
			} else {
				NolphinDirectory *directory = nolphin_view_get_model (view);
				GList *all_files = nolphin_directory_get_file_list (directory);
				GList *l, *to_select = NULL;

				for (l = all_files; l != NULL; l = l->next) {
					NolphinFile *file = NOLPHIN_FILE (l->data);
					gchar *file_name = nolphin_file_get_name (file);
					gboolean wanted = (g_list_find_custom (basenames, file_name, (GCompareFunc) g_strcmp0) != NULL);
					g_free (file_name);

					if (wanted) {
						to_select = g_list_prepend (to_select, nolphin_file_ref (file));
					}
				}

				if (to_select != NULL) {
					nolphin_view_call_set_selection (view, to_select);
					nolphin_view_reveal_selection (view);
				}

				nolphin_file_list_free (to_select);
				nolphin_file_list_free (all_files);
				g_list_free_full (basenames, g_free);
			}

			g_free (folder_uri);
			g_free (chosen_name);
		}
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
action_restore_selection_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	gchar *folder_uri;
	GList *names, *l;
	GtkWidget *dialog;

	folder_uri = nolphin_directory_get_uri (nolphin_view_get_model (view));
	names = nolphin_saved_selections_list_names (folder_uri);
	g_free (folder_uri);

	if (names == NULL) {
		dialog = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						 "%s", _("Für diesen Ordner ist keine Auswahl gespeichert."));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		return;
	}

	{
		GtkWidget *grid, *label, *combo;

		dialog = gtk_dialog_new_with_buttons (_("Gespeicherte Auswahl wiederherstellen"),
						      nolphin_view_get_containing_window (view),
						      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						      GTK_STOCK_OK, GTK_RESPONSE_OK,
						      NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

		grid = gtk_grid_new ();
		g_object_set (grid, "border-width", 12, "row-spacing", 8, "column-spacing", 12, NULL);
		label = gtk_label_new (_("Gespeicherte Auswahl:"));
		gtk_widget_set_halign (label, GTK_ALIGN_START);
		gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

		combo = gtk_combo_box_text_new ();
		for (l = names; l != NULL; l = l->next) {
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), (const gchar *) l->data);
		}
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
		gtk_widget_set_hexpand (combo, TRUE);
		gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);

		gtk_widget_show_all (grid);
		gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
		g_object_set_data (G_OBJECT (dialog), "combo", combo);
	}

	g_list_free_full (names, g_free);

	g_signal_connect (dialog, "response", G_CALLBACK (restore_selection_response_cb), view);
	gtk_widget_show_all (dialog);
}

static void
action_reset_to_defaults_callback (GtkAction *action,
				   gpointer callback_data)
{
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_reset_to_defaults (callback_data);
}


static void
hidden_files_mode_changed (NolphinWindow *window,
			   gpointer callback_data)
{
	NolphinView *directory_view;

	directory_view = NOLPHIN_VIEW (callback_data);

	nolphin_view_init_show_hidden_files (directory_view);
}

static void
action_empty_trash_callback (GtkAction *action,
			     gpointer callback_data)
{
        g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_file_operations_empty_trash (GTK_WIDGET (callback_data));
}

typedef struct {
	NolphinView *view;
	NolphinFile *new_file;
} RenameData;

static gboolean
delayed_rename_file_hack_callback (RenameData *data)
{
	NolphinView *view;
	NolphinFile *new_file;

	view = data->view;
	new_file = data->new_file;

	if (view->details->window != NULL &&
	    view->details->active) {
		NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->start_renaming_file (view, new_file, FALSE);
		nolphin_view_reveal_selection (view);
	}

    view->details->delayed_rename_file_id = 0;
	return FALSE;
}

static void
delayed_rename_file_hack_removed (RenameData *data)
{
	g_object_unref (data->view);
	nolphin_file_unref (data->new_file);
	g_free (data);
}


static void
rename_file (NolphinView *view, NolphinFile *new_file)
{
	RenameData *data;

	/* HACK!!!!
	   This is a work around bug in listview. After the rename is
	   enabled we will get file changes due to info about the new
	   file being read, which will cause the model to change. When
	   the model changes GtkTreeView clears the editing. This hack just
	   delays editing for some time to try to avoid this problem.
	   A major problem is that the selection of the row causes us
	   to load the slow mimetype for the file, which leads to a
	   file_changed. So, before we delay we select the row.
	*/
	if (NOLPHIN_IS_LIST_VIEW (view)) {
		nolphin_view_select_file (view, new_file);

		data = g_new (RenameData, 1);
		data->view = g_object_ref (view);
		data->new_file = nolphin_file_ref (new_file);
		if (view->details->delayed_rename_file_id != 0) {
			g_source_remove (view->details->delayed_rename_file_id);
		}
		view->details->delayed_rename_file_id =
			g_timeout_add_full (G_PRIORITY_DEFAULT,
					    100, (GSourceFunc)delayed_rename_file_hack_callback,
					    data, (GDestroyNotify) delayed_rename_file_hack_removed);

		return;
	}

	/* no need to select because start_renaming_file selects
	 * nolphin_view_select_file (view, new_file);
	 */
	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->start_renaming_file (view, new_file, FALSE);
	nolphin_view_reveal_selection (view);
}

static void
reveal_newly_added_folder (NolphinView *view, NolphinFile *new_file,
			   NolphinDirectory *directory, GFile *target_location)
{
	GFile *location;

	location = nolphin_file_get_location (new_file);
	if (g_file_equal (location, target_location)) {
		g_signal_handlers_disconnect_by_func (view,
						      G_CALLBACK (reveal_newly_added_folder),
						      (void *) target_location);
		rename_file (view, new_file);
	}
	g_object_unref (location);
}

typedef struct {
	NolphinView *directory_view;
	GHashTable *added_locations;
} NewFolderData;


static void
track_newly_added_locations (NolphinView *view, NolphinFile *new_file,
			     NolphinDirectory *directory, gpointer user_data)
{
	NewFolderData *data;

	data = user_data;

	g_hash_table_insert (data->added_locations, nolphin_file_get_location (new_file), NULL);
}

static void
new_folder_done (GFile *new_folder,
		 gboolean success,
		 gpointer user_data)
{
	NolphinView *directory_view;
	NolphinFile *file;
	char screen_string[32];
	GdkScreen *screen;
	NewFolderData *data;

	data = (NewFolderData *)user_data;

	directory_view = data->directory_view;

	if (directory_view == NULL) {
		goto fail;
	}

	g_signal_handlers_disconnect_by_func (directory_view,
					      G_CALLBACK (track_newly_added_locations),
					      (void *) data);

	if (new_folder == NULL) {
		goto fail;
	}

	screen = gtk_widget_get_screen (GTK_WIDGET (directory_view));
	g_snprintf (screen_string, sizeof (screen_string), "%d", gdk_screen_get_number (screen));


	file = nolphin_file_get (new_folder);

	if (g_hash_table_lookup_extended (data->added_locations, new_folder, NULL, NULL)) {
		/* The file was already added */
		rename_file (directory_view, file);
	} else {
		/* We need to run after the default handler adds the folder we want to
		 * operate on. The ADD_FILE signal is registered as G_SIGNAL_RUN_LAST, so we
		 * must use connect_after.
		 */
		g_signal_connect_data (directory_view,
				       "add_file",
				       G_CALLBACK (reveal_newly_added_folder),
				       g_object_ref (new_folder),
				       (GClosureNotify)g_object_unref,
				       G_CONNECT_AFTER);
	}
	nolphin_file_unref (file);

 fail:
	g_hash_table_destroy (data->added_locations);

	if (data->directory_view != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (data->directory_view),
					      (gpointer *) &data->directory_view);
	}

	g_free (data);
}


static NewFolderData *
new_folder_data_new (NolphinView *directory_view)
{
	NewFolderData *data;

	data = g_new (NewFolderData, 1);
	data->directory_view = directory_view;
	data->added_locations = g_hash_table_new_full (g_file_hash, (GEqualFunc)g_file_equal,
						       g_object_unref, NULL);
	g_object_add_weak_pointer (G_OBJECT (data->directory_view),
				   (gpointer *) &data->directory_view);

	return data;
}

static GdkPoint *
context_menu_to_file_operation_position (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	if (nolphin_view_using_manual_layout (view)
	    && view->details->context_menu_position.x >= 0
	    && view->details->context_menu_position.y >= 0) {
		NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->widget_to_file_operation_position
			(view, &view->details->context_menu_position);
		return &view->details->context_menu_position;
	} else {
		return NULL;
	}
}

void
nolphin_view_new_folder (NolphinView *view)
{
	char *parent_uri;
	NewFolderData *data;
	GdkPoint *pos;

	data = new_folder_data_new (view);

	g_signal_connect_data (view,
			       "add_file",
			       G_CALLBACK (track_newly_added_locations),
			       data,
			       (GClosureNotify)NULL,
			       G_CONNECT_AFTER);

	pos = context_menu_to_file_operation_position (view);

	parent_uri = nolphin_view_get_backing_uri (view);
	nolphin_file_operations_new_folder (GTK_WIDGET (view),
					     pos, parent_uri,
					     new_folder_done, data);

	g_free (parent_uri);
}

static NewFolderData *
setup_new_folder_data (NolphinView *directory_view)
{
	NewFolderData *data;

	data = new_folder_data_new (directory_view);

	g_signal_connect_data (directory_view,
			       "add_file",
			       G_CALLBACK (track_newly_added_locations),
			       data,
			       (GClosureNotify)NULL,
			       G_CONNECT_AFTER);

	return data;
}

void
nolphin_view_new_file_with_initial_contents (NolphinView *view,
					      const char *parent_uri,
					      const char *filename,
					      const char *initial_contents,
					      int length,
					      GdkPoint *pos)
{
	NewFolderData *data;

	g_assert (parent_uri != NULL);

	data = setup_new_folder_data (view);

	if (pos == NULL) {
		pos = context_menu_to_file_operation_position (view);
	}

	nolphin_file_operations_new_file (GTK_WIDGET (view),
					   pos, parent_uri, filename,
					   initial_contents, length,
					   new_folder_done, data);
}

static void
nolphin_view_new_file (NolphinView *directory_view,
			const char *parent_uri,
			NolphinFile *source)
{
	GdkPoint *pos;
	NewFolderData *data;
	char *source_uri;
	char *container_uri;

	container_uri = NULL;
	if (parent_uri == NULL) {
		container_uri = nolphin_view_get_backing_uri (directory_view);
		g_assert (container_uri != NULL);
	}

	if (source == NULL) {
		nolphin_view_new_file_with_initial_contents (directory_view,
							      parent_uri != NULL ? parent_uri : container_uri,
							      NULL,
							      "\n",
							      1,
							      NULL);
		g_free (container_uri);
		return;
	}

	g_return_if_fail (nolphin_file_is_local (source));

	pos = context_menu_to_file_operation_position (directory_view);

	data = setup_new_folder_data (directory_view);

	source_uri = nolphin_file_get_uri (source);

	nolphin_file_operations_new_file_from_template (GTK_WIDGET (directory_view),
							 pos,
							 parent_uri != NULL ? parent_uri : container_uri,
							 NULL,
							 source_uri,
							 new_folder_done, data);

	g_free (source_uri);
	g_free (container_uri);
}

static void
action_new_folder_callback (GtkAction *action,
			    gpointer callback_data)
{
        g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_new_folder (NOLPHIN_VIEW (callback_data));
}

static void
action_new_empty_file_callback (GtkAction *action,
				gpointer callback_data)
{
        g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_new_file (NOLPHIN_VIEW (callback_data), NULL, NULL);
}

static void
action_properties_callback (GtkAction *action,
			    gpointer callback_data)
{
        NolphinView *view;
        GList *selection;
	GList *files;
	NolphinWindow *window;
	GtkWidget *workspace_panel;

        g_assert (NOLPHIN_IS_VIEW (callback_data));

        view = NOLPHIN_VIEW (callback_data);
	window = NOLPHIN_WINDOW (nolphin_view_get_containing_window (view));
	workspace_panel = nolphin_window_get_workspace_panel (window);

	selection = nolphin_view_get_selection (view);
	if (g_list_length (selection) == 0) {
		if (view->details->directory_as_file != NULL) {
            if (NOLPHIN_IS_SEARCH_DIRECTORY (view->details->model)) {
                files = nolphin_directory_get_file_list (view->details->model);
            } else {
                files = g_list_append (NULL, nolphin_file_ref (view->details->directory_as_file));
            }

			nolphin_workspace_panel_show_properties (workspace_panel, window, files);

			nolphin_file_list_free (files);
		}
	} else {
		nolphin_workspace_panel_show_properties (workspace_panel, window, selection);
	}
        nolphin_file_list_free (selection);
}

static void
action_location_properties_callback (GtkAction *action,
				     gpointer   callback_data)
{
	NolphinView *view;
	GList           *files;

	g_assert (NOLPHIN_IS_VIEW (callback_data));

	view = NOLPHIN_VIEW (callback_data);
	g_assert (NOLPHIN_IS_FILE (view->details->location_popup_directory_as_file));

    if (NOLPHIN_IS_SEARCH_DIRECTORY (view->details->model)) {
        files = nolphin_directory_get_file_list (view->details->model);
    } else {
        files = g_list_append (NULL, nolphin_file_ref (view->details->location_popup_directory_as_file));
    }

	nolphin_workspace_panel_show_properties_anywhere (files, GTK_WIDGET (view));

	nolphin_file_list_free (files);
}

static gboolean
all_files_in_trash (GList *files)
{
	GList *node;

	/* Result is ambiguous if called on NULL, so disallow. */
	g_return_val_if_fail (files != NULL, FALSE);

	for (node = files; node != NULL; node = node->next) {
		if (!nolphin_file_is_in_trash (NOLPHIN_FILE (node->data))) {
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
all_selected_items_in_trash (NolphinView *view, GList *selection)
{
	gboolean result;

	/* If the contents share a parent directory, we need only
	 * check that parent directory. Otherwise we have to inspect
	 * each selected item.
	 */

	result = (selection == NULL) ? FALSE : all_files_in_trash (selection);

	return result;
}

static void
click_policy_changed_callback (gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->click_policy_changed (view);
}

static void
click_to_rename_changed_callback (gpointer callback_data)
{
    NolphinView *view;

    view = NOLPHIN_VIEW (callback_data);

    NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->click_to_rename_mode_changed (view);
}

static void
nolphin_to_menu_preferences_changed_callback (NolphinView *view)
{
    view->details->showing_bookmarks_in_to_menus = g_settings_get_boolean (nolphin_preferences,
                                                                           NOLPHIN_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS);
    view->details->showing_places_in_to_menus = g_settings_get_boolean (nolphin_preferences,
                                                                        NOLPHIN_PREFERENCES_SHOW_PLACES_IN_TO_MENUS);
}

gboolean
nolphin_view_should_sort_directories_first (NolphinView *view)
{
	return view->details->sort_directories_first;
}

gboolean
nolphin_view_should_sort_favorites_first (NolphinView *view)
{
	return view->details->sort_favorites_first;
}

static void
sort_directories_first_changed_callback (gpointer callback_data)
{
	NolphinView *view;
	gboolean preference_value;

	view = NOLPHIN_VIEW (callback_data);

	preference_value =
		g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SORT_DIRECTORIES_FIRST);

	if (preference_value != view->details->sort_directories_first) {
		view->details->sort_directories_first = preference_value;
		return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->sort_directories_first_changed (view);
	}
}

static void
sort_favorites_first_changed_callback (gpointer callback_data)
{
	NolphinView *view;
	gboolean preference_value;

	view = NOLPHIN_VIEW (callback_data);

	preference_value =
		g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SORT_FAVORITES_FIRST);

	if (preference_value != view->details->sort_favorites_first) {
		view->details->sort_favorites_first = preference_value;
		return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->sort_favorites_first_changed (view);
	}
}

static void
swap_delete_keybinding_changed_callback (gpointer callback_data)
{
    GtkBindingSet *binding_set = gtk_binding_set_find ("NolphinView");

    gboolean swap_keys = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SWAP_TRASH_DELETE);

    gtk_binding_entry_remove (binding_set, GDK_KEY_Delete, 0);
    gtk_binding_entry_remove (binding_set, GDK_KEY_KP_Delete, 0);
    gtk_binding_entry_remove (binding_set, GDK_KEY_KP_Delete, GDK_SHIFT_MASK);
    gtk_binding_entry_remove (binding_set, GDK_KEY_Delete, GDK_SHIFT_MASK);

    if (swap_keys) {
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, 0,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, 0,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, GDK_SHIFT_MASK,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, GDK_SHIFT_MASK,
                          "trash", 0);
    } else {
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, 0,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, 0,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, GDK_SHIFT_MASK,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, GDK_SHIFT_MASK,
                          "delete", 0);
    }
}

static gboolean
set_up_scripts_directory_global (void)
{
	char *scripts_directory_path;

	if (scripts_directory_uri != NULL) {
		return TRUE;
	}

	scripts_directory_path = nolphin_get_scripts_directory_path ();

	if (g_mkdir_with_parents (scripts_directory_path, 0755) == 0) {
		scripts_directory_uri = g_filename_to_uri (scripts_directory_path, NULL, NULL);
		scripts_directory_uri_length = strlen (scripts_directory_uri);
	}

	g_free (scripts_directory_path);

	return (scripts_directory_uri != NULL) ? TRUE : FALSE;
}

static void
scripts_added_or_changed_callback (NolphinDirectory *directory,
				   GList *files,
				   gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	view->details->scripts_invalid = TRUE;
	if (view->details->active) {
		schedule_update_menus (view);
	}
}

static void
templates_added_or_changed_callback (NolphinDirectory *directory,
				     GList *files,
				     gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	view->details->templates_invalid = TRUE;
	if (view->details->active) {
		schedule_update_menus (view);
	}
}

static void
actions_added_or_changed_callback (NolphinView *view)
{
    view->details->actions_invalid = TRUE;
    if (view->details->active) {
        schedule_update_menus (view);
    }
}

static void
add_directory_to_directory_list (NolphinView *view,
				 NolphinDirectory *directory,
				 GList **directory_list,
				 GCallback changed_callback)
{
	NolphinFileAttributes attributes;

	if (g_list_find (*directory_list, directory) == NULL) {
		nolphin_directory_ref (directory);

		attributes =
			NOLPHIN_FILE_ATTRIBUTES_FOR_ICON |
			NOLPHIN_FILE_ATTRIBUTE_INFO |
			NOLPHIN_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT;

		nolphin_directory_file_monitor_add (directory, directory_list,
						     FALSE, attributes,
						     (NolphinDirectoryCallback)changed_callback, view);

		g_signal_connect_object (directory, "files_added",
					 G_CALLBACK (changed_callback), view, 0);
		g_signal_connect_object (directory, "files_changed",
					 G_CALLBACK (changed_callback), view, 0);

		*directory_list = g_list_append	(*directory_list, directory);
	}
}

static void
remove_directory_from_directory_list (NolphinView *view,
				      NolphinDirectory *directory,
				      GList **directory_list,
				      GCallback changed_callback)
{
	*directory_list = g_list_remove	(*directory_list, directory);

	g_signal_handlers_disconnect_by_func (directory,
					      G_CALLBACK (changed_callback),
					      view);

	nolphin_directory_file_monitor_remove (directory, directory_list);

	nolphin_directory_unref (directory);
}

static void
add_directory_to_scripts_directory_list (NolphinView *view,
					 NolphinDirectory *directory)
{
	add_directory_to_directory_list (view, directory,
					 &view->details->scripts_directory_list,
					 G_CALLBACK (scripts_added_or_changed_callback));
}

static void
remove_directory_from_scripts_directory_list (NolphinView *view,
					      NolphinDirectory *directory)
{
	remove_directory_from_directory_list (view, directory,
					      &view->details->scripts_directory_list,
					      G_CALLBACK (scripts_added_or_changed_callback));
}

static void
add_directory_to_templates_directory_list (NolphinView *view,
					   NolphinDirectory *directory)
{
	add_directory_to_directory_list (view, directory,
					 &view->details->templates_directory_list,
					 G_CALLBACK (templates_added_or_changed_callback));
}

static void
remove_directory_from_templates_directory_list (NolphinView *view,
						NolphinDirectory *directory)
{
	remove_directory_from_directory_list (view, directory,
					      &view->details->templates_directory_list,
					      G_CALLBACK (templates_added_or_changed_callback));
}

static void
slot_active (NolphinWindowSlot *slot,
	     NolphinView *view)
{
	if (view->details->active) {
		return;
	}

	view->details->active = TRUE;

	nolphin_view_merge_menus (view);
	schedule_update_menus (view);
}

static void
slot_inactive (NolphinWindowSlot *slot,
	       NolphinView *view)
{
	if (!view->details->active) {
		return;
	}

	view->details->active = FALSE;

	nolphin_view_unmerge_menus (view);
	remove_update_menus_timeout_callback (view);
}

static void slot_changed_pane (NolphinWindowSlot *slot,
			       NolphinView *view)
{
	g_signal_handlers_disconnect_matched (view->details->window,
					      G_SIGNAL_MATCH_DATA, 0, 0,
					      NULL, NULL, view);

	view->details->window = nolphin_window_slot_get_window (slot);
	schedule_update_menus (view);

	g_signal_connect_object (view->details->window,
		"hidden-files-mode-changed", G_CALLBACK (hidden_files_mode_changed),
		view, 0);
	hidden_files_mode_changed (view->details->window, view);
}

static void
plugin_prefs_changed (GSettings *settings, gchar *key, gpointer user_data)
{
    scripts_added_or_changed_callback (NULL, NULL, user_data);
}

void
nolphin_view_grab_focus (NolphinView *view)
{
	/* focus the child of the scrolled window if it exists */
	GtkWidget *child;
	child = gtk_bin_get_child (GTK_BIN (view));
	if (child) {
		gtk_widget_grab_focus (GTK_WIDGET (child));
	}
}

int
nolphin_view_get_selection_count (NolphinView *view)
{
    /* FIXME: This could be faster if we special cased it in subclasses */
    GList *files;
    int len;

    files = nolphin_view_get_selection (NOLPHIN_VIEW (view));
    len = g_list_length (files);
    nolphin_file_list_free (files);

    return len;
}

static void
update_undo_actions (NolphinView *view)
{
	NolphinFileUndoInfo *info;
	NolphinFileUndoManagerState undo_state;
	GtkAction *action;
	const gchar *label, *tooltip;
	gboolean available;
	gboolean undo_active, redo_active;
	gchar *undo_label, *undo_description, *redo_label, *redo_description;

	undo_label = undo_description = redo_label = redo_description = NULL;

	undo_active = FALSE;
	redo_active = FALSE;

	info = nolphin_file_undo_manager_get_action ();
	undo_state = nolphin_file_undo_manager_get_state ();

	if (info != NULL &&
	    (undo_state > NOLPHIN_FILE_UNDO_MANAGER_STATE_NONE)) {
		if (undo_state == NOLPHIN_FILE_UNDO_MANAGER_STATE_UNDO) {
			undo_active = TRUE;
		} else {
			redo_active = TRUE;
		}

		nolphin_file_undo_info_get_strings (info,
						     &undo_label, &undo_description,
						     &redo_label, &redo_description);
	}

	/* Update undo entry */
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      "Undo");
	available = undo_active;
	if (available) {
		label = undo_label;
		tooltip = undo_description;
	} else {
		/* Reset to default info */
		label = _("Rückgängig machen");
		tooltip = _("Letzte Aktion zurücknehmen");
	}

	g_object_set (action,
		      "label", label,
		      "tooltip", tooltip,
		      NULL);
	gtk_action_set_sensitive (action, available);

	/* Update redo entry */
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      "Redo");
	available = redo_active;
	if (available) {
		label = redo_label;
		tooltip = redo_description;
	} else {
		/* Reset to default info */
		label = _("Wiederherstellen");
		tooltip = _("Die zuletzt rückgängig gemachte Aktion wiederholen");
	}

	g_object_set (action,
		      "label", label,
		      "tooltip", tooltip,
		      NULL);
	gtk_action_set_sensitive (action, available);

	g_free (undo_label);
	g_free (undo_description);
	g_free (redo_label);
	g_free (redo_description);
}

static void
undo_manager_changed_cb (NolphinFileUndoManager* manager,
			 NolphinView *view)
{
    if (view->details->dir_action_group == NULL) {
        return;
    }

	update_undo_actions (view);
}

void
nolphin_view_set_selection (NolphinView *nolphin_view,
			     GList *selection)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (nolphin_view);

	if (!view->details->loading) {
		/* If we aren't still loading, set the selection right now,
		 * and reveal the new selection.
		 */
		view->details->selection_change_is_due_to_shell = TRUE;
		nolphin_view_call_set_selection (view, selection);
		view->details->selection_change_is_due_to_shell = FALSE;
		nolphin_view_reveal_selection (view);
	} else {
		/* If we are still loading, set the list of pending URIs instead.
		 * done_loading() will eventually select the pending URIs and reveal them.
		 */
		g_list_free_full (view->details->pending_selection, g_object_unref);
		view->details->pending_selection =
			eel_g_object_list_copy (selection);
	}
}

static void
nolphin_view_init (NolphinView *view)
{
	AtkObject *atk_object;
	NolphinDirectory *scripts_directory;
	NolphinDirectory *templates_directory;
	char *templates_uri;
	NolphinFileUndoManager* manager;
	view->details = G_TYPE_INSTANCE_GET_PRIVATE (view, NOLPHIN_TYPE_VIEW,
						     NolphinViewDetails);

    view->details->load_timer = g_timer_new ();

	/* Default to true; desktop-icon-view sets to false */
	view->details->show_foreign_files = TRUE;

	view->details->non_ready_files =
		g_hash_table_new_full (file_and_directory_hash,
				       file_and_directory_equal,
				       (GDestroyNotify)file_and_directory_free,
				       NULL);

	view->details->filter_score_cache =
		g_hash_table_new (g_direct_hash, g_direct_equal);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (view),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (view), NULL);
	gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (view), NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (view), GTK_SHADOW_NONE);

	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (view)),
					      GTK_JUNCTION_TOP | GTK_JUNCTION_LEFT);

	if (set_up_scripts_directory_global ()) {
		scripts_directory = nolphin_directory_get_by_uri (scripts_directory_uri);
		add_directory_to_scripts_directory_list (view, scripts_directory);
		nolphin_directory_unref (scripts_directory);
	} else {
		g_warning ("Ignoring scripts directory, it may be a broken link\n");
	}

	if (nolphin_should_use_templates_directory ()) {
		templates_uri = nolphin_get_templates_directory_uri ();
		templates_directory = nolphin_directory_get_by_uri (templates_uri);
		g_free (templates_uri);
		add_directory_to_templates_directory_list (view, templates_directory);
		nolphin_directory_unref (templates_directory);
	}
	update_templates_directory (view);
	g_signal_connect_object (nolphin_signaller_get_current (),
				 "user_dirs_changed",
				 G_CALLBACK (user_dirs_changed),
				 view, G_CONNECT_SWAPPED);

	view->details->sort_directories_first =
		g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SORT_DIRECTORIES_FIRST);

	view->details->sort_favorites_first =
		g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SORT_FAVORITES_FIRST);

	g_signal_connect_object (nolphin_trash_monitor_get (), "trash_state_changed",
				 G_CALLBACK (nolphin_view_trash_state_changed_callback), view, 0);

	/* React to clipboard changes */
	g_signal_connect_object (nolphin_clipboard_monitor_get (), "clipboard_changed",
				 G_CALLBACK (clipboard_changed_callback), view, 0);

	/* Register to menu provider extension signal managing menu updates */
	g_signal_connect_object (nolphin_signaller_get_current (), "popup_menu_changed",
				 G_CALLBACK (nolphin_view_update_menus), view, G_CONNECT_SWAPPED);

	gtk_widget_show (GTK_WIDGET (view));

	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_ENABLE_DELETE,
				  G_CALLBACK (schedule_update_menus_callback), view);
    g_signal_connect_swapped (nolphin_menu_config_preferences,
                              "changed",
                              G_CALLBACK (schedule_update_menus_callback), view);
    g_signal_connect_swapped (nolphin_preferences,
                  "changed::" NOLPHIN_PREFERENCES_SWAP_TRASH_DELETE,
                  G_CALLBACK (swap_delete_keybinding_changed_callback), view);
	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_CLICK_POLICY,
				  G_CALLBACK(click_policy_changed_callback),
				  view);
    g_signal_connect_swapped (nolphin_preferences,
                  "changed::" NOLPHIN_PREFERENCES_CLICK_TO_RENAME,
                  G_CALLBACK(click_to_rename_changed_callback),
                  view);
	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_SORT_DIRECTORIES_FIRST,
				  G_CALLBACK(sort_directories_first_changed_callback), view);
	g_signal_connect_swapped (nolphin_preferences,
				  "changed::" NOLPHIN_PREFERENCES_SORT_FAVORITES_FIRST,
				  G_CALLBACK(sort_favorites_first_changed_callback), view);
	g_signal_connect_swapped (gnome_lockdown_preferences,
				  "changed::" NOLPHIN_PREFERENCES_LOCKDOWN_COMMAND_LINE,
				  G_CALLBACK (schedule_update_menus), view);

	g_signal_connect_swapped (nolphin_window_state,
				  "changed::" NOLPHIN_WINDOW_STATE_START_WITH_STATUS_BAR,
				  G_CALLBACK (nolphin_view_display_selection_info), view);

    g_signal_connect_swapped (nolphin_preferences,
                  "changed::" NOLPHIN_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS,
                  G_CALLBACK (nolphin_to_menu_preferences_changed_callback), view);
    g_signal_connect_swapped (nolphin_preferences,
                  "changed::" NOLPHIN_PREFERENCES_SHOW_PLACES_IN_TO_MENUS,
                  G_CALLBACK (nolphin_to_menu_preferences_changed_callback), view);

    nolphin_to_menu_preferences_changed_callback (view);

	manager = nolphin_file_undo_manager_get ();
	g_signal_connect_object (manager, "undo-changed",
				 G_CALLBACK (undo_manager_changed_cb), view, 0);

    g_signal_connect (nolphin_plugin_preferences,
                      "changed::" NOLPHIN_PLUGIN_PREFERENCES_DISABLED_SCRIPTS,
                      G_CALLBACK (plugin_prefs_changed), view);

	/* Accessibility */
	atk_object = gtk_widget_get_accessible (GTK_WIDGET (view));
	atk_object_set_name (atk_object, _("Inhaltsansicht"));
	atk_object_set_description (atk_object, _("Ansicht des aktuellen Ordners"));

    view->details->action_manager = nolphin_action_manager_new ();

    view->details->action_manager_changed_id =
        g_signal_connect_swapped (view->details->action_manager, "changed",
                      G_CALLBACK (actions_added_or_changed_callback),
                                  view);

    view->details->bookmarks = nolphin_bookmark_list_get_default ();

    view->details->bookmarks_changed_id =
        g_signal_connect_swapped (view->details->bookmarks, "changed",
                      G_CALLBACK (schedule_update_menus),
                      view);
}

static void
disconnect_action_activate (NolphinAction *action, NolphinView *view)
{
    g_signal_handlers_disconnect_by_func (action, run_action_callback, view);
}

static void
real_unmerge_menus (NolphinView *view)
{
	GtkUIManager *ui_manager;

	if (view->details->window == NULL) {
		return;
	}
    if (GTK_IS_ACTION_GROUP (view->details->copy_move_action_groups[0])) {
        disconnect_bookmark_signals (view);
    }

	ui_manager = nolphin_window_get_ui_manager (view->details->window);

	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->dir_merge_id,
				&view->details->dir_action_group);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->extensions_menu_merge_id,
				&view->details->extensions_menu_action_group);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->open_with_merge_id,
				&view->details->open_with_action_group);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->scripts_merge_id,
				&view->details->scripts_action_group);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->templates_merge_id,
				&view->details->templates_action_group);

    if (view->details->actions_action_group) {
        GList *action_list;

        action_list = gtk_action_group_list_actions (view->details->actions_action_group);
        g_list_foreach (action_list, (GFunc) disconnect_action_activate, view);

        g_list_free (action_list);
    }

    nolphin_ui_unmerge_ui (ui_manager,
                &view->details->actions_merge_id,
                &view->details->actions_action_group);
    int i;
    for (i = 0; i < 4; i++) {
        nolphin_ui_unmerge_ui (ui_manager,
                &view->details->copy_move_merge_ids[i],
                &view->details->copy_move_action_groups[i]);
    }
}

static void
nolphin_view_destroy (GtkWidget *object)
{
	NolphinView *view;
	GList *node, *next;

	view = NOLPHIN_VIEW (object);

	disconnect_model_handlers (view);

    if (view->details->bookmarks_changed_id != 0) {
        g_signal_handler_disconnect (view->details->bookmarks,
                         view->details->bookmarks_changed_id);
        view->details->bookmarks_changed_id = 0;
    }

    if (view->details->action_manager_changed_id != 0) {
        g_signal_handler_disconnect (view->details->action_manager,
                         view->details->action_manager_changed_id);
        view->details->action_manager_changed_id = 0;
    }
    g_clear_object (&view->details->action_manager);

	nolphin_view_unmerge_menus (view);

	/* We don't own the window, so no unref */
	view->details->slot = NULL;
	view->details->window = NULL;

	nolphin_view_stop_loading (view);

	for (node = view->details->scripts_directory_list; node != NULL; node = next) {
		next = node->next;
		remove_directory_from_scripts_directory_list (view, node->data);
	}

	for (node = view->details->templates_directory_list; node != NULL; node = next) {
		next = node->next;
		remove_directory_from_templates_directory_list (view, node->data);
	}

	while (view->details->subdirectory_list != NULL) {
		nolphin_view_remove_subdirectory (view,
						   view->details->subdirectory_list->data);
	}

	remove_update_menus_timeout_callback (view);
	remove_update_status_idle_callback (view);

	if (view->details->display_selection_idle_id != 0) {
		g_source_remove (view->details->display_selection_idle_id);
		view->details->display_selection_idle_id = 0;
	}

	if (view->details->reveal_selection_idle_id != 0) {
		g_source_remove (view->details->reveal_selection_idle_id);
		view->details->reveal_selection_idle_id = 0;
	}

	if (view->details->delayed_rename_file_id != 0) {
		g_source_remove (view->details->delayed_rename_file_id);
		view->details->delayed_rename_file_id = 0;
	}

	reset_filter_state (view);

	if (view->details->model) {
		nolphin_directory_unref (view->details->model);
		view->details->model = NULL;
	}

	if (view->details->directory_as_file) {
		nolphin_file_unref (view->details->directory_as_file);
		view->details->directory_as_file = NULL;
	}

    g_signal_handlers_disconnect_by_func (nolphin_plugin_preferences, G_CALLBACK (plugin_prefs_changed), view);

	GTK_WIDGET_CLASS (nolphin_view_parent_class)->destroy (object);
}

static void
nolphin_view_finalize (GObject *object)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (object);

	g_signal_handlers_disconnect_by_func (nolphin_preferences,
					      schedule_update_menus_callback, view);
	g_signal_handlers_disconnect_by_func (nolphin_preferences,
					      click_policy_changed_callback, view);
    g_signal_handlers_disconnect_by_func (nolphin_preferences,
                          click_to_rename_changed_callback, view);
	g_signal_handlers_disconnect_by_func (nolphin_preferences,
					      sort_directories_first_changed_callback, view);
	g_signal_handlers_disconnect_by_func (nolphin_preferences,
					      sort_favorites_first_changed_callback, view);
	g_signal_handlers_disconnect_by_func (nolphin_window_state,
					      nolphin_view_display_selection_info, view);
    g_signal_handlers_disconnect_by_func (nolphin_menu_config_preferences,
                          schedule_update_menus_callback, view);

	g_signal_handlers_disconnect_by_func (gnome_lockdown_preferences,
					      schedule_update_menus, view);

    g_signal_handlers_disconnect_by_func (nolphin_preferences,
                          nolphin_to_menu_preferences_changed_callback, view);

    g_signal_handlers_disconnect_by_func (nolphin_preferences,
                          schedule_update_menus, view);

	unschedule_pop_up_location_context_menu (view);
	if (view->details->location_popup_event != NULL) {
		gdk_event_free ((GdkEvent *) view->details->location_popup_event);
	}

    g_clear_pointer (&view->details->load_timer, g_timer_destroy);

    g_clear_pointer (&view->details->detail_string, g_free);

    reset_filter_state (view);

	g_hash_table_destroy (view->details->non_ready_files);
	g_hash_table_destroy (view->details->filter_score_cache);

	G_OBJECT_CLASS (nolphin_view_parent_class)->finalize (object);
}

/**
 * nolphin_view_display_selection_info:
 *
 * Display information about the current selection, and notify the view frame of the changed selection.
 * @view: NolphinView for which to display selection info.
 *
 **/
void
nolphin_view_display_selection_info (NolphinView *view)
{
	GList *selection;
	goffset non_folder_size;
	gboolean non_folder_size_known;
	guint non_folder_count, folder_count, folder_item_count;
	gboolean folder_item_count_known;
	guint file_item_count;
	GList *p;
	char *first_item_name;
	char *non_folder_str;
	char *folder_count_str;
	char *folder_item_count_str;
	char *status_string;
	char *view_status_string;
	char *free_space_str;
	char *obj_selected_free_space_str;
	NolphinFile *file;

	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	selection = nolphin_view_get_selection (view);

	folder_item_count_known = TRUE;
	folder_count = 0;
	folder_item_count = 0;
	non_folder_count = 0;
	non_folder_size_known = FALSE;
	non_folder_size = 0;
	first_item_name = NULL;
	folder_count_str = NULL;
	non_folder_str = NULL;
	folder_item_count_str = NULL;
	free_space_str = NULL;
	obj_selected_free_space_str = NULL;
	status_string = NULL;
	view_status_string = NULL;

	for (p = selection; p != NULL; p = p->next) {
		file = p->data;
		if (nolphin_file_is_directory (file)) {
			folder_count++;
			if (nolphin_file_get_directory_item_count (file, &file_item_count, NULL)) {
				folder_item_count += file_item_count;
			} else {
				folder_item_count_known = FALSE;
			}
		} else {
			non_folder_count++;
			if (!nolphin_file_can_get_size (file)) {
				non_folder_size_known = TRUE;
				non_folder_size += nolphin_file_get_size (file);
			}
		}

		if (first_item_name == NULL) {
			first_item_name = nolphin_file_get_display_name (file);
		}
	}

	nolphin_file_list_free (selection);

	/* Break out cases for localization's sake. But note that there are still pieces
	 * being assembled in a particular order, which may be a problem for some localizers.
	 */

	if (folder_count != 0) {
		if (folder_count == 1 && non_folder_count == 0) {
			folder_count_str = g_strdup_printf (_("»%s« ausgewählt"), first_item_name);
		} else {
			folder_count_str = g_strdup_printf (ngettext("%'d Ordner markiert",
								     "%'d Ordner markiert",
								     folder_count),
							    folder_count);
		}

		if (folder_count == 1) {
			if (!folder_item_count_known) {
				folder_item_count_str = g_strdup ("");
			} else {
				folder_item_count_str = g_strdup_printf (ngettext(" (enthält %'d Objekt)",
										  " (enthält %'d Objekte)",
										  folder_item_count),
									 folder_item_count);
			}
		}
		else {
			if (!folder_item_count_known) {
				folder_item_count_str = g_strdup ("");
			} else {
				/* translators: this is preceded with a string of form 'N folders' (N more than 1) */
				folder_item_count_str = g_strdup_printf (ngettext(" (enthält insgesamt %'d Objekt)",
										  " (enthält insgesamt %'d Objekte)",
										  folder_item_count),
									 folder_item_count);
			}

		}
		if (view->details->detail_string != NULL) {
			g_free (view->details->detail_string);
		}
		view->details->detail_string = g_strdup (folder_item_count_str);
	}

	if (non_folder_count != 0) {
		char *items_string;

		if (folder_count == 0) {
			if (non_folder_count == 1) {
				items_string = g_strdup_printf (_("»%s« ausgewählt"),
								first_item_name);
			} else {
				items_string = g_strdup_printf (ngettext("%'d Objekt ausgewählt",
									 "%'d Objekte ausgewählt",
									 non_folder_count),
								non_folder_count);
			}
		} else {
			/* Folders selected also, use "other" terminology */
			items_string = g_strdup_printf (ngettext("%'d weiteres Objekt ausgewählt",
								 "%'d weitere Objekte ausgewählt",
								 non_folder_count),
							non_folder_count);
		}

		if (non_folder_size_known) {
			char *size_string;
			int prefix;

			prefix = nolphin_global_preferences_get_size_prefix_preference ();
			size_string = g_format_size_full (non_folder_size, prefix);
			non_folder_str = g_strdup_printf ("%s (%s)",
							  items_string,
							  size_string);


			if (view->details->detail_string != NULL) {
				g_free (view->details->detail_string);
			}
			view->details->detail_string = g_strdup_printf (" (%s)", size_string);
			g_free (size_string);
		} else {
			non_folder_str = g_strdup (items_string);
		}

		g_free (items_string);
	}

	free_space_str = nolphin_file_get_volume_free_space (view->details->directory_as_file);
	if (free_space_str != NULL) {
		obj_selected_free_space_str = g_strdup_printf (_("Freier Speicherplatz: %s"), free_space_str);
	}
	if (folder_count == 0 && non_folder_count == 0)	{
		char *item_count_str;
		guint item_count;

		item_count = nolphin_view_get_item_count (view);

		item_count_str = g_strdup_printf (ngettext ("%'u Objekt", "%'u Objekte", item_count), item_count);

		if (free_space_str != NULL) {
			status_string = g_strdup_printf (_("%s, freier Speicherplatz: %s"), item_count_str, free_space_str);
			g_free (item_count_str);
		} else {
			status_string = item_count_str;
		}

	} else if (folder_count == 0) {
		view_status_string = g_strdup (non_folder_str);

		if (free_space_str != NULL) {
			status_string = g_strdup_printf ("%s, %s",
							 non_folder_str,
							 obj_selected_free_space_str);
		}
	} else if (non_folder_count == 0) {
		view_status_string = g_strdup_printf ("%s%s",
						      folder_count_str,
						      folder_item_count_str);

		if (free_space_str != NULL) {
			status_string = g_strdup_printf ("%s%s, %s",
							 folder_count_str,
							 folder_item_count_str,
							 obj_selected_free_space_str);
		}
	} else {
		view_status_string = g_strdup_printf ("%s%s, %s",
						      folder_count_str,
						      folder_item_count_str,
						      non_folder_str);

		if (obj_selected_free_space_str != NULL) {
			status_string = g_strdup_printf ("%s%s, %s, %s",
							 folder_count_str,
							 folder_item_count_str,
							 non_folder_str,
							 obj_selected_free_space_str);
		}
		if (view->details->detail_string != NULL) {
			g_free (view->details->detail_string);
		}
		view->details->detail_string = g_strdup ("");
	}

	g_free (free_space_str);
	g_free (obj_selected_free_space_str);
	g_free (first_item_name);
	g_free (folder_count_str);
	g_free (folder_item_count_str);
	g_free (non_folder_str);

	if (status_string == NULL) {
		status_string = g_strdup (view_status_string);
	}

    nolphin_window_slot_set_status (view->details->slot,
                                 status_string,
                                 view_status_string,
                                 view->details->loading);

	g_free (status_string);
	g_free (view_status_string);
}

static void
nolphin_view_send_selection_change (NolphinView *view)
{
	g_signal_emit (view, signals[SELECTION_CHANGED], 0);

	view->details->send_selection_change_to_shell = FALSE;
}

void
nolphin_view_load_location (NolphinView *nolphin_view,
			     GFile        *location)
{
	NolphinDirectory *directory;
	NolphinView *directory_view;

	directory_view = NOLPHIN_VIEW (nolphin_view);

	directory = nolphin_directory_get (location);
	load_directory (directory_view, directory);
	nolphin_directory_unref (directory);
}

static gboolean
reveal_selection_idle_callback (gpointer data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (data);

	view->details->reveal_selection_idle_id = 0;
	nolphin_view_reveal_selection (view);

	return FALSE;
}

static gboolean
idle_timer_report (gpointer user_data)
{
    g_return_val_if_fail (NOLPHIN_IS_VIEW (user_data), G_SOURCE_REMOVE);

    NolphinView *view = NOLPHIN_VIEW (user_data);

    g_printerr ("Idle...Folder load time: %f seconds\n", g_timer_elapsed (view->details->load_timer, NULL));

    return G_SOURCE_REMOVE;
}

static void
done_loading (NolphinView *view,
	      gboolean all_files_seen)
{
	GList *selection;

	if (!view->details->loading) {
		return;
	}

	/* This can be called during destruction, in which case there
	 * is no NolphinWindow any more.
	 */
	if (view->details->window != NULL) {
		if (all_files_seen) {
			nolphin_window_report_load_complete (view->details->window, NOLPHIN_VIEW (view));
		}

		schedule_update_menus (view);
		schedule_update_status (view);
		reset_update_interval (view);

		selection = view->details->pending_selection;
		if (selection != NULL && all_files_seen) {
			view->details->pending_selection = NULL;

			view->details->selection_change_is_due_to_shell = TRUE;
			nolphin_view_call_set_selection (view, selection);
			view->details->selection_change_is_due_to_shell = FALSE;

			if (NOLPHIN_IS_LIST_VIEW (view)) {
				/* HACK: We should be able to directly call reveal_selection here,
				 * but at this point the GtkTreeView hasn't allocated the new nodes
				 * yet, and it has a bug in the scroll calculation dealing with this
				 * special case. It would always make the selection the top row, even
				 * if no scrolling would be neccessary to reveal it. So we let it
				 * allocate before revealing.
				 */
				if (view->details->reveal_selection_idle_id != 0) {
					g_source_remove (view->details->reveal_selection_idle_id);
				}
				view->details->reveal_selection_idle_id =
					g_idle_add (reveal_selection_idle_callback, view);
			} else {
				nolphin_view_reveal_selection (view);
			}
		}
		g_list_free_full (selection, g_object_unref);
		nolphin_view_display_selection_info (view);
	}

	view->details->loading = FALSE;
	g_signal_emit (view, signals[END_LOADING], 0, all_files_seen);

    if (g_getenv("NOLPHIN_BENCHMARK_LOADING")) {
        if (nolphin_startup_timer != NULL) {
            g_printerr ("Nolphin startup time: %f seconds\n", g_timer_elapsed (nolphin_startup_timer, NULL));
            g_clear_pointer (&nolphin_startup_timer, g_timer_destroy);
        }

        gchar *uri = nolphin_view_get_uri (view);
        g_printerr ("Folder load time: %f seconds. First render: %.0fms. Method: %s. URI: %s\n",
                    g_timer_elapsed (view->details->load_timer, NULL),
                    view->details->first_render_elapsed,
                    view->details->display_method,
                    uri);
        g_free (uri);

        g_idle_add_full (1000, (GSourceFunc) idle_timer_report, view, NULL);
    }
}

typedef struct {
	GHashTable *debuting_files;
	GList	   *added_files;
} DebutingFilesData;

static void
debuting_files_data_free (DebutingFilesData *data)
{
	g_hash_table_unref (data->debuting_files);
	nolphin_file_list_free (data->added_files);
	g_free (data);
}

/* This signal handler watch for the arrival of the icons created
 * as the result of a file operation. Once the last one is detected
 * it selects and reveals them all.
 */
static void
debuting_files_add_file_callback (NolphinView *view,
				  NolphinFile *new_file,
				  NolphinDirectory *directory,
				  DebutingFilesData *data)
{
	GFile *location;

	location = nolphin_file_get_location (new_file);

	if (g_hash_table_remove (data->debuting_files, location)) {
		nolphin_file_ref (new_file);
		data->added_files = g_list_prepend (data->added_files, new_file);

		if (g_hash_table_size (data->debuting_files) == 0) {
			nolphin_view_call_set_selection (view, data->added_files);
			nolphin_view_reveal_selection (view);
			g_signal_handlers_disconnect_by_func (view,
							      G_CALLBACK (debuting_files_add_file_callback),
							      data);
		}
	}

	g_object_unref (location);
}

typedef struct {
	GList		*added_files;
	NolphinView *directory_view;
} CopyMoveDoneData;

static void
copy_move_done_data_free (CopyMoveDoneData *data)
{
	g_assert (data != NULL);

	if (data->directory_view != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (data->directory_view),
					      (gpointer *) &data->directory_view);
	}

	nolphin_file_list_free (data->added_files);
	g_free (data);
}

static void
pre_copy_move_add_file_callback (NolphinView *view,
				 NolphinFile *new_file,
				 NolphinDirectory *directory,
				 CopyMoveDoneData *data)
{
	nolphin_file_ref (new_file);
	data->added_files = g_list_prepend (data->added_files, new_file);
}

/* This needs to be called prior to nolphin_file_operations_copy_move.
 * It hooks up a signal handler to catch any icons that get added before
 * the copy_done_callback is invoked. The return value should  be passed
 * as the data for uri_copy_move_done_callback.
 */
static CopyMoveDoneData *
pre_copy_move (NolphinView *directory_view)
{
	CopyMoveDoneData *copy_move_done_data;

	copy_move_done_data = g_new0 (CopyMoveDoneData, 1);
	copy_move_done_data->directory_view = directory_view;

	g_object_add_weak_pointer (G_OBJECT (copy_move_done_data->directory_view),
				   (gpointer *) &copy_move_done_data->directory_view);

	/* We need to run after the default handler adds the folder we want to
	 * operate on. The ADD_FILE signal is registered as G_SIGNAL_RUN_LAST, so we
	 * must use connect_after.
	 */
	g_signal_connect (directory_view, "add_file",
			  G_CALLBACK (pre_copy_move_add_file_callback), copy_move_done_data);

	return copy_move_done_data;
}

/* This function is used to pull out any debuting uris that were added
 * and (as a side effect) remove them from the debuting uri hash table.
 */
static gboolean
copy_move_done_partition_func (gpointer data, gpointer callback_data)
{
 	GFile *location;
 	gboolean result;

	location = nolphin_file_get_location (NOLPHIN_FILE (data));
	result = g_hash_table_remove ((GHashTable *) callback_data, location);
	g_object_unref (location);

	return result;
}

static gboolean
remove_not_really_moved_files (gpointer key,
			       gpointer value,
			       gpointer callback_data)
{
	GList **added_files;
	GFile *loc;

	loc = key;

	if (GPOINTER_TO_INT (value)) {
		return FALSE;
	}

	added_files = callback_data;
	*added_files = g_list_prepend (*added_files,
				       nolphin_file_get (loc));
	return TRUE;
}


/* When this function is invoked, the file operation is over, but all
 * the icons may not have been added to the directory view yet, so
 * we can't select them yet.
 *
 * We're passed a hash table of the uri's to look out for, we hook
 * up a signal handler to await their arrival.
 */
static void
copy_move_done_callback (GHashTable *debuting_files,
			 gboolean success,
			 gpointer data)
{
	NolphinView  *directory_view;
	CopyMoveDoneData *copy_move_done_data;
	DebutingFilesData  *debuting_files_data;

	copy_move_done_data = (CopyMoveDoneData *) data;
	directory_view = copy_move_done_data->directory_view;

	if (directory_view != NULL) {
		g_assert (NOLPHIN_IS_VIEW (directory_view));

		debuting_files_data = g_new (DebutingFilesData, 1);
		debuting_files_data->debuting_files = g_hash_table_ref (debuting_files);
		debuting_files_data->added_files = eel_g_list_partition
			(copy_move_done_data->added_files,
			 copy_move_done_partition_func,
			 debuting_files,
			 &copy_move_done_data->added_files);

		/* We're passed the same data used by pre_copy_move_add_file_callback, so disconnecting
		 * it will free data. We've already siphoned off the added_files we need, and stashed the
		 * directory_view pointer.
		 */
		g_signal_handlers_disconnect_by_func (directory_view,
						      G_CALLBACK (pre_copy_move_add_file_callback),
						      data);

		/* Any items in the debuting_files hash table that have
		 * "FALSE" as their value aren't really being copied
		 * or moved, so we can't wait for an add_file signal
		 * to come in for those.
		 */
		g_hash_table_foreach_remove (debuting_files,
					     remove_not_really_moved_files,
					     &debuting_files_data->added_files);

		if (g_hash_table_size (debuting_files) == 0) {
			/* on the off-chance that all the icons have already been added */
			if (debuting_files_data->added_files != NULL) {
				nolphin_view_call_set_selection (directory_view,
								  debuting_files_data->added_files);
				nolphin_view_reveal_selection (directory_view);
			}
			debuting_files_data_free (debuting_files_data);
		} else {
			/* We need to run after the default handler adds the folder we want to
			 * operate on. The ADD_FILE signal is registered as G_SIGNAL_RUN_LAST, so we
			 * must use connect_after.
			 */
			g_signal_connect_data (directory_view,
					       "add_file",
					       G_CALLBACK (debuting_files_add_file_callback),
					       debuting_files_data,
					       (GClosureNotify) debuting_files_data_free,
					       G_CONNECT_AFTER);
		}
		/* Schedule menu update for undo items */
		schedule_update_menus (directory_view);
	}

	copy_move_done_data_free (copy_move_done_data);
}

static gboolean
view_file_still_belongs (NolphinView *view,
			 NolphinFile *file,
			 NolphinDirectory *directory)
{
	if (view->details->model != directory &&
	    g_list_find (view->details->subdirectory_list, directory) == NULL) {
		return FALSE;
	}

	return nolphin_directory_contains_file (directory, file);
}

static gboolean
still_should_show_file (NolphinView *view, NolphinFile *file, NolphinDirectory *directory)
{
	return nolphin_view_should_show_file (view, file) &&
		view_file_still_belongs (view, file, directory);
}

static gboolean
ready_to_load (NolphinFile *file)
{
	return nolphin_file_check_if_ready (file, NOLPHIN_FILE_ATTRIBUTE_INFO);
}

static int
compare_files_cover (gconstpointer a, gconstpointer b, gpointer callback_data)
{
	const FileAndDirectory *fad1, *fad2;
	NolphinView *view;

	view = callback_data;
	fad1 = a; fad2 = b;

	if (fad1->directory < fad2->directory) {
		return -1;
	} else if (fad1->directory > fad2->directory) {
		return 1;
	} else {
		return NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->compare_files (view, fad1->file, fad2->file);
	}
}
static void
sort_files (NolphinView *view, GList **list)
{
	*list = g_list_sort_with_data (*list, compare_files_cover, view);

}

/* Go through all the new added and changed files.
 * Put any that are not ready to load in the non_ready_files hash table.
 * Add all the rest to the old_added_files and old_changed_files lists.
 * Sort the old_*_files lists if anything was added to them.
 */
static void
process_new_files (NolphinView *view)
{
	GList *new_added_files, *new_changed_files, *old_added_files, *old_changed_files;
	GHashTable *non_ready_files;
	GList *node, *next;
	FileAndDirectory *pending;
	gboolean in_non_ready;

	new_added_files = view->details->new_added_files;
	view->details->new_added_files = NULL;

	if (view->details->first_render_elapsed < 0.0 && new_added_files != NULL)
		view->details->first_render_elapsed = g_timer_elapsed (view->details->load_timer, NULL) * 1000.0;
	new_changed_files = view->details->new_changed_files;
	view->details->new_changed_files = NULL;

	non_ready_files = view->details->non_ready_files;

	old_added_files = view->details->old_added_files;
	old_changed_files = view->details->old_changed_files;

	/* Newly added files go into the old_added_files list if they're
	 * ready, and into the hash table if they're not.
	 */
	for (node = new_added_files; node != NULL; node = next) {
		next = node->next;
		pending = (FileAndDirectory *)node->data;
		in_non_ready = g_hash_table_lookup (non_ready_files, pending) != NULL;
		if (nolphin_view_should_show_file (view, pending->file)) {
			if (ready_to_load (pending->file)) {
				if (in_non_ready) {
					g_hash_table_remove (non_ready_files, pending);
				}
				new_added_files = g_list_delete_link (new_added_files, node);
				old_added_files = g_list_prepend (old_added_files, pending);
			} else {
				if (!in_non_ready) {
					new_added_files = g_list_delete_link (new_added_files, node);
					g_hash_table_insert (non_ready_files, pending, pending);
				}
			}
		}
	}
	file_and_directory_list_free (new_added_files);

	/* Newly changed files go into the old_added_files list if they're ready
	 * and were seen non-ready in the past, into the old_changed_files list
	 * if they are read and were not seen non-ready in the past, and into
	 * the hash table if they're not ready.
	 */
	for (node = new_changed_files; node != NULL; node = next) {
		next = node->next;
		pending = (FileAndDirectory *)node->data;
		if (!still_should_show_file (view, pending->file, pending->directory) || ready_to_load (pending->file)) {
			if (g_hash_table_lookup (non_ready_files, pending) != NULL) {
				g_hash_table_remove (non_ready_files, pending);
				if (still_should_show_file (view, pending->file, pending->directory)) {
					new_changed_files = g_list_delete_link (new_changed_files, node);
					old_added_files = g_list_prepend (old_added_files, pending);
				}
			} else if (nolphin_view_should_show_file (view, pending->file)) {
				new_changed_files = g_list_delete_link (new_changed_files, node);
				old_changed_files = g_list_prepend (old_changed_files, pending);
			}
		}
	}
	file_and_directory_list_free (new_changed_files);

	/* If any files were added to old_added_files, then resort it. */
	if (old_added_files != view->details->old_added_files) {
		view->details->old_added_files = old_added_files;
		sort_files (view, &view->details->old_added_files);
	}

	/* Resort old_changed_files too, since file attributes
	 * relevant to sorting could have changed.
	 */
	if (old_changed_files != view->details->old_changed_files) {
		view->details->old_changed_files = old_changed_files;
		sort_files (view, &view->details->old_changed_files);
	}

}

static void
process_old_files (NolphinView *view)
{
	GList *files_added, *files_changed, *node;
	FileAndDirectory *pending;
	GList *selection, *files;
	gboolean send_selection_change;

	files_added = view->details->old_added_files;
	files_changed = view->details->old_changed_files;

	send_selection_change = FALSE;

	if (files_added != NULL || files_changed != NULL) {
		g_signal_emit (view, signals[BEGIN_FILE_CHANGES], 0);

		for (node = files_added; node != NULL; node = node->next) {
			pending = node->data;
			g_signal_emit (view,
				       signals[ADD_FILE], 0, pending->file, pending->directory);
		}

		for (node = files_changed; node != NULL; node = node->next) {
			pending = node->data;
			g_hash_table_remove (view->details->filter_score_cache,
			                     pending->file);
			g_signal_emit (view,
				       signals[still_should_show_file (view, pending->file, pending->directory)
					       ? FILE_CHANGED : REMOVE_FILE], 0,
				       pending->file, pending->directory);
		}

		g_signal_emit (view, signals[END_FILE_CHANGES], 0);

		if (files_changed != NULL) {
			selection = nolphin_view_get_selection (view);
			files = file_and_directory_list_to_files (files_changed);
			send_selection_change = eel_g_lists_sort_and_check_for_intersection
				(&files, &selection);
			nolphin_file_list_free (files);
			nolphin_file_list_free (selection);
		}

		file_and_directory_list_free (view->details->old_added_files);
		view->details->old_added_files = NULL;

		file_and_directory_list_free (view->details->old_changed_files);
		view->details->old_changed_files = NULL;
	}

	if (send_selection_change) {
		/* Send a selection change since some file names could
		 * have changed.
		 */
		nolphin_view_send_selection_change (view);
	}
}

/* Completion only waits on the essential file info needed to place a row.
 * Attributes that load asynchronously and only affect a subset of files
 * cosmetically - thumbnails and .desktop link info (the Name/Icon decoration)
 * fill in afterwards and should not keep a folder out of the 'immediate' path.
 */
static gboolean
non_ready_files_pending (NolphinView *view)
{
	GHashTableIter iter;
	gpointer key;

	g_hash_table_iter_init (&iter, view->details->non_ready_files);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		FileAndDirectory *fad = key;

		if (!nolphin_file_check_if_ready (fad->file, NOLPHIN_FILE_ATTRIBUTE_INFO)) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
display_pending_files (NolphinView *view)
{

	/* Don't dispatch any updates while the view is frozen. */
	if (view->details->updates_frozen) {
		return;
	}

	view->details->loading_pending_held = 0;

	process_new_files (view);
	process_old_files (view);

	if (view->details->model != NULL
	    && nolphin_directory_are_all_files_seen (view->details->model)
	    && !non_ready_files_pending (view)) {
		done_loading (view, TRUE);
	}
}

void
nolphin_view_freeze_updates (NolphinView *view)
{
	view->details->updates_frozen = TRUE;
	view->details->updates_queued = 0;
	view->details->needs_reload = FALSE;
}

void
nolphin_view_unfreeze_updates (NolphinView *view)
{
	view->details->updates_frozen = FALSE;

	if (view->details->needs_reload) {
		view->details->needs_reload = FALSE;
		if (view->details->model != NULL) {
			load_directory (view, view->details->model);
		}
	} else {
		schedule_idle_display_of_pending_files (view);
	}
}

static gboolean
display_selection_info_idle_callback (gpointer data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (data);

	g_object_ref (G_OBJECT (view));

	view->details->display_selection_idle_id = 0;
	nolphin_view_display_selection_info (view);
	if (view->details->send_selection_change_to_shell) {
		nolphin_view_send_selection_change (view);
	}

	g_object_unref (G_OBJECT (view));

	return FALSE;
}

static void
remove_update_menus_timeout_callback (NolphinView *view)
{
	if (view->details->update_menus_timeout_id != 0) {
		g_source_remove (view->details->update_menus_timeout_id);
		view->details->update_menus_timeout_id = 0;
	}
}

static void
update_menus_if_pending (NolphinView *view)
{
	if (!view->details->menu_states_untrustworthy) {
		return;
	}

	remove_update_menus_timeout_callback (view);
	nolphin_view_update_menus (view);
}

static gboolean
update_menus_timeout_callback (gpointer data)
{
	NolphinView *view;
	view = NOLPHIN_VIEW (data);

	g_object_ref (G_OBJECT (view));

	view->details->update_menus_timeout_id = 0;
	nolphin_view_update_menus (view);

	g_object_unref (G_OBJECT (view));

	return FALSE;
}

static gboolean
display_pending_callback (gpointer data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (data);

	g_object_ref (G_OBJECT (view));

	view->details->display_pending_source_id = 0;

	if (view->details->loading) {
		view->details->displayed_during_load = TRUE;
		if (g_getenv ("NOLPHIN_BENCHMARK_LOADING")) {
			g_printerr ("  progressive flush during load: held=%u at %.0fms\n",
			            view->details->loading_pending_held,
			            g_timer_elapsed (view->details->load_timer, NULL) * 1000.0);
		}
	}

	display_pending_files (view);

	g_object_unref (G_OBJECT (view));

	return FALSE;
}

static void
schedule_idle_display_of_pending_files (NolphinView *view)
{
	/* Get rid of a pending source as it might be a timeout */
	unschedule_display_of_pending_files (view);

	/* We want higher priority than the idle that handles the relayout
	   to avoid a resort on each add. But we still want to allow repaints
	   and other hight prio events while we have pending files to show. */
	view->details->display_pending_source_id =
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE - 20,
				 display_pending_callback, view, NULL);
}

static void
schedule_timeout_display_of_pending_files (NolphinView *view, guint interval)
{
 	/* No need to schedule an update if there's already one pending. */
	if (view->details->display_pending_source_id != 0) {
 		return;
	}

	view->details->display_pending_source_id =
		g_timeout_add (interval, display_pending_callback, view);
}

static void
unschedule_display_of_pending_files (NolphinView *view)
{
	/* Get rid of source if it's active. */
	if (view->details->display_pending_source_id != 0) {
		g_source_remove (view->details->display_pending_source_id);
		view->details->display_pending_source_id = 0;
	}
}

static void
queue_pending_files (NolphinView *view,
		     NolphinDirectory *directory,
		     GList *files,
		     GList **pending_list)
{
	if (files == NULL) {
		return;
	}

	/* Don't queue any more updates if we need to reload anyway */
	if (view->details->needs_reload) {
		return;
	}

	if (view->details->updates_frozen) {
		view->details->updates_queued += g_list_length (files);
		/* Mark the directory for reload when there are too much queued
		 * changes to prevent the pending list from growing infinitely.
		 */
		if (view->details->updates_queued > MAX_QUEUED_UPDATES) {
			view->details->needs_reload = TRUE;
			return;
		}
	}

	*pending_list = g_list_concat (file_and_directory_list_from_files (directory, files),
				       *pending_list);

	/* During loading of a normal local directory, hold files in the pending
	 * list and let done_loading_callback flush them all at once. For search,
	 * non-native filesystems, post-load changes, or very large directories,
	 * show progressively. */
	view->details->loading_pending_held += g_list_length (files);

	gboolean schedule = !view->details->loading ||
	                    nolphin_directory_are_all_files_seen (directory) ||
	                    nolphin_directory_is_in_search (directory) ||
	                    !g_file_is_native (nolphin_directory_get_location (directory)) ||
	                    view->details->loading_pending_held > MAX_LOADING_PENDING_HELD;

	if (schedule) {
		schedule_timeout_display_of_pending_files (view, view->details->update_interval);
	}
}

static void
remove_changes_timeout_callback (NolphinView *view)
{
	if (view->details->changes_timeout_id != 0) {
		g_source_remove (view->details->changes_timeout_id);
		view->details->changes_timeout_id = 0;
	}
}

static void
reset_update_interval (NolphinView *view)
{
	view->details->update_interval = UPDATE_INTERVAL_MIN;
	remove_changes_timeout_callback (view);
	/* Reschedule a pending timeout to idle */
	if (view->details->display_pending_source_id != 0) {
		schedule_idle_display_of_pending_files (view);
	}
}

static gboolean
changes_timeout_callback (gpointer data)
{
	gint64 now;
	gint64 time_delta;
	gboolean ret;
	NolphinView *view;
	view = NOLPHIN_VIEW (data);

	g_object_ref (G_OBJECT (view));

	now = g_get_monotonic_time ();
	time_delta = now - view->details->last_queued;

	if (time_delta < UPDATE_INTERVAL_RESET*1000) {
		if (view->details->update_interval < UPDATE_INTERVAL_MAX &&
		    view->details->loading) {
			/* Increase */
			view->details->update_interval += UPDATE_INTERVAL_INC;
            if (g_getenv("NOLPHIN_BENCHMARK_LOADING")) {
                g_printerr ("Increasing update interval to: %d ms\n", view->details->update_interval);
            }
		}
		ret = TRUE;
	} else {
		/* Reset */
		reset_update_interval (view);
		ret = FALSE;
	}

	g_object_unref (G_OBJECT (view));

	return ret;
}

static void
schedule_changes (NolphinView *view)
{
	/* Remember when the change was queued */
	view->details->last_queued = g_get_monotonic_time ();

	if (view->details->changes_timeout_id != 0) {
		return;
	}

	view->details->changes_timeout_id =
		g_timeout_add (UPDATE_INTERVAL_TIMEOUT_INTERVAL, changes_timeout_callback, view);
}

static void
files_added_callback (NolphinDirectory *directory,
		      GList *files,
		      gpointer callback_data)
{
	NolphinView *view;
	GtkWindow *window;
	char *uri;

	view = NOLPHIN_VIEW (callback_data);

	window = nolphin_view_get_containing_window (view);
	uri = nolphin_view_get_uri (view);
	DEBUG_FILES (files, "Files added in window %p: %s",
		     window, uri ? uri : "(no directory)");
	g_free (uri);

	schedule_changes (view);

	queue_pending_files (view, directory, files, &view->details->new_added_files);

	/* The number of items could have changed */
	schedule_update_status (view);
}

static void
files_changed_callback (NolphinDirectory *directory,
			GList *files,
			gpointer callback_data)
{
	NolphinView *view;
	GtkWindow *window;
	char *uri;

	view = NOLPHIN_VIEW (callback_data);

	window = nolphin_view_get_containing_window (view);
	uri = nolphin_view_get_uri (view);
	DEBUG_FILES (files, "Files changed in window %p: %s",
		     window, uri ? uri : "(no directory)");
	g_free (uri);

	schedule_changes (view);

	queue_pending_files (view, directory, files, &view->details->new_changed_files);

	/* The free space or the number of items could have changed */
	schedule_update_status (view);

	/* A change in MIME type could affect the Open with menu, for
	 * one thing, so we need to update menus when files change.
	 */
	schedule_update_menus (view);
}

static void
display_pending_files_with_tradeoff (NolphinView *view)
{
	NolphinViewClass *klass = NOLPHIN_VIEW_GET_CLASS (view);
	const gchar *sort_attribute = NULL;

	unschedule_display_of_pending_files (view);

	/* Skip relabeling our method if we ended up progressively loading. */
	if (view->details->displayed_during_load) {
		display_pending_files (view);
		return;
	}

	if (klass->get_sort_attribute != NULL) {
		sort_attribute = klass->get_sort_attribute (view);
	}

	gboolean fast = sort_attribute == NULL || !nolphin_file_attribute_slow_sort (sort_attribute);
	view->details->display_method = fast ? "immediate" : "deferred";

	if (fast) {
		display_pending_files (view);
	} else {
		schedule_timeout_display_of_pending_files (view, UPDATE_INTERVAL_DEFERRED);
	}
}

static void
done_loading_callback (NolphinDirectory *directory,
		       gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	process_new_files (view);
	if (!non_ready_files_pending (view)) {
		display_pending_files_with_tradeoff (view);
	}

	nolphin_schedule_heap_trim ();
}

static void
load_error_callback (NolphinDirectory *directory,
		     GError *error,
		     gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	/* FIXME: By doing a stop, we discard some pending files. Is
	 * that OK?
	 */
	nolphin_view_stop_loading (view);

    nolphin_window_back_or_forward (NOLPHIN_WINDOW (view->details->window),
                                 TRUE, 0, FALSE);

	/* Emit a signal to tell subclasses that a load error has
	 * occurred, so they can handle it in the UI.
	 */
	g_signal_emit (view,
		       signals[LOAD_ERROR], 0, error);
}

static void
real_load_error (NolphinView *view, GError *error)
{
	/* Report only one error per failed directory load (from the UI
	 * point of view, not from the NolphinDirectory point of view).
	 * Otherwise you can get multiple identical errors caused by
	 * unrelated code that just happens to try to iterate this
	 * directory.
	 */
	if (!view->details->reported_load_error) {
		nolphin_report_error_loading_directory
			(nolphin_view_get_directory_as_file (view),
			 error,
			 nolphin_view_get_containing_window (view));
	}
	view->details->reported_load_error = TRUE;
}

void
nolphin_view_add_subdirectory (NolphinView  *view,
				NolphinDirectory*directory)
{
	NolphinFileAttributes attributes;

	g_assert (!g_list_find (view->details->subdirectory_list, directory));

	nolphin_directory_ref (directory);

	attributes =
		NOLPHIN_FILE_ATTRIBUTES_FOR_ICON |
		NOLPHIN_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT |
		NOLPHIN_FILE_ATTRIBUTE_INFO |
		NOLPHIN_FILE_ATTRIBUTE_LINK_INFO |
		NOLPHIN_FILE_ATTRIBUTE_MOUNT |
		NOLPHIN_FILE_ATTRIBUTE_EXTENSION_INFO;

	nolphin_directory_file_monitor_add (directory,
					     &view->details->model,
					     view->details->show_hidden_files,
					     attributes,
					     files_added_callback, view);

	g_signal_connect
		(directory, "files_added",
		 G_CALLBACK (files_added_callback), view);
	g_signal_connect
		(directory, "files_changed",
		 G_CALLBACK (files_changed_callback), view);

	view->details->subdirectory_list = g_list_prepend (
							   view->details->subdirectory_list, directory);
}

void
nolphin_view_remove_subdirectory (NolphinView  *view,
				   NolphinDirectory*directory)
{
	g_assert (g_list_find (view->details->subdirectory_list, directory));

	view->details->subdirectory_list = g_list_remove (
							  view->details->subdirectory_list, directory);

	g_signal_handlers_disconnect_by_func (directory,
					      G_CALLBACK (files_added_callback),
					      view);
	g_signal_handlers_disconnect_by_func (directory,
					      G_CALLBACK (files_changed_callback),
					      view);

	nolphin_directory_file_monitor_remove (directory, &view->details->model);

	nolphin_directory_unref (directory);
}

/**
 * nolphin_view_get_loading:
 * @view: an #NolphinView.
 *
 * Return value: #gboolean inicating whether @view is currently loaded.
 *
 **/
gboolean
nolphin_view_get_loading (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return view->details->loading;
}

GtkUIManager *
nolphin_view_get_ui_manager (NolphinView  *view)
{
	if (view->details->window == NULL) {
		return NULL;
	}
	return nolphin_window_get_ui_manager (view->details->window);
}

/**
 * nolphin_view_get_model:
 *
 * Get the model for this NolphinView.
 * @view: NolphinView of interest.
 *
 * Return value: NolphinDirectory for this view.
 *
 **/
NolphinDirectory *
nolphin_view_get_model (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);

	return view->details->model;
}

GdkAtom
nolphin_view_get_copied_files_atom (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), GDK_NONE);

	return copied_files_atom;
}

static void
prepend_uri_one (gpointer data, gpointer callback_data)
{
	NolphinFile *file;
	GList **result;

	g_assert (NOLPHIN_IS_FILE (data));
	g_assert (callback_data != NULL);

	result = (GList **) callback_data;
	file = (NolphinFile *) data;
	*result = g_list_prepend (*result, nolphin_file_get_uri (file));
}

static void
offset_drop_points (GArray *relative_item_points,
		    int x_offset, int y_offset)
{
	guint index;

	if (relative_item_points == NULL) {
		return;
	}

	for (index = 0; index < relative_item_points->len; index++) {
		g_array_index (relative_item_points, GdkPoint, index).x += x_offset;
		g_array_index (relative_item_points, GdkPoint, index).y += y_offset;
	}
}

static void
nolphin_view_create_links_for_files (NolphinView *view, GList *files,
				      GArray *relative_item_points)
{
	GList *uris;
	char *dir_uri;
	CopyMoveDoneData *copy_move_done_data;
	g_assert (relative_item_points->len == 0
		  || g_list_length (files) == relative_item_points->len);

        g_assert (NOLPHIN_IS_VIEW (view));
        g_assert (files != NULL);

	/* create a list of URIs */
	uris = NULL;
	g_list_foreach (files, prepend_uri_one, &uris);
	uris = g_list_reverse (uris);

        g_assert (g_list_length (uris) == g_list_length (files));

	/* offset the drop locations a bit so that we don't pile
	 * up the icons on top of each other
	 */
	offset_drop_points (relative_item_points,
			    DUPLICATE_HORIZONTAL_ICON_OFFSET,
			    DUPLICATE_VERTICAL_ICON_OFFSET);

        copy_move_done_data = pre_copy_move (view);
	dir_uri = nolphin_view_get_backing_uri (view);
	nolphin_file_operations_copy_move (uris, relative_item_points, dir_uri, GDK_ACTION_LINK,
					    GTK_WIDGET (view), copy_move_done_callback, copy_move_done_data);
	g_free (dir_uri);
	g_list_free_full (uris, g_free);
}

static void
nolphin_view_duplicate_selection (NolphinView *view, GList *files,
				   GArray *relative_item_points)
{
	GList *uris;
	CopyMoveDoneData *copy_move_done_data;

        g_assert (NOLPHIN_IS_VIEW (view));
        g_assert (files != NULL);
	g_assert (g_list_length (files) == relative_item_points->len
		  || relative_item_points->len == 0);

	/* create a list of URIs */
	uris = NULL;
	g_list_foreach (files, prepend_uri_one, &uris);
	uris = g_list_reverse (uris);

        g_assert (g_list_length (uris) == g_list_length (files));

	/* offset the drop locations a bit so that we don't pile
	 * up the icons on top of each other
	 */
	offset_drop_points (relative_item_points,
			    DUPLICATE_HORIZONTAL_ICON_OFFSET,
			    DUPLICATE_VERTICAL_ICON_OFFSET);

        copy_move_done_data = pre_copy_move (view);
	nolphin_file_operations_copy_move (uris, relative_item_points, NULL, GDK_ACTION_COPY,
					    GTK_WIDGET (view), copy_move_done_callback, copy_move_done_data);
	g_list_free_full (uris, g_free);
}

/* special_link_in_selection
 *
 * Return TRUE if one of our special links is in the selection.
 * Special links include the following:
 *	 NOLPHIN_DESKTOP_LINK_TRASH, NOLPHIN_DESKTOP_LINK_HOME, NOLPHIN_DESKTOP_LINK_MOUNT
 */

static gboolean
special_link_in_selection (NolphinView *view, GList *selection)
{
	gboolean saw_link;
	GList *node;
	NolphinFile *file;

	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	saw_link = FALSE;

	for (node = selection; node != NULL; node = node->next) {
		file = NOLPHIN_FILE (node->data);

		saw_link = NOLPHIN_IS_DESKTOP_ICON_FILE (file);

		if (saw_link) {
			break;
		}
	}

	return saw_link;
}

/* desktop_or_home_dir_in_selection
 *
 * Return TRUE if either the desktop or the home directory is in the selection.
 */

static gboolean
desktop_or_home_dir_in_selection (NolphinView *view, GList *selection)
{
	gboolean saw_desktop_or_home_dir;
	GList *node;
	NolphinFile *file;

	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	saw_desktop_or_home_dir = FALSE;

	for (node = selection; node != NULL; node = node->next) {
		file = NOLPHIN_FILE (node->data);

		saw_desktop_or_home_dir =
			nolphin_file_is_home (file)
			|| nolphin_file_is_desktop_directory (file);

		if (saw_desktop_or_home_dir) {
			break;
		}
	}

	return saw_desktop_or_home_dir;
}

/* directory_in_selection
 *
 * Return TRUE if selection contains a directory.
 */

static gboolean
directory_in_selection (NolphinView *view, GList *selection)
{
    gboolean has_dir;
    GList *node;
    NolphinFile *file;

    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

    has_dir = FALSE;

    for (node = selection; node != NULL; node = node->next) {
        file = NOLPHIN_FILE (node->data);

        has_dir = nolphin_file_is_directory (file);
        if (has_dir) {
            break;
        }
    }

    return has_dir;
}

static void
trash_or_delete_done_cb (GHashTable *debuting_uris,
			 gboolean user_cancel,
			 NolphinView *view)
{
	if (user_cancel) {
		view->details->selection_was_removed = FALSE;
	}
}

static void
trash_or_delete_files (GtkWindow *parent_window,
		       const GList *files,
		       gboolean delete_if_all_already_in_trash,
		       NolphinView *view)
{
	GList *locations;
	const GList *node;

	locations = NULL;
	for (node = files; node != NULL; node = node->next) {
		locations = g_list_prepend (locations,
					    nolphin_file_get_location ((NolphinFile *) node->data));
	}

	locations = g_list_reverse (locations);

	nolphin_file_operations_trash_or_delete (locations,
						  parent_window,
						  (NolphinDeleteCallback) trash_or_delete_done_cb,
						  view);
	g_list_free_full (locations, g_object_unref);
}

static gboolean
can_rename_file (NolphinView *view, NolphinFile *file)
{
	return nolphin_file_can_rename (file);
}

gboolean
nolphin_view_get_is_renaming (NolphinView *view)
{
	return view->details->is_renaming;
}

void
nolphin_view_set_is_renaming (NolphinView *view,
			       gboolean      is_renaming)
{
	view->details->is_renaming = is_renaming;
}

static void
start_renaming_file (NolphinView *view,
		     NolphinFile *file,
		     gboolean select_all)
{
	view->details->is_renaming = TRUE;

	if (file !=  NULL) {
		nolphin_view_select_file (view, file);
	}
}

static void
update_context_menu_position_from_event (NolphinView *view,
					 GdkEventButton  *event)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	if (event != NULL) {
		view->details->context_menu_position.x = event->x;
		view->details->context_menu_position.y = event->y;
	} else {
		view->details->context_menu_position.x = -1;
		view->details->context_menu_position.y = -1;
	}
}

/* handle the open command */

static void
open_one_in_new_window (gpointer data, gpointer callback_data)
{
	g_assert (NOLPHIN_IS_FILE (data));
	g_assert (NOLPHIN_IS_VIEW (callback_data));

	nolphin_view_activate_file (NOLPHIN_VIEW (callback_data),
				     NOLPHIN_FILE (data),
				     NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

NolphinFile *
nolphin_view_get_directory_as_file (NolphinView *view)
{
	g_assert (NOLPHIN_IS_VIEW (view));

	return view->details->directory_as_file;
}

static void
open_with_launch_application_callback (GtkAction *action,
				       gpointer callback_data)
{
	ApplicationLaunchParameters *launch_parameters;

	launch_parameters = (ApplicationLaunchParameters *) callback_data;
	nolphin_launch_application
		(launch_parameters->application,
		 launch_parameters->files,
		 nolphin_view_get_containing_window (launch_parameters->directory_view));
}

static char *
escape_action_name (const char *action_name,
		    const char *prefix)
{
	GString *s;

	if (action_name == NULL) {
		return NULL;
	}

	s = g_string_new (prefix);

	while (*action_name != 0) {
		switch (*action_name) {
		case '\\':
			g_string_append (s, "\\\\");
			break;
		case '/':
			g_string_append (s, "\\s");
			break;
		case '&':
			g_string_append (s, "\\a");
			break;
		case '"':
			g_string_append (s, "\\q");
			break;
		default:
			g_string_append_c (s, *action_name);
		}

		action_name ++;
	}
	return g_string_free (s, FALSE);
}

static char *
escape_action_path (const char *action_path)
{
	GString *s;

	if (action_path == NULL) {
		return NULL;
	}

	s = g_string_sized_new (strlen (action_path) + 2);

	while (*action_path != 0) {
		switch (*action_path) {
		case '\\':
			g_string_append (s, "\\\\");
			break;
		case '&':
			g_string_append (s, "\\a");
			break;
		case '"':
			g_string_append (s, "\\q");
			break;
		default:
			g_string_append_c (s, *action_path);
		}

		action_path ++;
	}
	return g_string_free (s, FALSE);
}


static void
add_submenu (GtkUIManager *ui_manager,
	     GtkActionGroup *action_group,
	     guint merge_id,
	     const char *parent_path,
	     const char *uri,
	     const char *label,
	     GdkPixbuf *pixbuf,
	     gboolean add_action)
{
	char *escaped_label;
	char *action_name;
	char *submenu_name;
	char *escaped_submenu_name;
	GtkAction *action;

	if (parent_path != NULL) {
		action_name = escape_action_name (uri, "submenu_");
		submenu_name = g_path_get_basename (uri);
		escaped_submenu_name = escape_action_path (submenu_name);
		escaped_label = eel_str_double_underscores (label);

		if (add_action) {
			action = gtk_action_new (action_name,
						 escaped_label,
						 NULL,
						 NULL);
			if (pixbuf != NULL) {
				gtk_action_set_gicon (action, G_ICON (pixbuf));
			}

			g_object_set (action, "hide-if-empty", FALSE, NULL);

			gtk_action_group_add_action (action_group,
						     action);
			g_object_unref (action);
		}

		gtk_ui_manager_add_ui (ui_manager,
				       merge_id,
				       parent_path,
				       escaped_submenu_name,
				       action_name,
				       GTK_UI_MANAGER_MENU,
				       FALSE);
		g_free (action_name);
		g_free (escaped_label);
		g_free (submenu_name);
		g_free (escaped_submenu_name);
	}
}

static void
menu_item_show_image (GtkUIManager *ui_manager,
		      const char   *parent_path,
		      const char   *action_name,
                gboolean    ignore_gtk_pref)
{
    GtkImageMenuItem *menuitem;
    char *path;
    gboolean show = TRUE;

    path = g_strdup_printf ("%s/%s", parent_path, action_name);
    menuitem = GTK_IMAGE_MENU_ITEM (gtk_ui_manager_get_widget (ui_manager,
                                                               path));
    g_free (path);

    if (!ignore_gtk_pref) {
        g_object_get (gtk_settings_get_default (), "gtk-menu-images", &show, NULL);

        if (!show && !gtk_image_menu_item_get_always_show_image (menuitem)) {
            return;
        }
    }

    if (menuitem != NULL) {
        gtk_image_menu_item_set_always_show_image (menuitem, show);
    }
}

static void
add_application_to_open_with_menu (NolphinView *view,
				   GAppInfo *application,
				   GList *files,
				   int index,
				   const char *menu_placeholder,
				   const char *popup_placeholder,
				   const gboolean submenu)
{
	ApplicationLaunchParameters *launch_parameters;
	char *tip;
	char *label;
	char *action_name;
	char *escaped_app;
	GtkAction *action;
	GIcon *app_icon;
	GtkUIManager *ui_manager;

	launch_parameters = application_launch_parameters_new
		(application, files, view);
	escaped_app = eel_str_double_underscores (g_app_info_get_name (application));
	if (submenu)
		label = g_strdup_printf ("%s", escaped_app);
	else
		label = g_strdup_printf (_("Mit %s öffnen"), escaped_app);

	tip = g_strdup_printf (ngettext ("»%s« verwenden, um gewähltes Objekt zu öffnen",
					 "»%s« verwenden, um gewählte Objekte zu öffnen",
					 g_list_length (files)),
			       escaped_app);
	g_free (escaped_app);

	action_name = g_strdup_printf ("open_with_%d", index);

	action = gtk_action_new (action_name,
				 label,
				 tip,
				 NULL);

	app_icon = g_app_info_get_icon (application);
	if (app_icon != NULL) {
		g_object_ref (app_icon);
	} else {
		app_icon = g_themed_icon_new ("application-x-executable");
	}

	gtk_action_set_gicon (action, app_icon);
	g_object_unref (app_icon);

	g_signal_connect_data (action, "activate",
			       G_CALLBACK (open_with_launch_application_callback),
			       launch_parameters,
			       (GClosureNotify)application_launch_parameters_free, 0);

	gtk_action_group_add_action (view->details->open_with_action_group,
				     action);
	g_object_unref (action);

	ui_manager = nolphin_window_get_ui_manager (view->details->window);
	gtk_ui_manager_add_ui (ui_manager,
			       view->details->open_with_merge_id,
			       menu_placeholder,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	menu_item_show_image (ui_manager, menu_placeholder, action_name, TRUE);

	gtk_ui_manager_add_ui (ui_manager,
			       view->details->open_with_merge_id,
			       popup_placeholder,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	menu_item_show_image (ui_manager, popup_placeholder, action_name, TRUE);

	g_free (action_name);
	g_free (label);
	g_free (tip);
}

static void
get_x_content_async_callback (const char **content,
			      gpointer user_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (user_data);

	if (view->details->window != NULL) {
		schedule_update_menus (view);
	}
	g_object_unref (view);
}

static void
add_x_content_apps (NolphinView *view, NolphinFile *file, GList **applications)
{
	GMount *mount;
	char **x_content_types;
	unsigned int n;

	g_return_if_fail (applications != NULL);

	mount = nolphin_file_get_mount (file);

	if (mount == NULL) {
		return;
	}

	x_content_types = nolphin_get_cached_x_content_types_for_mount (mount);
	if (x_content_types != NULL) {
		for (n = 0; x_content_types[n] != NULL; n++) {
			char *x_content_type = x_content_types[n];
			GList *app_info_for_x_content_type;

			app_info_for_x_content_type = g_app_info_get_all_for_type (x_content_type);
			*applications = g_list_concat (*applications, app_info_for_x_content_type);
		}
		g_strfreev (x_content_types);
	} else {
		nolphin_get_x_content_types_for_mount_async (mount,
							      get_x_content_async_callback,
							      NULL,
							      g_object_ref (view));

	}

	g_object_unref (mount);
}

static void
reset_open_with_menu (NolphinView *view, GList *selection, gboolean filter_default)
{
	GList *applications, *node;
	NolphinFile *file;
	gboolean submenu_visible;
	int num_applications;
	int index;
	gboolean other_applications_visible;
	gboolean open_with_chooser_visible;
	GtkUIManager *ui_manager;
	GtkAction *action;
	GAppInfo *default_app;

	/* Clear any previous inserted items in the applications and viewers placeholders */

	ui_manager = nolphin_window_get_ui_manager (view->details->window);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->open_with_merge_id,
				&view->details->open_with_action_group);

	nolphin_ui_prepare_merge_ui (ui_manager,
				      "OpenWithGroup",
				      &view->details->open_with_merge_id,
				      &view->details->open_with_action_group);

	other_applications_visible = (selection != NULL);

	for (node = selection; node != NULL; node = node->next) {

		file = NOLPHIN_FILE (node->data);

		other_applications_visible &= (!nolphin_mime_file_opens_in_view (file) ||
                                        nolphin_file_is_directory (file));
	}

	default_app = NULL;
	if (filter_default && selection != NULL) {
		default_app = nolphin_mime_get_default_application_for_files (selection);
	}

	applications = NULL;
	if (other_applications_visible) {
		applications = nolphin_mime_get_applications_for_files (selection);
	}

	if (g_list_length (selection) == 1) {
		add_x_content_apps (view, NOLPHIN_FILE (selection->data), &applications);
	}


	num_applications = g_list_length (applications);

	if (file_list_all_are_folders (selection)) {
		submenu_visible = (num_applications > 0);
	} else {
		submenu_visible = (num_applications > 1);
	}

	for (node = applications, index = 0; node != NULL; node = node->next, index++) {
		GAppInfo *application;
		char *menu_path;
		char *popup_path;

		application = node->data;

		if (default_app != NULL && g_app_info_equal (default_app, application)) {
			continue;
		}

		if (submenu_visible) {
			menu_path = (char *)NOLPHIN_VIEW_MENU_PATH_APPLICATIONS_SUBMENU_PLACEHOLDER;
			popup_path = (char *)NOLPHIN_VIEW_POPUP_PATH_APPLICATIONS_SUBMENU_PLACEHOLDER;
		} else {
			menu_path = (char *)NOLPHIN_VIEW_MENU_PATH_APPLICATIONS_PLACEHOLDER;
			popup_path = (char *)NOLPHIN_VIEW_POPUP_PATH_APPLICATIONS_PLACEHOLDER;
		}

		gtk_ui_manager_add_ui (nolphin_window_get_ui_manager (view->details->window),
				       view->details->open_with_merge_id,
				       menu_path,
				       "separator",
				       NULL,
				       GTK_UI_MANAGER_SEPARATOR,
				       FALSE);

		add_application_to_open_with_menu (view,
						   node->data,
						   selection,
						   index,
						   menu_path, popup_path, submenu_visible);
	}
	g_list_free_full (applications, g_object_unref);
	if (default_app != NULL) {
		g_object_unref (default_app);
	}

	open_with_chooser_visible = other_applications_visible &&
		g_list_length (selection) == 1;

	if (submenu_visible) {
		action = gtk_action_group_get_action (view->details->dir_action_group,
						      NOLPHIN_ACTION_OTHER_APPLICATION1);
		gtk_action_set_visible (action, open_with_chooser_visible);
		action = gtk_action_group_get_action (view->details->dir_action_group,
						      NOLPHIN_ACTION_OTHER_APPLICATION2);
		gtk_action_set_visible (action, FALSE);
	} else {
		action = gtk_action_group_get_action (view->details->dir_action_group,
						      NOLPHIN_ACTION_OTHER_APPLICATION1);
		gtk_action_set_visible (action, FALSE);
		action = gtk_action_group_get_action (view->details->dir_action_group,
						      NOLPHIN_ACTION_OTHER_APPLICATION2);
		gtk_action_set_visible (action, open_with_chooser_visible);
	}
}

static void
move_copy_selection_to_location (NolphinView *view,
                 int copy_action,
                 char *target_uri)
{
    GList *selection, *uris, *l;

    selection = nolphin_view_get_selection_for_file_transfer (view);
    if (selection == NULL) {
        return;
    }

    uris = NULL;
    for (l = selection; l != NULL; l = l->next) {
        uris = g_list_prepend (uris,
                       nolphin_file_get_uri ((NolphinFile *) l->data));
    }
    uris = g_list_reverse (uris);

    nolphin_view_move_copy_items (view, uris, NULL, target_uri,
                       copy_action,
                       0, 0);

    g_list_free_full (uris, g_free);
    nolphin_file_list_free (selection);
}

static void
action_move_bookmark_callback (GtkAction *action, gpointer callback_data)
{
    NolphinView *view;
    BookmarkCallbackData *data;

    data = (BookmarkCallbackData *) callback_data;
    view = NOLPHIN_VIEW(data->view);
    move_copy_selection_to_location (view, GDK_ACTION_MOVE, data->dest_uri);
}

static void
action_copy_bookmark_callback (GtkAction *action, gpointer callback_data)
{
    NolphinView *view;
    BookmarkCallbackData *data;

    data = (BookmarkCallbackData *) callback_data;
    view = NOLPHIN_VIEW(data->view);
    move_copy_selection_to_location (view, GDK_ACTION_COPY, data->dest_uri);
}

static void
setup_bookmark_action(      char *action_name,
                      const char *bookmark_name,
                      const char *icon_name,
                            char *mount_uri,
                    GtkUIManager *ui_manager,
                         gboolean move,
                  GtkActionGroup *action_group,
                             gint merge_id,
                            const char *path,
                        NolphinView *view)
{

    GtkAction *action;
    action = gtk_action_new (action_name,
             bookmark_name,
             NULL,
             NULL);
    gtk_action_set_icon_name (action, icon_name);

    if (move) {
        g_signal_connect_data (action, "activate",
               G_CALLBACK (action_move_bookmark_callback),
               bookmark_callback_data_new(view, mount_uri),
               (GClosureNotify)bookmark_callback_data_free, 0);
    } else {
        g_signal_connect_data (action, "activate",
               G_CALLBACK (action_copy_bookmark_callback),
               bookmark_callback_data_new(view, mount_uri),
               (GClosureNotify)bookmark_callback_data_free, 0);
    }

    gtk_action_group_add_action (action_group, action);

    gtk_ui_manager_add_ui ( ui_manager,
                            merge_id,
                            path,
                            action_name,
                            action_name,
                            GTK_UI_MANAGER_MENUITEM,
                            FALSE);
    g_object_unref (action);
    g_free (action_name);
}

static void
add_bookmark_to_action (NolphinView *view, const gchar *bookmark_name, const gchar *icon_name, gchar *mount_uri, gint index)
{
    GtkUIManager *ui_manager;
    ui_manager = nolphin_window_get_ui_manager (view->details->window);

    setup_bookmark_action(g_strdup_printf ("BM_MOVETO_POPUP_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            TRUE,
                                            view->details->copy_move_action_groups[0],
                                            view->details->copy_move_merge_ids[0],
                                            NOLPHIN_VIEW_POPUP_PATH_BOOKMARK_MOVETO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("BM_COPYTO_POPUP_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            FALSE,
                                            view->details->copy_move_action_groups[1],
                                            view->details->copy_move_merge_ids[1],
                                            NOLPHIN_VIEW_POPUP_PATH_BOOKMARK_COPYTO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("BM_MOVETO_MENU_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            TRUE,
                                            view->details->copy_move_action_groups[2],
                                            view->details->copy_move_merge_ids[2],
                                            NOLPHIN_VIEW_MENU_PATH_BOOKMARK_MOVETO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("BM_COPYTO_MENU_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            FALSE,
                                            view->details->copy_move_action_groups[3],
                                            view->details->copy_move_merge_ids[3],
                                            NOLPHIN_VIEW_MENU_PATH_BOOKMARK_COPYTO_ENTRIES_PLACEHOLDER,
                                            view);
}

static void
add_place_to_action (NolphinView *view, const gchar *bookmark_name, const gchar *icon_name, gchar *mount_uri, gint index)
{
    GtkUIManager *ui_manager;
    ui_manager = nolphin_window_get_ui_manager (view->details->window);

    setup_bookmark_action(g_strdup_printf ("PLACE_MOVETO_POPUP_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            TRUE,
                                            view->details->copy_move_action_groups[0],
                                            view->details->copy_move_merge_ids[0],
                                            NOLPHIN_VIEW_POPUP_PATH_PLACES_MOVETO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("PLACE_COPYTO_POPUP_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            FALSE,
                                            view->details->copy_move_action_groups[1],
                                            view->details->copy_move_merge_ids[1],
                                            NOLPHIN_VIEW_POPUP_PATH_PLACES_COPYTO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("PLACE_MOVETO_MENU_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            TRUE,
                                            view->details->copy_move_action_groups[2],
                                            view->details->copy_move_merge_ids[2],
                                            NOLPHIN_VIEW_MENU_PATH_PLACES_MOVETO_ENTRIES_PLACEHOLDER,
                                            view);

    setup_bookmark_action(g_strdup_printf ("PLACE_COPYTO_MENU_%d", index),
                                            bookmark_name,
                                            icon_name,
                                            mount_uri,
                                            ui_manager,
                                            FALSE,
                                            view->details->copy_move_action_groups[3],
                                            view->details->copy_move_merge_ids[3],
                                            NOLPHIN_VIEW_MENU_PATH_PLACES_COPYTO_ENTRIES_PLACEHOLDER,
                                            view);
}

static void
reset_move_copy_to_menu (NolphinView *view)
{
    NolphinBookmark *bookmark;
    NolphinFile *file;
    int bookmark_count, index;
    GtkUIManager *ui_manager;
    GFile *root;
    const gchar *bookmark_name;
    gchar *icon_name;
    char *mount_uri;

    ui_manager = nolphin_window_get_ui_manager (view->details->window);

    int i;

    for (i = 0; i < 4; i++) {
        nolphin_ui_unmerge_ui (ui_manager,
                &view->details->copy_move_merge_ids[i],
                &view->details->copy_move_action_groups[i]);
        gchar *id = g_strdup_printf ("MoveCopyMenuGroup_%d", i);
        nolphin_ui_prepare_merge_ui (ui_manager,
                                  id,
                                  &view->details->copy_move_merge_ids[i],
                                  &view->details->copy_move_action_groups[i]);
        g_free (id);
    }

    GtkAction *action;

    mount_uri = nolphin_get_home_directory_uri ();
    file = nolphin_file_get_by_uri (mount_uri);
    g_free (mount_uri);

    icon_name = nolphin_file_get_control_icon_name (file);

    action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_COPY_TO_HOME);
    gtk_action_set_icon_name (action, icon_name);

    action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_MOVE_TO_HOME);
    gtk_action_set_icon_name (action, icon_name);

    g_clear_pointer (&icon_name, g_free);
    nolphin_file_unref (file);

    mount_uri = nolphin_get_desktop_directory_uri ();
    file = nolphin_file_get_by_uri (mount_uri);
    g_free (mount_uri);

    icon_name = nolphin_file_get_control_icon_name (file);

    action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_COPY_TO_DESKTOP);
    gtk_action_set_icon_name (action, icon_name);

    action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_MOVE_TO_DESKTOP);
    gtk_action_set_icon_name (action, icon_name);

    g_clear_pointer (&icon_name, g_free);
    nolphin_file_unref (file);

    if (view->details->showing_bookmarks_in_to_menus) {
        bookmark_count = nolphin_bookmark_list_length (view->details->bookmarks);
        for (index = 0; index < bookmark_count; ++index) {
            bookmark = nolphin_bookmark_list_item_at (view->details->bookmarks, index);

            /* Unlike the sidebar or the bookmarks menu, we are using bookmarks as
               file operation targets, not locations to open.  As such, we should
               never show unmounted/invalid locations in the move-to/copy-to menu */
            if (!nolphin_bookmark_uri_get_exists (bookmark)) {
                continue;
            }

            root = nolphin_bookmark_get_location (bookmark);
            file = nolphin_file_get (root);

            nolphin_file_unref (file);

            bookmark_name = nolphin_bookmark_get_name (bookmark);
            icon_name = nolphin_bookmark_get_icon_name (bookmark);
            mount_uri = nolphin_bookmark_get_uri (bookmark);

            add_bookmark_to_action (view,
                                    bookmark_name,
                                    icon_name,
                                    mount_uri,
                                    index);

            g_object_unref (root);
            g_free (icon_name);
            g_free (mount_uri);
        }
    }

    if (view->details->showing_places_in_to_menus) {

        GList *mounts, *l, *ll, *drives, *volumes;
        GVolumeMonitor *volume_monitor;
        GMount *mount;
        GVolume *volume;
        GDrive *drive;
        GList *network_mounts = NULL;
        GList *network_volumes = NULL;
        gchar *name, *identifier;
        index = 0;

        /* add mounts that has no volume (/etc/mtab mounts, ftp, sftp,...) */
        volume_monitor = g_volume_monitor_get ();
        mounts = g_volume_monitor_get_mounts (volume_monitor);

        for (l = mounts; l != NULL; l = l->next) {
            mount = l->data;
            if (g_mount_is_shadowed (mount)) {
                g_object_unref (mount);
                continue;
            }
            volume = g_mount_get_volume (mount);
            if (volume != NULL) {
                    g_object_unref (volume);
                g_object_unref (mount);
                continue;
            }
            root = g_mount_get_default_location (mount);

            if (!g_file_is_native (root)) {
                gboolean really_network = TRUE;
                gchar *path = g_file_get_path (root);
                if (!path) {
                    network_mounts = g_list_prepend (network_mounts, mount);
                    g_object_unref (root);
                    continue;
                }
                gchar *escaped1 = g_uri_unescape_string (path, "");
                gchar *escaped2 = g_uri_unescape_string (escaped1, "");
                gchar *ptr = g_strrstr (escaped2, "file://");
                if (ptr != NULL) {
                    GFile *actual_file = g_file_new_for_uri (ptr);
                    if (g_file_is_native(actual_file)) {
                        really_network = FALSE;
                    }
                    g_object_unref(actual_file);
                }
                g_free (path);
                g_free (escaped1);
                g_free (escaped2);
                if (really_network) {
                    network_mounts = g_list_prepend (network_mounts, mount);
                    g_object_unref (root);
                    continue;
                }
            }

            icon_name = nolphin_get_mount_icon_name (mount);
            mount_uri = g_file_get_uri (root);
            name = g_mount_get_name (mount);

            add_place_to_action (view,
                                 name,
                                 icon_name,
                                 mount_uri,
                                 index);

            g_object_unref (root);
            g_object_unref (mount);
            g_free (icon_name);
            g_free (name);
            g_free (mount_uri);

            index++;
        }
        g_list_free (mounts);

        /* first go through all connected drives */
        drives = g_volume_monitor_get_connected_drives (volume_monitor);

        for (l = drives; l != NULL; l = l->next) {
            drive = l->data;

            volumes = g_drive_get_volumes (drive);
            if (volumes != NULL) {
                for (ll = volumes; ll != NULL; ll = ll->next) {
                    volume = ll->data;
                    identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

                    if (g_strcmp0 (identifier, "network") == 0) {
                        g_free (identifier);
                        network_volumes = g_list_prepend (network_volumes, volume);
                        continue;
                    }
                    g_free (identifier);

                    mount = g_volume_get_mount (volume);
                    if (mount != NULL) {
                        /* Show mounted volume in the sidebar */
                        icon_name = nolphin_get_mount_icon_name (mount);
                        root = g_mount_get_default_location (mount);
                        mount_uri = g_file_get_uri (root);
                        name = g_mount_get_name (mount);

                        add_place_to_action (view,
                                             name,
                                             icon_name,
                                             mount_uri,
                                             index);

                        g_object_unref (root);
                        g_object_unref (mount);
                        g_free (icon_name);
                        g_free (name);
                        g_free (mount_uri);
                        index++;
                    }
                    g_object_unref (volume);
                }
                g_list_free (volumes);
            }
            g_object_unref (drive);
        }
        g_list_free (drives);

        /* add all volumes that is not associated with a drive */
        volumes = g_volume_monitor_get_volumes (volume_monitor);
        for (l = volumes; l != NULL; l = l->next) {
            volume = l->data;
            drive = g_volume_get_drive (volume);
            if (drive != NULL) {
                    g_object_unref (volume);
                g_object_unref (drive);
                continue;
            }

            identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

            if (g_strcmp0 (identifier, "network") == 0) {
                g_free (identifier);
                network_volumes = g_list_prepend (network_volumes, volume);
                continue;
            }
            g_free (identifier);

            mount = g_volume_get_mount (volume);
            if (mount != NULL) {
                icon_name = nolphin_get_mount_icon_name (mount);
                root = g_mount_get_default_location (mount);
                mount_uri = g_file_get_uri (root);

                g_object_unref (root);
                name = g_mount_get_name (mount);

                add_place_to_action (view,
                                     name,
                                     icon_name,
                                     mount_uri,
                                     index);

                g_object_unref (mount);
                g_free (icon_name);
                g_free (name);
                g_free (mount_uri);
                index++;
            }
            g_object_unref (volume);
        }
        g_list_free (volumes);
        g_object_unref (volume_monitor);

        network_volumes = g_list_reverse (network_volumes);
        for (l = network_volumes; l != NULL; l = l->next) {
            volume = l->data;
            mount = g_volume_get_mount (volume);

            if (mount != NULL) {
                network_mounts = g_list_prepend (network_mounts, mount);
                continue;
            }
        }

        g_list_free_full (network_volumes, g_object_unref);

        network_mounts = g_list_reverse (network_mounts);
        for (l = network_mounts; l != NULL; l = l->next) {
            mount = l->data;
            root = g_mount_get_default_location (mount);
            icon_name = nolphin_get_mount_icon_name (mount);
            mount_uri = g_file_get_uri (root);
            name = g_mount_get_name (mount);

            add_place_to_action (view,
                                 name,
                                 icon_name,
                                 mount_uri,
                                 index);

            g_object_unref (root);
            g_free (icon_name);
            g_free (name);
            g_free (mount_uri);
            index++;
        }

        g_list_free_full (network_mounts, g_object_unref);
    }
}

static void
disconnect_bookmark (gpointer data, gpointer callback_data)
{
    GtkAction *action = GTK_ACTION (data);
    g_signal_handlers_disconnect_matched (action,
                          G_SIGNAL_MATCH_FUNC, 0, 0,
                          NULL, action_move_bookmark_callback, NULL);
    g_signal_handlers_disconnect_matched (action,
                          G_SIGNAL_MATCH_FUNC, 0, 0,
                          NULL, action_copy_bookmark_callback, NULL);
}

static void
disconnect_bookmark_signals (NolphinView *view)
{
    int i;
    GList *list;
    GtkActionGroup *group;
    for (i = 0; i < 4; i++) {
        group = GTK_ACTION_GROUP (view->details->copy_move_action_groups[i]);
        list = gtk_action_group_list_actions (group);
        g_list_foreach (list, disconnect_bookmark, NULL);
        g_list_free (list);
    }
}

static GList *
get_all_extension_menu_items (GtkWidget *window,
			      GList *selection)
{
	GList *items;
	GList *providers;
	GList *l;

	providers = nolphin_module_get_extensions_for_type (NOLPHIN_TYPE_MENU_PROVIDER);
	items = NULL;

	for (l = providers; l != NULL; l = l->next) {
		NolphinMenuProvider *provider;
		GList *file_items;

		provider = NOLPHIN_MENU_PROVIDER (l->data);
		file_items = nolphin_menu_provider_get_file_items (provider,
								    window,
								    selection);
		items = g_list_concat (items, file_items);
	}

	nolphin_module_extension_list_free (providers);

	return items;
}

typedef struct
{
	NolphinMenuItem *item;
	NolphinView *view;
	GList *selection;
	GtkAction *action;
} ExtensionActionCallbackData;


static void
extension_action_callback_data_free (ExtensionActionCallbackData *data)
{
	g_object_unref (data->item);
	nolphin_file_list_free (data->selection);

	g_free (data);
}

static gboolean
search_in_menu_items (GList* items, const char *item_name)
{
	GList* list;

	for (list = items; list != NULL; list = list->next) {
		NolphinMenu* menu;
		char *name;

		g_object_get (list->data, "name", &name, NULL);
		if (strcmp (name, item_name) == 0) {
			g_free (name);
			return TRUE;
		}
		g_free (name);

		menu = NULL;
		g_object_get (list->data, "menu", &menu, NULL);
		if (menu != NULL) {
			gboolean ret;
			GList* submenus;

			submenus = nolphin_menu_get_items (menu);
			ret = search_in_menu_items (submenus, item_name);
			nolphin_menu_item_list_free (submenus);
			g_object_unref (menu);
			if (ret) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

static void
extension_action_callback (GtkAction *action,
			   gpointer callback_data)
{
	ExtensionActionCallbackData *data;
	char *item_name;
	gboolean is_valid;
	GList *l;
	GList *items;

	data = callback_data;

	/* Make sure the selected menu item is valid for the final sniffed
	 * mime type */
	g_object_get (data->item, "name", &item_name, NULL);
	items = get_all_extension_menu_items (gtk_widget_get_toplevel (GTK_WIDGET (data->view)),
					      data->selection);

	is_valid = search_in_menu_items (items, item_name);

	for (l = items; l != NULL; l = l->next) {
		g_object_unref (l->data);
	}
	g_list_free (items);

	g_free (item_name);

	if (is_valid) {
		nolphin_menu_item_activate (data->item);
	}
}

static GdkPixbuf *
get_menu_icon_for_file (NolphinFile *file,
                        GtkWidget *widget)
{
	NolphinIconInfo *info;
	GdkPixbuf *pixbuf;
	int size, scale;

	size = nolphin_get_icon_size_for_stock_size (GTK_ICON_SIZE_MENU);
    scale = gtk_widget_get_scale_factor (widget);

	info = nolphin_file_get_icon (file, size, 0, scale, 0);
	pixbuf = nolphin_icon_info_get_pixbuf_nodefault_at_size (info, size);
	nolphin_icon_info_unref (info);

	return pixbuf;
}

static GtkAction *
add_extension_action_for_files (NolphinView *view,
				NolphinMenuItem *item,
				GList *files)
{
    GtkAction *ret = NULL;
	char *name, *label, *tip, *icon;
	gboolean sensitive, priority;
    gboolean separator;
	GtkAction *action;
    GtkWidget *widget_a, *widget_b;
	ExtensionActionCallbackData *data;

	g_object_get (G_OBJECT (item),
		      "name", &name, "label", &label,
		      "tip", &tip, "icon", &icon,
		      "sensitive", &sensitive,
		      "priority", &priority,
              "widget-a", &widget_a,
              "widget-b", &widget_b,
              "separator", &separator,
		      NULL);

    if (widget_a == NULL && !separator) {
        action = gtk_action_new (name,
                                 label,
                                 tip,
                                 NULL);
        if (icon != NULL) {
            GIcon *gicon;

            if (g_path_is_absolute (icon)) {
                GFile *file;

                file = g_file_new_for_path (icon);
                gicon = g_file_icon_new (file);

                g_object_unref (file);
            } else if (icon) {
                gicon = g_themed_icon_new (icon);
            } else {
                gicon = NULL;
            }

            gtk_action_set_gicon (action, gicon);

            g_clear_object (&gicon);
        }
    } else if (separator) {
        action = nolphin_separator_action_new (name);
    } else {
        action = nolphin_widget_action_new (name, widget_a, widget_b);
    }

	gtk_action_set_sensitive (action, sensitive);
	g_object_set (action, "is-important", priority, NULL);

    // FIXME: Don't add the selection to the action data.
    // Just grab the selection in the activation callback.
	data = g_new0 (ExtensionActionCallbackData, 1);
	data->item = g_object_ref (item);
	data->view = view;
	data->selection = nolphin_file_list_copy (files);
	data->action = action;

	g_signal_connect_data (action, "activate",
			       G_CALLBACK (extension_action_callback),
			       data,
			       (GClosureNotify)extension_action_callback_data_free, 0);

    if (gtk_action_group_get_action (view->details->extensions_menu_action_group, gtk_action_get_name (GTK_ACTION (action))) == NULL) {
        gtk_action_group_add_action (view->details->extensions_menu_action_group,
                                     GTK_ACTION (action));
        ret = action;
    }

	g_object_unref (action);

	g_free (name);
	g_free (label);
	g_free (tip);
	g_free (icon);

	return ret;
}

static void
add_extension_menu_items (NolphinView *view,
			  GList *files,
			  GList *menu_items,
			  const char *subdirectory)
{
	GtkUIManager *ui_manager;
	GList *l;

	ui_manager = nolphin_window_get_ui_manager (view->details->window);

	for (l = menu_items; l; l = l->next) {
		NolphinMenuItem *item;
		NolphinMenu *menu;
		GtkAction *action = NULL;
		char *path;

		item = NOLPHIN_MENU_ITEM (l->data);

		g_object_get (item, "menu", &menu, NULL);

		action = add_extension_action_for_files (view, item, files);

        if (action) {
            GtkUIManagerItemType item_type;

            if (G_OBJECT_TYPE (action) == NOLPHIN_TYPE_SEPARATOR_ACTION)
                item_type = GTK_UI_MANAGER_SEPARATOR;
            else {
                item_type = (menu != NULL) ? GTK_UI_MANAGER_MENU : GTK_UI_MANAGER_MENUITEM;
            }
    		path = g_build_path ("/", NOLPHIN_VIEW_POPUP_PATH_EXTENSION_ACTIONS, subdirectory, NULL);
    		gtk_ui_manager_add_ui (ui_manager,
    				       view->details->extensions_menu_merge_id,
    				       path,
    				       gtk_action_get_name (action),
    				       gtk_action_get_name (action),
    				       item_type,
    				       FALSE);
    		g_free (path);

    		path = g_build_path ("/", NOLPHIN_VIEW_MENU_PATH_EXTENSION_ACTIONS_PLACEHOLDER, subdirectory, NULL);
    		gtk_ui_manager_add_ui (ui_manager,
    				       view->details->extensions_menu_merge_id,
    				       path,
    				       gtk_action_get_name (action),
    				       gtk_action_get_name (action),
                           item_type,
    				       FALSE);
    		g_free (path);

    		/* recursively fill the menu */
    		if (menu != NULL) {
    			char *subdir;
    			GList *children;

    			children = nolphin_menu_get_items (menu);

    			subdir = g_build_path ("/", subdirectory, gtk_action_get_name (action), NULL);
    			add_extension_menu_items (view,
    						  files,
    						  children,
    						  subdir);

    			nolphin_menu_item_list_free (children);
    			g_free (subdir);
    		}
        }
	}
}

static void
reset_extension_actions_menu (NolphinView *view, GList *selection)
{
	GList *items;
	GtkUIManager *ui_manager;

	/* Clear any previous inserted items in the extension actions placeholder */
	ui_manager = nolphin_window_get_ui_manager (view->details->window);

	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->extensions_menu_merge_id,
				&view->details->extensions_menu_action_group);

	nolphin_ui_prepare_merge_ui (ui_manager,
				      "DirExtensionsMenuGroup",
				      &view->details->extensions_menu_merge_id,
				      &view->details->extensions_menu_action_group);

	items = get_all_extension_menu_items (gtk_widget_get_toplevel (GTK_WIDGET (view)),
					      selection);
	if (items != NULL) {
		add_extension_menu_items (view, selection, items, "");

		g_list_foreach (items, (GFunc) g_object_unref, NULL);
		g_list_free (items);
	}
}

static char *
change_to_view_directory (NolphinView *view)
{
	char *path;
	char *old_path;

	old_path = g_get_current_dir ();

	path = get_view_directory (view);

	/* FIXME: What to do about non-local directories? */
	if (path != NULL) {
		g_chdir (path);
	}

	g_free (path);

	return old_path;
}

static char **
get_file_names_as_parameter_array (GList *selection,
				   NolphinDirectory *model)
{
	NolphinFile *file;
	char **parameters;
	GList *node;
	GFile *file_location;
	GFile *model_location;
	int i;

	if (model == NULL) {
		return NULL;
	}

	parameters = g_new (char *, g_list_length (selection) + 1);

	model_location = nolphin_directory_get_location (model);

	for (node = selection, i = 0; node != NULL; node = node->next, i++) {
		file = NOLPHIN_FILE (node->data);

		if (!nolphin_file_is_local (file)) {
			parameters[i] = NULL;
			g_strfreev (parameters);
			return NULL;
		}

		file_location = nolphin_file_get_location (NOLPHIN_FILE (node->data));
		parameters[i] = g_file_get_relative_path (model_location, file_location);
		if (parameters[i] == NULL) {
			parameters[i] = g_file_get_path (file_location);
		}
		g_object_unref (file_location);
	}

	g_object_unref (model_location);

	parameters[i] = NULL;
	return parameters;
}

static char *
get_file_paths_or_uris_as_newline_delimited_string (GList *selection, gboolean get_paths)
{
	char *path;
	char *uri;
	NolphinDesktopLink *link;
	GString *expanding_string;
	GList *node;
	GFile *location;

	expanding_string = g_string_new ("");
	for (node = selection; node != NULL; node = node->next) {
		uri = NULL;
		if (NOLPHIN_IS_DESKTOP_ICON_FILE (node->data)) {
			link = nolphin_desktop_icon_file_get_link (NOLPHIN_DESKTOP_ICON_FILE (node->data));
			if (link != NULL) {
				location = nolphin_desktop_link_get_activation_location (link);
				uri = g_file_get_uri (location);
				g_object_unref (location);
				g_object_unref (G_OBJECT (link));
			}
		} else {
			uri = nolphin_file_get_uri (NOLPHIN_FILE (node->data));
		}
		if (uri == NULL) {
			continue;
		}

		if (get_paths) {
			path = g_filename_from_uri (uri, NULL, NULL);
			if (path != NULL) {
				g_string_append (expanding_string, path);
				g_free (path);
				g_string_append (expanding_string, "\n");
			}
		} else {
			g_string_append (expanding_string, uri);
			g_string_append (expanding_string, "\n");
		}
		g_free (uri);
	}

    return g_string_free (expanding_string, FALSE);
}

static char *
get_file_paths_as_newline_delimited_string (GList *selection)
{
	return get_file_paths_or_uris_as_newline_delimited_string (selection, TRUE);
}

static char *
get_file_uris_as_newline_delimited_string (GList *selection)
{
	return get_file_paths_or_uris_as_newline_delimited_string (selection, FALSE);
}

/* returns newly allocated strings for setting the environment variables */
static void
get_strings_for_environment_variables (NolphinView *view, GList *selected_files,
				       char **file_paths, char **uris, char **uri)
{
	char *directory_uri;

	/* We need to check that the directory uri starts with "file:" since
	 * nolphin_directory_is_local returns FALSE for nfs.
	 */
	directory_uri = nolphin_directory_get_uri (view->details->model);
	if (g_str_has_prefix (directory_uri, "file:") ||
	    eel_uri_is_desktop (directory_uri) ||
	    eel_uri_is_trash (directory_uri)) {
		*file_paths = get_file_paths_as_newline_delimited_string (selected_files);
	} else {
		*file_paths = g_strdup ("");
	}
	g_free (directory_uri);

	*uris = get_file_uris_as_newline_delimited_string (selected_files);

	*uri = nolphin_directory_get_uri (view->details->model);
	if (eel_uri_is_desktop (*uri)) {
		g_free (*uri);
		*uri = nolphin_get_desktop_directory_uri ();
	}
}

static NolphinView *
get_directory_view_of_extra_pane (NolphinView *view)
{
	NolphinWindowSlot *slot;
	NolphinView *next_view;

	slot = nolphin_window_get_extra_slot (nolphin_view_get_nolphin_window (view));
	if (slot != NULL) {
		next_view = nolphin_window_slot_get_current_view (slot);

		if (NOLPHIN_IS_VIEW (next_view)) {
			return NOLPHIN_VIEW (next_view);
		}
	}
	return NULL;
}

/*
 * Set up some environment variables that scripts can use
 * to take advantage of the current Nolphin state.
 */
static void
set_script_environment_variables (NolphinView *view, GList *selected_files)
{
	char *file_paths;
	char *uris;
	char *uri;
	char *geometry_string;
	NolphinView *next_view;

	get_strings_for_environment_variables (view, selected_files,
					       &file_paths, &uris, &uri);

	g_setenv ("NOLPHIN_SCRIPT_SELECTED_FILE_PATHS", file_paths, TRUE);
	g_free (file_paths);

	g_setenv ("NOLPHIN_SCRIPT_SELECTED_URIS", uris, TRUE);
	g_free (uris);

	g_setenv ("NOLPHIN_SCRIPT_CURRENT_URI", uri, TRUE);
	g_free (uri);

	geometry_string = eel_gtk_window_get_geometry_string
		(GTK_WINDOW (nolphin_view_get_containing_window (view)));
	g_setenv ("NOLPHIN_SCRIPT_WINDOW_GEOMETRY", geometry_string, TRUE);
	g_free (geometry_string);

	/* next pane */
	next_view = get_directory_view_of_extra_pane (view);
	if (next_view) {
		GList *next_pane_selected_files;
		next_pane_selected_files = nolphin_view_get_selection (next_view);

		get_strings_for_environment_variables (next_view, next_pane_selected_files,
						       &file_paths, &uris, &uri);
		nolphin_file_list_free (next_pane_selected_files);
	} else {
		file_paths = g_strdup("");
		uris = g_strdup("");
		uri = g_strdup("");
	}

	g_setenv ("NOLPHIN_SCRIPT_NEXT_PANE_SELECTED_FILE_PATHS", file_paths, TRUE);
	g_free (file_paths);

	g_setenv ("NOLPHIN_SCRIPT_NEXT_PANE_SELECTED_URIS", uris, TRUE);
	g_free (uris);

	g_setenv ("NOLPHIN_SCRIPT_NEXT_PANE_CURRENT_URI", uri, TRUE);
	g_free (uri);
}

/* Unset all the special script environment variables. */
static void
unset_script_environment_variables (void)
{
	g_unsetenv ("NOLPHIN_SCRIPT_SELECTED_FILE_PATHS");
	g_unsetenv ("NOLPHIN_SCRIPT_SELECTED_URIS");
	g_unsetenv ("NOLPHIN_SCRIPT_CURRENT_URI");
	g_unsetenv ("NOLPHIN_SCRIPT_WINDOW_GEOMETRY");
	g_unsetenv ("NOLPHIN_SCRIPT_NEXT_PANE_SELECTED_FILE_PATHS");
	g_unsetenv ("NOLPHIN_SCRIPT_NEXT_PANE_SELECTED_URIS");
	g_unsetenv ("NOLPHIN_SCRIPT_NEXT_PANE_CURRENT_URI");
}

static void
run_script_callback (GtkAction *action, gpointer callback_data)
{
	ScriptLaunchParameters *launch_parameters;
	GdkScreen *screen;
	GList *selected_files;
	char *file_uri;
	char *local_file_path;
	char *quoted_path;
	char *old_working_dir;
	char **parameters;

	launch_parameters = (ScriptLaunchParameters *) callback_data;

	file_uri = nolphin_file_get_uri (launch_parameters->file);
	local_file_path = g_filename_from_uri (file_uri, NULL, NULL);
	g_assert (local_file_path != NULL);
	g_free (file_uri);

	quoted_path = g_shell_quote (local_file_path);
	g_free (local_file_path);

	old_working_dir = change_to_view_directory (launch_parameters->directory_view);

	selected_files = nolphin_view_get_selection (launch_parameters->directory_view);
	set_script_environment_variables (launch_parameters->directory_view, selected_files);

	parameters = get_file_names_as_parameter_array (selected_files,
						        launch_parameters->directory_view->details->model);

	screen = gtk_widget_get_screen (GTK_WIDGET (launch_parameters->directory_view));

	DEBUG ("run_script_callback, script_path=\"%s\" (omitting script parameters)",
	       local_file_path);

	nolphin_launch_application_from_command_array (screen, quoted_path, FALSE,
							(const char * const *) parameters);
	g_strfreev (parameters);

	nolphin_file_list_free (selected_files);
	unset_script_environment_variables ();
	g_chdir (old_working_dir);
	g_free (old_working_dir);
	g_free (quoted_path);
}

static void
add_script_to_scripts_menus (NolphinView *directory_view,
			     NolphinFile *file,
			     const char *menu_path,
			     const char *popup_path,
			     const char *popup_bg_path)
{
	ScriptLaunchParameters *launch_parameters;
	char *tip;
	char *name;
	char *uri;
	char *action_name;
	char *escaped_label;
	GdkPixbuf *pixbuf;
	GtkUIManager *ui_manager;
	GtkAction *action;

	name = nolphin_file_get_display_name (file);
	uri = nolphin_file_get_uri (file);
	tip = g_strdup_printf (_("»%s« mit allen gewählten Objekten starten"), name);

	launch_parameters = script_launch_parameters_new (file, directory_view);

	action_name = escape_action_name (uri, "script_");
	escaped_label = eel_str_double_underscores (name);

	action = gtk_action_new (action_name,
				 escaped_label,
				 tip,
				 NULL);

	pixbuf = get_menu_icon_for_file (file, GTK_WIDGET (directory_view));
	if (pixbuf != NULL) {
		gtk_action_set_gicon (action, G_ICON (pixbuf));
		g_object_unref (pixbuf);
	}

	g_signal_connect_data (action, "activate",
			       G_CALLBACK (run_script_callback),
			       launch_parameters,
			       (GClosureNotify)script_launch_parameters_free, 0);

	gtk_action_group_add_action_with_accel (directory_view->details->scripts_action_group,
						action, NULL);
	g_object_unref (action);

	ui_manager = nolphin_window_get_ui_manager (directory_view->details->window);

	gtk_ui_manager_add_ui (ui_manager,
			       directory_view->details->scripts_merge_id,
			       menu_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);
	gtk_ui_manager_add_ui (ui_manager,
			       directory_view->details->scripts_merge_id,
			       popup_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);
	gtk_ui_manager_add_ui (ui_manager,
			       directory_view->details->scripts_merge_id,
			       popup_bg_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	menu_item_show_image (ui_manager, menu_path, action_name, FALSE);
	menu_item_show_image (ui_manager, popup_path, action_name, FALSE);
	menu_item_show_image (ui_manager, popup_bg_path, action_name, FALSE);

	g_free (name);
	g_free (uri);
	g_free (tip);
	g_free (action_name);
	g_free (escaped_label);
}

static void
add_submenu_to_directory_menus (NolphinView *directory_view,
				GtkActionGroup *action_group,
				guint merge_id,
				NolphinFile *file,
				const char *menu_path,
				const char *popup_path,
				const char *popup_bg_path)
{
	char *name;
	GdkPixbuf *pixbuf;
	char *uri;
	GtkUIManager *ui_manager;

	ui_manager = nolphin_window_get_ui_manager (directory_view->details->window);
	uri = nolphin_file_get_uri (file);
	name = nolphin_file_get_display_name (file);
	pixbuf = get_menu_icon_for_file (file, GTK_WIDGET (directory_view));
	add_submenu (ui_manager, action_group, merge_id, menu_path, uri, name, pixbuf, TRUE);
	add_submenu (ui_manager, action_group, merge_id, popup_path, uri, name, pixbuf, FALSE);
	add_submenu (ui_manager, action_group, merge_id, popup_bg_path, uri, name, pixbuf, FALSE);
	if (pixbuf) {
		g_object_unref (pixbuf);
	}
	g_free (name);
	g_free (uri);
}

static gboolean
directory_belongs_in_scripts_menu (const char *uri)
{
	int num_levels;
	int i;

	if (!g_str_has_prefix (uri, scripts_directory_uri)) {
		return FALSE;
	}

	num_levels = 0;
	for (i = scripts_directory_uri_length; uri[i] != '\0'; i++) {
		if (uri[i] == '/') {
			num_levels++;
		}
	}

	if (num_levels > MAX_MENU_LEVELS) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
update_directory_in_scripts_menu (NolphinView *view, NolphinDirectory *directory)
{
	char *menu_path, *popup_path, *popup_bg_path;
	GList *file_list, *filtered, *node;
	gboolean any_scripts;
	NolphinFile *file;
	NolphinDirectory *dir;
	char *uri;
	char *escaped_path;

	uri = nolphin_directory_get_uri (directory);
	escaped_path = escape_action_path (uri + scripts_directory_uri_length);
	g_free (uri);
	menu_path = g_strconcat (NOLPHIN_VIEW_MENU_PATH_SCRIPTS_PLACEHOLDER,
				 escaped_path,
				 NULL);
	popup_path = g_strconcat (NOLPHIN_VIEW_POPUP_PATH_SCRIPTS_PLACEHOLDER,
				  escaped_path,
				  NULL);
	popup_bg_path = g_strconcat (NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_SCRIPTS_PLACEHOLDER,
				     escaped_path,
				     NULL);
	g_free (escaped_path);

	file_list = nolphin_directory_get_file_list (directory);
	filtered = nolphin_file_list_filter_hidden (file_list, FALSE);
	nolphin_file_list_free (file_list);

	file_list = nolphin_file_list_sort_by_display_name (filtered);

	any_scripts = FALSE;
	for (node = file_list; node != NULL; node = node->next) {
		file = node->data;

		if (nolphin_file_is_launchable (file) &&
            nolphin_global_preferences_should_load_plugin (nolphin_file_peek_name (file), NOLPHIN_PLUGIN_PREFERENCES_DISABLED_SCRIPTS)) {
			add_script_to_scripts_menus (view, file, menu_path, popup_path, popup_bg_path);
			any_scripts = TRUE;
		} else if (nolphin_file_is_directory (file)) {
			uri = nolphin_file_get_uri (file);
			if (directory_belongs_in_scripts_menu (uri)) {
				dir = nolphin_directory_get_by_uri (uri);
				add_directory_to_scripts_directory_list (view, dir);
				nolphin_directory_unref (dir);

				add_submenu_to_directory_menus (view,
								view->details->scripts_action_group,
								view->details->scripts_merge_id,
								file, menu_path, popup_path, popup_bg_path);

				any_scripts = TRUE;
			}
			g_free (uri);
		}
	}

	nolphin_file_list_free (file_list);

	g_free (popup_path);
	g_free (popup_bg_path);
	g_free (menu_path);

	return any_scripts;
}

static void
update_scripts_menu (NolphinView *view)
{
	gboolean any_scripts;
	GList *sorted_copy, *node;
	NolphinDirectory *directory;
	char *uri;
	GtkUIManager *ui_manager;
	GtkAction *action;

	/* There is a race condition here.  If we don't mark the scripts menu as
	   valid before we begin our task then we can lose script menu updates that
	   occur before we finish. */
	view->details->scripts_invalid = FALSE;

	ui_manager = nolphin_window_get_ui_manager (view->details->window);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->scripts_merge_id,
				&view->details->scripts_action_group);

	nolphin_ui_prepare_merge_ui (ui_manager,
				      "ScriptsGroup",
				      &view->details->scripts_merge_id,
				      &view->details->scripts_action_group);

	/* As we walk through the directories, remove any that no longer belong. */
	any_scripts = FALSE;
	sorted_copy = nolphin_directory_list_sort_by_uri
		(nolphin_directory_list_copy (view->details->scripts_directory_list));
	for (node = sorted_copy; node != NULL; node = node->next) {
		directory = node->data;

		uri = nolphin_directory_get_uri (directory);
		if (!directory_belongs_in_scripts_menu (uri)) {
			remove_directory_from_scripts_directory_list (view, directory);
		} else if (update_directory_in_scripts_menu (view, directory)) {
			any_scripts = TRUE;
		}
		g_free (uri);
	}
	nolphin_directory_list_free (sorted_copy);

	action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_SCRIPTS);
	gtk_action_set_visible (action, any_scripts);
}

static void
run_action_callback (NolphinAction *action, gpointer callback_data)
{

    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GList *selected_files;

    selected_files = nolphin_view_get_selection (view);
    NolphinFile *parent = nolphin_view_get_directory_as_file (view);

    nolphin_action_activate (action, selected_files, parent, GTK_WINDOW (view->details->window));

    nolphin_file_list_free (selected_files);
}

static void
update_actions_visibility (NolphinView *view,
                           GList    *selection,
                           gboolean  for_accelerators)
{
    NolphinFile *parent = nolphin_view_get_directory_as_file (view);

    nolphin_action_manager_update_action_states (view->details->action_manager,
                                              view->details->actions_action_group,
                                              selection,
                                              parent,
                                              FALSE,
                                              for_accelerators,
                                              GTK_WINDOW (view->details->window));
}

static void
add_action_to_ui (NolphinActionManager    *manager,
                  GtkAction            *action,
                  GtkUIManagerItemType  type,
                  const gchar          *path,
                  const gchar          *accelerator,
                  gpointer              user_data)
{
    NolphinView *view = NOLPHIN_VIEW (user_data);

    static const gchar *roots[] = {
        NOLPHIN_VIEW_MENU_PATH_ACTIONS_PLACEHOLDER,
        NOLPHIN_VIEW_POPUP_PATH_ACTIONS_PLACEHOLDER,
        NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_ACTIONS_PLACEHOLDER,
        NULL
    };

    nolphin_action_manager_add_action_ui (manager,
                                       nolphin_window_get_ui_manager (view->details->window),
                                       action,
                                       path,
                                       accelerator,
                                       view->details->actions_action_group,
                                       view->details->actions_merge_id,
                                       roots,
                                       type,
                                       G_CALLBACK (run_action_callback),
                                       view);
}

static void
update_actions (NolphinView *view)
{
    DEBUG ("Refreshing menu actions");

    nolphin_action_manager_iterate_actions (view->details->action_manager,
                                         (NolphinActionManagerIterFunc) add_action_to_ui,
                                         view);

    update_accelerated_actions (view);
}

static void
update_actions_menu (NolphinView *view)
{
    GtkUIManager *ui_manager;

    view->details->actions_invalid = FALSE;

    ui_manager = nolphin_window_get_ui_manager (view->details->window);
    nolphin_ui_unmerge_ui (ui_manager,
                &view->details->actions_merge_id,
                &view->details->actions_action_group);

    nolphin_ui_prepare_merge_ui (ui_manager,
                      "ActionsGroup",
                      &view->details->actions_merge_id,
                      &view->details->actions_action_group);

    update_actions (view);
}

static void
create_template_callback (GtkAction *action, gpointer callback_data)
{
	CreateTemplateParameters *parameters;

	parameters = callback_data;

	nolphin_view_new_file (parameters->directory_view, NULL, parameters->file);
}

static void
add_template_to_templates_menus (NolphinView *directory_view,
				 NolphinFile *file,
				 const char *menu_path,
				 const char *popup_bg_path)
{
	char *tmp, *tip, *uri, *name;
	char *escaped_label;
	GdkPixbuf *pixbuf;
	char *action_name;
	CreateTemplateParameters *parameters;
	GtkUIManager *ui_manager;
	GtkAction *action;

	tmp = nolphin_file_get_display_name (file);
	name = eel_filename_strip_extension (tmp);
	g_free (tmp);

	uri = nolphin_file_get_uri (file);
	tip = g_strdup_printf (_("Ein neues Dokument aus Vorlage »%s« anlegen"), name);

	action_name = escape_action_name (uri, "template_");
	escaped_label = eel_str_double_underscores (name);

	parameters = create_template_parameters_new (file, directory_view);

	action = gtk_action_new (action_name,
				 escaped_label,
				 tip,
				 NULL);

	pixbuf = get_menu_icon_for_file (file, GTK_WIDGET (directory_view));
	if (pixbuf != NULL) {
		gtk_action_set_gicon (action, G_ICON (pixbuf));
		g_object_unref (pixbuf);
	}

	g_signal_connect_data (action, "activate",
			       G_CALLBACK (create_template_callback),
			       parameters,
			       (GClosureNotify)create_templates_parameters_free, 0);

	gtk_action_group_add_action (directory_view->details->templates_action_group,
				     action);
	g_object_unref (action);

	ui_manager = nolphin_window_get_ui_manager (directory_view->details->window);

	gtk_ui_manager_add_ui (ui_manager,
			       directory_view->details->templates_merge_id,
			       menu_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	gtk_ui_manager_add_ui (ui_manager,
			       directory_view->details->templates_merge_id,
			       popup_bg_path,
			       action_name,
			       action_name,
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	menu_item_show_image (ui_manager, menu_path, action_name, TRUE);
	menu_item_show_image (ui_manager, popup_bg_path, action_name, TRUE);

	g_free (escaped_label);
	g_free (name);
	g_free (tip);
	g_free (uri);
	g_free (action_name);
}

static void
update_templates_directory (NolphinView *view)
{
	NolphinDirectory *templates_directory;
	GList *node, *next;
	char *templates_uri;

	for (node = view->details->templates_directory_list; node != NULL; node = next) {
		next = node->next;
		remove_directory_from_templates_directory_list (view, node->data);
	}

	if (nolphin_should_use_templates_directory ()) {
		templates_uri = nolphin_get_templates_directory_uri ();
		templates_directory = nolphin_directory_get_by_uri (templates_uri);
		g_free (templates_uri);
		add_directory_to_templates_directory_list (view, templates_directory);
		nolphin_directory_unref (templates_directory);
	}
}

static void
user_dirs_changed (NolphinView *view)
{
	update_templates_directory (view);
	view->details->templates_invalid = TRUE;
	schedule_update_menus (view);
}

static gboolean
directory_belongs_in_templates_menu (const char *templates_directory_uri,
				     const char *uri)
{
	int num_levels;
	int i;

	if (templates_directory_uri == NULL) {
		return FALSE;
	}

	if (!g_str_has_prefix (uri, templates_directory_uri)) {
		return FALSE;
	}

	num_levels = 0;
	for (i = strlen (templates_directory_uri); uri[i] != '\0'; i++) {
		if (uri[i] == '/') {
			num_levels++;
		}
	}

	if (num_levels > MAX_MENU_LEVELS) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
update_directory_in_templates_menu (NolphinView *view,
				    const char *templates_directory_uri,
				    NolphinDirectory *directory)
{
	char *menu_path, *popup_bg_path;
	GList *file_list, *filtered, *node;
	gboolean any_templates;
	NolphinFile *file;
	NolphinDirectory *dir;
	char *escaped_path;
	char *uri;
	int num;

	/* We know this directory belongs to the template dir, so it must exist */
	g_assert (templates_directory_uri);

	uri = nolphin_directory_get_uri (directory);
	escaped_path = escape_action_path (uri + strlen (templates_directory_uri));
	g_free (uri);
	menu_path = g_strconcat (NOLPHIN_VIEW_MENU_PATH_NEW_DOCUMENTS_PLACEHOLDER,
				 escaped_path,
				 NULL);
	popup_bg_path = g_strconcat (NOLPHIN_VIEW_POPUP_PATH_BACKGROUND_NEW_DOCUMENTS_PLACEHOLDER,
				     escaped_path,
				     NULL);
	g_free (escaped_path);

	file_list = nolphin_directory_get_file_list (directory);
	filtered = nolphin_file_list_filter_hidden (file_list, FALSE);
	nolphin_file_list_free (file_list);

	file_list = nolphin_file_list_sort_by_display_name (filtered);

	num = 0;
	any_templates = FALSE;
	for (node = file_list; num < TEMPLATE_LIMIT && node != NULL; node = node->next, num++) {
		file = node->data;

		if (nolphin_file_is_directory (file)) {
			uri = nolphin_file_get_uri (file);
			if (directory_belongs_in_templates_menu (templates_directory_uri, uri)) {
				dir = nolphin_directory_get_by_uri (uri);
				add_directory_to_templates_directory_list (view, dir);
				nolphin_directory_unref (dir);

				add_submenu_to_directory_menus (view,
								view->details->templates_action_group,
								view->details->templates_merge_id,
								file, menu_path, NULL, popup_bg_path);

				any_templates = TRUE;
			}
			g_free (uri);
		} else if (nolphin_file_can_read (file)) {
			add_template_to_templates_menus (view, file, menu_path, popup_bg_path);
			any_templates = TRUE;
		}
	}

	nolphin_file_list_free (file_list);

	g_free (popup_bg_path);
	g_free (menu_path);

	return any_templates;
}



static void
update_templates_menu (NolphinView *view)
{
	gboolean any_templates;
	GList *sorted_copy, *node;
	NolphinDirectory *directory;
	GtkUIManager *ui_manager;
	char *uri;
	GtkAction *action;
	char *templates_directory_uri;

	if (nolphin_should_use_templates_directory ()) {
		templates_directory_uri = nolphin_get_templates_directory_uri ();
	} else {
		templates_directory_uri = NULL;
	}

	/* There is a race condition here.  If we don't mark the scripts menu as
	   valid before we begin our task then we can lose template menu updates that
	   occur before we finish. */
	view->details->templates_invalid = FALSE;

	ui_manager = nolphin_window_get_ui_manager (view->details->window);
	nolphin_ui_unmerge_ui (ui_manager,
				&view->details->templates_merge_id,
				&view->details->templates_action_group);

	nolphin_ui_prepare_merge_ui (ui_manager,
				      "TemplatesGroup",
				      &view->details->templates_merge_id,
				      &view->details->templates_action_group);

	/* As we walk through the directories, remove any that no longer belong. */
	any_templates = FALSE;
	sorted_copy = nolphin_directory_list_sort_by_uri
		(nolphin_directory_list_copy (view->details->templates_directory_list));
	for (node = sorted_copy; node != NULL; node = node->next) {
		directory = node->data;

		uri = nolphin_directory_get_uri (directory);
		if (!directory_belongs_in_templates_menu (templates_directory_uri, uri)) {
			remove_directory_from_templates_directory_list (view, directory);
		} else if (update_directory_in_templates_menu (view,
							       templates_directory_uri,
							       directory)) {
			any_templates = TRUE;
		}
		g_free (uri);
	}
	nolphin_directory_list_free (sorted_copy);

	action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_NO_TEMPLATES);
	gtk_action_set_visible (action, !any_templates);

	g_free (templates_directory_uri);
}

static GtkMenu *
create_popup_menu (NolphinView *view, const char *popup_path)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (nolphin_window_get_ui_manager (view->details->window),
					  popup_path);
	gtk_menu_set_screen (GTK_MENU (menu),
			     gtk_widget_get_screen (GTK_WIDGET (view)));
	gtk_widget_show (GTK_WIDGET (menu));

	return GTK_MENU (menu);
}

static void
copy_or_cut_files (NolphinView *view,
		   GList           *clipboard_contents,
		   gboolean         cut)
{
    GtkClipboard *clipboard;
	int count;
	char *status_string, *name;
	NolphinClipboardInfo info;
        GtkTargetList *target_list;
        GtkTargetEntry *targets;
        int n_targets;

	info.files = clipboard_contents;
	info.cut = cut;

        target_list = gtk_target_list_new (NULL, 0);
        gtk_target_list_add (target_list, copied_files_atom, 0, 0);
        gtk_target_list_add_uri_targets (target_list, 0);
        gtk_target_list_add_text_targets (target_list, 0);

        targets = gtk_target_table_new_from_list (target_list, &n_targets);
        gtk_target_list_unref (target_list);

    clipboard = nolphin_clipboard_get (GTK_WIDGET (view));

    gtk_clipboard_set_with_data (clipboard,
                                 targets, n_targets,
                                 nolphin_get_clipboard_callback, nolphin_clear_clipboard_callback,
                                 NULL);
    gtk_clipboard_set_can_store (clipboard, NULL, 0);
    gtk_target_table_free (targets, n_targets);

	nolphin_clipboard_monitor_set_clipboard_info (nolphin_clipboard_monitor_get (), &info);

	count = g_list_length (clipboard_contents);
	if (count == 1) {
		name = nolphin_file_get_display_name (clipboard_contents->data);
		if (cut) {
			status_string = g_strdup_printf (_("»%s« wird verschoben, sobald Sie »Einfügen« auswählen"),
							 name);
		} else {
			status_string = g_strdup_printf (_("»%s« wird kopiert, sobald Sie »Einfügen« auswählen"),
							 name);
		}
		g_free (name);
	} else {
		if (cut) {
			/* translators: this is preceded with a string of form 'N selected items' (N more than 1) */
			status_string = g_strdup_printf (ngettext("Das %'d ausgewählte Objekt wird verschoben, sobald Sie »Einfügen« auswählen",
								  "Die %'d ausgewählten Objekte%s werden verschoben, sobald Sie »Einfügen« auswählen",
								  count),
							 count, view->details->detail_string);
		} else {
			/* translators: this is preceded with a string of form 'N selected items' (N more than 1) */
			status_string = g_strdup_printf (ngettext("Das %'d ausgewählte Objekt wird kopiert, sobald Sie »Einfügen« auswählen",
								  "Die %'d ausgewählten Objekte%s werden kopiert, sobald Sie »Einfügen« auswählen",
								  count),
							 count, view->details->detail_string);
		}
	}

	nolphin_window_slot_set_status (view->details->slot,
					 status_string, NULL, FALSE);
	g_free (status_string);
}

static void
action_copy_files_callback (GtkAction *action,
			    gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);

	selection = nolphin_view_get_selection_for_file_transfer (view);
	copy_or_cut_files (view, selection, FALSE);
	nolphin_file_list_free (selection);
}

static void
move_copy_selection_to_next_pane (NolphinView *view,
				  int copy_action)
{
	NolphinWindowSlot *slot;
	char *dest_location;

	slot = nolphin_window_get_extra_slot (nolphin_view_get_nolphin_window (view));
	g_return_if_fail (slot != NULL);

	dest_location = nolphin_window_slot_get_current_uri (slot);
	g_return_if_fail (dest_location != NULL);

	move_copy_selection_to_location (view, copy_action, dest_location);
}

static void
action_copy_to_next_pane_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);
	move_copy_selection_to_next_pane (view,
					  GDK_ACTION_COPY);
}

static void
action_move_to_next_pane_callback (GtkAction *action, gpointer callback_data)
{
	NolphinWindowSlot *slot;
	char *dest_location;
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	slot = nolphin_window_get_extra_slot (nolphin_view_get_nolphin_window (view));
	g_return_if_fail (slot != NULL);

	dest_location = nolphin_window_slot_get_current_uri (slot);
	g_return_if_fail (dest_location != NULL);

	move_copy_selection_to_location (view, GDK_ACTION_MOVE, dest_location);
}

static void
action_copy_to_home_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view;
	char *dest_location;

	view = NOLPHIN_VIEW (callback_data);

	dest_location = nolphin_get_home_directory_uri ();
	move_copy_selection_to_location (view, GDK_ACTION_COPY, dest_location);
	g_free (dest_location);
}

static void
action_move_to_home_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view;
	char *dest_location;

	view = NOLPHIN_VIEW (callback_data);

	dest_location = nolphin_get_home_directory_uri ();
	move_copy_selection_to_location (view, GDK_ACTION_MOVE, dest_location);
	g_free (dest_location);
}

static void
action_copy_to_desktop_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view;
	char *dest_location;

	view = NOLPHIN_VIEW (callback_data);

	dest_location = nolphin_get_desktop_directory_uri ();
	move_copy_selection_to_location (view, GDK_ACTION_COPY, dest_location);
	g_free (dest_location);
}

static void
action_move_to_desktop_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view;
	char *dest_location;

	view = NOLPHIN_VIEW (callback_data);

	dest_location = nolphin_get_desktop_directory_uri ();
	move_copy_selection_to_location (view, GDK_ACTION_MOVE, dest_location);
	g_free (dest_location);
}

static void
browse_move_to_response_cb (GtkDialog *dialog, gint response, NolphinView *view)
{
    gchar *uri;

    switch (response) {
        case GTK_RESPONSE_OK:
            uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
            move_copy_selection_to_location (view, GDK_ACTION_MOVE, uri);
            g_free (uri);
            break;
        case GTK_RESPONSE_CANCEL:
        default:
            break;
    }

    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
browse_copy_to_response_cb (GtkDialog *dialog, gint response, NolphinView *view)
{
    gchar *uri;

    switch (response) {
        case GTK_RESPONSE_OK:
            uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
            move_copy_selection_to_location (view, GDK_ACTION_COPY, uri);
            g_free (uri);
            break;
        case GTK_RESPONSE_CANCEL:
        default:
            break;
    }

    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
action_browse_for_move_to_folder_callback (GtkAction *action, gpointer callback_data)
{
    GtkWidget *dialog;
    NolphinView *view;

    view = NOLPHIN_VIEW (callback_data);

    dialog = gtk_file_chooser_dialog_new (_("Zielordner zum Verschieben auswählen"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                          NULL);

    gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE);

    g_signal_connect (dialog, "response",
                      G_CALLBACK (browse_move_to_response_cb), view);

    gtk_widget_show (dialog);
}

static void
action_browse_for_copy_to_folder_callback (GtkAction *action, gpointer callback_data)
{
    GtkWidget *dialog;
    NolphinView *view;

    view = NOLPHIN_VIEW (callback_data);

    dialog = gtk_file_chooser_dialog_new (_("Zielordner zum Kopieren auswählen"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                          NULL);

    gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE);

    g_signal_connect (dialog, "response",
                      G_CALLBACK (browse_copy_to_response_cb), view);

    gtk_widget_show (dialog);
}

static void
action_cut_files_callback (GtkAction *action,
			   gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);

	selection = nolphin_view_get_selection_for_file_transfer (view);
	copy_or_cut_files (view, selection, TRUE);
	nolphin_file_list_free (selection);
}

static void
paste_clipboard_data (NolphinView *view,
		      GtkSelectionData *selection_data,
		      char *destination_uri)
{
	gboolean cut;
	GList *item_uris;

	cut = FALSE;
	item_uris = nolphin_clipboard_get_uri_list_from_selection_data (selection_data, &cut,
									 copied_files_atom);

	if (item_uris == NULL|| destination_uri == NULL) {
		nolphin_window_slot_set_status (view->details->slot,
						 _("In der Zwischenablage ist nichts zum Einfügen."),
						 NULL,
                         FALSE);
	} else {
		nolphin_view_move_copy_items (view, item_uris, NULL, destination_uri,
					       cut ? GDK_ACTION_MOVE : GDK_ACTION_COPY,
					       0, 0);

		/* If items are cut then remove from clipboard */
		if (cut) {
			gtk_clipboard_clear (nolphin_clipboard_get (GTK_WIDGET (view)));
		}

		g_list_free_full (item_uris, g_free);
	}
}

static void
paste_clipboard_received_callback (GtkClipboard     *clipboard,
				   GtkSelectionData *selection_data,
				   gpointer          data)
{
	NolphinView *view;
	char *view_uri;

	view = NOLPHIN_VIEW (data);

	view_uri = nolphin_view_get_backing_uri (view);

	if (view->details->window != NULL) {
		paste_clipboard_data (view, selection_data, view_uri);
	}

	g_free (view_uri);

	g_object_unref (view);
}

typedef struct {
	NolphinView *view;
	NolphinFile *target;
} PasteIntoData;

static void
paste_into_clipboard_received_callback (GtkClipboard     *clipboard,
					GtkSelectionData *selection_data,
					gpointer          callback_data)
{
	PasteIntoData *data;
	NolphinView *view;
	char *directory_uri;

	data = (PasteIntoData *) callback_data;

	view = NOLPHIN_VIEW (data->view);

	if (view->details->window != NULL) {
		directory_uri = nolphin_file_get_activation_uri (data->target);

		paste_clipboard_data (view, selection_data, directory_uri);

		g_free (directory_uri);
	}

	g_object_unref (view);
	nolphin_file_unref (data->target);
	g_free (data);
}

static void
action_paste_files_callback (GtkAction *action,
			     gpointer callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);

	g_object_ref (view);
	gtk_clipboard_request_contents (nolphin_clipboard_get (GTK_WIDGET (view)),
					copied_files_atom,
					paste_clipboard_received_callback,
					view);
}

static void
paste_into (NolphinView *view,
	    NolphinFile *target)
{
	PasteIntoData *data;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (NOLPHIN_IS_FILE (target));

	data = g_new (PasteIntoData, 1);

	data->view = g_object_ref (view);
	data->target = nolphin_file_ref (target);

	gtk_clipboard_request_contents (nolphin_clipboard_get (GTK_WIDGET (view)),
					copied_files_atom,
					paste_into_clipboard_received_callback,
					data);
}

static void
cb_open_as_root_watch (GPid pid, gint status, gpointer user_data)
{
    g_spawn_close_pid(pid);
}

static void
open_as_admin (NolphinView *view, const gchar *path) {
    g_autoptr(GUri) uri_obj = g_uri_build (0, "admin", NULL, NULL, -1, path, NULL, NULL);
    g_autofree gchar *uri = g_uri_to_string (uri_obj);
    g_autoptr(GFile) location = g_file_new_for_uri (uri);

    nolphin_window_slot_open_location (view->details->slot, location, 0);
}

static void
open_as_root (NolphinView *view, const gchar *path)
{
    if (eel_check_is_wayland ()) {
        open_as_admin (view, path);
        return;
    }

    gchar *argv[4];
    argv[0] = (gchar *)"pkexec";
    argv[1] = (gchar *)"nolphin";
    argv[2] = g_strdup (path);
    argv[3] = NULL;
    GPid pid;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                  NULL, NULL, &pid, NULL);
    g_child_watch_add(pid, (GChildWatchFunc)cb_open_as_root_watch, NULL);
    g_free (argv[2]);
}

static void
action_paste_files_into_callback (GtkAction *action,
				  gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);
	if (selection != NULL) {
		paste_into (view, NOLPHIN_FILE (selection->data));
		nolphin_file_list_free (selection);
	}

}

static void
action_open_as_root_callback (GtkAction *action,
				  gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);
	if (selection != NULL) {
        gchar *path = nolphin_file_get_path (NOLPHIN_FILE (selection->data));
		open_as_root (view, path);
		nolphin_file_list_free (selection);
        g_free (path);
	} else {
        gchar *path;
        gchar *uri = nolphin_view_get_uri (view);
        GFile *gfile = g_file_new_for_uri (uri);
        if (g_file_has_uri_scheme (gfile, "x-nolphin-desktop")) {
            path = nolphin_get_desktop_directory ();
        } else {
            path = g_file_get_path (gfile);
        }
        open_as_root (view, path);
        g_free (uri);
        g_free (path);
        g_object_unref (gfile);
    }

}

/* Ctrl+I ("zeigt eine Such-/Filterleiste"). There is no separate
 * bar widget to show here - the type-ahead filter (see
 * nolphin_view_activate_filter() below) already turns itself on the
 * moment a character arrives, as long as the view has focus. So
 * "activating" it via a shortcut, rather than by just typing, mainly
 * means: guarantee that focus, for discoverability and for the case
 * where focus is currently elsewhere (address bar, sidebar, ...). */
static void
action_activate_filter_callback (GtkAction *action,
                                 gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);

    nolphin_view_grab_focus (view);
}

static void
action_follow_symlink_callback (GtkAction *action,
                                gpointer callback_data)
{
    NolphinView *view;
    GList *selection;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    if (nolphin_file_is_symbolic_link (selection->data)) {
        gchar *uri = nolphin_file_get_symbolic_link_target_uri (selection->data);
        gchar *view_uri = nolphin_view_get_uri (view);
        GFile *location = g_file_new_for_uri (uri);
        GFile *parent = g_file_get_parent (location);
        GFile *current = g_file_new_for_uri (view_uri);

        if (g_file_equal (current, parent)) {
            nolphin_view_scroll_to_file (view, uri);
            GList *l = NULL;
            l = g_list_append (l, nolphin_file_get_existing (location));
            nolphin_view_set_selection (view, l);
            nolphin_file_list_free (l);
        } else {
            if (get_is_desktop_view (view)) {
                nolphin_mime_launch_fm_and_select_file (location);
            } else {
                nolphin_window_slot_open_location (view->details->slot, location, 0);
            }
        }

        g_free (uri);
        g_free (view_uri);
        g_object_unref (location);
        g_object_unref (parent);
        g_object_unref (current);
    }

    nolphin_file_list_free (selection);
}

/* Returns a child of @parent named @base_name if that's free, otherwise
 * "@base_name (1)@suffix", "@base_name (2)@suffix", etc. @suffix (may be
 * "") is kept at the end, e.g. for "archive.zip" -> "archive (1).zip". */
static GFile *
find_unique_destination (GFile *parent, const gchar *base_name, const gchar *suffix)
{
    GFile *candidate;
    gint n;

    candidate = g_file_get_child (parent, base_name);
    if (!g_file_query_exists (candidate, NULL)) {
        return candidate;
    }
    g_object_unref (candidate);

    for (n = 1; n < 1000; n++) {
        gchar *name = g_strdup_printf ("%s (%d)%s", base_name, n, suffix);
        candidate = g_file_get_child (parent, name);
        g_free (name);
        if (!g_file_query_exists (candidate, NULL)) {
            return candidate;
        }
        g_object_unref (candidate);
    }

    /* Give up gracefully rather than looping forever or overwriting. */
    return NULL;
}

static void
send_archive_notification (const gchar *title, gboolean success, const gchar *detail_on_error)
{
    GNotification *notification = g_notification_new (title);

    if (success) {
        g_notification_set_body (notification, _("Erfolgreich abgeschlossen."));
    } else {
        gchar *body = g_strdup_printf (_("Fehlgeschlagen: %s"), detail_on_error);
        g_notification_set_body (notification, body);
        g_free (body);
    }

    g_application_send_notification (G_APPLICATION (nolphin_application_get_singleton ()), NULL, notification);
    g_object_unref (notification);
}

/* Zeigt die Archiv-Seite der rechten Arbeitsbereich-Leiste, in der Format,
 * Dateiname und Zielordner gewählt werden können, statt im Hintergrund
 * stillschweigend immer ein ZIP mit automatischem Namen anzulegen (siehe
 * nolphin-workspace-panel.c, build_archive_tab()). */
static void
action_compress_callback (GtkAction *action,
                          gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    NolphinWindow *window;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (selection == NULL) {
        return;
    }
    nolphin_file_list_free (selection);

    window = NOLPHIN_WINDOW (nolphin_view_get_containing_window (view));
    nolphin_workspace_panel_show_archive (nolphin_window_get_workspace_panel (window), window);
}

static void
extract_here_finished_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_archive_extract_finish (result, &error);

    /* Im Fenster „Dateivorgänge“ abgebrochen: keine Fehlermeldung. */
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        send_archive_notification (_("Entpacken"), success, error ? error->message : NULL);
    }
    g_clear_error (&error);
}

static void
action_extract_here_callback (GtkAction *action,
                              gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    GFile *archive_location;
    GFile *parent;
    NolphinArchiveFormat format;
    gchar *base_name;
    const gchar *ext;
    GFile *destination;
    GError *error = NULL;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    archive_location = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
    format = nolphin_archive_detect_format (archive_location);
    if (format == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
        g_object_unref (archive_location);
        nolphin_file_list_free (selection);
        return;
    }

    parent = g_file_get_parent (archive_location);
    base_name = g_file_get_basename (archive_location);
    ext = nolphin_archive_format_get_extension (format);
    if (g_str_has_suffix (base_name, ext)) {
        base_name[strlen (base_name) - strlen (ext)] = '\0';
    }

    destination = find_unique_destination (parent, base_name, "");
    g_free (base_name);
    g_object_unref (parent);

    if (destination == NULL || !g_file_make_directory (destination, NULL, &error)) {
        send_archive_notification (_("Entpacken"), FALSE,
                                   error ? error->message : _("Zielordner konnte nicht angelegt werden."));
        g_clear_error (&error);
        g_clear_object (&destination);
        g_object_unref (archive_location);
        nolphin_file_list_free (selection);
        return;
    }

    nolphin_archive_extract_async (archive_location, destination, NULL, extract_here_finished_cb, NULL);

    g_object_unref (destination);
    g_object_unref (archive_location);
    nolphin_file_list_free (selection);
}

/* Bearbeiten ▸ Kopieren als ▸ Pfad / Dateiname */
static void
copy_selection_text (NolphinView *view, gboolean full_path)
{
	GList *selection, *l;
	GString *text;

	selection = nolphin_view_get_selection (view);
	if (selection == NULL) {
		return;
	}

	text = g_string_new (NULL);
	for (l = selection; l != NULL; l = l->next) {
		gchar *item;

		if (full_path) {
			GFile *location = nolphin_file_get_location (NOLPHIN_FILE (l->data));

			item = g_file_is_native (location) ? g_file_get_path (location) : g_file_get_uri (location);
			g_object_unref (location);
		} else {
			item = nolphin_file_get_display_name (NOLPHIN_FILE (l->data));
		}

		if (text->len > 0) {
			g_string_append_c (text, '\n');
		}
		g_string_append (text, item);
		g_free (item);
	}

	gtk_clipboard_set_text (gtk_clipboard_get_for_display (gtk_widget_get_display (GTK_WIDGET (view)),
							       GDK_SELECTION_CLIPBOARD),
				text->str, -1);

	g_string_free (text, TRUE);
	nolphin_file_list_free (selection);
}

static void
action_copy_path_callback (GtkAction *action, gpointer callback_data)
{
	copy_selection_text (NOLPHIN_VIEW (callback_data), TRUE);
}

static void
action_copy_filename_callback (GtkAction *action, gpointer callback_data)
{
	copy_selection_text (NOLPHIN_VIEW (callback_data), FALSE);
}

static void
show_view_error (NolphinView *view, const gchar *message)
{
	GtkWidget *dialog = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
						     GTK_DIALOG_DESTROY_WITH_PARENT,
						     GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						     "%s", message);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/* Datei ▸ Verknüpfung erstellen ▸ Hardlink: nur Dateien, nur innerhalb
 * desselben Dateisystems (link(2)); sonst verständliche Meldung. */
static void
action_create_hardlink_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection, *l;
	gchar *first_error = NULL;
	guint created = 0;

	selection = nolphin_view_get_selection (view);

	for (l = selection; l != NULL; l = l->next) {
		NolphinFile *file = NOLPHIN_FILE (l->data);
		GFile *src = nolphin_file_get_location (file);
		gchar *display = nolphin_file_get_display_name (file);
		gchar *src_path = g_file_get_path (src);
		const gchar *problem = NULL;

		if (nolphin_file_is_directory (file)) {
			problem = _("Hardlinks sind nur für Dateien möglich, nicht für Ordner.");
		} else if (src_path == NULL) {
			problem = _("Hardlinks sind nur für lokale Dateien möglich.");
		} else {
			GFile *parent = g_file_get_parent (src);
			gchar *link_name = g_strdup_printf (_("Hardlink zu %s"), display);
			GFile *dest = parent != NULL ? find_unique_destination (parent, link_name, "") : NULL;

			if (dest == NULL) {
				problem = _("Es konnte kein freier Name für den Hardlink gefunden werden.");
			} else {
				gchar *dest_path = g_file_get_path (dest);

				if (link (src_path, dest_path) == 0) {
					created++;
				} else if (errno == EXDEV) {
					problem = _("Hardlinks sind nur innerhalb desselben Dateisystems möglich.");
				} else {
					problem = g_strerror (errno);
				}
				g_free (dest_path);
				g_object_unref (dest);
			}
			g_free (link_name);
			g_clear_object (&parent);
		}

		if (problem != NULL && first_error == NULL) {
			first_error = g_strdup_printf ("%s: %s", display, problem);
		}

		g_free (src_path);
		g_free (display);
		g_object_unref (src);
	}

	if (first_error != NULL) {
		show_view_error (view, first_error);
		g_free (first_error);
	}
	(void) created;

	nolphin_file_list_free (selection);
}

/* Bearbeiten ▸ Zwischenablage als Datei einfügen: Bild oder Text der
 * Zwischenablage wird als neue Datei im aktuellen Ordner angelegt. */
static void
action_paste_clipboard_as_file_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GtkClipboard *clipboard;
	gchar *uri;
	GFile *folder, *dest = NULL;
	GError *error = NULL;

	clipboard = gtk_clipboard_get_for_display (gtk_widget_get_display (GTK_WIDGET (view)),
						   GDK_SELECTION_CLIPBOARD);
	uri = nolphin_view_get_backing_uri (view);
	if (uri == NULL) {
		return;
	}
	folder = g_file_new_for_uri (uri);
	g_free (uri);

	if (gtk_clipboard_wait_is_image_available (clipboard)) {
		GdkPixbuf *pixbuf = gtk_clipboard_wait_for_image (clipboard);

		dest = find_unique_destination (folder, _("Zwischenablage.png"), ".png");
		if (pixbuf != NULL && dest != NULL) {
			GFileOutputStream *stream = g_file_create (dest, G_FILE_CREATE_NONE, NULL, &error);

			if (stream != NULL) {
				gdk_pixbuf_save_to_stream (pixbuf, G_OUTPUT_STREAM (stream), "png", NULL, &error, NULL);
				g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, error == NULL ? &error : NULL);
				g_object_unref (stream);
			}
		}
		g_clear_object (&pixbuf);
	} else if (gtk_clipboard_wait_is_text_available (clipboard)) {
		gchar *text = gtk_clipboard_wait_for_text (clipboard);

		dest = find_unique_destination (folder, _("Zwischenablage.txt"), ".txt");
		if (text != NULL && dest != NULL) {
			g_file_replace_contents (dest, text, strlen (text), NULL, FALSE,
						 G_FILE_CREATE_NONE, NULL, NULL, &error);
		}
		g_free (text);
	} else {
		show_view_error (view, _("Die Zwischenablage enthält weder Text noch ein Bild."));
		g_object_unref (folder);
		return;
	}

	if (error != NULL) {
		show_view_error (view, error->message);
		g_clear_error (&error);
	} else if (dest == NULL) {
		show_view_error (view, _("Es konnte kein freier Dateiname gefunden werden."));
	} else {
		send_archive_notification (_("Zwischenablage einfügen"), TRUE, NULL);
	}

	g_clear_object (&dest);
	g_object_unref (folder);
}

/* Bearbeiten ▸ Nach Größe/Datum auswählen … (ergänzt Muster und Typ). Die
 * Eingabe erscheint als Popover an der Ansicht statt in einem Dialog. */
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
criteria_select_apply (GtkWidget *popover)
{
	NolphinView *view = g_object_get_data (G_OBJECT (popover), "criteria-view");
	goffset min_size = (goffset) gtk_spin_button_get_value (GTK_SPIN_BUTTON (g_object_get_data (G_OBJECT (popover), "min"))) * 1024;
	goffset max_size = (goffset) gtk_spin_button_get_value (GTK_SPIN_BUTTON (g_object_get_data (G_OBJECT (popover), "max"))) * 1024;
	gint days = (gint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (g_object_get_data (G_OBJECT (popover), "days")));
	time_t cutoff = days > 0 ? time (NULL) - (time_t) days * 86400 : 0;
	GList *all, *l, *matches = NULL;

	all = nolphin_directory_get_file_list (nolphin_view_get_model (view));
	for (l = all; l != NULL; l = l->next) {
		NolphinFile *file = NOLPHIN_FILE (l->data);

		if (nolphin_file_is_directory (file)) {
			continue;
		}
		if (nolphin_file_get_size (file) < min_size) {
			continue;
		}
		if (max_size > 0 && nolphin_file_get_size (file) > max_size) {
			continue;
		}
		if (cutoff > 0 && nolphin_file_get_mtime (file) < cutoff) {
			continue;
		}
		matches = g_list_prepend (matches, nolphin_file_ref (file));
	}
	nolphin_file_list_free (all);

	if (matches != NULL) {
		nolphin_view_call_set_selection (view, matches);
		nolphin_file_list_free (matches);
		nolphin_view_reveal_selection (view);
	}

	gtk_widget_destroy (popover);
}

static void
criteria_select_ok_cb (GtkButton *button, gpointer popover)
{
	criteria_select_apply (GTK_WIDGET (popover));
}

static void
action_select_criteria_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GtkWidget *popover, *grid, *label, *min, *max, *days, *ok;
	GdkRectangle anchor = { 40, 8, 1, 1 };

	popover = gtk_popover_new (GTK_WIDGET (view));
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &anchor);
	gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_BOTTOM);

	grid = gtk_grid_new ();
	g_object_set (grid, "border-width", 12, "row-spacing", 6, "column-spacing", 12, NULL);

	min = gtk_spin_button_new_with_range (0, 100000000, 1);
	max = gtk_spin_button_new_with_range (0, 100000000, 1);
	days = gtk_spin_button_new_with_range (0, 36500, 1);

	label = gtk_label_new (_("Mindestgröße (KiB):"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), min, 1, 0, 1, 1);
	label = gtk_label_new (_("Höchstgröße (KiB, 0 = unbegrenzt):"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), max, 1, 1, 1, 1);
	label = gtk_label_new (_("Geändert in den letzten (Tage, 0 = beliebig):"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), days, 1, 2, 1, 1);
	ok = gtk_button_new_with_label (_("Auswählen"));
	gtk_style_context_add_class (gtk_widget_get_style_context (ok), "suggested-action");
	gtk_grid_attach (GTK_GRID (grid), ok, 0, 3, 2, 1);

	gtk_container_add (GTK_CONTAINER (popover), grid);
	g_object_set_data (G_OBJECT (popover), "criteria-view", view);
	g_object_set_data (G_OBJECT (popover), "min", min);
	g_object_set_data (G_OBJECT (popover), "max", max);
	g_object_set_data (G_OBJECT (popover), "days", days);
	g_signal_connect (ok, "clicked", G_CALLBACK (criteria_select_ok_cb), popover);

	gtk_widget_show_all (grid);
	popover_popup_later (popover, min);
}

/* Archiv öffnen: Inhalt im Archiv-Panel anzeigen und verwalten. */
static void
action_archive_manage_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection = nolphin_view_get_selection (view);

	if (g_list_length (selection) == 1) {
		NolphinWindow *window = NOLPHIN_WINDOW (nolphin_view_get_containing_window (view));
		GFile *archive = nolphin_file_get_location (NOLPHIN_FILE (selection->data));

		nolphin_workspace_panel_show_archive_manager (nolphin_window_get_workspace_panel (window), window, archive);
		g_object_unref (archive);
	}
	nolphin_file_list_free (selection);
}

/* Archiv entpacken nach … (Zielordner wählen). */
static void
action_extract_to_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection = nolphin_view_get_selection (view);
	GtkWidget *chooser;

	if (g_list_length (selection) == 1) {
		GFile *archive = nolphin_file_get_location (NOLPHIN_FILE (selection->data));

		if (nolphin_archive_detect_format (archive) != NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
			chooser = gtk_file_chooser_dialog_new (_("Entpacken nach …"),
							       nolphin_view_get_containing_window (view),
							       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
							       _("_Abbrechen"), GTK_RESPONSE_CANCEL,
							       _("_Entpacken"), GTK_RESPONSE_ACCEPT, NULL);
			if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
				GFile *destination = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (chooser));

				if (destination != NULL) {
					nolphin_archive_extract_async (archive, destination, NULL,
								       extract_here_finished_cb, NULL);
					g_object_unref (destination);
				}
			}
			gtk_widget_destroy (chooser);
		}
		g_object_unref (archive);
	}

	nolphin_file_list_free (selection);
}

static void
test_archive_finished_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_archive_test_finish (result, &error);

    /* Im Fenster „Dateivorgänge“ abgebrochen: keine Fehlermeldung. */
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        send_archive_notification (_("Archiv prüfen"), success, error ? error->message : NULL);
    }
    g_clear_error (&error);
}

static void
action_test_archive_callback (GtkAction *action,
                              gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    GFile *archive_location;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    archive_location = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
    if (nolphin_archive_detect_format (archive_location) == NOLPHIN_ARCHIVE_FORMAT_UNKNOWN) {
        g_object_unref (archive_location);
        nolphin_file_list_free (selection);
        return;
    }

    nolphin_archive_test_async (archive_location, NULL, test_archive_finished_cb, NULL);

    g_object_unref (archive_location);
    nolphin_file_list_free (selection);
}

/* Prüfsummen - berechnen und mit einem eingegebenen Wert vergleichen. */

typedef struct {
    GFile *file;
    GtkWidget *type_combo;
    GtkWidget *compute_button;
    GtkWidget *spinner;
    GtkWidget *result_entry;
    GtkWidget *expected_entry;
    GtkWidget *match_label;
    NolphinChecksumType shown_types[5];
    guint shown_count;
} ChecksumDialogData;

static void
checksum_dialog_data_free (ChecksumDialogData *data)
{
    g_clear_object (&data->file);
    g_free (data);
}

static void
checksum_update_match_label (ChecksumDialogData *data)
{
    const gchar *result = gtk_entry_get_text (GTK_ENTRY (data->result_entry));
    const gchar *expected = gtk_entry_get_text (GTK_ENTRY (data->expected_entry));

    if (result[0] == '\0' || expected[0] == '\0') {
        gtk_label_set_text (GTK_LABEL (data->match_label), "");
        return;
    }

    if (nolphin_checksum_matches (result, expected)) {
        gtk_label_set_text (GTK_LABEL (data->match_label), _("Stimmt überein"));
        gtk_style_context_remove_class (gtk_widget_get_style_context (data->match_label), "error");
        gtk_style_context_add_class (gtk_widget_get_style_context (data->match_label), "success");
    } else {
        gtk_label_set_text (GTK_LABEL (data->match_label), _("Stimmt nicht überein"));
        gtk_style_context_remove_class (gtk_widget_get_style_context (data->match_label), "success");
        gtk_style_context_add_class (gtk_widget_get_style_context (data->match_label), "error");
    }
}

static void
checksum_expected_changed_cb (GtkEditable *editable, gpointer user_data)
{
    checksum_update_match_label (user_data);
}

static void
checksum_compute_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    ChecksumDialogData *data = user_data;
    GError *error = NULL;
    gchar *digest;

    gtk_spinner_stop (GTK_SPINNER (data->spinner));
    gtk_widget_hide (data->spinner);
    gtk_widget_set_sensitive (data->compute_button, TRUE);

    digest = nolphin_checksum_compute_finish (result, &error);
    if (digest == NULL) {
        gtk_entry_set_text (GTK_ENTRY (data->result_entry), "");
        gtk_label_set_text (GTK_LABEL (data->match_label),
                            error ? error->message : _("Unbekannter Fehler"));
        g_clear_error (&error);
        return;
    }

    gtk_entry_set_text (GTK_ENTRY (data->result_entry), digest);
    g_free (digest);

    checksum_update_match_label (data);
}

static void
checksum_compute_clicked_cb (GtkButton *button, gpointer user_data)
{
    ChecksumDialogData *data = user_data;
    gint active;
    NolphinChecksumType type;

    active = gtk_combo_box_get_active (GTK_COMBO_BOX (data->type_combo));
    if (active < 0 || (guint) active >= data->shown_count) {
        return;
    }
    type = data->shown_types[active];

    gtk_entry_set_text (GTK_ENTRY (data->result_entry), "");
    gtk_label_set_text (GTK_LABEL (data->match_label), "");
    gtk_widget_set_sensitive (data->compute_button, FALSE);
    gtk_widget_show (data->spinner);
    gtk_spinner_start (GTK_SPINNER (data->spinner));

    nolphin_checksum_compute_async (data->file, type, NULL, checksum_compute_ready_cb, data);
}

static void
checksum_dialog_response_cb (GtkDialog *dialog, int response, gpointer user_data)
{
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
action_compute_checksum_callback (GtkAction *action,
                                  gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    ChecksumDialogData *data;
    GtkWidget *dialog, *grid, *label, *hbox;
    static const NolphinChecksumType all_types[] = {
        NOLPHIN_CHECKSUM_MD5, NOLPHIN_CHECKSUM_SHA1, NOLPHIN_CHECKSUM_SHA256,
        NOLPHIN_CHECKSUM_SHA512, NOLPHIN_CHECKSUM_BLAKE2
    };
    guint i;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1 || nolphin_file_is_directory (NOLPHIN_FILE (selection->data))) {
        nolphin_file_list_free (selection);
        return;
    }

    data = g_new0 (ChecksumDialogData, 1);
    data->file = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
    nolphin_file_list_free (selection);

    dialog = gtk_dialog_new_with_buttons (_("Prüfsumme berechnen"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                          NULL);
    g_object_set_data_full (G_OBJECT (dialog), "checksum-data", data,
                            (GDestroyNotify) checksum_dialog_data_free);

    grid = gtk_grid_new ();
    g_object_set (grid, "border-width", 12, "row-spacing", 8, "column-spacing", 12, NULL);

    label = gtk_label_new (_("Algorithmus:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    data->type_combo = gtk_combo_box_text_new ();
    for (i = 0; i < G_N_ELEMENTS (all_types); i++) {
        if (nolphin_checksum_type_is_available (all_types[i])) {
            gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (data->type_combo),
                                            nolphin_checksum_type_get_label (all_types[i]));
            data->shown_types[data->shown_count++] = all_types[i];
        }
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (data->type_combo), 0);
    gtk_widget_set_hexpand (data->type_combo, TRUE);
    gtk_grid_attach (GTK_GRID (grid), data->type_combo, 1, 0, 1, 1);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    data->compute_button = gtk_button_new_with_label (_("Berechnen"));
    data->spinner = gtk_spinner_new ();
    gtk_box_pack_start (GTK_BOX (hbox), data->compute_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), data->spinner, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), hbox, 1, 1, 1, 1);

    label = gtk_label_new (_("Ergebnis:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
    data->result_entry = gtk_entry_new ();
    gtk_editable_set_editable (GTK_EDITABLE (data->result_entry), FALSE);
    gtk_entry_set_width_chars (GTK_ENTRY (data->result_entry), 40);
    gtk_grid_attach (GTK_GRID (grid), data->result_entry, 1, 2, 1, 1);

    label = gtk_label_new (_("Vergleichswert:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);
    data->expected_entry = gtk_entry_new ();
    gtk_grid_attach (GTK_GRID (grid), data->expected_entry, 1, 3, 1, 1);

    data->match_label = gtk_label_new ("");
    gtk_grid_attach (GTK_GRID (grid), data->match_label, 1, 4, 1, 1);

    g_signal_connect (data->compute_button, "clicked", G_CALLBACK (checksum_compute_clicked_cb), data);
    g_signal_connect (data->expected_entry, "changed", G_CALLBACK (checksum_expected_changed_cb), data);
    g_signal_connect (dialog, "response", G_CALLBACK (checksum_dialog_response_cb), NULL);

    gtk_widget_show_all (grid);
    gtk_widget_hide (data->spinner);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
    gtk_widget_show (dialog);
}

/* Verschlüsselung über gpg - Datei verschlüsseln/entschlüsseln,
 * Ordner verschlüsseln (als verschlüsseltes Archiv: erst komprimieren,
 * dann das Archiv verschlüsseln, danach das unverschlüsselte
 * Zwischenarchiv löschen). */

static gchar *
prompt_passphrase (GtkWindow *parent, const gchar *title, gboolean confirm)
{
    GtkWidget *dialog, *grid, *label, *entry, *confirm_entry = NULL;
    gchar *result = NULL;

    dialog = gtk_dialog_new_with_buttons (title, parent,
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

    grid = gtk_grid_new ();
    g_object_set (grid, "border-width", 12, "row-spacing", 8, "column-spacing", 12, NULL);

    label = gtk_label_new (_("Passwort:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
    gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
    gtk_widget_set_hexpand (entry, TRUE);
    gtk_grid_attach (GTK_GRID (grid), entry, 1, 0, 1, 1);

    if (confirm) {
        label = gtk_label_new (_("Passwort bestätigen:"));
        gtk_widget_set_halign (label, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

        confirm_entry = gtk_entry_new ();
        gtk_entry_set_visibility (GTK_ENTRY (confirm_entry), FALSE);
        gtk_entry_set_activates_default (GTK_ENTRY (confirm_entry), TRUE);
        gtk_grid_attach (GTK_GRID (grid), confirm_entry, 1, 1, 1, 1);
    }

    gtk_widget_show_all (grid);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);

    while (TRUE) {
        int response = gtk_dialog_run (GTK_DIALOG (dialog));
        const gchar *pass;

        if (response != GTK_RESPONSE_OK) {
            break;
        }

        pass = gtk_entry_get_text (GTK_ENTRY (entry));
        if (pass[0] == '\0') {
            continue;
        }

        if (confirm) {
            const gchar *pass2 = gtk_entry_get_text (GTK_ENTRY (confirm_entry));
            if (strcmp (pass, pass2) != 0) {
                gtk_entry_set_text (GTK_ENTRY (entry), "");
                gtk_entry_set_text (GTK_ENTRY (confirm_entry), "");
                gtk_widget_grab_focus (entry);
                continue;
            }
        }

        result = g_strdup (pass);
        break;
    }

    gtk_widget_destroy (dialog);
    return result;
}

static void
send_encryption_notification (const gchar *title, gboolean success, const gchar *detail_on_error)
{
    send_archive_notification (title, success, detail_on_error);
}

static void
encrypt_finished_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_encryption_encrypt_finish (result, &error);

    send_encryption_notification (_("Verschlüsseln"), success, error ? error->message : NULL);
    g_clear_error (&error);
}

typedef struct {
    GFile *archive_file;
    GFile *encrypted_file;
    gchar *passphrase;
} EncryptFolderData;

static void
encrypt_folder_data_free (EncryptFolderData *data)
{
    g_clear_object (&data->archive_file);
    g_clear_object (&data->encrypted_file);
    g_free (data->passphrase);
    g_free (data);
}

static void
encrypt_folder_archive_encrypted_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    EncryptFolderData *data = user_data;
    GError *error = NULL;
    gboolean success = nolphin_encryption_encrypt_finish (result, &error);

    /* The intermediate plaintext archive is temporary - remove it
     * either way, it's not what the user asked to keep. */
    g_file_delete (data->archive_file, NULL, NULL);

    send_encryption_notification (_("Ordner verschlüsseln"), success, error ? error->message : NULL);
    g_clear_error (&error);

    encrypt_folder_data_free (data);
}

static void
encrypt_folder_compressed_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    EncryptFolderData *data = user_data;
    GError *error = NULL;

    if (!nolphin_archive_compress_finish (result, &error)) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            send_encryption_notification (_("Ordner verschlüsseln"), FALSE, error->message);
        }
        g_clear_error (&error);
        encrypt_folder_data_free (data);
        return;
    }

    nolphin_encryption_encrypt_async (data->archive_file, data->encrypted_file, data->passphrase,
                                      NULL, encrypt_folder_archive_encrypted_cb, data);
}

static void
action_encrypt_callback (GtkAction *action,
                         gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    NolphinFile *file;
    GFile *location, *parent;
    gchar *display_name, *passphrase;
    gboolean is_dir;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    file = NOLPHIN_FILE (selection->data);
    location = nolphin_file_get_location (file);
    is_dir = nolphin_file_is_directory (file);
    display_name = nolphin_file_get_display_name (file);
    nolphin_file_list_free (selection);

    passphrase = prompt_passphrase (nolphin_view_get_containing_window (view),
                                    _("Verschlüsseln"), TRUE);
    if (passphrase == NULL) {
        g_object_unref (location);
        g_free (display_name);
        return;
    }

    parent = g_file_get_parent (location);

    if (is_dir) {
        GFile *archive_file;
        GFile *encrypted_file;
        gchar *archive_name, *encrypted_name;
        GList *sources = g_list_prepend (NULL, location);
        EncryptFolderData *data;

        archive_name = g_strconcat (display_name, ".tar.gz", NULL);
        archive_file = find_unique_destination (parent, archive_name, "");
        g_free (archive_name);

        encrypted_name = g_strconcat (display_name, ".tar.gz.gpg", NULL);
        encrypted_file = find_unique_destination (parent, encrypted_name, "");
        g_free (encrypted_name);

        if (archive_file == NULL || encrypted_file == NULL) {
            send_encryption_notification (_("Ordner verschlüsseln"), FALSE,
                                          _("Konnte keinen eindeutigen Zieldateinamen finden."));
            g_clear_object (&archive_file);
            g_clear_object (&encrypted_file);
            g_list_free (sources);
        } else {
            data = g_new0 (EncryptFolderData, 1);
            data->archive_file = archive_file;
            data->encrypted_file = encrypted_file;
            data->passphrase = g_strdup (passphrase);

            nolphin_archive_compress_async (sources, archive_file, NOLPHIN_ARCHIVE_FORMAT_TAR_GZ,
                                            NULL, 0, NULL, encrypt_folder_compressed_cb, data);
            g_list_free (sources);
        }
    } else {
        gchar *encrypted_name = g_strconcat (display_name, ".gpg", NULL);
        GFile *encrypted_file = find_unique_destination (parent, encrypted_name, "");
        g_free (encrypted_name);

        if (encrypted_file == NULL) {
            send_encryption_notification (_("Verschlüsseln"), FALSE,
                                          _("Konnte keinen eindeutigen Zieldateinamen finden."));
        } else {
            nolphin_encryption_encrypt_async (location, encrypted_file, passphrase,
                                              NULL, encrypt_finished_cb, NULL);
            g_object_unref (encrypted_file);
        }
    }

    g_object_unref (parent);
    g_object_unref (location);
    g_free (display_name);
    /* Overwrite the passphrase in memory before freeing - it already
     * did its job (handed to gpg via stdin), no reason to keep a
     * readable copy in freed heap memory longer than necessary. */
    if (passphrase != NULL) {
        memset (passphrase, 0, strlen (passphrase));
    }
    g_free (passphrase);
}

static void
decrypt_finished_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_encryption_decrypt_finish (result, &error);

    send_encryption_notification (_("Entschlüsseln"), success, error ? error->message : NULL);
    g_clear_error (&error);
}

static void
action_decrypt_callback (GtkAction *action,
                         gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    GFile *location, *parent;
    gchar *display_name, *passphrase, *dest_name;
    GFile *dest_file;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    location = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
    display_name = nolphin_file_get_display_name (NOLPHIN_FILE (selection->data));
    nolphin_file_list_free (selection);

    if (!g_str_has_suffix (display_name, ".gpg")) {
        g_object_unref (location);
        g_free (display_name);
        return;
    }

    passphrase = prompt_passphrase (nolphin_view_get_containing_window (view),
                                    _("Entschlüsseln"), FALSE);
    if (passphrase == NULL) {
        g_object_unref (location);
        g_free (display_name);
        return;
    }

    dest_name = g_strndup (display_name, strlen (display_name) - strlen (".gpg"));
    parent = g_file_get_parent (location);
    dest_file = find_unique_destination (parent, dest_name, "");
    g_free (dest_name);
    g_object_unref (parent);

    if (dest_file == NULL) {
        send_encryption_notification (_("Entschlüsseln"), FALSE,
                                      _("Konnte keinen eindeutigen Zieldateinamen finden."));
    } else {
        nolphin_encryption_decrypt_async (location, dest_file, passphrase,
                                          NULL, decrypt_finished_cb, NULL);
        g_object_unref (dest_file);
    }

    g_object_unref (location);
    g_free (display_name);
    if (passphrase != NULL) {
        memset (passphrase, 0, strlen (passphrase));
    }
    g_free (passphrase);
}

/* ACL (weitere Benutzer und Gruppen) wird im Eigenschaften-Panel
 * bearbeitet (nolphin-properties-panel.c, Abschnitt "Weitere Benutzer und
 * Gruppen") - es gibt keinen eigenen Dialog mehr. Diese Aktion zeigt nur
 * noch dieses Panel, falls sie z. B. per Tastenkürzel ausgelöst wird. */
static void
action_edit_acl_callback (GtkAction *action,
                          gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GList *selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) == 1) {
        NolphinWindow *window = NOLPHIN_WINDOW (nolphin_view_get_containing_window (view));

        nolphin_workspace_panel_show_properties (nolphin_window_get_workspace_panel (window), window, selection);
    }
    nolphin_file_list_free (selection);
}

/* Git - Status, Hinzufügen, Commit, Pull, Push, Log, Diff über
 * das Systemwerkzeug git. */

/* Finds the git repository root for the view's current selection (or
 * its directory if nothing is selected), then calls @found_cb with
 * it - or shows an error dialog and calls nothing if it's not inside
 * a repository or the lookup itself failed. Centralizes the
 * "resolve which repo we're even talking about" step shared by every
 * git action below. */
typedef void (*GitRootFoundCallback) (GFile *repo_root, GFile *selected_file /* nullable */, gpointer user_data);

typedef struct {
    GitRootFoundCallback found_cb;
    gpointer             found_cb_data;
    GFile               *selected_file;
    GFile               *start_file;
    GtkWindow            *parent_window;
} GitRootLookup;

/* Legt per "git init" ein neues Repository an. Läuft synchron: das ist
 * eine Sache von Millisekunden und braucht keinen Netzwerkzugriff. */
static gboolean
git_init_repository (GFile *start_file, GFile **out_root, GError **error)
{
    GFile *dir;
    gchar *path;
    gchar *argv[] = { "git", "init", NULL };
    gchar *std_err = NULL;
    gint status = 0;
    gboolean ok;

    if (g_file_query_file_type (start_file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY) {
        dir = g_object_ref (start_file);
    } else {
        dir = g_file_get_parent (start_file);
    }
    path = (dir != NULL) ? g_file_get_path (dir) : NULL;

    if (path == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             _("Für diesen Ort kann kein Git-Repository angelegt werden."));
        g_clear_object (&dir);
        return FALSE;
    }

    ok = g_spawn_sync (path, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &std_err, &status, error)
         && g_spawn_check_exit_status (status, NULL);
    if (!ok && error != NULL && *error == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                     (std_err != NULL && *std_err != '\0') ? std_err : _("git init ist fehlgeschlagen."));
    }

    if (ok) {
        *out_root = g_object_ref (dir);
    }

    g_free (std_err);
    g_free (path);
    g_object_unref (dir);
    return ok;
}

static void
git_root_lookup_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GitRootLookup *lookup = user_data;
    GError *error = NULL;
    GFile *root = nolphin_git_find_repository_root_finish (result, &error);

    if (error != NULL) {
        GtkWidget *dialog = gtk_message_dialog_new (lookup->parent_window, GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                     "%s", error->message);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        g_clear_error (&error);
    } else if (root == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new (lookup->parent_window, GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                     "%s", _("Dieser Ort ist noch kein Git-Repository."));
        GFile *new_root = NULL;

        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
                                                  _("Soll hier ein neues Repository angelegt werden? Danach kannst du Dateien hinzufügen, committen und mit „Remote hinzufügen“ zu GitHub hochladen."));
        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                _("Abbrechen"), GTK_RESPONSE_CANCEL,
                                _("Repository anlegen"), GTK_RESPONSE_ACCEPT, NULL);

        if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
            if (git_init_repository (lookup->start_file, &new_root, &error)) {
                lookup->found_cb (new_root, lookup->selected_file, lookup->found_cb_data);
                g_object_unref (new_root);
            } else {
                GtkWidget *err = gtk_message_dialog_new (lookup->parent_window, GTK_DIALOG_DESTROY_WITH_PARENT,
                                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                          "%s", error != NULL ? error->message : "");
                gtk_dialog_run (GTK_DIALOG (err));
                gtk_widget_destroy (err);
                g_clear_error (&error);
            }
        }
        gtk_widget_destroy (dialog);
    } else {
        lookup->found_cb (root, lookup->selected_file, lookup->found_cb_data);
        g_object_unref (root);
    }

    g_clear_object (&lookup->selected_file);
    g_clear_object (&lookup->start_file);
    g_free (lookup);
}

static void
git_resolve_repo_root (NolphinView *view, GitRootFoundCallback found_cb, gpointer found_cb_data)
{
    GList *selection;
    GFile *start_file;
    GFile *selected_file = NULL;
    GitRootLookup *lookup;

    selection = nolphin_view_get_selection (view);
    if (g_list_length (selection) == 1) {
        selected_file = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
        start_file = g_object_ref (selected_file);
    } else {
        start_file = nolphin_file_get_location (nolphin_view_get_directory_as_file (view));
    }
    nolphin_file_list_free (selection);

    lookup = g_new0 (GitRootLookup, 1);
    lookup->found_cb = found_cb;
    lookup->found_cb_data = found_cb_data;
    lookup->selected_file = selected_file;
    lookup->parent_window = nolphin_view_get_containing_window (view);
    lookup->start_file = g_object_ref (start_file);

    nolphin_git_find_repository_root_async (start_file, NULL, git_root_lookup_ready_cb, lookup);
    g_object_unref (start_file);
}

/* Status, Hinzufügen, Commit, Pull, Push, Log und Diff zeigen alle
 * denselben Git-Reiter der rechten Arbeitsbereich-Leiste - dort stehen
 * eigene Knöpfe für jede dieser Aktionen (siehe nolphin-workspace-panel.c,
 * build_git_tab()), statt dass jede Aktion hier ihr eigenes Dialogfenster
 * öffnet. Nur "Remote hinzufügen" (unten) braucht weiterhin einen eigenen
 * kleinen Eingabedialog, da der Reiter dafür keine Felder hat. */
static void
git_menu_action_show_tab (NolphinView *view)
{
    NolphinWindow *window = NOLPHIN_WINDOW (nolphin_view_get_containing_window (view));

    nolphin_workspace_panel_show_git (nolphin_window_get_workspace_panel (window), window);
}

static void
action_git_status_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_add_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_commit_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_pull_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_push_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_log_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

static void
action_git_diff_callback (GtkAction *action, gpointer callback_data)
{
    git_menu_action_show_tab (NOLPHIN_VIEW (callback_data));
}

void
nolphin_view_activate_action_by_name (NolphinView *view, const gchar *action_name)
{
    GtkAction *action;

    g_return_if_fail (NOLPHIN_IS_VIEW (view));
    g_return_if_fail (action_name != NULL);

    action = gtk_action_group_get_action (view->details->dir_action_group, action_name);
    if (action != NULL && gtk_action_is_sensitive (action)) {
        gtk_action_activate (action);
    }
}

static void
git_sync_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    gboolean apply = GPOINTER_TO_INT (user_data);
    GError *error = NULL;
    gchar *text = nolphin_git_sync_finish (result, &error);
    GNotification *notification;

    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error (&error);
        return;
    }

    notification = g_notification_new (apply ? _("Git: Synchronisieren") : _("Git: Abgleich"));
    if (text != NULL) {
        g_notification_set_body (notification, text);
    } else {
        gchar *body = g_strdup_printf (_("Fehlgeschlagen: %s"), error != NULL ? error->message : "");
        g_notification_set_body (notification, body);
        g_free (body);
    }
    g_application_send_notification (G_APPLICATION (nolphin_application_get_singleton ()), NULL, notification);
    g_object_unref (notification);
    g_free (text);
    g_clear_error (&error);
}

static void
git_sync_root_found_cb (GFile *repo_root, GFile *selected_file, gpointer user_data)
{
    nolphin_git_sync_async (repo_root, GPOINTER_TO_INT (user_data), NULL, git_sync_ready_cb, user_data);
}

static void
action_git_compare_callback (GtkAction *action, gpointer callback_data)
{
    git_resolve_repo_root (NOLPHIN_VIEW (callback_data), git_sync_root_found_cb, GINT_TO_POINTER (FALSE));
}

static void
action_git_sync_callback (GtkAction *action, gpointer callback_data)
{
    git_resolve_repo_root (NOLPHIN_VIEW (callback_data), git_sync_root_found_cb, GINT_TO_POINTER (TRUE));
}

static void
git_clone_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_git_clone_finish (result, &error);

    /* Im Fenster „Dateivorgänge“ abgebrochen: keine Fehlermeldung. */
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        send_archive_notification (_("Git: Repository klonen"), success, error ? error->message : NULL);
    }
    g_clear_error (&error);
}

/* Lädt ein Repository per URL in den aktuell geöffneten Ordner. Braucht
 * keinen bestehenden Repository-Kontext - deshalb ohne
 * git_resolve_repo_root(). */
static void
action_git_clone_callback (GtkAction *action, gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GtkWidget *dialog, *label, *url_entry, *box;
    int response;

    dialog = gtk_dialog_new_with_buttons (_("Repository klonen"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          _("Abbrechen"), GTK_RESPONSE_CANCEL,
                                          _("Klonen"), GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (box), 12);

    label = gtk_label_new (_("Adresse des Repositorys (wird in den aktuellen Ordner heruntergeladen):"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

    url_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (url_entry), "https://github.com/user/repo.git");
    gtk_entry_set_activates_default (GTK_ENTRY (url_entry), TRUE);
    gtk_widget_set_size_request (url_entry, 420, -1);
    gtk_box_pack_start (GTK_BOX (box), url_entry, FALSE, FALSE, 0);

    gtk_widget_show_all (box);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), box);
    gtk_widget_grab_focus (url_entry);

    response = gtk_dialog_run (GTK_DIALOG (dialog));
    if (response == GTK_RESPONSE_OK) {
        gchar *url = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (url_entry))));

        if (url[0] != '\0') {
            GFile *dir = nolphin_file_get_location (nolphin_view_get_directory_as_file (view));

            nolphin_git_clone_async (dir, url, NULL, git_clone_ready_cb, NULL);
            g_object_unref (dir);
        }
        g_free (url);
    }
    gtk_widget_destroy (dialog);
}

static void
git_remote_add_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_git_remote_add_finish (result, &error);

    send_archive_notification (_("Git: Remote hinzufügen"), success, error ? error->message : NULL);
    g_clear_error (&error);
}

static void
git_remote_set_url_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gboolean success = nolphin_git_remote_set_url_finish (result, &error);

    send_archive_notification (_("Git: Remote-Adresse ändern"), success, error ? error->message : NULL);
    g_clear_error (&error);
}

typedef struct {
    NolphinView *view;
    GFile       *repo_root;
} GitRemoteDialogContext;

/* Ändert die Adresse im URL-Feld, sobald der eingegebene Name ein schon
 * eingetragener Remote ist, und zeigt dazu einen Hinweis an. */
static void
git_remote_name_changed_cb (GtkEditable *editable, gpointer user_data)
{
    GtkWidget *dialog = GTK_WIDGET (user_data);
    GHashTable *remotes = g_object_get_data (G_OBJECT (dialog), "remotes");
    GtkWidget *url_entry = g_object_get_data (G_OBJECT (dialog), "url-entry");
    GtkWidget *hint = g_object_get_data (G_OBJECT (dialog), "hint");
    const gchar *known_url = g_hash_table_lookup (remotes, gtk_entry_get_text (GTK_ENTRY (editable)));

    if (known_url != NULL) {
        gtk_entry_set_text (GTK_ENTRY (url_entry), known_url);
        gtk_label_set_text (GTK_LABEL (hint),
                            _("Dieser Remote ist schon eingetragen - beim Bestätigen wird seine Adresse geändert."));
    } else {
        gtk_label_set_text (GTK_LABEL (hint), _("Neuer Remote - er wird hinzugefügt."));
    }
}

static void
git_remote_list_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GitRemoteDialogContext *ctx = user_data;
    NolphinView *view = ctx->view;
    GError *error = NULL;
    GHashTable *remotes = nolphin_git_remote_list_finish (result, &error);
    GtkWidget *dialog, *grid, *label, *name_entry, *url_entry, *hint, *existing;
    GString *existing_text;
    GHashTableIter iter;
    gpointer key, value;
    const gchar *first_name = "origin";
    int response;

    if (remotes == NULL) {
        GtkWidget *err = gtk_message_dialog_new (nolphin_view_get_containing_window (view),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                 "%s", error ? error->message : _("Unbekannter Fehler"));
        gtk_dialog_run (GTK_DIALOG (err));
        gtk_widget_destroy (err);
        g_clear_error (&error);
        goto out;
    }

    dialog = gtk_dialog_new_with_buttons (_("Remote hinzufügen oder ändern"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

    grid = gtk_grid_new ();
    g_object_set (grid, "border-width", 12, "row-spacing", 8, "column-spacing", 12, NULL);

    existing_text = g_string_new (NULL);
    g_hash_table_iter_init (&iter, remotes);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        g_string_append_printf (existing_text, "%s  →  %s\n", (const gchar *) key, (const gchar *) value);
        if (g_hash_table_contains (remotes, "origin") == FALSE) {
            first_name = key;
        }
    }
    if (existing_text->len == 0) {
        g_string_assign (existing_text, _("Noch kein Remote eingetragen."));
    } else {
        g_string_truncate (existing_text, existing_text->len - 1);
    }

    label = gtk_label_new (_("Eingetragen:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
    existing = gtk_label_new (existing_text->str);
    gtk_label_set_selectable (GTK_LABEL (existing), TRUE);
    gtk_widget_set_halign (existing, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), existing, 1, 0, 1, 1);
    g_string_free (existing_text, TRUE);

    label = gtk_label_new (_("Name:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
    name_entry = gtk_entry_new ();
    gtk_entry_set_activates_default (GTK_ENTRY (name_entry), TRUE);
    gtk_widget_set_hexpand (name_entry, TRUE);
    gtk_widget_set_size_request (name_entry, 420, -1);
    gtk_grid_attach (GTK_GRID (grid), name_entry, 1, 1, 1, 1);

    label = gtk_label_new (_("URL:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
    url_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (url_entry), "https://github.com/user/repo.git");
    gtk_entry_set_activates_default (GTK_ENTRY (url_entry), TRUE);
    gtk_widget_set_hexpand (url_entry, TRUE);
    gtk_grid_attach (GTK_GRID (grid), url_entry, 1, 2, 1, 1);

    hint = gtk_label_new ("");
    gtk_widget_set_halign (hint, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), hint, 1, 3, 1, 1);

    g_object_set_data (G_OBJECT (dialog), "remotes", remotes);
    g_object_set_data (G_OBJECT (dialog), "url-entry", url_entry);
    g_object_set_data (G_OBJECT (dialog), "hint", hint);
    g_signal_connect (name_entry, "changed", G_CALLBACK (git_remote_name_changed_cb), dialog);
    /* löst das erste Befüllen von URL-Feld und Hinweis über den Handler aus */
    gtk_entry_set_text (GTK_ENTRY (name_entry), first_name);

    gtk_widget_show_all (grid);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid);
    gtk_widget_grab_focus (url_entry);

    response = gtk_dialog_run (GTK_DIALOG (dialog));
    if (response == GTK_RESPONSE_OK) {
        const gchar *name = gtk_entry_get_text (GTK_ENTRY (name_entry));
        const gchar *url = gtk_entry_get_text (GTK_ENTRY (url_entry));

        if (name[0] != '\0' && url[0] != '\0') {
            if (g_hash_table_contains (remotes, name)) {
                nolphin_git_remote_set_url_async (ctx->repo_root, name, url, NULL,
                                                  git_remote_set_url_ready_cb, NULL);
            } else {
                nolphin_git_remote_add_async (ctx->repo_root, name, url, NULL,
                                              git_remote_add_ready_cb, NULL);
            }
        }
    }
    gtk_widget_destroy (dialog);
    g_hash_table_unref (remotes);

out:
    g_object_unref (ctx->repo_root);
    g_object_unref (ctx->view);
    g_free (ctx);
}

static void
git_remote_root_found_cb (GFile *repo_root, GFile *selected_file, gpointer user_data)
{
    GitRemoteDialogContext *ctx = user_data;

    ctx->repo_root = g_object_ref (repo_root);
    nolphin_git_remote_list_async (repo_root, NULL, git_remote_list_ready_cb, ctx);
}

static void
action_git_remote_add_callback (GtkAction *action, gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GitRemoteDialogContext *ctx = g_new0 (GitRemoteDialogContext, 1);

    ctx->view = g_object_ref (view);
    git_resolve_repo_root (view, git_remote_root_found_cb, ctx);
}

/* METADATEN UND TAGS: Benutzerdefinierte Tags/Emblems teilen sich den
 * GVFS-Metadatenschlüssel "emblems" (NOLPHIN_METADATA_KEY_EMBLEMS) mit der
 * schon bestehenden Emblem-Darstellung in nolphin_file_get_emblem_icons()
 * (libnolphin-private/nolphin-file.c). Die hier auswählbaren Emblem-Namen
 * sind der Emblems-Kontext der Freedesktop-Icon-Naming-Spezifikation,
 * ohne die von Nolphin bereits automatisch vergebenen Namen (readonly,
 * unreadable, symbolic-link, note, xapp-favorite, trash). */
static const struct {
    const char *keyword;
    const char *label;
} nolphin_emblem_choices[] = {
    { "default",      N_("Standard") },
    { "documents",    N_("Dokumente") },
    { "downloads",    N_("Downloads") },
    { "favorite",     N_("Favorit") },
    { "important",    N_("Wichtig") },
    { "mail",         N_("E-Mail") },
    { "photos",       N_("Fotos") },
    { "shared",       N_("Geteilt") },
    { "synchronized", N_("Synchronisiert") },
    { "system",       N_("System") },
};

static void
show_tags_and_emblem_dialog (NolphinView *view, GList *selection, gboolean focus_emblems)
{
    NolphinFile *file;
    GtkWidget *dialog, *vbox, *label, *tags_entry, *grid;
    GtkWidget *emblem_checks[G_N_ELEMENTS (nolphin_emblem_choices)];
    GList *current_keywords, *l;
    GString *joined;
    guint i;
    int response;

    if (g_list_length (selection) != 1) {
        return;
    }
    file = NOLPHIN_FILE (selection->data);

    current_keywords = nolphin_file_get_metadata_list (file, NOLPHIN_METADATA_KEY_EMBLEMS);

    dialog = gtk_dialog_new_with_buttons (_("Tags und Emblem bearbeiten"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 380, -1);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    g_object_set (vbox, "border-width", 12, NULL);

    label = gtk_label_new (_("Tags (durch Komma getrennt):"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    joined = g_string_new ("");
    for (l = current_keywords; l != NULL; l = l->next) {
        if (joined->len > 0) {
            g_string_append (joined, ", ");
        }
        g_string_append (joined, (const char *) l->data);
    }
    tags_entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (tags_entry), joined->str);
    g_string_free (joined, TRUE);
    gtk_entry_set_activates_default (GTK_ENTRY (tags_entry), TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), tags_entry, FALSE, FALSE, 0);

    label = gtk_label_new (_("Emblem (zusätzliches Symbol auf dem Dateisymbol):"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    g_object_set (grid, "row-spacing", 4, "column-spacing", 12, NULL);
    for (i = 0; i < G_N_ELEMENTS (nolphin_emblem_choices); i++) {
        gboolean active = FALSE;

        for (l = current_keywords; l != NULL; l = l->next) {
            if (g_strcmp0 ((const char *) l->data, nolphin_emblem_choices[i].keyword) == 0) {
                active = TRUE;
                break;
            }
        }
        emblem_checks[i] = gtk_check_button_new_with_label (_(nolphin_emblem_choices[i].label));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (emblem_checks[i]), active);
        gtk_grid_attach (GTK_GRID (grid), emblem_checks[i], i % 2, i / 2, 1, 1);
    }
    gtk_box_pack_start (GTK_BOX (vbox), grid, FALSE, FALSE, 0);

    gtk_widget_show_all (vbox);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox);
    gtk_widget_grab_focus (focus_emblems ? grid : tags_entry);

    response = gtk_dialog_run (GTK_DIALOG (dialog));
    if (response == GTK_RESPONSE_OK) {
        GList *new_keywords = NULL;
        gchar **parts;
        guint j;

        parts = g_strsplit (gtk_entry_get_text (GTK_ENTRY (tags_entry)), ",", -1);
        for (j = 0; parts[j] != NULL; j++) {
            gchar *trimmed = g_strdup (g_strstrip (parts[j]));

            if (trimmed[0] != '\0') {
                new_keywords = g_list_prepend (new_keywords, trimmed);
            } else {
                g_free (trimmed);
            }
        }
        g_strfreev (parts);

        for (i = 0; i < G_N_ELEMENTS (nolphin_emblem_choices); i++) {
            if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (emblem_checks[i]))) {
                gboolean already = FALSE;
                GList *nl;

                for (nl = new_keywords; nl != NULL; nl = nl->next) {
                    if (g_strcmp0 ((const char *) nl->data, nolphin_emblem_choices[i].keyword) == 0) {
                        already = TRUE;
                        break;
                    }
                }
                if (!already) {
                    new_keywords = g_list_prepend (new_keywords, g_strdup (nolphin_emblem_choices[i].keyword));
                }
            }
        }

        nolphin_file_set_keywords (file, new_keywords);
        g_list_free_full (new_keywords, g_free);
    }

    g_list_free_full (current_keywords, g_free);
    gtk_widget_destroy (dialog);
}

static void
action_edit_tags_callback (GtkAction *action,
                           gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GList *selection = nolphin_view_get_selection (view);

    show_tags_and_emblem_dialog (view, selection, FALSE);
    nolphin_file_list_free (selection);
}

static void
action_edit_emblem_callback (GtkAction *action,
                             gpointer callback_data)
{
    NolphinView *view = NOLPHIN_VIEW (callback_data);
    GList *selection = nolphin_view_get_selection (view);

    show_tags_and_emblem_dialog (view, selection, TRUE);
    nolphin_file_list_free (selection);
}

static void
action_edit_comment_callback (GtkAction *action,
                              gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    NolphinFile *file;
    GtkWidget *dialog, *vbox, *label, *scrolled, *text_view;
    GtkTextBuffer *buffer;
    gchar *current_comment;
    int response;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);
    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }
    file = NOLPHIN_FILE (selection->data);

    dialog = gtk_dialog_new_with_buttons (_("Kommentar bearbeiten"),
                                          nolphin_view_get_containing_window (view),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          NULL);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 420, 260);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    g_object_set (vbox, "border-width", 12, NULL);

    label = gtk_label_new (_("Kommentar:"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand (scrolled, TRUE);

    text_view = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
    current_comment = nolphin_file_get_comment (file);
    gtk_text_buffer_set_text (buffer, current_comment, -1);
    g_free (current_comment);

    gtk_container_add (GTK_CONTAINER (scrolled), text_view);
    gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

    gtk_widget_show_all (vbox);
    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), vbox);
    gtk_widget_grab_focus (text_view);

    response = gtk_dialog_run (GTK_DIALOG (dialog));
    if (response == GTK_RESPONSE_OK) {
        GtkTextIter start, end;
        gchar *new_text;

        gtk_text_buffer_get_bounds (buffer, &start, &end);
        new_text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        nolphin_file_set_comment (file, new_text);
        g_free (new_text);
    }

    gtk_widget_destroy (dialog);
    nolphin_file_list_free (selection);
}

static void
apply_rating_to_selection (NolphinView *view, int rating)
{
    GList *selection, *l;

    selection = nolphin_view_get_selection (view);
    for (l = selection; l != NULL; l = l->next) {
        nolphin_file_set_rating (NOLPHIN_FILE (l->data), rating);
    }
    nolphin_file_list_free (selection);
}

static void
action_rating_0_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 0);
}

static void
action_rating_1_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 1);
}

static void
action_rating_2_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 2);
}

static void
action_rating_3_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 3);
}

static void
action_rating_4_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 4);
}

static void
action_rating_5_callback (GtkAction *action, gpointer callback_data)
{
    apply_rating_to_selection (NOLPHIN_VIEW (callback_data), 5);
}

static void
action_open_containing_folder_callback (GtkAction *action,
                                        gpointer callback_data)
{
    NolphinView *view;
    GList *selection;
    NolphinFile *item;
    GFile *activation_location;
    NolphinFile *activation_file;
    NolphinFile *location;

    view = NOLPHIN_VIEW (callback_data);
    selection = nolphin_view_get_selection (view);

    if (g_list_length (selection) != 1) {
        nolphin_file_list_free (selection);
        return;
    }

    item = NOLPHIN_FILE (selection->data);
    activation_location = nolphin_file_get_activation_location (item);
    activation_file = nolphin_file_get (activation_location);
    location = nolphin_file_get_parent (activation_file);

    nolphin_view_activate_file (view, location, 0);

    nolphin_file_unref (location);
    nolphin_file_unref (activation_file);
    g_object_unref (activation_location);
}

/* "Im Terminal öffnen" zeigt das eingebettete Terminal im rechten
 * Arbeitsbereich (wie F4), navigiert zum gewünschten Ordner - statt wie
 * bisher ein externes Terminalfenster zu starten (verbotenes eigenständiges
 * Fenster). */
static void
open_embedded_terminal_at_path (NolphinView *view, const gchar *path)
{
	GtkWidget *toplevel;
	NolphinWindow *window;
	GFile *location;

	if (path == NULL) {
		return;
	}

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
	if (!NOLPHIN_IS_WINDOW (toplevel)) {
		return;
	}
	window = NOLPHIN_WINDOW (toplevel);

	nolphin_window_set_show_terminal (window, TRUE);

	location = g_file_new_for_path (path);
	nolphin_terminal_set_location (NOLPHIN_TERMINAL (nolphin_window_get_terminal (window)), location);
	g_object_unref (location);
}

static void
action_open_in_terminal_callback(GtkAction *action,
				  gpointer callback_data)
{
	NolphinView *view;
	GList *selection;

	view = NOLPHIN_VIEW (callback_data);
	selection = nolphin_view_get_selection (view);
	if (selection != NULL) {
        gchar *path;
        if (nolphin_file_is_directory (NOLPHIN_FILE (selection->data))) {
            path = nolphin_file_get_path (NOLPHIN_FILE (selection->data));
        } else {
            NolphinFile *location = nolphin_file_get_parent (NOLPHIN_FILE (selection->data));
            path = nolphin_file_get_path (location);
            nolphin_file_unref (location);
        }
        open_embedded_terminal_at_path (view, path);
        g_free (path);
		nolphin_file_list_free (selection);
	} else {
        gchar *path;
        gchar *uri = nolphin_view_get_uri (view);
        GFile *gfile = g_file_new_for_uri (uri);
        if (g_file_has_uri_scheme (gfile, "x-nolphin-desktop")) {
            path = nolphin_get_desktop_directory ();
        } else {
            path = g_file_get_path (gfile);
        }
        open_embedded_terminal_at_path (view, path);
        g_free (uri);
        g_free (path);
        g_object_unref (gfile);
    }
}

/* Ordner, den "Als GID-Projekt hinzufügen" betrifft: der genau eine
 * gewählte lokale Ordner, ohne Auswahl der aktuelle lokale Ordner. NULL,
 * wenn keiner in Frage kommt. */
static gchar *
gid_candidate_path (NolphinView *view, GList *selection)
{
	gchar *path = NULL;

	if (selection != NULL) {
		if (selection->next == NULL && nolphin_file_is_directory (NOLPHIN_FILE (selection->data)))
			path = nolphin_file_get_path (NOLPHIN_FILE (selection->data));
	} else {
		gchar *uri = nolphin_view_get_uri (view);
		GFile *gfile = g_file_new_for_uri (uri);

		if (g_file_is_native (gfile) && !g_file_has_uri_scheme (gfile, "x-nolphin-desktop"))
			path = g_file_get_path (gfile);
		g_object_unref (gfile);
		g_free (uri);
	}
	return path;
}

static void
action_gid_add_project_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection = nolphin_view_get_selection (view);
	gchar *path = gid_candidate_path (view, selection);
	GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));

	if (path != NULL && nolphin_gid_projects_add (path) && NOLPHIN_IS_WINDOW (toplevel)) {
		GFile *folder = g_file_new_for_path (path);
		NolphinWindow *window = NOLPHIN_WINDOW (toplevel);

		nolphin_workspace_panel_show_gid (nolphin_window_get_workspace_panel (window), window, folder);
		g_object_unref (folder);
	}
	g_free (path);
	nolphin_file_list_free (selection);
}

static void
action_phone_send_callback (GtkAction *action, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection = nolphin_view_get_selection (view);
	GList *files = NULL, *l;
	gchar *outbox;
	GFile *target;

	if (selection == NULL || !nolphin_phone_ensure_folders ()) {
		nolphin_file_list_free (selection);
		return;
	}
	for (l = selection; l != NULL; l = l->next) {
		files = g_list_prepend (files, nolphin_file_get_location (NOLPHIN_FILE (l->data)));
	}
	files = g_list_reverse (files);
	outbox = nolphin_phone_outbox_path ();
	target = g_file_new_for_path (outbox);
	/* kopieren, nie verschieben: das Original bleibt, wo es ist */
	nolphin_file_operations_copy (files, NULL, target,
				      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view))), NULL, NULL);
	g_object_unref (target);
	g_free (outbox);
	g_list_free_full (files, g_object_unref);
	nolphin_file_list_free (selection);
}

static void
real_action_undo (NolphinView *view)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
	nolphin_file_undo_manager_undo (GTK_WINDOW (toplevel));
}

static void
real_action_redo (NolphinView *view)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
	nolphin_file_undo_manager_redo (GTK_WINDOW (toplevel));
}

static void
action_undo_callback (GtkAction *action,
		      gpointer callback_data)
{
	real_action_undo (NOLPHIN_VIEW (callback_data));
}

static void
action_redo_callback (GtkAction *action,
		      gpointer callback_data)
{
	real_action_redo (NOLPHIN_VIEW (callback_data));
}

/* (Massenumbenennung): oeffnet die native Massenumbenennung-Seite im
 * rechten Arbeitsbereich fuer @selection - der Aufrufer behaelt das
 * Eigentum an @selection. */
static void
open_batch_rename_panel (NolphinView *view, GList *selection)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
	if (!NOLPHIN_IS_WINDOW (toplevel)) {
		return;
	}

	nolphin_workspace_panel_show_batch_rename (nolphin_window_get_workspace_panel (NOLPHIN_WINDOW (toplevel)),
						   NOLPHIN_WINDOW (toplevel), selection);
}

static void
action_batch_rename_callback (GtkAction *action,
			      gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);
	GList *selection = nolphin_view_get_selection (view);

	if (selection_not_empty_in_menu_callback (view, selection)) {
		open_batch_rename_panel (view, selection);
	}

	nolphin_file_list_free (selection);
}

static void
real_action_rename (NolphinView *view,
		    gboolean select_all)
{
	NolphinFile *file;
	GList *selection;

	g_assert (NOLPHIN_IS_VIEW (view));

	selection = nolphin_view_get_selection (view);

	if (selection_not_empty_in_menu_callback (view, selection)) {
		/* If there is more than one file selected, invoke the native
		 * Massenumbenennung-Panel statt eines externen Werkzeugs. */
		if (selection->next != NULL) {
			open_batch_rename_panel (view, selection);
		} else {
			file = NOLPHIN_FILE (selection->data);
			if (!select_all) {
				/* directories don't have a file extension, so
				 * they are always pre-selected as a whole */
				select_all = nolphin_file_is_directory (file);
			}
			NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->start_renaming_file (view, file, select_all);
		}
	}

	nolphin_file_list_free (selection);
}

static void
action_rename_callback (GtkAction *action,
			gpointer callback_data)
{
	real_action_rename (NOLPHIN_VIEW (callback_data), FALSE);
}

static void
action_rename_select_all_callback (GtkAction *action,
				   gpointer callback_data)
{
	real_action_rename (NOLPHIN_VIEW (callback_data), TRUE);
}

static void
file_mount_callback (NolphinFile  *file,
		     GFile         *result_location,
		     GError        *error,
		     gpointer       callback_data)
{
	if (error != NULL &&
	    (error->domain != G_IO_ERROR ||
	     (error->code != G_IO_ERROR_CANCELLED &&
	      error->code != G_IO_ERROR_FAILED_HANDLED &&
	      error->code != G_IO_ERROR_ALREADY_MOUNTED))) {
		eel_show_error_dialog (_("Einhängen des Ortes nicht möglich"),
				       error->message, NULL);
	}
}

static void
file_unmount_callback (NolphinFile  *file,
		       GFile         *result_location,
		       GError        *error,
		       gpointer       callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);
	g_object_unref (view);

	if (error != NULL &&
	    (error->domain != G_IO_ERROR ||
	     (error->code != G_IO_ERROR_CANCELLED &&
	      error->code != G_IO_ERROR_FAILED_HANDLED))) {
		eel_show_error_dialog (_("Aushängen des Ortes nicht möglich"),
				       error->message, NULL);
	}
}

static void
file_eject_callback (NolphinFile  *file,
		     GFile         *result_location,
		     GError        *error,
		     gpointer       callback_data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (callback_data);
	g_object_unref (view);

	if (error != NULL &&
	    (error->domain != G_IO_ERROR ||
	     (error->code != G_IO_ERROR_CANCELLED &&
	      error->code != G_IO_ERROR_FAILED_HANDLED))) {
		eel_show_error_dialog (_("Auswerfen des Ortes nicht möglich"),
				       error->message, NULL);
	}
}

static void
file_stop_callback (NolphinFile  *file,
		    GFile         *result_location,
		    GError        *error,
		    gpointer       callback_data)
{
	if (error != NULL &&
	    (error->domain != G_IO_ERROR ||
	     (error->code != G_IO_ERROR_CANCELLED &&
	      error->code != G_IO_ERROR_FAILED_HANDLED))) {
		eel_show_error_dialog (_("Laufwerk konnte nicht angehalten werden"),
				       error->message, NULL);
	}
}

static void
action_mount_volume_callback (GtkAction *action,
			      gpointer data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;
	GMountOperation *mount_op;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);
	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);

		if (nolphin_file_can_mount (file)) {
			mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
			g_mount_operation_set_password_save (mount_op, G_PASSWORD_SAVE_FOR_SESSION);
			nolphin_file_mount (file, mount_op, NULL,
					     file_mount_callback, NULL);
			g_object_unref (mount_op);
		}
	}
	nolphin_file_list_free (selection);
}

static void
action_unmount_volume_callback (GtkAction *action,
				gpointer data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);

	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);
		if (nolphin_file_can_unmount (file)) {
			GMountOperation *mount_op;
			mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
			nolphin_file_unmount (file, mount_op, NULL,
					       file_unmount_callback, g_object_ref (view));
			g_object_unref (mount_op);
		}
	}
	nolphin_file_list_free (selection);
}

static void
action_eject_volume_callback (GtkAction *action,
			      gpointer data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);
	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);

		if (nolphin_file_can_eject (file)) {
			GMountOperation *mount_op;
			mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
			nolphin_file_eject (file, mount_op, NULL,
					     file_eject_callback, g_object_ref (view));
			g_object_unref (mount_op);
		}
	}
	nolphin_file_list_free (selection);
}

static void
file_start_callback (NolphinFile  *file,
		     GFile         *result_location,
		     GError        *error,
		     gpointer       callback_data)
{
	if (error != NULL &&
	    (error->domain != G_IO_ERROR ||
	     (error->code != G_IO_ERROR_CANCELLED &&
	      error->code != G_IO_ERROR_FAILED_HANDLED &&
	      error->code != G_IO_ERROR_ALREADY_MOUNTED))) {
		eel_show_error_dialog (_("Starten des Ortes nicht möglich"),
				       error->message, NULL);
	}
}

static void
action_start_volume_callback (GtkAction *action,
			      gpointer   data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;
	GMountOperation *mount_op;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);
	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);

		if (nolphin_file_can_start (file) || nolphin_file_can_start_degraded (file)) {
			mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
			nolphin_file_start (file, mount_op, NULL,
					     file_start_callback, NULL);
			g_object_unref (mount_op);
		}
	}
	nolphin_file_list_free (selection);
}

static void
action_stop_volume_callback (GtkAction *action,
			     gpointer   data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);
	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);

		if (nolphin_file_can_stop (file)) {
			GMountOperation *mount_op;
			mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
			nolphin_file_stop (file, mount_op, NULL,
					    file_stop_callback, NULL);
			g_object_unref (mount_op);
		}
	}
	nolphin_file_list_free (selection);
}

static void
action_detect_media_callback (GtkAction *action,
			      gpointer   data)
{
	NolphinFile *file;
	GList *selection, *l;
	NolphinView *view;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);
	for (l = selection; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);

		if (nolphin_file_can_poll_for_media (file) && !nolphin_file_is_media_check_automatic (file)) {
			nolphin_file_poll_for_media (file);
		}
	}
	nolphin_file_list_free (selection);
}

static void
action_self_mount_volume_callback (GtkAction *action,
				   gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	g_mount_operation_set_password_save (mount_op, G_PASSWORD_SAVE_FOR_SESSION);
	nolphin_file_mount (file, mount_op, NULL, file_mount_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_self_unmount_volume_callback (GtkAction *action,
				     gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_unmount (file, mount_op, NULL, file_unmount_callback, g_object_ref (view));
	g_object_unref (mount_op);
}

static void
action_self_eject_volume_callback (GtkAction *action,
				   gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_eject (file, mount_op, NULL, file_eject_callback, g_object_ref (view));
	g_object_unref (mount_op);
}

static void
action_self_start_volume_callback (GtkAction *action,
				   gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_start (file, mount_op, NULL, file_start_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_self_stop_volume_callback (GtkAction *action,
				  gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_stop (file, mount_op, NULL,
			    file_stop_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_self_detect_media_callback (GtkAction *action,
				   gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;

	view = NOLPHIN_VIEW (data);

	file = nolphin_view_get_directory_as_file (view);
	if (file == NULL) {
		return;
	}

	nolphin_file_poll_for_media (file);
}

static void
action_location_mount_volume_callback (GtkAction *action,
				       gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	g_mount_operation_set_password_save (mount_op, G_PASSWORD_SAVE_FOR_SESSION);
	nolphin_file_mount (file, mount_op, NULL, file_mount_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_location_unmount_volume_callback (GtkAction *action,
					 gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_unmount (file, mount_op, NULL,
			       file_unmount_callback, g_object_ref (view));
	g_object_unref (mount_op);
}

static void
action_location_eject_volume_callback (GtkAction *action,
				       gpointer data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_eject (file, mount_op, NULL,
			     file_eject_callback, g_object_ref (view));
	g_object_unref (mount_op);
}

static void
action_location_start_volume_callback (GtkAction *action,
				       gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_start (file, mount_op, NULL, file_start_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_location_stop_volume_callback (GtkAction *action,
				      gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;
	GMountOperation *mount_op;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	mount_op = nolphin_mount_operation_new (nolphin_view_get_containing_window (view));
	nolphin_file_stop (file, mount_op, NULL,
			    file_stop_callback, NULL);
	g_object_unref (mount_op);
}

static void
action_location_detect_media_callback (GtkAction *action,
				       gpointer   data)
{
	NolphinFile *file;
	NolphinView *view;

	view = NOLPHIN_VIEW (data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	nolphin_file_poll_for_media (file);
}

static void
connect_to_server_response_callback (GtkDialog *dialog,
				     int response_id,
				     gpointer data)
{
#ifdef GIO_CONVERSION_DONE
	GtkEntry *entry;
	char *uri;
	const char *name;
	char *icon;

	entry = GTK_ENTRY (data);

	switch (response_id) {
	case GTK_RESPONSE_OK:
		uri = g_object_get_data (G_OBJECT (dialog), "link-uri");
		icon = g_object_get_data (G_OBJECT (dialog), "link-icon");
		name = gtk_entry_get_text (entry);
		gnome_vfs_connect_to_server (uri, (char *)name, icon);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_NONE:
	case GTK_RESPONSE_DELETE_EVENT:
	case GTK_RESPONSE_CANCEL:
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	default :
		g_assert_not_reached ();
	}
#endif
	/* FIXME: the above code should make a server connection permanent */
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
entry_activate_callback (GtkEntry *entry,
			 gpointer user_data)
{
	GtkDialog *dialog;

	dialog = GTK_DIALOG (user_data);
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
action_connect_to_server_link_callback (GtkAction *action,
					gpointer data)
{
	NolphinFile *file;
	GList *selection;
	NolphinView *view;
	char *uri;
	NolphinIconInfo *icon;
	const char *icon_name;
	char *name;
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *box;
	char *title;

        view = NOLPHIN_VIEW (data);

	selection = nolphin_view_get_selection (view);

	if (g_list_length (selection) != 1) {
		nolphin_file_list_free (selection);
		return;
	}

	file = NOLPHIN_FILE (selection->data);

	uri = nolphin_file_get_activation_uri (file);
	icon = nolphin_file_get_icon (file, NOLPHIN_ICON_SIZE_STANDARD, 0, gtk_widget_get_scale_factor (GTK_WIDGET (view)), 0);
	icon_name = nolphin_icon_info_get_used_name (icon);
	name = nolphin_file_get_display_name (file);

	if (uri != NULL) {
		title = g_strdup_printf (_("Mit Server %s verbinden"), name);
		dialog = gtk_dialog_new_with_buttons (title,
						      nolphin_view_get_containing_window (view),
						      0,
						      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						      _("_Verbinden"), GTK_RESPONSE_OK,
						      NULL);

		g_object_set_data_full (G_OBJECT (dialog), "link-uri", g_strdup (uri), g_free);
		g_object_set_data_full (G_OBJECT (dialog), "link-icon", g_strdup (icon_name), g_free);

		gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
		gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), 2);

		box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_show (box);
		gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
				    box, TRUE, TRUE, 0);

		label = gtk_label_new_with_mnemonic (_("Verknüpfungs_name:"));
		gtk_widget_show (label);

		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 12);

		entry = gtk_entry_new ();
		if (name) {
			gtk_entry_set_text (GTK_ENTRY (entry), name);
		}
		g_signal_connect (entry,
				  "activate",
				  G_CALLBACK (entry_activate_callback),
				  dialog);

		gtk_widget_show (entry);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

		gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 12);

		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 GTK_RESPONSE_OK);
		g_signal_connect (dialog, "response",
				  G_CALLBACK (connect_to_server_response_callback),
				  entry);
		gtk_widget_show (dialog);
	}

	g_free (uri);
	nolphin_icon_info_unref (icon);
	g_free (name);
}

static void
action_location_open_alternate_callback (GtkAction *action,
					 gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}
	nolphin_view_activate_file (view,
				     file,
				     NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

static void
action_location_open_in_new_tab_callback (GtkAction *action,
					  gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	if (file == NULL) {
		return;
	}

	nolphin_view_activate_file (view,
				     file,
				     NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB);
}

static void
action_location_cut_callback (GtkAction *action,
			      gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;
	GList *files;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	g_return_if_fail (file != NULL);

	files = g_list_append (NULL, file);
	copy_or_cut_files (view, files, TRUE);
	g_list_free (files);
}

static void
action_location_copy_callback (GtkAction *action,
			       gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;
	GList *files;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	g_return_if_fail (file != NULL);

	files = g_list_append (NULL, file);
	copy_or_cut_files (view, files, FALSE);
	g_list_free (files);
}

static void
action_location_paste_files_into_callback (GtkAction *action,
					   gpointer callback_data)
{
	NolphinView *view;
	NolphinFile *file;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	g_return_if_fail (file != NULL);

	paste_into (view, file);
}

static void
action_location_trash_callback (GtkAction *action,
				gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;
	GList *files;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	g_return_if_fail (file != NULL);

	files = g_list_append (NULL, file);
	trash_or_delete_files (nolphin_view_get_containing_window (view),
			       files, TRUE,
			       view);
	g_list_free (files);
}

static void
action_location_delete_callback (GtkAction *action,
				 gpointer   callback_data)
{
	NolphinView *view;
	NolphinFile *file;
	GFile *location;
	GList *files;

	view = NOLPHIN_VIEW (callback_data);

	file = view->details->location_popup_directory_as_file;
	g_return_if_fail (file != NULL);

	location = nolphin_file_get_location (file);

	files = g_list_append (NULL, location);
	nolphin_file_operations_delete (files, nolphin_view_get_containing_window (view),
					 NULL, NULL);

	g_list_free_full (files, g_object_unref);
}

static void
action_location_restore_from_trash_callback (GtkAction *action,
					     gpointer callback_data)
{
	NolphinView *view;
	NolphinFile *file;
	GList l;

	view = NOLPHIN_VIEW (callback_data);
	file = view->details->location_popup_directory_as_file;

	l.prev = NULL;
	l.next = NULL;
	l.data = file;
	nolphin_restore_files_from_trash (&l,
					   nolphin_view_get_containing_window (view));
}

static void
nolphin_view_init_show_hidden_files (NolphinView *view)
{
	NolphinWindowShowHiddenFilesMode mode;
	gboolean show_hidden_changed;

	if (view->details->ignore_hidden_file_preferences) {
		return;
	}

	mode = nolphin_window_get_hidden_files_mode (view->details->window);

    if (mode == NOLPHIN_WINDOW_SHOW_HIDDEN_FILES_ENABLE) {
        show_hidden_changed = !view->details->show_hidden_files;
        view->details->show_hidden_files = TRUE;
    } else {
        show_hidden_changed = view->details->show_hidden_files;
        view->details->show_hidden_files = FALSE;
    }

    if (show_hidden_changed && (view->details->model != NULL)) {
        load_directory (view, view->details->model);
    }
}

static const GtkActionEntry directory_view_entries[] = {
  /* name, stock id, label */  { "New Documents", "xsi-document-new-symbolic", N_("Neues _Dokument anlegen") },
  /* name, stock id, label */  { "Open With", NULL, N_("Öffnen _mit"),
				 NULL, N_("Ein Programm auswählen, mit dem das gewählte Objekt geöffnet werden soll.") },
  /* name, stock id */         { "Properties", "xsi-document-properties-symbolic",
  /* label, accelerator */       N_("_Eigenschaften"), "<alt>Return",
  /* tooltip */                  N_("Die Eigenschaften aller gewählten Objekte anzeigen/ändern"),
				 G_CALLBACK (action_properties_callback) },
  /* name, stock id */         { "ActivateFilter", NULL,
  /* label, accelerator */       "ActivateFilter", "<control>I",
  /* tooltip */                  NULL,
				 G_CALLBACK (action_activate_filter_callback) },
  /* name, stock id */         { "New Folder", "xsi-folder-new-symbolic",
  /* label, accelerator */       N_("Neuen _Ordner anlegen"), "<control><shift>N",
  /* tooltip */                  N_("Einen neuen leeren Ordner in diesem Ordner anlegen"),
				 G_CALLBACK (action_new_folder_callback) },
  /* name, stock id, label */  { "No Templates", NULL, N_("Keine Vorlagen installiert") },
  /* name, stock id */         { "New Empty Document", NULL,
    /* translators: this is used to indicate that a document doesn't contain anything */
  /* label, accelerator */       N_("_Leeres Dokument"), NULL,
  /* tooltip */                  N_("Ein neues leeres Dokument in diesem Ordner anlegen"),
				 G_CALLBACK (action_new_empty_file_callback) },
  /* name, stock id */         { "Open", NULL,
  /* label, accelerator */       N_("_Öffnen"), "<control>o",
  /* tooltip */                  N_("Das gewählte Objekt in diesem Fenster öffnen"),
                 G_CALLBACK (action_open_callback) },
  /* name, stock id */         { "OpenAccel", NULL,
  /* label, accelerator */       "OpenAccel", "<alt>Down",
  /* tooltip */                  NULL,
				 G_CALLBACK (action_open_callback) },
  /* name, stock id */         { "OpenAlternate", NULL,
  /* label, accelerator */       N_("In Navigationsfenster öffnen"), "<control><shift>o",
  /* tooltip */                  N_("Jedes gewählte Objekt in einem einzelnen Navigationsfenster öffnen"),
				 G_CALLBACK (action_open_alternate_callback) },
  /* name, stock id */         { "OpenInNewTab", NULL,
  /* label, accelerator */       N_("In neuem _Reiter öffnen"), "<control><shift>t",
  /* tooltip */                  N_("Jedes gewählte Objekt in einem neuen Reiter öffnen"),
				 G_CALLBACK (action_open_new_tab_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_OPEN_IN_TERMINAL, "xsi-utilities-terminal-symbolic",
  /* label, accelerator */       N_("Im Terminal öffnen"), "<shift>F4",
  /* tooltip */                  N_("Terminal im gewähltem Ordner öffnen"),
				 G_CALLBACK (action_open_in_terminal_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_PHONE_SEND, "send-to-symbolic",
  /* label, accelerator */       N_("An Handy senden"), NULL,
  /* tooltip */                  N_("Die gewählten Dateien in den Ordner „Handy“ kopieren; das Handy kann sie dann laden"),
				 G_CALLBACK (action_phone_send_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GID_ADD_PROJECT, "list-add-symbolic",
  /* label, accelerator */       N_("Als GID-Projekt hinzufügen"), NULL,
  /* tooltip */                  N_("Diesen Ordner in der Seitenleiste als GID-Projekt eintragen"),
				 G_CALLBACK (action_gid_add_project_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_OPEN_AS_ROOT, "xsi-dialog-password-symbolic",
  /* label, accelerator */       N_("Als Systemverwalter öffnen"), "",
  /* tooltip */                  N_("Ordner mit Administratorrechten öffnen"),
				 G_CALLBACK (action_open_as_root_callback) },

  /* name, stock id */         { NOLPHIN_ACTION_FOLLOW_SYMLINK, "xsi-go-jump-symbolic",
  /* label, accelerator */       N_("Der Verknüpfung zur Originaldatei folgen"), "",
  /* tooltip */                  N_("Zur Originaldatei navigieren, zu welcher diese symbolische Verknüpfung zeigt."),
                 G_CALLBACK (action_follow_symlink_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_OPEN_CONTAINING_FOLDER, "xsi-go-jump-symbolic",
  /* label, accelerator */       N_("Übergeordneten Ordner öffnen"), "<control><alt>O",
  /* tooltip */                  N_("Den Ordner öffnen, der das ausgewählte Element enthält"),
                 G_CALLBACK (action_open_containing_folder_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_COMPRESS, NULL,
  /* label, accelerator */       N_("Kom_primieren …"), NULL,
  /* tooltip */                  N_("Ein ZIP-Archiv der ausgewählten Objekte erstellen"),
                 G_CALLBACK (action_compress_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_EXTRACT_HERE, NULL,
  /* label, accelerator */       N_("Hier _entpacken"), NULL,
  /* tooltip */                  N_("Das Archiv in einen neuen, danebenliegenden Ordner entpacken"),
                 G_CALLBACK (action_extract_here_callback) },
  /* name, stock id */         { "ArchiveManage", NULL,
  /* label, accelerator */       N_("Archiv _öffnen …"), NULL,
  /* tooltip */                  N_("Inhalt des Archivs anzeigen; Dateien entpacken, hinzufügen, ersetzen und entfernen"),
                 G_CALLBACK (action_archive_manage_callback) },
  /* name, stock id */         { "ExtractTo", NULL,
  /* label, accelerator */       N_("Entpacken _nach …"), NULL,
  /* tooltip */                  N_("Das Archiv in einen frei gewählten Ordner entpacken"),
                 G_CALLBACK (action_extract_to_callback) },
  /* name, stock id, label */  { "CopyAsMenu", NULL, N_("Kopieren _als") },
  /* name, stock id */         { "CopyPath", NULL,
  /* label, accelerator */       N_("_Pfad"), NULL,
  /* tooltip */                  N_("Den vollständigen Pfad der ausgewählten Objekte in die Zwischenablage kopieren"),
                 G_CALLBACK (action_copy_path_callback) },
  /* name, stock id */         { "CopyFileName", NULL,
  /* label, accelerator */       N_("_Dateiname"), NULL,
  /* tooltip */                  N_("Den Namen der ausgewählten Objekte in die Zwischenablage kopieren"),
                 G_CALLBACK (action_copy_filename_callback) },
  /* name, stock id */         { "CreateHardlink", NULL,
  /* label, accelerator */       N_("_Hardlink anlegen"), NULL,
  /* tooltip */                  N_("Einen Hardlink für jede gewählte Datei im selben Ordner anlegen"),
                 G_CALLBACK (action_create_hardlink_callback) },
  /* name, stock id */         { "PasteClipboardAsFile", NULL,
  /* label, accelerator */       N_("Zwischenablage als _Datei einfügen"), NULL,
  /* tooltip */                  N_("Text oder Bild aus der Zwischenablage als neue Datei im aktuellen Ordner anlegen"),
                 G_CALLBACK (action_paste_clipboard_as_file_callback) },
  /* name, stock id */         { "SelectCriteria", NULL,
  /* label, accelerator */       N_("Nach _Größe und Datum auswählen …"), NULL,
  /* tooltip */                  N_("Dateien nach Größe und Änderungsdatum auswählen"),
                 G_CALLBACK (action_select_criteria_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_TEST_ARCHIVE, NULL,
  /* label, accelerator */       N_("Archiv _prüfen"), NULL,
  /* tooltip */                  N_("Das Archiv auf Fehler prüfen, ohne es zu entpacken"),
                 G_CALLBACK (action_test_archive_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_CHECKSUM, NULL,
  /* label, accelerator */       N_("Prüfsumme _berechnen …"), NULL,
  /* tooltip */                  N_("Eine Prüfsumme der Datei berechnen und mit einem Vergleichswert abgleichen"),
                 G_CALLBACK (action_compute_checksum_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_ENCRYPT, NULL,
  /* label, accelerator */       N_("_Verschlüsseln …"), NULL,
  /* tooltip */                  N_("Datei oder Ordner mit einem Passwort verschlüsseln (gpg)"),
                 G_CALLBACK (action_encrypt_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_DECRYPT, NULL,
  /* label, accelerator */       N_("En_tschlüsseln …"), NULL,
  /* tooltip */                  N_("Eine mit gpg verschlüsselte Datei entschlüsseln"),
                 G_CALLBACK (action_decrypt_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_EDIT_ACL, NULL,
  /* label, accelerator */       N_("_Zugriffsrechte …"), NULL,
  /* tooltip */                  N_("Zugriffsrechte für weitere Benutzer und Gruppen festlegen (ACL)"),
                 G_CALLBACK (action_edit_acl_callback) },
  /* name, stock id, label */  { NOLPHIN_ACTION_GIT_MENU, NULL, N_("_Git") },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_STATUS, NULL,
  /* label, accelerator */       N_("_Status anzeigen"), NULL,
  /* tooltip */                  N_("Git-Status des Repositorys anzeigen"),
                 G_CALLBACK (action_git_status_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_ADD, NULL,
  /* label, accelerator */       N_("_Hinzufügen"), NULL,
  /* tooltip */                  N_("Ausgewähltes Objekt zur Staging-Area hinzufügen (git add)"),
                 G_CALLBACK (action_git_add_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_COMMIT, NULL,
  /* label, accelerator */       N_("_Commit …"), NULL,
  /* tooltip */                  N_("Bereitgestellte Änderungen committen"),
                 G_CALLBACK (action_git_commit_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_PULL, NULL,
  /* label, accelerator */       N_("Pu_ll"), NULL,
  /* tooltip */                  N_("Änderungen vom entfernten Repository holen"),
                 G_CALLBACK (action_git_pull_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_PUSH, NULL,
  /* label, accelerator */       N_("Pus_h"), NULL,
  /* tooltip */                  N_("Änderungen zum entfernten Repository senden"),
                 G_CALLBACK (action_git_push_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_LOG, NULL,
  /* label, accelerator */       N_("_Log anzeigen"), NULL,
  /* tooltip */                  N_("Commit-Verlauf anzeigen"),
                 G_CALLBACK (action_git_log_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_DIFF, NULL,
  /* label, accelerator */       N_("_Diff anzeigen"), NULL,
  /* tooltip */                  N_("Nicht committete Änderungen anzeigen"),
                 G_CALLBACK (action_git_diff_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_COMPARE, NULL,
  /* label, accelerator */       N_("Mit Server _abgleichen"), NULL,
  /* tooltip */                  N_("Prüfen, was lokal und auf dem Server (z. B. GitHub) unterschiedlich ist"),
                 G_CALLBACK (action_git_compare_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_SYNC, NULL,
  /* label, accelerator */       N_("_Synchronisieren"), NULL,
  /* tooltip */                  N_("Änderungen vom Server holen und eigene Änderungen hochladen"),
                 G_CALLBACK (action_git_sync_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_CLONE, NULL,
  /* label, accelerator */       N_("Repository _klonen …"), NULL,
  /* tooltip */                  N_("Ein Repository (z. B. von GitHub) in den aktuellen Ordner herunterladen"),
                 G_CALLBACK (action_git_clone_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_GIT_REMOTE_ADD, NULL,
  /* label, accelerator */       N_("_Remote hinzufügen/ändern …"), NULL,
  /* tooltip */                  N_("Ein entferntes Repository (z. B. auf GitHub) für Pull/Push eintragen"),
                 G_CALLBACK (action_git_remote_add_callback) },
  /* name, stock id, label */  { NOLPHIN_ACTION_METADATA_MENU, NULL, N_("Me_tadaten") },
  /* name, stock id */         { NOLPHIN_ACTION_EDIT_TAGS, NULL,
  /* label, accelerator */       N_("_Tags bearbeiten …"), NULL,
  /* tooltip */                  N_("Eigene Tags für dieses Objekt vergeben oder entfernen (über GVFS-Metadaten)"),
                 G_CALLBACK (action_edit_tags_callback) },
  /* name, stock id, label */  { NOLPHIN_ACTION_RATING_MENU, NULL, N_("_Bewertung") },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_0, NULL,
  /* label, accelerator */       N_("Keine Bewertung"), NULL,
  /* tooltip */                  N_("Bewertung entfernen"),
                 G_CALLBACK (action_rating_0_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_1, NULL,
  /* label, accelerator */       N_("★☆☆☆☆"), NULL,
  /* tooltip */                  N_("Mit 1 Stern bewerten"),
                 G_CALLBACK (action_rating_1_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_2, NULL,
  /* label, accelerator */       N_("★★☆☆☆"), NULL,
  /* tooltip */                  N_("Mit 2 Sternen bewerten"),
                 G_CALLBACK (action_rating_2_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_3, NULL,
  /* label, accelerator */       N_("★★★☆☆"), NULL,
  /* tooltip */                  N_("Mit 3 Sternen bewerten"),
                 G_CALLBACK (action_rating_3_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_4, NULL,
  /* label, accelerator */       N_("★★★★☆"), NULL,
  /* tooltip */                  N_("Mit 4 Sternen bewerten"),
                 G_CALLBACK (action_rating_4_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RATING_5, NULL,
  /* label, accelerator */       N_("★★★★★"), NULL,
  /* tooltip */                  N_("Mit 5 Sternen bewerten"),
                 G_CALLBACK (action_rating_5_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_EDIT_COMMENT, NULL,
  /* label, accelerator */       N_("_Kommentar …"), NULL,
  /* tooltip */                  N_("Einen Kommentar für dieses Objekt schreiben oder ändern (über GVFS-Metadaten)"),
                 G_CALLBACK (action_edit_comment_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_EDIT_EMBLEM, NULL,
  /* label, accelerator */       N_("_Emblem …"), NULL,
  /* tooltip */                  N_("Ein Emblem (zusätzliches Symbol auf dem Dateisymbol) zuweisen oder entfernen"),
                 G_CALLBACK (action_edit_emblem_callback) },
  /* name, stock id */         { "OtherApplication1", NULL,
  /* label, accelerator */       N_("Andere _Anwendung …"), NULL,
  /* tooltip */                  N_("Eine andere Anwendung auswählen, mit der das gewählte Objekt geöffnet werden soll"),
				 G_CALLBACK (action_other_application_callback) },
  /* name, stock id */         { "OtherApplication2", NULL,
  /* label, accelerator */       N_("Mit anderer _Anwendung öffnen …"), NULL,
  /* tooltip */                  N_("Eine andere Anwendung auswählen, mit der das gewählte Objekt geöffnet werden soll"),
				 G_CALLBACK (action_other_application_callback) },
  /* name, stock id */         { "Empty Trash", NULL,
  /* label, accelerator */       N_("Papierkorb _leeren"), NULL,
  /* tooltip */                  N_("Alle Objekte im Papierkorb löschen"),
				 G_CALLBACK (action_empty_trash_callback) },
  /* name, stock id */         { "Cut", "xsi-edit-cut-symbolic",
  /* label, accelerator */       N_("_Ausschneiden"), "<control>X",
  /* tooltip */                  N_("Die gewählten Dateien, auf das Verschieben, mit dem Einfügenbefehl, vorbereiten"),
				 G_CALLBACK (action_cut_files_callback) },
  /* name, stock id */         { "Copy", "xsi-edit-copy-symbolic",
  /* label, accelerator */       N_("_Kopieren"), "<control>C",
  /* tooltip */                  N_("Die gewählten Dateien, auf das Kopieren, mit dem Einfügenbefehl, vorbereiten"),
				 G_CALLBACK (action_copy_files_callback) },
  /* name, stock id */         { "Paste", "xsi-edit-paste-symbolic",
  /* label, accelerator */       N_("_Einfügen"), "<control>V",
  /* tooltip */                  N_("Zuvor durch »Ausschneiden« oder »Kopieren« ausgewählte Dateien verschieben oder kopieren"),
				 G_CALLBACK (action_paste_files_callback) },
  /* We make accelerator "" instead of null here to not inherit the stock
     accelerator for paste */
  /* name, stock id */         { "Paste Files Into", "xsi-edit-paste-symbolic",
  /* label, accelerator */       N_("In Ordner e_infügen"), "",
  /* tooltip */                  N_("Zuvor durch »Ausschneiden« oder »Kopieren« ausgewählte Dateien in den gewählten Ordner verschieben oder kopieren"),
				 G_CALLBACK (action_paste_files_into_callback) },
  /* name, stock id, label */  { "CopyToMenu", NULL, N_("Kop_ieren nach") },
  /* name, stock id, label */  { "MoveToMenu", NULL, N_("Verschieben _nach") },
  /* name, stock id */         { "Select All", NULL,
  /* label, accelerator */       N_("_Alles auswählen"), "<control>A",
  /* tooltip */                  N_("Alle Objekte in diesem Fenster auswählen"),
				 G_CALLBACK (action_select_all_callback) },
  /* name, stock id */         { "Select Pattern", NULL,
  /* label, accelerator */       N_("_Nach Muster auswählen …"), "<control>S",
  /* tooltip */                  N_("Alle Objekte in diesem Fenster auswählen, die auf ein bestimmtes Muster passen"),
				 G_CALLBACK (action_select_pattern_callback) },
  /* name, stock id */         { "Select Type", NULL,
  /* label, accelerator */       N_("Nach _Typ auswählen …"), NULL,
  /* tooltip */                  N_("Objekte in diesem Fenster auswählen, die zu einer bestimmten Dateityp-Kategorie gehören"),
				 G_CALLBACK (action_select_type_callback) },
  /* name, stock id */         { "Invert Selection", NULL,
  /* label, accelerator */       N_("Aus_wahl umkehren"), "<control><shift>I",
  /* tooltip */                  N_("Alle und nur die Objekte auswählen, die momentan nicht ausgewählt sind"),
				 G_CALLBACK (action_invert_selection_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_SAVE_SELECTION, NULL,
  /* label, accelerator */       N_("Auswahl _speichern …"), NULL,
  /* tooltip */                  N_("Die aktuelle Auswahl unter einem Namen merken, um sie später in diesem Ordner wiederherzustellen"),
				 G_CALLBACK (action_save_selection_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RESTORE_SELECTION, NULL,
  /* label, accelerator */       N_("Gespeicherte Auswahl _wiederherstellen …"), NULL,
  /* tooltip */                  N_("Eine zuvor in diesem Ordner gespeicherte Auswahl wiederherstellen"),
				 G_CALLBACK (action_restore_selection_callback) },
  /* name, stock id */         { "Duplicate", NULL,
  /* label, accelerator */       N_("Ver_doppeln"), NULL,
  /* tooltip */                  N_("Alle gewählten Objekte verdoppeln"),
				 G_CALLBACK (action_duplicate_callback) },
  /* name, stock id */         { "Create Link", NULL,
  /* label, accelerator */       N_("_Verknüpfung anlegen"), "<control>M",
  /* tooltip */                  N_("Eine symbolische Verknüpfung für jedes gewählte Objekt anlegen"),
				 G_CALLBACK (action_create_link_callback) },
  /* name, stock id */         { "Rename", NULL,
  /* label, accelerator */       N_("_Umbenennen …"), "F2",
  /* tooltip */                  N_("Ausgewähltes Objekt umbenennen"),
				 G_CALLBACK (action_rename_callback) },
  /* name, stock id */         { "RenameSelectAll", NULL,
  /* label, accelerator */       "RenameSelectAll", "<shift>F2",
  /* tooltip */                  NULL,
				 G_CALLBACK (action_rename_select_all_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_BATCH_RENAME, NULL,
  /* label, accelerator */       N_("_Massenumbenennung …"), NULL,
  /* tooltip */                  N_("Mehrere ausgewählte Objekte über Suchen/Ersetzen, Nummerierung und "
				     "Groß-/Kleinschreibung auf einmal umbenennen"),
				 G_CALLBACK (action_batch_rename_callback) },
  /* name, stock id */         { "Trash", NULL,
  /* label, accelerator */       N_("In den _Papierkorb verschieben"), NULL,
  /* tooltip */                  N_("Jedes gewählte Objekt in den Papierkorb verschieben"),
				 G_CALLBACK (action_trash_callback) },
  /* name, stock id */         { "Delete", NULL,
  /* label, accelerator */       N_("_Löschen"), NULL,
  /* tooltip */                  N_("Jedes gewählte Objekt löschen, ohne es in den Papierkorb zu verschieben"),
				 G_CALLBACK (action_delete_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_RESTORE_FROM_TRASH, NULL,
  /* label, accelerator */       N_("Am _ursprünglichen Ort wiederherstellen"), NULL,
				 NULL,
                 G_CALLBACK (action_restore_from_trash_callback) },
 /* name, stock id */          { "Undo", "xsi-edit-undo-symbolic",
 /* label, accelerator */        N_("_Rückgängig"), "<control>Z",
 /* tooltip */                   N_("Letzte Änderung rückgängig machen"),
                                 G_CALLBACK (action_undo_callback) },
 /* name, stock id */	       { "Redo", "xsi-edit-redo-symbolic",
 /* label, accelerator */        N_("_Wiederholen"), "<control>Y",
 /* tooltip */                   N_("Die zuletzt rückgängig gemachte Aktion wiederherstellen"),
                                 G_CALLBACK (action_redo_callback) },
  /*
   * multiview-TODO: decide whether "Reset to Defaults" should
   * be window-wide, and not just view-wide.
   * Since this also resets the "Show hidden files" mode,
   * it is a mixture of both ATM.
   */
  /* name, stock id */         { "Reset to Defaults", NULL,
  /* label, accelerator */       N_("Ansicht auf _Vorgaben zurücksetzen"), NULL,
  /* tooltip */                  N_("Sortierreihenfolge und Vergrößerungsstufe auf Vorgaben für diese Ansicht zurücksetzen"),
				 G_CALLBACK (action_reset_to_defaults_callback) },
  /* name, stock id */         { "Connect To Server Link", NULL,
  /* label, accelerator */       N_("Mit diesem Server verbinden"), NULL,
  /* tooltip */                  N_("Eine dauerhafte Verbindung mit diesem Server herstellen"),
				 G_CALLBACK (action_connect_to_server_link_callback) },
  /* name, stock id */         { "Mount Volume", "xsi-media-mount-symbolic",
  /* label, accelerator */       N_("_Einhängen"), NULL,
  /* tooltip */                  N_("Den gewählten Datenträger einbinden"),
				 G_CALLBACK (action_mount_volume_callback) },
  /* name, stock id */         { "Unmount Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Aushängen"), NULL,
  /* tooltip */                  N_("Den gewählten Datenträger aushängen"),
				 G_CALLBACK (action_unmount_volume_callback) },
  /* name, stock id */         { "Eject Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Auswerfen"), NULL,
  /* tooltip */                  N_("Den ausgewählten Datenträger auswerfen"),
				 G_CALLBACK (action_eject_volume_callback) },
  /* name, stock id */         { "Start Volume", NULL,
  /* label, accelerator */       N_("_Start"), NULL,
  /* tooltip */                  N_("Den ausgewählten Datenträger starten"),
				 G_CALLBACK (action_start_volume_callback) },
  /* name, stock id */         { "Stop Volume", NULL,
  /* label, accelerator */       N_("_Anhalten"), NULL,
  /* tooltip */                  N_("Den gewählten Datenträger anhalten"),
				 G_CALLBACK (action_stop_volume_callback) },
  /* name, stock id */         { "Poll", NULL,
  /* label, accelerator */       N_("Me_dien erkennen"), NULL,
  /* tooltip */                  N_("Medium im gewählten Datenträger erkennen"),
				 G_CALLBACK (action_detect_media_callback) },
  /* name, stock id */         { "Self Mount Volume", "xsi-media-mount-symbolic",
  /* label, accelerator */       N_("_Einhängen"), NULL,
  /* tooltip */                  N_("Den zum geöffneten Ordner gehörenden Datenträger einhängen"),
				 G_CALLBACK (action_self_mount_volume_callback) },
  /* name, stock id */         { "Self Unmount Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Aushängen"), NULL,
  /* tooltip */                  N_("Den zum geöffneten Ordner gehörenden Datenträger aushängen"),
				 G_CALLBACK (action_self_unmount_volume_callback) },
  /* name, stock id */         { "Self Eject Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Auswerfen"), NULL,
  /* tooltip */                  N_("Den zum geöffneten Ordner gehörenden Datenträger auswerfen"),
				 G_CALLBACK (action_self_eject_volume_callback) },
  /* name, stock id */         { "Self Start Volume", NULL,
  /* label, accelerator */       N_("_Start"), NULL,
  /* tooltip */                  N_("Den zum geöffneten Ordner gehörenden Datenträger starten"),
				 G_CALLBACK (action_self_start_volume_callback) },
  /* name, stock id */         { "Self Stop Volume", NULL,
  /* label, accelerator */       N_("_Anhalten"), NULL,
  /* tooltip */                  N_("Den zum geöffneten Ordner gehörenden Datenträger anhalten"),
				 G_CALLBACK (action_self_stop_volume_callback) },
  /* name, stock id */         { "Self Poll", NULL,
  /* label, accelerator */       N_("Me_dien erkennen"), NULL,
  /* tooltip */                  N_("Medium im gewählten Datenträger erkennen"),
				 G_CALLBACK (action_self_detect_media_callback) },
  /* Location-specific actions */
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_OPEN_ALTERNATE, NULL,
  /* label, accelerator */       N_("In Navigationsfenster öffnen"), "",
  /* tooltip */                  N_("Diesen Ordner in einem einzelnen Navigationsfenster öffnen"),
				 G_CALLBACK (action_location_open_alternate_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_OPEN_IN_NEW_TAB, NULL,
  /* label, accelerator */       N_("In neuem _Reiter öffnen"), "",
  /* tooltip */                  N_("Diesen Ordner in einem neuen Reiter öffnen"),
				 G_CALLBACK (action_location_open_in_new_tab_callback) },

  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_CUT, "xsi-edit-cut-symbolic",
  /* label, accelerator */       N_("_Ausschneiden"), "",
  /* tooltip */                  N_("Diesen Ordner auf Verschieben mit »Einfügen« vorbereiten"),
				 G_CALLBACK (action_location_cut_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_COPY, "xsi-edit-copy-symbolic",
  /* label, accelerator */       N_("_Kopieren"), "",
  /* tooltip */                  N_("Diesen Ordner auf Kopieren mit »Einfügen« vorbereiten"),
				 G_CALLBACK (action_location_copy_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_PASTE_FILES_INTO, "xsi-edit-paste-symbolic",
  /* label, accelerator */       N_("In Ordner e_infügen"), "",
  /* tooltip */                  N_("Zuvor durch »Ausschneiden« oder »Kopieren« gewählte Dateien in diesen Ordner verschieben oder kopieren"),
				 G_CALLBACK (action_location_paste_files_into_callback) },

  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_TRASH, NULL,
  /* label, accelerator */       N_("In den _Papierkorb verschieben"), "",
  /* tooltip */                  N_("Diesen Ordner in den Papierkorb verschieben"),
				 G_CALLBACK (action_location_trash_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_DELETE, NOLPHIN_ICON_DELETE,
  /* label, accelerator */       N_("_Löschen"), "",
  /* tooltip */                  N_("Diesen Ordner löschen, ohne ihn in den Papierkorb zu verschieben"),
				 G_CALLBACK (action_location_delete_callback) },
  /* name, stock id */         { NOLPHIN_ACTION_LOCATION_RESTORE_FROM_TRASH, NULL,
  /* label, accelerator */       N_("Am _ursprünglichen Ort wiederherstellen"), NULL, NULL,
				 G_CALLBACK (action_location_restore_from_trash_callback) },

  /* name, stock id */         { "Location Mount Volume", "xsi-media-mount-symbolic",
  /* label, accelerator */       N_("_Einhängen"), NULL,
  /* tooltip */                  N_("Den zu diesem Ordner gehörenden Datenträger einhängen"),
				 G_CALLBACK (action_location_mount_volume_callback) },
  /* name, stock id */         { "Location Unmount Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Aushängen"), NULL,
  /* tooltip */                  N_("Den zu diesem Ordner gehörenden Datenträger aushängen"),
				 G_CALLBACK (action_location_unmount_volume_callback) },
  /* name, stock id */         { "Location Eject Volume", "xsi-media-eject-symbolic",
  /* label, accelerator */       N_("_Auswerfen"), NULL,
  /* tooltip */                  N_("Den zu diesem Ordner gehörenden Datenträger auswerfen"),
				 G_CALLBACK (action_location_eject_volume_callback) },
  /* name, stock id */         { "Location Start Volume", NULL,
  /* label, accelerator */       N_("_Start"), NULL,
  /* tooltip */                  N_("Den zu diesem Ordner gehörenden Datenträger starten"),
				 G_CALLBACK (action_location_start_volume_callback) },
  /* name, stock id */         { "Location Stop Volume", NULL,
  /* label, accelerator */       N_("_Anhalten"), NULL,
  /* tooltip */                  N_("Den zu diesem Ordner gehörenden Datenträger anhalten"),
				 G_CALLBACK (action_location_stop_volume_callback) },
  /* name, stock id */         { "Location Poll", NULL,
  /* label, accelerator */       N_("Me_dien erkennen"), NULL,
  /* tooltip */                  N_("Medium im gewählten Datenträger erkennen"),
				 G_CALLBACK (action_location_detect_media_callback) },

  /* name, stock id */         { "LocationProperties", "xsi-document-properties-symbolic",
  /* label, accelerator */       N_("_Eigenschaften"), NULL,
  /* tooltip */                  N_("Die Eigenschaften dieses Ordners anzeigen/ändern"),
				 G_CALLBACK (action_location_properties_callback) },

  /* name, stock id, label */  {NOLPHIN_ACTION_COPY_TO_NEXT_PANE, NULL, N_("_Andere Leiste"),
				NULL, N_("Die aktuelle Auswahl in die andere Leiste dieses Fensters kopieren"),
				G_CALLBACK (action_copy_to_next_pane_callback) },
  /* name, stock id, label */  {NOLPHIN_ACTION_MOVE_TO_NEXT_PANE, NULL, N_("_Andere Leiste"),
				NULL, N_("Die aktuelle Auswahl in die andere Leiste dieses Fensters verschieben"),
				G_CALLBACK (action_move_to_next_pane_callback) },
  /* name, stock id, label */  {NOLPHIN_ACTION_COPY_TO_HOME, NULL,
				N_("_Persönlicher Ordner"), NULL,
				N_("Die aktuelle Auswahl in den persönlichen Ordner kopieren"),
				G_CALLBACK (action_copy_to_home_callback) },
  /* name, stock id, label */  {NOLPHIN_ACTION_MOVE_TO_HOME, NULL,
				N_("_Persönlicher Ordner"), NULL,
				N_("Die aktuelle Auswahl in den persönlichen Ordner verschieben"),
				G_CALLBACK (action_move_to_home_callback) },
  /* name, stock id, label */  {NOLPHIN_ACTION_COPY_TO_DESKTOP, NULL,
				N_("_Schreibtisch"), NULL,
				N_("Die aktuelle Auswahl auf den Schreibtisch kopieren"),
				G_CALLBACK (action_copy_to_desktop_callback) },
  /* name, stock id, label */  {NOLPHIN_ACTION_MOVE_TO_DESKTOP, NULL,
				N_("_Schreibtisch"), NULL,
				N_("Die aktuelle Auswahl auf den Schreibtisch verschieben"),
				G_CALLBACK (action_move_to_desktop_callback) },
                               {NOLPHIN_ACTION_BROWSE_MOVE_TO, "xsi-document-open-symbolic",
                N_("Durchsuchen …"), NULL,
                N_("Nach einem Zielordner zum Verschieben der Auswahl suchen"),
                G_CALLBACK (action_browse_for_move_to_folder_callback) },
                               {NOLPHIN_ACTION_BROWSE_COPY_TO, "xsi-document-open-symbolic",
                N_("Durchsuchen …"), NULL,
                N_("Nach einem Zielordner zum Kopieren der Auswahl suchen"),
                G_CALLBACK (action_browse_for_copy_to_folder_callback) },
                               {NOLPHIN_ACTION_PIN_FILE, "xsi-pin-symbolic",
                N_("_Anheften"), "<control><shift>D",
                N_("Die ausgewählte Datei anheften, so dass sie immer ganz oben in der Dateiliste dieses Ortes erscheint."),
                G_CALLBACK (action_pin_unpin_file_callback) },
                               {NOLPHIN_ACTION_UNPIN_FILE, "xsi-unpin-symbolic",
                N_("_Lösen"), "<control><shift>D",
                N_("Die ausgewählte Datei vom Anfang der Dateiliste dieses Ortes lösen."),
                G_CALLBACK (action_pin_unpin_file_callback) },
                               {NOLPHIN_ACTION_FAVORITE_FILE, "xsi-favorite-symbolic",
                N_("Zu Favoriten hinzufügen"), NULL,
                N_("Die ausgewählte Datei zu Ihren Favoriten hinzufügen"),
                G_CALLBACK (action_favorite_unfavorite_file_callback) },
                               {NOLPHIN_ACTION_UNFAVORITE_FILE, "xsi-unfavorite-symbolic",
                N_("Aus Favoriten entfernen"), NULL,
                N_("Die ausgewählte Datei aus Ihren Favoriten entfernen"),
                G_CALLBACK (action_favorite_unfavorite_file_callback) }
};

static void
connect_proxy (NolphinView *view,
               GtkAction *action,
               GtkWidget *proxy,
               GtkActionGroup *action_group)
{
    if (strcmp (gtk_action_get_name (action), NOLPHIN_ACTION_NEW_EMPTY_DOCUMENT) == 0 &&
        GTK_IS_IMAGE_MENU_ITEM (proxy)) {

        GtkWidget *image;

        image = gtk_image_new_from_icon_name ("text-x-generic", GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (proxy), image);
        gtk_action_set_always_show_image (action, TRUE);
    }
}

static void
pre_activate (NolphinView *view,
	      GtkAction *action,
	      GtkActionGroup *action_group)
{
	GdkEvent *event;
	GtkWidget *proxy;
	gboolean activated_from_popup;

	/* check whether action was activated through a popup menu.
	 * If not, unset the last stored context menu popup position */
	activated_from_popup = FALSE;

	event = gtk_get_current_event ();
	proxy = gtk_get_event_widget (event);

	if (proxy != NULL) {
		GtkWidget *toplevel;
		GdkWindowTypeHint hint;

		toplevel = gtk_widget_get_toplevel (proxy);

		if (GTK_IS_WINDOW (toplevel)) {
			hint = gtk_window_get_type_hint (GTK_WINDOW (toplevel));

			if (hint == GDK_WINDOW_TYPE_HINT_POPUP_MENU) {
				activated_from_popup = TRUE;
			}
		}
	}

	if (!activated_from_popup) {
		update_context_menu_position_from_event (view, NULL);
	}
}

static void
real_merge_menus (NolphinView *view)
{
	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;
	GtkAction *action;
	char *tooltip;

	ui_manager = nolphin_window_get_ui_manager (view->details->window);

	action_group = gtk_action_group_new ("DirViewActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	view->details->dir_action_group = action_group;

	gtk_action_group_add_actions (action_group,
				      directory_view_entries, G_N_ELEMENTS (directory_view_entries),
				      view);

	tooltip = g_strdup_printf (_("Skripte ausführen"));
	/* Create a script action here specially because its tooltip is dynamic */
	action = gtk_action_new ("Scripts", _("_Skripte"), tooltip, NULL);
	gtk_action_group_add_action (action_group, action);
	g_object_unref (action);
	g_free (tooltip);

	action = gtk_action_group_get_action (action_group, NOLPHIN_ACTION_NO_TEMPLATES);
	gtk_action_set_sensitive (action, FALSE);

	g_signal_connect_object (action_group, "connect-proxy",
				 G_CALLBACK (connect_proxy), G_OBJECT (view),
				 G_CONNECT_SWAPPED);
	g_signal_connect_object (action_group, "pre-activate",
				 G_CALLBACK (pre_activate), G_OBJECT (view),
				 G_CONNECT_SWAPPED);

	/* Insert action group at end so clipboard action group ends up before it */
	gtk_ui_manager_insert_action_group (ui_manager, action_group, -1);
	g_object_unref (action_group); /* owned by ui manager */

    view->details->dir_merge_id = gtk_ui_manager_add_ui_from_resource (ui_manager, "/org/nolphin/nolphin-directory-view-ui.xml", NULL);

	view->details->scripts_invalid = TRUE;
	view->details->templates_invalid = TRUE;
    view->details->actions_invalid = TRUE;
}

static gboolean
can_paste_into_file (NolphinFile *file)
{
	if (nolphin_file_is_directory (file) &&
	    nolphin_file_can_write (file)) {
		return TRUE;
	}
	if (nolphin_file_has_activation_uri (file)) {
		GFile *location;
		NolphinFile *activation_file;
		gboolean res;

		location = nolphin_file_get_activation_location (file);
		activation_file = nolphin_file_get (location);
		g_object_unref (location);

		/* The target location might not have data for it read yet,
		   and we can't want to do sync I/O, so treat the unknown
		   case as can-write */
		res = (nolphin_file_get_file_type (activation_file) == G_FILE_TYPE_UNKNOWN) ||
			(nolphin_file_get_file_type (activation_file) == G_FILE_TYPE_DIRECTORY &&
			 nolphin_file_can_write (activation_file));

		nolphin_file_unref (activation_file);

		return res;
	}

	return FALSE;
}

static void
clipboard_targets_received (GtkClipboard     *clipboard,
                            GdkAtom          *targets,
                            int               n_targets,
			    gpointer          user_data)
{
	NolphinView *view;
	gboolean can_paste;
	int i;
	GList *selection;
	int count;
	GtkAction *action;

	view = NOLPHIN_VIEW (user_data);
	can_paste = FALSE;

	if (view->details->window == NULL ||
	    !view->details->active) {
		/* We've been destroyed or became inactive since call */
		g_object_unref (view);
		return;
	}

	if (targets) {
		for (i=0; i < n_targets; i++) {
			if (targets[i] == copied_files_atom) {
				can_paste = TRUE;
			}
		}
	}


	selection = nolphin_view_get_selection (view);
	count = g_list_length (selection);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_PASTE);
	gtk_action_set_sensitive (action,
				  can_paste && !nolphin_view_is_read_only (view));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_PASTE_FILES_INTO);
	gtk_action_set_sensitive (action,
	                          can_paste && count == 1 &&
	                          can_paste_into_file (NOLPHIN_FILE (selection->data)));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_PASTE_FILES_INTO);
	g_object_set_data (G_OBJECT (action),
			   "can-paste-according-to-clipboard",
			   GINT_TO_POINTER (can_paste));
	gtk_action_set_sensitive (action,
				  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (action),
								      "can-paste-according-to-clipboard")) &&
				  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (action),
								      "can-paste-according-to-destination")));

	nolphin_file_list_free (selection);

	g_object_unref (view);
}

static gboolean
should_show_empty_trash (NolphinView *view)
{
	return (showing_trash_directory (view));
}

static gboolean
file_list_all_are_folders (GList *file_list)
{
	GList *l;
	NolphinFile *file, *linked_file;
	char *activation_uri;
	gboolean is_dir;

	for (l = file_list; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);
		if (nolphin_file_is_nolphin_link (file) &&
		    !NOLPHIN_IS_DESKTOP_ICON_FILE (file)) {
			if (nolphin_file_is_launcher (file)) {
				return FALSE;
			}

			activation_uri = nolphin_file_get_activation_uri (file);

			if (activation_uri == NULL) {
				g_free (activation_uri);
				return FALSE;
			}

			linked_file = nolphin_file_get_existing_by_uri (activation_uri);

			/* We might not actually know the type of the linked file yet,
			 * however we don't want to schedule a read, since that might do things
			 * like ask for password etc. This is a bit unfortunate, but I don't
			 * know any way around it, so we do various heuristics here
			 * to get things mostly right
			 */
			is_dir =
				(linked_file != NULL &&
				 nolphin_file_is_directory (linked_file)) ||
				(activation_uri != NULL &&
				 activation_uri[strlen (activation_uri) - 1] == '/');

			nolphin_file_unref (linked_file);
			g_free (activation_uri);

			if (!is_dir) {
				return FALSE;
			}
		} else if (!(nolphin_file_is_directory (file) ||
			     NOLPHIN_IS_DESKTOP_ICON_FILE (file))) {
			return FALSE;
		}
	}
	return TRUE;
}

static void
file_should_show_foreach (NolphinFile        *file,
			  gboolean            *show_mount,
			  gboolean            *show_unmount,
			  gboolean            *show_eject,
			  gboolean            *show_connect,
			  gboolean            *show_start,
			  gboolean            *show_stop,
			  gboolean            *show_poll,
			  GDriveStartStopType *start_stop_type)
{
	char *uri;

	*show_mount = FALSE;
	*show_unmount = FALSE;
	*show_eject = FALSE;
	*show_connect = FALSE;
	*show_start = FALSE;
	*show_stop = FALSE;
	*show_poll = FALSE;

	if (nolphin_file_can_eject (file)) {
		*show_eject = TRUE;
	}

	if (nolphin_file_can_mount (file)) {
		*show_mount = TRUE;
	}

	if (nolphin_file_can_start (file) || nolphin_file_can_start_degraded (file)) {
		*show_start = TRUE;
	}

	if (nolphin_file_can_stop (file)) {
		*show_stop = TRUE;
	}

	/* Dot not show both Unmount and Eject/Safe Removal; too confusing to
	 * have too many menu entries */
	if (nolphin_file_can_unmount (file) && !*show_eject && !*show_stop) {
		*show_unmount = TRUE;
	}

	if (nolphin_file_can_poll_for_media (file) && !nolphin_file_is_media_check_automatic (file)) {
		*show_poll = TRUE;
	}

	*start_stop_type = nolphin_file_get_start_stop_type (file);

	if (nolphin_file_is_nolphin_link (file)) {
		uri = nolphin_file_get_activation_uri (file);
		if (uri != NULL &&
		    (g_str_has_prefix (uri, "ftp:") ||
		     g_str_has_prefix (uri, "ssh:") ||
		     g_str_has_prefix (uri, "sftp:") ||
		     g_str_has_prefix (uri, "dav:") ||
		     g_str_has_prefix (uri, "davs:"))) {
			*show_connect = TRUE;
		}
		g_free (uri);
	}
}

static void
file_should_show_self (NolphinFile        *file,
		       gboolean            *show_mount,
		       gboolean            *show_unmount,
		       gboolean            *show_eject,
		       gboolean            *show_start,
		       gboolean            *show_stop,
		       gboolean            *show_poll,
		       GDriveStartStopType *start_stop_type)
{
	*show_mount = FALSE;
	*show_unmount = FALSE;
	*show_eject = FALSE;
	*show_start = FALSE;
	*show_stop = FALSE;
	*show_poll = FALSE;

	if (file == NULL) {
		return;
	}

	if (nolphin_file_can_eject (file)) {
		*show_eject = TRUE;
	}

	if (nolphin_file_can_mount (file)) {
		*show_mount = TRUE;
	}

	if (nolphin_file_can_start (file) || nolphin_file_can_start_degraded (file)) {
		*show_start = TRUE;
	}

	if (nolphin_file_can_stop (file)) {
		*show_stop = TRUE;
	}

	/* Dot not show both Unmount and Eject/Safe Removal; too confusing to
	 * have too many menu entries */
	if (nolphin_file_can_unmount (file) && !*show_eject && !*show_stop) {
		*show_unmount = TRUE;
	}

	if (nolphin_file_can_poll_for_media (file) && !nolphin_file_is_media_check_automatic (file)) {
		*show_poll = TRUE;
	}

	*start_stop_type = nolphin_file_get_start_stop_type (file);

}

static gboolean
files_are_all_directories (GList *files)
{
	NolphinFile *file;
	GList *l;
	gboolean all_directories;

	all_directories = TRUE;

	for (l = files; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);
		all_directories &= nolphin_file_is_directory (file);
	}

	return all_directories;
}

static gboolean
files_is_none_directory (GList *files)
{
	NolphinFile *file;
	GList *l;
	gboolean no_directory;

	no_directory = TRUE;

	for (l = files; l != NULL; l = l->next) {
		file = NOLPHIN_FILE (l->data);
		no_directory &= !nolphin_file_is_directory (file);
	}

	return no_directory;
}

static void
update_restore_from_trash_action (GtkAction *action,
				  GList *files,
				  gboolean is_self)
{
	NolphinFile *original_file;
	NolphinFile *original_dir;
	GHashTable *original_dirs_hash;
	GList *original_dirs;
	GFile *original_location;
	char *tooltip, *original_name;

	original_file = NULL;
	original_dir = NULL;
	original_dirs = NULL;
	original_dirs_hash = NULL;
	original_location = NULL;
	original_name = NULL;

	if (files != NULL) {
		if (g_list_length (files) == 1) {
			original_file = nolphin_file_get_trash_original_file (files->data);
		} else {
			original_dirs_hash = nolphin_trashed_files_get_original_directories (files, NULL);
			if (original_dirs_hash != NULL) {
				original_dirs = g_hash_table_get_keys (original_dirs_hash);
				if (g_list_length (original_dirs) == 1) {
					original_dir = nolphin_file_ref (NOLPHIN_FILE (original_dirs->data));
				}
			}
		}
	}

	if (original_file != NULL || original_dirs != NULL) {
		gtk_action_set_visible (action, TRUE);

		if (original_file != NULL) {
			original_location = nolphin_file_get_location (original_file);
		} else if (original_dir != NULL) {
			original_location = nolphin_file_get_location (original_dir);
		}

		if (original_location != NULL) {
			original_name = g_file_get_parse_name (original_location);
		}

		if (is_self) {
			g_assert (g_list_length (files) == 1);
			g_assert (original_location != NULL);
			tooltip = g_strdup_printf (_("Den geöffneten Ordner aus dem Papierkorb nach »%s« verschieben"), original_name);
		} else if (files_are_all_directories (files)) {
			if (original_name != NULL) {
				tooltip = g_strdup_printf (ngettext ("Den gewählten Ordner aus dem Papierkorb nach »%s« verschieben",
								     "Die gewählten Ordner aus dem Papierkorb nach »%s« verschieben",
								     g_list_length (files)), original_name);
			} else {
				tooltip = g_strdup_printf (ngettext ("Den gewählten Ordner aus dem Papierkorb verschieben",
								     "Die gewählten Ordner aus dem Papierkorb verschieben",
								     g_list_length (files)));
			}
		} else if (files_is_none_directory (files)) {
			if (original_name != NULL) {
				tooltip = g_strdup_printf (ngettext ("Die gewählte Datei aus dem Papierkorb nach »%s« verschieben",
								     "Die gewählten Dateien aus dem Papierkorb nach »%s« verschieben",
								     g_list_length (files)), original_name);
			} else {
				tooltip = g_strdup_printf (ngettext ("Die gewählte Datei aus dem Papierkorb entfernen",
								     "Die gewählten Dateien aus dem Papierkorb entfernen",
								     g_list_length (files)));
			}
		} else {
			if (original_name != NULL) {
				tooltip = g_strdup_printf (ngettext ("Das gewählte Objekt aus dem Papierkorb nach »%s« verschieben",
								     "Die gewählten Objekte aus dem Papierkorb nach »%s« verschieben",
								     g_list_length (files)), original_name);
			} else {
				tooltip = g_strdup_printf (ngettext ("Das gewählte Objekt aus dem Papierkorb entfernen",
								     "Die gewählten Objekte aus dem Papierkorb entfernen",
								     g_list_length (files)));
			}
		}
		g_free (original_name);

		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		if (original_location != NULL) {
			g_object_unref (original_location);
		}
	} else {
		gtk_action_set_visible (action, FALSE);
	}

	nolphin_file_unref (original_file);
	nolphin_file_unref (original_dir);
	g_list_free (original_dirs);

	if (original_dirs_hash != NULL) {
		g_hash_table_destroy (original_dirs_hash);
	}
}

static void
real_update_menus_volumes (NolphinView *view,
			   GList *selection,
			   gint selection_count)
{
	GList *l;
	NolphinFile *file;
	gboolean show_mount;
	gboolean show_unmount;
	gboolean show_eject;
	gboolean show_connect;
	gboolean show_start;
	gboolean show_stop;
	gboolean show_poll;
	GDriveStartStopType start_stop_type;
	gboolean show_self_mount;
	gboolean show_self_unmount;
	gboolean show_self_eject;
	gboolean show_self_start;
	gboolean show_self_stop;
	gboolean show_self_poll;
	GDriveStartStopType self_start_stop_type;
	GtkAction *action;

	show_mount = (selection != NULL);
	show_unmount = (selection != NULL);
	show_eject = (selection != NULL);
	show_connect = (selection != NULL && selection_count == 1);
	show_start = (selection != NULL && selection_count == 1);
	show_stop = (selection != NULL && selection_count == 1);
	show_poll = (selection != NULL && selection_count == 1);
	start_stop_type = G_DRIVE_START_STOP_TYPE_UNKNOWN;
	self_start_stop_type = G_DRIVE_START_STOP_TYPE_UNKNOWN;

	for (l = selection; l != NULL && (show_mount || show_unmount
					  || show_eject || show_connect
                                          || show_start || show_stop
					  || show_poll);
	     l = l->next) {
		gboolean show_mount_one;
		gboolean show_unmount_one;
		gboolean show_eject_one;
		gboolean show_connect_one;
		gboolean show_start_one;
		gboolean show_stop_one;
		gboolean show_poll_one;

		file = NOLPHIN_FILE (l->data);
		file_should_show_foreach (file,
					  &show_mount_one,
					  &show_unmount_one,
					  &show_eject_one,
					  &show_connect_one,
                                          &show_start_one,
                                          &show_stop_one,
					  &show_poll_one,
					  &start_stop_type);

		show_mount &= show_mount_one;
		show_unmount &= show_unmount_one;
		show_eject &= show_eject_one;
		show_connect &= show_connect_one;
		show_start &= show_start_one;
		show_stop &= show_stop_one;
		show_poll &= show_poll_one;
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_CONNECT_TO_SERVER_LINK);
	gtk_action_set_visible (action, show_connect);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_MOUNT_VOLUME);
	gtk_action_set_visible (action, show_mount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_UNMOUNT_VOLUME);
	gtk_action_set_visible (action, show_unmount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_EJECT_VOLUME);
	gtk_action_set_visible (action, show_eject);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_START_VOLUME);
	gtk_action_set_visible (action, show_start);
	if (show_start) {
		switch (start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Verbinden"));
			gtk_action_set_tooltip (action, _("Mit gewähltem Laufwerk verbinden"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _starten"));
			gtk_action_set_tooltip (action, _("Das ausgewählte Multimedienlaufwerk _starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk en_tsperren"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk entsperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_STOP_VOLUME);
	gtk_action_set_visible (action, show_stop);
	if (show_stop) {
		switch (start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Anhalten"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("Laufwerk _sicher entfernen"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk sicher entfernen"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Trennen"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk trennen"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _anhalten"));
			gtk_action_set_tooltip (action, _("Das ausgewählte Multimedienlaufwerk anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk _sperren"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk sperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_POLL);
	gtk_action_set_visible (action, show_poll);

	show_self_mount = show_self_unmount = show_self_eject =
		show_self_start = show_self_stop = show_self_poll = FALSE;

	file = nolphin_view_get_directory_as_file (view);
	file_should_show_self (file,
			       &show_self_mount,
			       &show_self_unmount,
			       &show_self_eject,
			       &show_self_start,
			       &show_self_stop,
			       &show_self_poll,
			       &self_start_stop_type);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_MOUNT_VOLUME);
	gtk_action_set_visible (action, show_self_mount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_UNMOUNT_VOLUME);
	gtk_action_set_visible (action, show_self_unmount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_EJECT_VOLUME);
	gtk_action_set_visible (action, show_self_eject);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_START_VOLUME);
	gtk_action_set_visible (action, show_self_start);
	if (show_self_start) {
		switch (self_start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Verbinden"));
			gtk_action_set_tooltip (action, _("Verbinden mit dem zum geöffneten Ordner gehörenden Laufwerk"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _starten"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Multimedienlaufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk _entsperren"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk entsperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_STOP_VOLUME);
	gtk_action_set_visible (action, show_self_stop);
	if (show_self_stop) {
		switch (self_start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Anhalten"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk _anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("Laufwerk _sicher entfernen"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk sicher entfernen"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Trennen"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk trennen"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _anhalten"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Multimedienlaufwerk anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk _sperren"));
			gtk_action_set_tooltip (action, _("Das zum geöffneten Ordner gehörende Laufwerk sperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELF_POLL);
	gtk_action_set_visible (action, show_self_poll);

}

static void
real_update_location_menu_volumes (NolphinView *view)
{
	GtkAction *action;
	NolphinFile *file;
	gboolean show_mount;
	gboolean show_unmount;
	gboolean show_eject;
	gboolean show_connect;
	gboolean show_start;
	gboolean show_stop;
	gboolean show_poll;
	GDriveStartStopType start_stop_type;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (NOLPHIN_IS_FILE (view->details->location_popup_directory_as_file));

	file = NOLPHIN_FILE (view->details->location_popup_directory_as_file);
	file_should_show_foreach (file,
				  &show_mount,
				  &show_unmount,
				  &show_eject,
				  &show_connect,
				  &show_start,
				  &show_stop,
				  &show_poll,
				  &start_stop_type);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_MOUNT_VOLUME);
	gtk_action_set_visible (action, show_mount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_UNMOUNT_VOLUME);
	gtk_action_set_visible (action, show_unmount);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_EJECT_VOLUME);
	gtk_action_set_visible (action, show_eject);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_START_VOLUME);
	gtk_action_set_visible (action, show_start);
	if (show_start) {
		switch (start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("_Start"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Verbinden"));
			gtk_action_set_tooltip (action, _("Mit gewähltem Laufwerk verbinden"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _starten"));
			gtk_action_set_tooltip (action, _("Das ausgewählte Multimedienlaufwerk _starten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk _entsperren"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk entsperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_STOP_VOLUME);
	gtk_action_set_visible (action, show_stop);
	if (show_stop) {
		switch (start_stop_type) {
		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			gtk_action_set_label (action, _("_Anhalten"));
			gtk_action_set_tooltip (action, _("Den gewählten Datenträger anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			gtk_action_set_label (action, _("Laufwerk _sicher entfernen"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk sicher entfernen"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (action, _("_Trennen"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk trennen"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (action, _("Multimedienlaufwerk _anhalten"));
			gtk_action_set_tooltip (action, _("Das ausgewählte Multimedienlaufwerk anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			gtk_action_set_label (action, _("Laufwerk _sperren"));
			gtk_action_set_tooltip (action, _("Gewähltes Laufwerk sperren"));
			break;
		}
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_POLL);
	gtk_action_set_visible (action, show_poll);
}

/* TODO: we should split out this routine into two functions:
 * Update on clipboard changes
 * Update on selection changes
 */
static void
real_update_paste_menu (NolphinView *view,
			GList *selection,
			gint selection_count)
{
	gboolean can_paste_files_into;
	gboolean selection_is_read_only;
    gboolean selection_contains_recent;
    gboolean selection_contains_favorites;
	gboolean is_read_only;
	GtkAction *action;

	selection_is_read_only = selection_count == 1 &&
		(!nolphin_file_can_write (NOLPHIN_FILE (selection->data)) &&
		 !nolphin_file_has_activation_uri (NOLPHIN_FILE (selection->data)));

	is_read_only = nolphin_view_is_read_only (view);

    selection_contains_recent = showing_recent_directory (view);
    selection_contains_favorites = showing_favorites_directory (view);

    can_paste_files_into = (!selection_contains_recent && !selection_contains_favorites &&
                            selection_count == 1 &&
                            can_paste_into_file (NOLPHIN_FILE (selection->data)));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_PASTE);
	gtk_action_set_sensitive (action, !is_read_only);

    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_PASTE_FILES_INTO);
	gtk_action_set_visible (action, can_paste_files_into);
	gtk_action_set_sensitive (action, !selection_is_read_only);

	/* Ask the clipboard */
	g_object_ref (view); /* Need to keep the object alive until we get the reply */
	gtk_clipboard_request_targets (nolphin_clipboard_get (GTK_WIDGET (view)),
				       clipboard_targets_received,
				       view);
}

static void
real_update_location_menu (NolphinView *view)
{
	GtkAction *action;
	NolphinFile *file;
	gboolean is_special_link;
	gboolean is_desktop_or_home_dir;
    gboolean is_recent;
	gboolean can_delete_file, show_delete;
	gboolean show_separate_delete_command;
	gboolean show_open_in_new_tab;
	gboolean show_open_alternate;
	GList l;
	char *label;
	char *tip;

	show_open_in_new_tab = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER);
	show_open_alternate = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_OPEN_ALTERNATE);
	gtk_action_set_visible (action, show_open_alternate);

	label = _("In neuem _Fenster öffnen");
	g_object_set (action,
		      "label", label,
		      NULL);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_OPEN_IN_NEW_TAB);
	gtk_action_set_visible (action, show_open_in_new_tab);

	label = _("In neuem _Reiter öffnen");
	g_object_set (action,
		      "label", label,
		      NULL);

	file = view->details->location_popup_directory_as_file;
	g_assert (NOLPHIN_IS_FILE (file));
	g_assert (nolphin_file_check_if_ready (file, NOLPHIN_FILE_ATTRIBUTE_INFO |
						NOLPHIN_FILE_ATTRIBUTE_MOUNT |
						NOLPHIN_FILE_ATTRIBUTE_FILESYSTEM_INFO));

	is_special_link = NOLPHIN_IS_DESKTOP_ICON_FILE (file);
	is_desktop_or_home_dir = nolphin_file_is_home (file)
		|| nolphin_file_is_desktop_directory (file);

    is_recent = nolphin_file_is_in_recent (file);

	can_delete_file =
		nolphin_file_can_delete (file) &&
		!is_special_link &&
		!is_desktop_or_home_dir;

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_CUT);
    gtk_action_set_sensitive (action, !is_recent && can_delete_file);
    gtk_action_set_visible (action, !is_recent);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_PASTE_FILES_INTO);
	g_object_set_data (G_OBJECT (action),
			   "can-paste-according-to-destination",
			   GINT_TO_POINTER (can_paste_into_file (file)));
	gtk_action_set_sensitive (action,
                              !is_recent &&
                              GPOINTER_TO_INT (g_object_get_data (G_OBJECT (action),
                                               "can-paste-according-to-clipboard")) &&
                              GPOINTER_TO_INT (g_object_get_data (G_OBJECT (action),
                                               "can-paste-according-to-destination")));

    gtk_action_set_visible (action, !is_recent);

	show_delete = TRUE;

	if (file != NULL &&
	    nolphin_file_is_in_trash (file)) {
		if (nolphin_file_is_self_owned (file)) {
			show_delete = FALSE;
		}

		label = _("_Dauerhaft löschen");
		tip = _("Den geöffneten Ordner dauerhaft löschen");
		show_separate_delete_command = FALSE;
	} else {
		label = _("In den _Papierkorb verschieben");
		tip = _("Den geöffneten Ordner in den Papierkorb verschieben");
		show_separate_delete_command = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ENABLE_DELETE);
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_TRASH);
	g_object_set (action,
		      "label", label,
		      "tooltip", tip,
		      "icon-name", (file != NULL &&
				    nolphin_file_is_in_trash (file)) ?
		      NOLPHIN_ICON_DELETE : NOLPHIN_ICON_SYMBOLIC_TRASH_FULL,
		      NULL);
	gtk_action_set_sensitive (action, can_delete_file);
	gtk_action_set_visible (action, show_delete);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_DELETE);
	gtk_action_set_visible (action, show_separate_delete_command);
	if (show_separate_delete_command) {
		gtk_action_set_sensitive (action, can_delete_file);
		g_object_set (action,
			      "icon-name", NOLPHIN_ICON_DELETE,
			      "sensitive", can_delete_file,
			      NULL);
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_LOCATION_RESTORE_FROM_TRASH);
	l.prev = NULL;
	l.next = NULL;
	l.data = file;
	update_restore_from_trash_action (action, &l, TRUE);

	real_update_location_menu_volumes (view);
}

static void
clipboard_changed_callback (NolphinClipboardMonitor *monitor, NolphinView *view)
{
	GList *selection;
	gint selection_count;

	if (!view->details->active) {
		return;
	}

	selection = nolphin_view_get_selection (view);
	selection_count = g_list_length (selection);

	real_update_paste_menu (view, selection, selection_count);

	nolphin_file_list_free (selection);
}

static gboolean
can_delete_all (GList *files)
{
	NolphinFile *file;
	GList *l;

	for (l = files; l != NULL; l = l->next) {
		file = l->data;
		if (!nolphin_file_can_delete (file)) {
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
has_writable_extra_pane (NolphinView *view)
{
	NolphinView *other_view;

	other_view = get_directory_view_of_extra_pane (view);
	if (other_view != NULL) {
		return !nolphin_view_is_read_only (other_view);
	}
	return FALSE;
}

static void
update_configurable_context_menu_items (NolphinView *view)
{
    GtkUIManager *ui_manager;
    GtkWidget *item;
    GtkAction *action;
    gint i;

    ui_manager = nolphin_window_get_ui_manager (view->details->window);

    for (i = 0; i < CONFIGURABLE_MENU_ITEM_COUNT; i++) {
        if (!CONFIGURABLE_MENU_ITEM_INFO[i].action_name) {
            continue;
        }

        item = gtk_ui_manager_get_widget (ui_manager,
                                          CONFIGURABLE_MENU_ITEM_INFO[i].ui_path);

        action = gtk_ui_manager_get_action (ui_manager,
                                            CONFIGURABLE_MENU_ITEM_INFO[i].ui_path);

        if (!item || !action) {
            DEBUG ("Configurable menu item widget or action not found (name: %s, path: %s)",
                         CONFIGURABLE_MENU_ITEM_INFO[i].action_name,
                         CONFIGURABLE_MENU_ITEM_INFO[i].ui_path);
            continue;
        }

        gboolean pref_visible = g_settings_get_boolean (nolphin_menu_config_preferences,
                                                        CONFIGURABLE_MENU_ITEM_INFO[i].settings_key);

        gtk_widget_set_visible (item, gtk_action_get_visible (action) && pref_visible);
    }
}

static void
real_update_menus (NolphinView *view)
{
	GList *selection, *l;
	gint selection_count;
	const char *tip, *label;
	char *label_with_underscore;
	gboolean selection_contains_special_link;
	gboolean selection_contains_desktop_or_home_dir;
    gboolean selection_contains_recent;
    gboolean selection_contains_favorites;
    gboolean selection_contains_directory;
    gboolean selection_contains_trash;
	gboolean can_create_files;
	gboolean can_delete_files;
	gboolean can_copy_files;
	gboolean can_link_files;
	gboolean can_duplicate_files;
	gboolean show_separate_delete_command;
	gboolean show_open_alternate;
	gboolean show_open_in_new_tab;
	gboolean can_open;
	gboolean show_app;
    gboolean showing_search;
	gboolean show_desktop_target;
    gboolean is_desktop_view;
	GtkAction *action;
	GAppInfo *app;
	GIcon *app_icon;
	gboolean next_pane_is_writable;
	gboolean show_properties;
    gboolean first_selected_is_pinned;
    gboolean trash_supported;

	selection = nolphin_view_get_selection (view);
	selection_count = g_list_length (selection);

	selection_contains_special_link = special_link_in_selection (view, selection);
	selection_contains_desktop_or_home_dir = desktop_or_home_dir_in_selection (view, selection);
    selection_contains_recent = showing_recent_directory (view);
    selection_contains_favorites = showing_favorites_directory (view);
    selection_contains_directory = directory_in_selection (view, selection);
    selection_contains_trash = all_selected_items_in_trash (view, selection);
	can_create_files = nolphin_view_supports_creating_files (view);
	can_delete_files =
		can_delete_all (selection) &&
		selection_count != 0 &&
		!selection_contains_special_link &&
		!selection_contains_desktop_or_home_dir;
	can_copy_files = selection_count != 0
                     && !selection_contains_special_link;

	can_duplicate_files = can_create_files && can_copy_files;
	can_link_files = can_create_files && can_copy_files;

    is_desktop_view = get_is_desktop_view (view);
    trash_supported = eel_vfs_supports_uri_scheme ("trash");

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_RENAME);
    /* rename sensitivity depending on selection */
    if (selection_count > 1) {
        GList *ptr;
        gboolean can_rename = TRUE;

        for (ptr = selection; ptr != NULL; ptr = ptr->next) {
            NolphinFile *item = NOLPHIN_FILE (ptr->data);
            // Favorites can be renamed, but only one at a time - bulk renamers don't know about the
            // xapp favorites api.
            if (!nolphin_view_can_rename_file (view, item) || nolphin_file_is_in_favorites (item)) {
                can_rename = FALSE;
                break;
            }
        }

		gtk_action_set_sensitive (action, can_rename);
	} else {
		gtk_action_set_sensitive (action,
					  selection_count == 1 &&
					  nolphin_view_can_rename_file (view, selection->data));
	}

    gtk_action_set_visible (action, !selection_contains_recent &&
                                    !selection_contains_special_link);

    gboolean no_selection_or_one_dir = ((selection_count == 1 && selection_contains_directory) ||
                                        selection_count == 0);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                         NOLPHIN_ACTION_OPEN_AS_ROOT);
    gtk_action_set_visible (action, (!(nolphin_user_is_root () || showing_admin_enabled_directory (view))) && no_selection_or_one_dir);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                         NOLPHIN_ACTION_OPEN_IN_TERMINAL);
    /* Anders als no_selection_or_one_dir: auch bei genau einer ausgewaehlten
     * Datei (nicht nur einem Ordner) sichtbar - der Callback oeffnet dann
     * im Elternordner der Datei, das unterstuetzt er laengst. */
    gtk_action_set_visible (action, selection_count <= 1);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                         NOLPHIN_ACTION_PHONE_SEND);
    gtk_action_set_sensitive (action, selection_count > 0);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                         NOLPHIN_ACTION_GID_ADD_PROJECT);
    {
        gchar *gid_path = gid_candidate_path (view, selection);

        gtk_action_set_visible (action, gid_path != NULL);
        gtk_action_set_sensitive (action, gid_path != NULL && !nolphin_gid_projects_contains (gid_path));
        g_free (gid_path);
    }

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_NEW_FOLDER);
	gtk_action_set_sensitive (action, can_create_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	can_open = show_app = selection_count != 0;

	for (l = selection; l != NULL; l = l->next) {
		NolphinFile *file;

		file = NOLPHIN_FILE (selection->data);

		if (!nolphin_mime_file_opens_in_external_app (file)) {
			show_app = FALSE;
		}

		if (!show_app) {
			break;
		}
	}

	label_with_underscore = NULL;

	app = NULL;
	app_icon = NULL;

	if (can_open && show_app) {
		app = nolphin_mime_get_default_application_for_files (selection);
	}

	if (app != NULL) {
		char *escaped_app;

		escaped_app = eel_str_double_underscores (g_app_info_get_name (app));
		label_with_underscore = g_strdup_printf (_("_Öffnen mit %s"),
							 escaped_app);

		app_icon = g_app_info_get_icon (app);
		if (app_icon != NULL) {
			g_object_ref (app_icon);
		}

		g_free (escaped_app);
		g_object_unref (app);
	}

    if (app_icon == NULL) {
        app_icon = g_themed_icon_new ("xsi-folder-open-symbolic");
    }

    action = gtk_action_group_get_action (view->details->dir_action_group,
                          NOLPHIN_ACTION_OPEN);
    gtk_action_set_sensitive (action, selection_count != 0);

    g_object_set (action, "label",
              label_with_underscore ? label_with_underscore : _("_Öffnen"),
              NULL);

    gtk_action_set_gicon (action, app_icon);
    gtk_action_set_visible (action, can_open);

    g_object_unref (app_icon);
    g_free (label_with_underscore);

    menu_item_show_image (nolphin_window_get_ui_manager (view->details->window),
                          NOLPHIN_VIEW_MENU_PATH_OPEN_PLACEHOLDER,
                          NOLPHIN_ACTION_OPEN,
                          FALSE);

	show_open_alternate = file_list_all_are_folders (selection) &&
		selection_count > 0 &&
		g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER) &&
		!is_desktop_view;

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_OPEN_ALTERNATE);

	gtk_action_set_sensitive (action,  selection_count != 0);
	gtk_action_set_visible (action, show_open_alternate);

	if (selection_count == 0 || selection_count == 1) {
		label_with_underscore = g_strdup (_("In neuem _Fenster öffnen"));
	} else {
		label_with_underscore = g_strdup_printf (ngettext("In %'d neuem _Fenster öffnen",
								  "In %'d neuen _Fenstern öffnen",
								  selection_count),
							 selection_count);
	}

	g_object_set (action, "label",
		      label_with_underscore,
		      NULL);
	g_free (label_with_underscore);

	show_open_in_new_tab = show_open_alternate;
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_OPEN_IN_NEW_TAB);
	gtk_action_set_sensitive (action, selection_count != 0);
	gtk_action_set_visible (action, show_open_in_new_tab);

	if (selection_count == 0 || selection_count == 1) {
		label_with_underscore = g_strdup (_("In neuem _Reiter öffnen"));
	} else {
		label_with_underscore = g_strdup_printf (ngettext("In %'d neuem _Reiter öffnen",
								  "In %'d neuen _Reitern öffnen",
								  selection_count),
							 selection_count);
	}

	g_object_set (action, "label",
		      label_with_underscore,
		      NULL);
	g_free (label_with_underscore);

	/* Broken into its own function just for convenience */
	reset_open_with_menu (view, selection, show_app);
    reset_move_copy_to_menu (view);

	if (selection_contains_trash) {
		label = _("_Dauerhaft löschen");
		tip = _("Alle gewählten Objekte dauerhaft löschen");
		show_separate_delete_command = FALSE;
	} else {
		label = _("In den _Papierkorb verschieben");
		tip = _("Jedes gewählte Objekt in den Papierkorb verschieben");
		show_separate_delete_command = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_ENABLE_DELETE);
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_TRASH);
	g_object_set (action,
		      "label", label,
		      "tooltip", tip,
		      "icon-name", all_selected_items_in_trash (view, selection) ?
		      NOLPHIN_ICON_DELETE : NOLPHIN_ICON_SYMBOLIC_TRASH_FULL,
		      NULL);
	gtk_action_set_sensitive (action, can_delete_files);
    gtk_action_set_visible (action, trash_supported && !(selection_contains_favorites || selection_contains_recent));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_DELETE);
	gtk_action_set_visible (action, show_separate_delete_command && !selection_contains_favorites);

    if (selection_contains_recent) {
        label = _("Aus »Kürzlich« _entfernen");
        tip = _("Jedes gewählte Objekt von der Liste zuletzt verwendeter entfernen");
    } else {
        label = _("_Löschen");
        tip = _("Jedes gewählte Objekt löschen, ohne es in den Papierkorb zu verschieben");
    }

	if (show_separate_delete_command) {
		g_object_set (action,
			      "label", label,
                  "tooltip", tip,
			      "icon-name", NOLPHIN_ICON_DELETE,
			      NULL);
	}
	gtk_action_set_sensitive (action, can_delete_files);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_RESTORE_FROM_TRASH);
	update_restore_from_trash_action (action, selection, FALSE);

	action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_DUPLICATE);
	gtk_action_set_sensitive (action, can_duplicate_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_CREATE_LINK);
	gtk_action_set_sensitive (action, can_link_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);
	g_object_set (action, "label",
		      ngettext ("_Verknüpfung anlegen",
			      	"_Verknüpfungen anlegen",
				selection_count),
		      NULL);

	action = gtk_action_group_get_action (view->details->dir_action_group, "CreateHardlink");
	gtk_action_set_sensitive (action, can_link_files);
	gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	action = gtk_action_group_get_action (view->details->dir_action_group, "CopyAsMenu");
	gtk_action_set_sensitive (action, selection_count > 0);

	show_properties = (!is_desktop_view || selection_count > 0);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_PROPERTIES);

	gtk_action_set_sensitive (action, show_properties);

	if (selection_count == 0) {
		gtk_action_set_tooltip (action, _("Die Eigenschaften des aktuellen Ordners anzeigen/ändern"));
	} else {
		gtk_action_set_tooltip (action, _("Die Eigenschaften aller gewählten Objekte anzeigen/ändern"));
	}

	gtk_action_set_visible (action, show_properties);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_EMPTY_TRASH);
	g_object_set (action,
		      "label", _("Papierkorb _leeren"),
		      NULL);
	gtk_action_set_sensitive (action, !nolphin_trash_monitor_is_empty ());
	gtk_action_set_visible (action, should_show_empty_trash (view));

    showing_search = view->details->model && NOLPHIN_IS_SEARCH_DIRECTORY (view->details->model);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELECT_ALL);
	gtk_action_set_sensitive (action, !nolphin_view_is_empty (view));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_SELECT_PATTERN);
	gtk_action_set_sensitive (action, !nolphin_view_is_empty (view));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_INVERT_SELECTION);
	gtk_action_set_sensitive (action, !nolphin_view_is_empty (view));

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_CUT);
	gtk_action_set_sensitive (action, can_delete_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_COPY);
	gtk_action_set_sensitive (action, can_copy_files);

	real_update_paste_menu (view, selection, selection_count);

	real_update_menus_volumes (view, selection, selection_count);

	update_undo_actions (view);

	if (view->details->scripts_invalid) {
		update_scripts_menu (view);
	}

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_NEW_DOCUMENTS);
	gtk_action_set_sensitive (action, can_create_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

	if (can_create_files && view->details->templates_invalid) {
		update_templates_menu (view);
	}

    update_accelerated_actions (view);

	next_pane_is_writable = has_writable_extra_pane (view);

	/* next pane: works if file is copyable, and next pane is writable */
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_COPY_TO_NEXT_PANE);
	gtk_action_set_visible (action, can_copy_files && next_pane_is_writable);

	/* move to next pane: works if file is cuttable, and next pane is writable */
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_MOVE_TO_NEXT_PANE);
	gtk_action_set_visible (action, can_delete_files &&
                            next_pane_is_writable &&
                            !selection_contains_recent &&
                            !selection_contains_favorites);

	show_desktop_target =
		g_settings_get_boolean (nolphin_desktop_preferences, NOLPHIN_PREFERENCES_SHOW_DESKTOP) &&
		!g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_DESKTOP_IS_HOME_DIR);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_COPY_TO_HOME);
	gtk_action_set_sensitive (action, can_copy_files);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_COPY_TO_DESKTOP);
	gtk_action_set_sensitive (action, can_copy_files);
	gtk_action_set_visible (action, show_desktop_target);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_MOVE_TO_HOME);
	gtk_action_set_sensitive (action, can_delete_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);
	action = gtk_action_group_get_action (view->details->dir_action_group,
					      NOLPHIN_ACTION_MOVE_TO_DESKTOP);
	gtk_action_set_sensitive (action, can_delete_files);
	gtk_action_set_visible (action, show_desktop_target &&
                            !selection_contains_recent &&
                            !selection_contains_favorites);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      "CopyToMenu");
	gtk_action_set_sensitive (action, can_copy_files);

	action = gtk_action_group_get_action (view->details->dir_action_group,
					      "MoveToMenu");
	gtk_action_set_sensitive (action, can_delete_files);
    gtk_action_set_visible (action, !selection_contains_recent && !selection_contains_favorites);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_FOLLOW_SYMLINK);
    gtk_action_set_visible (action,
                            selection_count == 1 &&
                            nolphin_file_is_symbolic_link (selection->data) &&
                            !selection_contains_favorites);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_COMPRESS);
    gtk_action_set_visible (action, selection_count >= 1);

    {
        gboolean is_archive = FALSE;

        if (selection_count == 1) {
            GFile *loc = nolphin_file_get_location (NOLPHIN_FILE (selection->data));
            is_archive = nolphin_archive_detect_format (loc) != NOLPHIN_ARCHIVE_FORMAT_UNKNOWN;
            g_object_unref (loc);
        }

        action = gtk_action_group_get_action (view->details->dir_action_group,
                                              NOLPHIN_ACTION_EXTRACT_HERE);
        gtk_action_set_visible (action, is_archive);

        action = gtk_action_group_get_action (view->details->dir_action_group,
                                              NOLPHIN_ACTION_TEST_ARCHIVE);
        gtk_action_set_visible (action, is_archive);

        action = gtk_action_group_get_action (view->details->dir_action_group, "ExtractTo");
        gtk_action_set_visible (action, is_archive);

        action = gtk_action_group_get_action (view->details->dir_action_group, "ArchiveManage");
        gtk_action_set_visible (action, is_archive);
    }

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_CHECKSUM);
    gtk_action_set_visible (action,
                            selection_count == 1 &&
                            !nolphin_file_is_directory (NOLPHIN_FILE (selection->data)));

    {
        gboolean is_gpg_file = FALSE;

        action = gtk_action_group_get_action (view->details->dir_action_group,
                                              NOLPHIN_ACTION_ENCRYPT);
        gtk_action_set_visible (action, selection_count == 1);

        if (selection_count == 1) {
            gchar *name = nolphin_file_get_display_name (NOLPHIN_FILE (selection->data));
            is_gpg_file = g_str_has_suffix (name, ".gpg");
            g_free (name);
        }

        action = gtk_action_group_get_action (view->details->dir_action_group,
                                              NOLPHIN_ACTION_DECRYPT);
        gtk_action_set_visible (action, is_gpg_file);
    }

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_EDIT_ACL);
    gtk_action_set_visible (action, selection_count == 1);

    {
        /* Git-Menü überall anbieten, sobald git installiert ist. Liegt
         * der Ort in keinem Repository, bietet die Aktion beim Aufruf an,
         * eines anzulegen (siehe git_root_lookup_ready_cb()). */
        gboolean show_git = nolphin_git_is_available ();

        action = gtk_action_group_get_action (view->details->dir_action_group, NOLPHIN_ACTION_GIT_MENU);
        gtk_action_set_visible (action, show_git);
    }

    /* METADATEN UND TAGS: Menüpunkt sichtbar, sobald mindestens ein
     * Objekt ausgewählt ist. Tags- und Emblem-Dialog bearbeiten genau
     * ein Objekt gleichzeitig (siehe show_tags_and_emblem_dialog()),
     * die Bewertung lässt sich dagegen auf die ganze Auswahl anwenden. */
    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_METADATA_MENU);
    gtk_action_set_visible (action, selection_count >= 1);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_EDIT_TAGS);
    gtk_action_set_sensitive (action, selection_count == 1);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_EDIT_EMBLEM);
    gtk_action_set_sensitive (action, selection_count == 1);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_EDIT_COMMENT);
    gtk_action_set_sensitive (action, selection_count == 1);

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_OPEN_CONTAINING_FOLDER);

    gtk_action_set_visible (action,
                            selection_count == 1 &&
                            (selection_contains_recent || selection_contains_favorites || showing_search));

    first_selected_is_pinned = selection_count > 0 &&
                               nolphin_file_get_pinning (NOLPHIN_FILE (selection->data));

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_PIN_FILE);

    gtk_action_set_visible (action, selection_count > 0 && !is_desktop_view && !first_selected_is_pinned &&
                                    !(selection_contains_recent || selection_contains_favorites || selection_contains_trash));

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_UNPIN_FILE);

    gtk_action_set_visible (action, selection_count > 0 && !is_desktop_view && first_selected_is_pinned &&
                                    !(selection_contains_recent || selection_contains_favorites || selection_contains_trash));

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_FAVORITE_FILE);

    gboolean first_selected_is_favorite = selection_count > 0 &&
                                          nolphin_file_get_is_favorite (NOLPHIN_FILE (selection->data));

    if (selection_contains_favorites || selection_count == 0) {
        gtk_action_set_visible (action, FALSE);
    } else {
        gtk_action_set_visible (action, !is_desktop_view && !first_selected_is_favorite &&
                                        !selection_contains_recent && !selection_contains_trash);
    }

    action = gtk_action_group_get_action (view->details->dir_action_group,
                                          NOLPHIN_ACTION_UNFAVORITE_FILE);

    if (selection_contains_favorites && selection_count > 0) {
        gtk_action_set_visible (action, TRUE);
    } else {
        gtk_action_set_visible (action, !is_desktop_view && first_selected_is_favorite &&
                                        !selection_contains_recent && !selection_contains_trash);
    }

#ifndef HAVE_XAPP
    /* Favoriten gibt es nur mit libxapp - nicht anbieten, was nichts bewirkt. */
    gtk_action_set_visible (gtk_action_group_get_action (view->details->dir_action_group,
                                                         NOLPHIN_ACTION_FAVORITE_FILE), FALSE);
    gtk_action_set_visible (gtk_action_group_get_action (view->details->dir_action_group,
                                                         NOLPHIN_ACTION_UNFAVORITE_FILE), FALSE);
#endif

    update_configurable_context_menu_items (view);

    nolphin_file_list_free (selection);
}

void
nolphin_view_update_actions_and_extensions (NolphinView *view)
{
    GList *selection;

    update_menus_if_pending (view);

    if (view->details->actions_invalid) {
        update_actions_menu (view);
    }

    selection = nolphin_view_get_selection (view);

    update_actions_visibility (view, selection, FALSE);
    reset_extension_actions_menu (view, selection);
    update_configurable_context_menu_items (view);

    nolphin_file_list_free (selection);
}

static void
update_accelerated_actions (NolphinView *view)
{
    GList *selection;

    if (view->details->actions_invalid) {
        update_actions_menu (view);
    }

    selection = nolphin_view_get_selection (view);

    update_actions_visibility (view, selection, TRUE);

    nolphin_file_list_free (selection);
}

/**
 * nolphin_view_pop_up_selection_context_menu
 *
 * Pop up a context menu appropriate to the selected items.
 * @view: NolphinView of interest.
 * @event: The event that triggered this context menu.
 *
 * Return value: NolphinDirectory for this view.
 *
 **/
void
nolphin_view_pop_up_selection_context_menu  (NolphinView *view,
					      GdkEventButton  *event)
{
	g_assert (NOLPHIN_IS_VIEW (view));

	/* Make the context menu items not flash as they update to proper disabled,
	 * etc. states by forcing menus to update now.
	 */
    nolphin_view_update_actions_and_extensions (view);
    update_context_menu_position_from_event (view, event);

    eel_pop_up_context_menu (create_popup_menu (view, NOLPHIN_VIEW_POPUP_PATH_SELECTION),
                             (GdkEvent *) event,
                             GTK_WIDGET (view));
}

/**
 * nolphin_view_pop_up_background_context_menu
 *
 * Pop up a context menu appropriate to the view globally at the last right click location.
 * @view: NolphinView of interest.
 *
 * Return value: NolphinDirectory for this view.
 *
 **/
void
nolphin_view_pop_up_background_context_menu (NolphinView *view,
					      GdkEventButton  *event)
{
	g_assert (NOLPHIN_IS_VIEW (view));

	/* Make the context menu items not flash as they update to proper disabled,
	 * etc. states by forcing menus to update now.
	 */
    nolphin_view_update_actions_and_extensions (view);
    update_context_menu_position_from_event (view, event);

	eel_pop_up_context_menu (create_popup_menu
				 (view, NOLPHIN_VIEW_POPUP_PATH_BACKGROUND),
				 (GdkEvent *) event,
                 GTK_WIDGET (view));
}

static void
real_pop_up_location_context_menu (NolphinView *view)
{
	/* always update the menu before showing it. Shouldn't be too expensive. */
	real_update_location_menu (view);

	update_context_menu_position_from_event (view, view->details->location_popup_event);

	eel_pop_up_context_menu (create_popup_menu
				 (view, NOLPHIN_VIEW_POPUP_PATH_LOCATION),
				 (GdkEvent *) view->details->location_popup_event,
                 GTK_WIDGET (view));
}

static void
location_popup_file_attributes_ready (NolphinFile *file,
				      gpointer      data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (data);
	g_assert (NOLPHIN_IS_VIEW (view));

	g_assert (file == view->details->location_popup_directory_as_file);

	real_pop_up_location_context_menu (view);
}

static void
unschedule_pop_up_location_context_menu (NolphinView *view)
{
	if (view->details->location_popup_directory_as_file != NULL) {
		g_assert (NOLPHIN_IS_FILE (view->details->location_popup_directory_as_file));
		nolphin_file_cancel_call_when_ready (view->details->location_popup_directory_as_file,
						      location_popup_file_attributes_ready,
						      view);
		nolphin_file_unref (view->details->location_popup_directory_as_file);
		view->details->location_popup_directory_as_file = NULL;
	}
}

static void
schedule_pop_up_location_context_menu (NolphinView *view,
				       GdkEventButton  *event,
				       NolphinFile    *file)
{
	g_assert (NOLPHIN_IS_FILE (file));

	if (view->details->location_popup_event != NULL) {
		gdk_event_free ((GdkEvent *) view->details->location_popup_event);
	}
	view->details->location_popup_event = (GdkEventButton *) gdk_event_copy ((GdkEvent *)event);

	if (file == view->details->location_popup_directory_as_file) {
		if (nolphin_file_check_if_ready (file, NOLPHIN_FILE_ATTRIBUTE_INFO |
						  NOLPHIN_FILE_ATTRIBUTE_MOUNT |
						  NOLPHIN_FILE_ATTRIBUTE_FILESYSTEM_INFO)) {
			real_pop_up_location_context_menu (view);
		}
	} else {
		unschedule_pop_up_location_context_menu (view);

		view->details->location_popup_directory_as_file = nolphin_file_ref (file);
		nolphin_file_call_when_ready (view->details->location_popup_directory_as_file,
					       NOLPHIN_FILE_ATTRIBUTE_INFO |
					       NOLPHIN_FILE_ATTRIBUTE_MOUNT |
					       NOLPHIN_FILE_ATTRIBUTE_FILESYSTEM_INFO,
					       location_popup_file_attributes_ready,
					       view);
	}
}

/**
 * nolphin_view_pop_up_location_context_menu
 *
 * Pop up a context menu appropriate to the view globally.
 * @view: NolphinView of interest.
 * @event: GdkEventButton triggering the popup.
 * @location: The location the popup-menu should be created for,
 * or NULL for the currently displayed location.
 *
 **/
void
nolphin_view_pop_up_location_context_menu (NolphinView *view,
					    GdkEventButton  *event,
					    const char      *location)
{
	NolphinFile *file;

	g_assert (NOLPHIN_IS_VIEW (view));

	if (location != NULL) {
		file = nolphin_file_get_by_uri (location);
	} else {
		file = nolphin_file_ref (view->details->directory_as_file);
	}

	if (file != NULL) {
		schedule_pop_up_location_context_menu (view, event, file);
		nolphin_file_unref (file);
	}
}

static void
real_schedule_update_menus (NolphinView *view, guint update_interval)
{
	/* Don't schedule updates after destroy (#349551),
 	 * or if we are not active.
	 */
	if (view->details->window == NULL ||
	    !view->details->active) {
		return;
	}

	view->details->menu_states_untrustworthy = TRUE;
	/* Schedule a menu update with the current update interval */
    if (view->details->update_menus_timeout_id != 0) {
        g_source_remove (view->details->update_menus_timeout_id);
        view->details->update_menus_timeout_id = 0;
    }

    view->details->update_menus_timeout_id = g_timeout_add (update_interval,
                                                            update_menus_timeout_callback,
                                                            view);
}

static void
schedule_update_menus (NolphinView *view)
{
    g_assert (NOLPHIN_IS_VIEW (view));

    real_schedule_update_menus (view, view->details->update_interval);
}

static void
selection_changed_schedule_update_menus (NolphinView *view)
{
    g_assert (NOLPHIN_IS_VIEW (view));

    real_schedule_update_menus (view, SELECTION_CHANGED_UPDATE_INTERVAL);
}

static void
remove_update_status_idle_callback (NolphinView *view)
{
	if (view->details->update_status_idle_id != 0) {
		g_source_remove (view->details->update_status_idle_id);
		view->details->update_status_idle_id = 0;
	}
}

static gboolean
update_status_idle_callback (gpointer data)
{
	NolphinView *view;

	view = NOLPHIN_VIEW (data);
	nolphin_view_display_selection_info (view);
	view->details->update_status_idle_id = 0;
	return FALSE;
}

static void
schedule_update_status (NolphinView *view)
{
	g_assert (NOLPHIN_IS_VIEW (view));

	/* Make sure we haven't already destroyed it */
	if (view->details->window == NULL) {
		return;
	}

	if (view->details->loading) {
		/* Don't update status bar while loading the dir */
		return;
	}

	if (view->details->update_status_idle_id == 0) {
		view->details->update_status_idle_id =
			g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
                                1000,
                                update_status_idle_callback, view, NULL);
	}
}

/**
 * nolphin_view_notify_selection_changed:
 *
 * Notify this view that the selection has changed. This is normally
 * called only by subclasses.
 * @view: NolphinView whose selection has changed.
 *
 **/
void
nolphin_view_notify_selection_changed (NolphinView *view)
{
	GtkWindow *window;
	GList *selection;

	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	selection = nolphin_view_get_selection (view);
	window = nolphin_view_get_containing_window (view);
	DEBUG_FILES (selection, "Selection changed in window %p", window);
    nolphin_file_list_free (selection);

	view->details->selection_was_removed = FALSE;

	if (view->details->filter_navigation_blocked && !view->details->filter_active) {
		view->details->filter_navigation_blocked = FALSE;
	}

	if (!view->details->selection_change_is_due_to_shell) {
		view->details->send_selection_change_to_shell = TRUE;
	}

	/* Schedule a display of the new selection. */
    if (view->details->display_selection_idle_id != 0) {
        g_source_remove (view->details->display_selection_idle_id);
        view->details->display_selection_idle_id = 0;
        nolphin_window_slot_set_status (view->details->slot, "", "", view->details->loading);
    }
    view->details->display_selection_idle_id = g_timeout_add (100,
                                                              display_selection_info_idle_callback,
                                                              view);

	if (view->details->batching_selection_level != 0) {
		view->details->selection_changed_while_batched = TRUE;
	} else {
		/* Here is the work we do only when we're not
		 * batching selection changes. In other words, it's the slower
		 * stuff that we don't want to slow down selection techniques
		 * such as rubberband-selecting in icon view.
		 */

		/* Schedule an update of menu item states to match selection */
        selection_changed_schedule_update_menus (view);
	}
}

static void
file_changed_callback (NolphinFile *file, gpointer callback_data)
{
	NolphinView *view = NOLPHIN_VIEW (callback_data);

	schedule_changes (view);

	schedule_update_menus (view);
	schedule_update_status (view);
}

/**
 * load_directory:
 *
 * Switch the displayed location to a new uri. If the uri is not valid,
 * the location will not be switched; user feedback will be provided instead.
 * @view: NolphinView whose location will be changed.
 * @uri: A string representing the uri to switch to.
 *
 **/
static void
load_directory (NolphinView *view,
		NolphinDirectory *directory)
{
	NolphinDirectory *old_directory;
	NolphinFile *old_file;
	NolphinFileAttributes attributes;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (NOLPHIN_IS_DIRECTORY (directory));

	nolphin_view_stop_loading (view);

    /* Clear any active filter when navigating to a new directory */
    view->details->filter_navigation_blocked = FALSE;

    if (view->details->filter_active) {
        reset_filter_state (view);
        NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->update_filter_text (view, NULL);
    }

	g_signal_emit (view, signals[CLEAR], 0);

	view->details->loading = TRUE;
	view->details->loading_pending_held = 0;
	view->details->display_method = "progressive";
	view->details->displayed_during_load = FALSE;
	view->details->first_render_elapsed = -1.0;

	/* Update menus when directory is empty, before going to new
	 * location, so they won't have any false lingering knowledge
	 * of old selection.
	 */
	schedule_update_menus (view);

	while (view->details->subdirectory_list != NULL) {
		nolphin_view_remove_subdirectory (view,
						   view->details->subdirectory_list->data);
	}

	disconnect_model_handlers (view);

	old_directory = view->details->model;
	nolphin_directory_ref (directory);
	view->details->model = directory;
	nolphin_directory_unref (old_directory);

	old_file = view->details->directory_as_file;
	view->details->directory_as_file =
		nolphin_directory_get_corresponding_file (directory);
	nolphin_file_unref (old_file);

	view->details->reported_load_error = FALSE;

	/* FIXME bugzilla.gnome.org 45062: In theory, we also need to monitor metadata here (as
         * well as doing a call when ready), in case external forces
         * change the directory's file metadata.
	 */
	attributes =
		NOLPHIN_FILE_ATTRIBUTE_INFO |
		NOLPHIN_FILE_ATTRIBUTE_MOUNT |
		NOLPHIN_FILE_ATTRIBUTE_FILESYSTEM_INFO;
	view->details->metadata_for_directory_as_file_pending = TRUE;
	view->details->metadata_for_files_in_directory_pending = TRUE;
	nolphin_file_call_when_ready
		(view->details->directory_as_file,
		 attributes,
		 metadata_for_directory_as_file_ready_callback, view);
	nolphin_directory_call_when_ready
		(view->details->model,
		 attributes,
		 FALSE,
		 metadata_for_files_in_directory_ready_callback, view);

	/* If capabilities change, then we need to update the menus
	 * because of New Folder, and relative emblems.
	 */
	attributes =
		NOLPHIN_FILE_ATTRIBUTE_INFO |
		NOLPHIN_FILE_ATTRIBUTE_FILESYSTEM_INFO;
	nolphin_file_monitor_add (view->details->directory_as_file,
				   &view->details->directory_as_file,
				   attributes);

	view->details->file_changed_handler_id = g_signal_connect
		(view->details->directory_as_file, "changed",
		 G_CALLBACK (file_changed_callback), view);
}

static void
finish_loading (NolphinView *view)
{
	NolphinFileAttributes attributes;

	nolphin_window_report_load_underway (view->details->window,
					      NOLPHIN_VIEW (view));

    g_timer_start (view->details->load_timer);

	/* Tell interested parties that we've begun loading this directory now.
	 * Subclasses use this to know that the new metadata is now available.
	 */
	g_signal_emit (view, signals[BEGIN_LOADING], 0);

	/* Assume we have now all information to show window */
	nolphin_window_view_visible  (view->details->window, NOLPHIN_VIEW (view));

	if (nolphin_directory_are_all_files_seen (view->details->model)) {
		/* Unschedule a pending update and schedule a new one with the minimal
		 * update interval. This gives the view a short chance at gathering the
		 * (cached) deep counts.
		 */
		unschedule_display_of_pending_files (view);
		schedule_timeout_display_of_pending_files (view, UPDATE_INTERVAL_DEFERRED);
	}

	/* Start loading. */

	/* Connect handlers to learn about loading progress. */
	view->details->done_loading_handler_id = g_signal_connect
		(view->details->model, "done_loading",
		 G_CALLBACK (done_loading_callback), view);
	view->details->load_error_handler_id = g_signal_connect
		(view->details->model, "load_error",
		 G_CALLBACK (load_error_callback), view);

	/* Monitor the things needed to get the right icon. Also
	 * monitor a directory's item count because the "size"
	 * attribute is based on that, and the file's metadata
	 * and possible custom name.
	 */
	attributes =
		NOLPHIN_FILE_ATTRIBUTES_FOR_ICON |
		NOLPHIN_FILE_ATTRIBUTE_DIRECTORY_ITEM_COUNT |
		NOLPHIN_FILE_ATTRIBUTE_INFO |
		NOLPHIN_FILE_ATTRIBUTE_LINK_INFO |
		NOLPHIN_FILE_ATTRIBUTE_MOUNT |
		NOLPHIN_FILE_ATTRIBUTE_EXTENSION_INFO |
        NOLPHIN_FILE_ATTRIBUTE_FAVORITE_CHECK;

	nolphin_directory_file_monitor_add (view->details->model,
					     &view->details->model,
					     view->details->show_hidden_files,
					     attributes,
					     files_added_callback, view);

    	view->details->files_added_handler_id = g_signal_connect
		(view->details->model, "files_added",
		 G_CALLBACK (files_added_callback), view);
	view->details->files_changed_handler_id = g_signal_connect
		(view->details->model, "files_changed",
		 G_CALLBACK (files_changed_callback), view);
}

static void
finish_loading_if_all_metadata_loaded (NolphinView *view)
{
	if (!view->details->metadata_for_directory_as_file_pending &&
	    !view->details->metadata_for_files_in_directory_pending) {
		finish_loading (view);
	}
}

static void
metadata_for_directory_as_file_ready_callback (NolphinFile *file,
			      		       gpointer callback_data)
{
	NolphinView *view;

	view = callback_data;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (view->details->directory_as_file == file);
	g_assert (view->details->metadata_for_directory_as_file_pending);

	view->details->metadata_for_directory_as_file_pending = FALSE;

	finish_loading_if_all_metadata_loaded (view);
}

static void
metadata_for_files_in_directory_ready_callback (NolphinDirectory *directory,
				   		GList *files,
			           		gpointer callback_data)
{
	NolphinView *view;

	view = callback_data;

	g_assert (NOLPHIN_IS_VIEW (view));
	g_assert (view->details->model == directory);
	g_assert (view->details->metadata_for_files_in_directory_pending);

	view->details->metadata_for_files_in_directory_pending = FALSE;

	finish_loading_if_all_metadata_loaded (view);
}

static void
disconnect_handler (GObject *object, guint *id)
{
	if (*id != 0) {
		g_signal_handler_disconnect (object, *id);
		*id = 0;
	}
}

static void
disconnect_directory_handler (NolphinView *view, guint *id)
{
	disconnect_handler (G_OBJECT (view->details->model), id);
}

static void
disconnect_directory_as_file_handler (NolphinView *view, guint *id)
{
	disconnect_handler (G_OBJECT (view->details->directory_as_file), id);
}

static void
disconnect_model_handlers (NolphinView *view)
{
	if (view->details->model == NULL) {
		return;
	}
	disconnect_directory_handler (view, &view->details->files_added_handler_id);
	disconnect_directory_handler (view, &view->details->files_changed_handler_id);
	disconnect_directory_handler (view, &view->details->done_loading_handler_id);
	disconnect_directory_handler (view, &view->details->load_error_handler_id);
	disconnect_directory_as_file_handler (view, &view->details->file_changed_handler_id);
	nolphin_file_cancel_call_when_ready (view->details->directory_as_file,
					      metadata_for_directory_as_file_ready_callback,
					      view);
	nolphin_directory_cancel_callback (view->details->model,
					    metadata_for_files_in_directory_ready_callback,
					    view);
	nolphin_directory_file_monitor_remove (view->details->model,
						&view->details->model);
	nolphin_file_monitor_remove (view->details->directory_as_file,
				      &view->details->directory_as_file);
}

static void
nolphin_view_select_file (NolphinView *view, NolphinFile *file)
{
	GList file_list;

	file_list.data = file;
	file_list.next = NULL;
	file_list.prev = NULL;
	nolphin_view_call_set_selection (view, &file_list);
}

/**
 * nolphin_view_stop_loading:
 *
 * Stop the current ongoing process, such as switching to a new uri.
 * @view: NolphinView in question.
 *
 **/
void
nolphin_view_stop_loading (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

    if (view->details->window) {
        GtkUIManager *ui_manager;

        ui_manager = nolphin_window_get_ui_manager (view->details->window);
        nolphin_ui_unmerge_ui (ui_manager,
                            &view->details->extensions_menu_merge_id,
                            &view->details->extensions_menu_action_group);
    }

	unschedule_display_of_pending_files (view);
	reset_update_interval (view);

	/* Free extra undisplayed files */
	file_and_directory_list_free (view->details->new_added_files);
	view->details->new_added_files = NULL;

	file_and_directory_list_free (view->details->new_changed_files);
	view->details->new_changed_files = NULL;

	g_hash_table_remove_all (view->details->non_ready_files);

	file_and_directory_list_free (view->details->old_added_files);
	view->details->old_added_files = NULL;

	file_and_directory_list_free (view->details->old_changed_files);
	view->details->old_changed_files = NULL;

	g_list_free_full (view->details->pending_selection, g_object_unref);
	view->details->pending_selection = NULL;

	if (view->details->model != NULL) {
		nolphin_directory_file_monitor_remove (view->details->model, view);
	}
	done_loading (view, FALSE);
}

gboolean
nolphin_view_is_editable (NolphinView *view)
{
	NolphinDirectory *directory;

	directory = nolphin_view_get_model (view);

	if (directory != NULL) {
		return nolphin_directory_is_editable (directory);
	}

	return TRUE;
}

static gboolean
real_is_read_only (NolphinView *view)
{
	NolphinFile *file;

    if (showing_recent_directory (view)) {
        return TRUE;
    }

    if (showing_favorites_directory (view)) {
        return TRUE;
    }

	if (!nolphin_view_is_editable (view)) {
		return TRUE;
	}

    file = nolphin_view_get_directory_as_file (view);

    if (file != NULL) {
        if (nolphin_file_is_in_admin (file)) {
            return TRUE;
        }

        return !nolphin_file_can_write (file);
    }

    return FALSE;
}

/**
 * nolphin_view_should_show_file
 *
 * Returns whether or not this file should be displayed based on
 * current filtering options.
 */
gboolean
nolphin_view_should_show_file (NolphinView *view, NolphinFile *file)
{
    if (!nolphin_file_should_show (file,
                                view->details->show_hidden_files,
                                view->details->show_foreign_files))
    {
        return FALSE;
    }

    if (view->details->filter_active &&
        nolphin_view_get_filter_match (view, file) == NOLPHIN_FILTER_NO_MATCH)
    {
        return FALSE;
    }

    return TRUE;
}

gint
nolphin_view_get_filter_match (NolphinView *view, NolphinFile *file)
{
    char *display_name, *stripped;
    score_t score;
    gint result;
    gpointer cached;

    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NOLPHIN_FILTER_NO_MATCH);

    if (!view->details->filter_active || view->details->filter_text_stripped == NULL) {
        return 0;
    }

    if (g_hash_table_lookup_extended (view->details->filter_score_cache,
                                      file, NULL, &cached))
    {
        return GPOINTER_TO_INT (cached);
    }

    display_name = nolphin_file_get_display_name (file);
    if (display_name == NULL) {
        result = NOLPHIN_FILTER_NO_MATCH;
        g_hash_table_insert (view->details->filter_score_cache,
                             file, GINT_TO_POINTER (result));
        return result;
    }

    stripped = nolphin_fzy_strip_combining_marks (display_name, NULL);
    g_free (display_name);

    if (!has_match (view->details->filter_text_stripped, stripped)) {
        g_free (stripped);
        result = NOLPHIN_FILTER_NO_MATCH;
        g_hash_table_insert (view->details->filter_score_cache,
                             file, GINT_TO_POINTER (result));
        return result;
    }

    score = match (view->details->filter_text_stripped, stripped);
    g_free (stripped);

    /* Convert fzy score (higher=better) to our sort rank (lower=better).
     * Scale by 1000 to preserve meaningful precision in the integer. */
    if (score == SCORE_MAX) {
        result = G_MININT;
    } else if (score == SCORE_MIN) {
        result = G_MAXINT - 1;
    } else {
        result = (gint) CLAMP (-score * 1000.0, (double) G_MININT + 1, (double) G_MAXINT - 2);
    }

    g_hash_table_insert (view->details->filter_score_cache,
                         file, GINT_TO_POINTER (result));

    return result;
}

gboolean
nolphin_view_get_filter_active (NolphinView *view)
{
    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);
    return view->details->filter_active;
}

const char *
nolphin_view_get_filter_text (NolphinView *view)
{
    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);
    return view->details->filter_text;
}

static void
reset_filter_state (NolphinView *view)
{
    if (view->details->filter_debounce_id != 0) {
        g_source_remove (view->details->filter_debounce_id);
        view->details->filter_debounce_id = 0;
    }

    g_clear_pointer (&view->details->filter_text, g_free);
    g_clear_pointer (&view->details->filter_text_stripped, g_free);
    g_hash_table_remove_all (view->details->filter_score_cache);
    view->details->filter_active = FALSE;
}

static gboolean
apply_filter_debounce_cb (gpointer data)
{
    NolphinView *view = NOLPHIN_VIEW (data);

    view->details->filter_debounce_id = 0;

    NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->update_filter_text (view, view->details->filter_text);
    nolphin_view_apply_filter (view);

    if (view->details->filter_active) {
        NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->select_first (view);
    }

    /* Re-emit so the slot can update the "no matching files" indicator
     * now that apply_filter has updated the view contents. */
    g_signal_emit (view, signals[ACTIVATE_FILTER], 0, view->details->filter_text);

    return G_SOURCE_REMOVE;
}

void
nolphin_view_set_filter_text (NolphinView *view, const char *text)
{
    g_return_if_fail (NOLPHIN_IS_VIEW (view));

    reset_filter_state (view);

    if (text != NULL && text[0] != '\0') {
        view->details->filter_text = g_strdup (text);
        view->details->filter_text_stripped = nolphin_fzy_strip_combining_marks (text, NULL);
        view->details->filter_navigation_blocked = TRUE;
        view->details->filter_active = TRUE;
    }

    g_signal_emit (view, signals[ACTIVATE_FILTER], 0, view->details->filter_text);

    view->details->filter_debounce_id = g_timeout_add (100, apply_filter_debounce_cb, view);
}

void
nolphin_view_clear_filter (NolphinView *view)
{
    g_return_if_fail (NOLPHIN_IS_VIEW (view));

    if (!view->details->filter_active) {
        return;
    }

    reset_filter_state (view);

    g_signal_emit (view, signals[ACTIVATE_FILTER], 0, NULL);

    nolphin_view_apply_filter (view);

    NOLPHIN_VIEW_CLASS (G_OBJECT_GET_CLASS (view))->update_filter_text (view, NULL);
}

gboolean
nolphin_view_activate_filter (NolphinView *view, GdkEventKey *event)
{
    guint32 unicode_ch;

    g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

    if (event == NULL) {
        return FALSE;
    }

    if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0) {
        return FALSE;
    }

    if (view->details->model != NULL &&
        NOLPHIN_IS_SEARCH_DIRECTORY (view->details->model)) {
        return FALSE;
    }

    /* Escape clears an active filter */
    if (event->keyval == GDK_KEY_Escape && view->details->filter_active) {
        nolphin_view_clear_filter (view);
        return TRUE;
    }

    /* Backspace removes the last character from the filter, or is
     * consumed (no-op) while navigation is blocked after filtering. */
    if (event->keyval == GDK_KEY_BackSpace &&
        (view->details->filter_active || view->details->filter_navigation_blocked)) {

        if (!view->details->filter_active) {
            return TRUE;
        }
        const char *text = view->details->filter_text;
        glong len = g_utf8_strlen (text, -1);

        if (len <= 1) {
            nolphin_view_clear_filter (view);
        } else {
            gchar *new_text = g_strndup (text, g_utf8_offset_to_pointer (text, len - 1) - text);
            nolphin_view_set_filter_text (view, new_text);
            g_free (new_text);
        }

        return TRUE;
    }

    /* Printable characters append to the filter.
     * Space only appends if filter is already active. */
    unicode_ch = gdk_keyval_to_unicode (event->keyval);

    if (unicode_ch != 0 && g_unichar_isprint (unicode_ch) &&
        (unicode_ch != ' ' || view->details->filter_active)) {
        gchar buf[6];
        gint char_len;
        gchar *new_text;

        char_len = g_unichar_to_utf8 (unicode_ch, buf);
        buf[char_len] = '\0';

        if (view->details->filter_text != NULL) {
            new_text = g_strconcat (view->details->filter_text, buf, NULL);
        } else {
            new_text = g_strdup (buf);
        }

        nolphin_view_set_filter_text (view, new_text);
        g_free (new_text);
        return TRUE;
    }

    return FALSE;
}

static void
nolphin_view_apply_filter (NolphinView *view)
{
    NolphinDirectory *directory;
    GList *all_files, *l;

    directory = view->details->model;

    if (directory == NULL) {
        return;
    }

    all_files = nolphin_directory_get_file_list (directory);

    g_signal_emit (view, signals[BEGIN_FILE_CHANGES], 0);

    for (l = all_files; l != NULL; l = l->next) {
        NolphinFile *file = l->data;

        if (nolphin_view_should_show_file (view, file)) {
            g_signal_emit (view, signals[ADD_FILE], 0, file, directory);
        } else {
            g_signal_emit (view, signals[REMOVE_FILE], 0, file, directory);
        }
    }

    g_signal_emit (view, signals[END_FILE_CHANGES], 0);

    nolphin_file_list_free (all_files);
}

static gboolean
real_using_manual_layout (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), FALSE);

	return FALSE;
}

static void
real_update_filter_text (NolphinView *view, const char *filter_text)
{
}

static void
real_select_first (NolphinView *view)
{
}

static void
schedule_update_menus_callback (gpointer callback_data)
{
	schedule_update_menus (NOLPHIN_VIEW (callback_data));
}

void
nolphin_view_ignore_hidden_file_preferences (NolphinView *view)
{
	g_return_if_fail (view->details->model == NULL);

	if (view->details->ignore_hidden_file_preferences) {
		return;
	}

	view->details->show_hidden_files = FALSE;
	view->details->ignore_hidden_file_preferences = TRUE;
}

void
nolphin_view_set_show_foreign (NolphinView *view,
				gboolean show_foreign)
{
	view->details->show_foreign_files = show_foreign;
}

char *
nolphin_view_get_uri (NolphinView *view)
{
	g_return_val_if_fail (NOLPHIN_IS_VIEW (view), NULL);
	if (view->details->model == NULL) {
		return NULL;
	}
	return nolphin_directory_get_uri (view->details->model);
}

void
nolphin_view_move_copy_items (NolphinView *view,
			       const GList *item_uris,
			       GArray *relative_item_points,
			       const char *target_uri,
			       int copy_action,
			       int x, int y)
{
	NolphinFile *target_file;

	g_assert (relative_item_points == NULL
		  || relative_item_points->len == 0
		  || g_list_length ((GList *)item_uris) == relative_item_points->len);

	/* add the drop location to the icon offsets */
	offset_drop_points (relative_item_points, x, y);

	target_file = nolphin_file_get_existing_by_uri (target_uri);
	/* special-case "command:" here instead of starting a move/copy */
	if (target_file != NULL && nolphin_file_is_launcher (target_file)) {
		nolphin_file_unref (target_file);
		nolphin_launch_desktop_file (
					      gtk_widget_get_screen (GTK_WIDGET (view)),
					      target_uri, item_uris,
					      nolphin_view_get_containing_window (view));
		return;
	} else if (copy_action == GDK_ACTION_COPY &&
		   nolphin_is_file_roller_installed () &&
		   target_file != NULL &&
		   nolphin_file_is_archive (target_file)) {
		char *command, *quoted_uri, *unescaped, *tmp;
		const GList *l;
		GdkScreen  *screen;

		/* Handle dropping onto a file-roller archiver file, instead of starting a move/copy */

		nolphin_file_unref (target_file);

        unescaped = g_uri_unescape_string (target_uri, "");
		quoted_uri = g_shell_quote (unescaped);

		command = g_strconcat ("file-roller -a ", quoted_uri, NULL);

        g_clear_pointer (&quoted_uri, g_free);
        g_clear_pointer (&unescaped, g_free);

		for (l = item_uris; l != NULL; l = l->next) {
            unescaped = g_uri_unescape_string ((char *) l->data, "");
            quoted_uri = g_shell_quote (unescaped);

			tmp = g_strconcat (command, " ", quoted_uri, NULL);
			g_free (command);
			command = tmp;

            g_clear_pointer (&quoted_uri, g_free);
            g_clear_pointer (&unescaped, g_free);
		}

		screen = gtk_widget_get_screen (GTK_WIDGET (view));
		if (screen == NULL) {
			screen = gdk_screen_get_default ();
		}

		nolphin_launch_application_from_command (screen, command, FALSE, NULL);
		g_free (command);

		return;
	}
	nolphin_file_unref (target_file);

	nolphin_file_operations_copy_move
		(item_uris, relative_item_points,
		 target_uri, copy_action, GTK_WIDGET (view),
		 copy_move_done_callback, pre_copy_move (view));
}

static void
nolphin_view_trash_state_changed_callback (NolphinTrashMonitor *trash_monitor,
					    gboolean state, gpointer callback_data)
{
	NolphinView *view;

	view = (NolphinView *) callback_data;
	g_assert (NOLPHIN_IS_VIEW (view));

	schedule_update_menus (view);
}

void
nolphin_view_start_batching_selection_changes (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));

	++view->details->batching_selection_level;
	view->details->selection_changed_while_batched = FALSE;
}

void
nolphin_view_stop_batching_selection_changes (NolphinView *view)
{
	g_return_if_fail (NOLPHIN_IS_VIEW (view));
	g_return_if_fail (view->details->batching_selection_level > 0);

	if (--view->details->batching_selection_level == 0) {
		if (view->details->selection_changed_while_batched) {
			nolphin_view_notify_selection_changed (view);
		}
	}
}

gboolean
nolphin_view_get_active (NolphinView *view)
{
	g_assert (NOLPHIN_IS_VIEW (view));
	return view->details->active;
}

static GArray *
real_get_selected_icon_locations (NolphinView *view)
{
        /* By default, just return an empty list. */
        return g_array_new (FALSE, TRUE, sizeof (GdkPoint));
}

static void
nolphin_view_set_property (GObject         *object,
			    guint            prop_id,
			    const GValue    *value,
			    GParamSpec      *pspec)
{
	NolphinView *directory_view;
	NolphinWindowSlot *slot;
	NolphinWindow *window;

	directory_view = NOLPHIN_VIEW (object);

	switch (prop_id)  {
	case PROP_WINDOW_SLOT:
		g_assert (directory_view->details->slot == NULL);

		slot = NOLPHIN_WINDOW_SLOT (g_value_get_object (value));
		window = nolphin_window_slot_get_window (slot);

		directory_view->details->slot = slot;
		directory_view->details->window = window;

		g_signal_connect_object (directory_view->details->slot,
					 "active", G_CALLBACK (slot_active),
					 directory_view, 0);
		g_signal_connect_object (directory_view->details->slot,
					 "inactive", G_CALLBACK (slot_inactive),
					 directory_view, 0);
		g_signal_connect_object (directory_view->details->slot,
					 "changed-pane", G_CALLBACK (slot_changed_pane),
					 directory_view, 0);

		g_signal_connect_object (directory_view->details->window,
					 "hidden-files-mode-changed", G_CALLBACK (hidden_files_mode_changed),
					 directory_view, 0);
		nolphin_view_init_show_hidden_files (directory_view);
		break;
	case PROP_SUPPORTS_ZOOMING:
		directory_view->details->supports_zooming = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


gboolean
nolphin_view_handle_scroll_event (NolphinView *directory_view,
				   GdkEventScroll *event)
{
	static gdouble total_delta_y = 0;
	gdouble delta_x, delta_y;

	if (event->state & GDK_CONTROL_MASK) {
		switch (event->direction) {
		case GDK_SCROLL_UP:
			/* Zoom In */
			nolphin_view_bump_zoom_level (directory_view, 1);
			return TRUE;

		case GDK_SCROLL_DOWN:
			/* Zoom Out */
			nolphin_view_bump_zoom_level (directory_view, -1);
			return TRUE;

		case GDK_SCROLL_SMOOTH:
			gdk_event_get_scroll_deltas ((const GdkEvent *) event,
						     &delta_x, &delta_y);

			/* try to emulate a normal scrolling event by summing deltas */
			total_delta_y += delta_y;

			if (total_delta_y >= 1) {
				total_delta_y = 0;
				/* emulate scroll down */
				nolphin_view_bump_zoom_level (directory_view, -1);
				return TRUE;
			} else if (total_delta_y <= - 1) {
				total_delta_y = 0;
				/* emulate scroll up */
				nolphin_view_bump_zoom_level (directory_view, 1);
				return TRUE;
			} else {
				/* eat event */
				return TRUE;
			}

		case GDK_SCROLL_LEFT:
		case GDK_SCROLL_RIGHT:
			break;

		default:
			g_assert_not_reached ();
		}
	}

	return FALSE;
}

/* handle Shift+Scroll, which will cause a zoom-in/out */
static gboolean
nolphin_view_scroll_event (GtkWidget *widget,
			    GdkEventScroll *event)
{
	NolphinView *directory_view;

	directory_view = NOLPHIN_VIEW (widget);
    if (!get_is_desktop_view (directory_view) &&
        nolphin_view_handle_scroll_event (directory_view, event)) {
		return TRUE;
	}

	return GTK_WIDGET_CLASS (parent_class)->scroll_event (widget, event);
}


static void
nolphin_view_parent_set (GtkWidget *widget,
			  GtkWidget *old_parent)
{
	NolphinView *view;
	GtkWidget *parent;

	view = NOLPHIN_VIEW (widget);

	parent = gtk_widget_get_parent (widget);
	g_assert (parent == NULL || old_parent == NULL);

	if (GTK_WIDGET_CLASS (parent_class)->parent_set != NULL) {
		GTK_WIDGET_CLASS (parent_class)->parent_set (widget, old_parent);
	}

	if (parent != NULL) {
		g_assert (old_parent == NULL);

		if (view->details->slot ==
		    nolphin_window_get_active_slot (view->details->window)) {
			view->details->active = TRUE;

			nolphin_view_merge_menus (view);
			schedule_update_menus (view);
		}
	} else {
		nolphin_view_unmerge_menus (view);
		remove_update_menus_timeout_callback (view);
	}
}

static void
nolphin_view_class_init (NolphinViewClass *klass)
{
	GObjectClass *oclass;
	GtkWidgetClass *widget_class;
	GtkScrolledWindowClass *scrolled_window_class;
	GtkBindingSet *binding_set;

	widget_class = GTK_WIDGET_CLASS (klass);
	scrolled_window_class = GTK_SCROLLED_WINDOW_CLASS (klass);
	oclass = G_OBJECT_CLASS (klass);

	oclass->finalize = nolphin_view_finalize;
	oclass->set_property = nolphin_view_set_property;

	widget_class->destroy = nolphin_view_destroy;
	widget_class->scroll_event = nolphin_view_scroll_event;
	widget_class->parent_set = nolphin_view_parent_set;

	g_type_class_add_private (klass, sizeof (NolphinViewDetails));

	/* Get rid of the strange 3-pixel gap that GtkScrolledWindow
	 * uses by default. It does us no good.
	 */
	scrolled_window_class->scrollbar_spacing = 0;

	signals[ADD_FILE] =
		g_signal_new ("add_file",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, add_file),
		              NULL, NULL,
		              g_cclosure_marshal_generic,
		              G_TYPE_NONE, 2, NOLPHIN_TYPE_FILE, NOLPHIN_TYPE_DIRECTORY);
	signals[BEGIN_FILE_CHANGES] =
		g_signal_new ("begin_file_changes",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, begin_file_changes),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
	signals[BEGIN_LOADING] =
		g_signal_new ("begin_loading",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, begin_loading),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
	signals[CLEAR] =
		g_signal_new ("clear",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, clear),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
	signals[END_FILE_CHANGES] =
		g_signal_new ("end_file_changes",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, end_file_changes),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
	signals[END_LOADING] =
		g_signal_new ("end_loading",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, end_loading),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOOLEAN,
		              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[FILE_CHANGED] =
		g_signal_new ("file_changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, file_changed),
		              NULL, NULL,
		              g_cclosure_marshal_generic,
		              G_TYPE_NONE, 2, NOLPHIN_TYPE_FILE, NOLPHIN_TYPE_DIRECTORY);
	signals[LOAD_ERROR] =
		g_signal_new ("load_error",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, load_error),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[REMOVE_FILE] =
		g_signal_new ("remove_file",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NolphinViewClass, remove_file),
		              NULL, NULL,
		              g_cclosure_marshal_generic,
		              G_TYPE_NONE, 2, NOLPHIN_TYPE_FILE, NOLPHIN_TYPE_DIRECTORY);
	signals[ZOOM_LEVEL_CHANGED] =
		g_signal_new ("zoom-level-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[TRASH] =
		g_signal_new ("trash",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NolphinViewClass, trash),
			      g_signal_accumulator_true_handled, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_BOOLEAN, 0);
	signals[DELETE] =
		g_signal_new ("delete",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (NolphinViewClass, delete),
			      g_signal_accumulator_true_handled, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_BOOLEAN, 0);
    signals[ACTIVATE_FILTER] =
        g_signal_new ("activate-filter",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);

	klass->get_selected_icon_locations = real_get_selected_icon_locations;
	klass->is_read_only = real_is_read_only;
	klass->load_error = real_load_error;
	klass->can_rename_file = can_rename_file;
	klass->start_renaming_file = start_renaming_file;
	klass->get_backing_uri = real_get_backing_uri;
	klass->using_manual_layout = real_using_manual_layout;
        klass->merge_menus = real_merge_menus;
        klass->unmerge_menus = real_unmerge_menus;
        klass->update_menus = real_update_menus;
	klass->update_filter_text = real_update_filter_text;
	klass->select_first = real_select_first;
	klass->trash = real_trash;
	klass->delete = real_delete;

	copied_files_atom = gdk_atom_intern ("x-special/gnome-copied-files", FALSE);

	properties[PROP_WINDOW_SLOT] =
		g_param_spec_object ("window-slot",
				     "Window Slot",
				     "The parent window slot reference",
				     NOLPHIN_TYPE_WINDOW_SLOT,
				     G_PARAM_WRITABLE |
				     G_PARAM_CONSTRUCT_ONLY);
	properties[PROP_SUPPORTS_ZOOMING] =
		g_param_spec_boolean ("supports-zooming",
				      "Supports zooming",
				      "Whether the view supports zooming",
				      TRUE,
				      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);

	binding_set = gtk_binding_set_by_class (klass);

    gboolean swap_keys = g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_SWAP_TRASH_DELETE);

    if (swap_keys) {
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, 0,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, 0,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, GDK_SHIFT_MASK,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, GDK_SHIFT_MASK,
                          "trash", 0);
    } else {
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, 0,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, 0,
                          "trash", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Delete, GDK_SHIFT_MASK,
                          "delete", 0);
        gtk_binding_entry_add_signal (binding_set, GDK_KEY_Delete, GDK_SHIFT_MASK,
                          "delete", 0);
    }
}
