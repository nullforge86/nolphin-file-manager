/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Nolphin
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 *  Authors : Mr Jamie McCracken (jamiemcc at blueyonder dot co dot uk)
 *            Cosimo Cecchi <cosimoc@gnome.org>
 *
 */

#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <math.h>
#include <cairo-gobject.h>
#include <libnolphin-private/nolphin-xapp-compat.h>

#include <libnolphin-private/nolphin-dnd.h>
#include <libnolphin-private/nolphin-bookmark.h>
#include <libnolphin-private/nolphin-global-preferences.h>
#include <libnolphin-private/nolphin-module.h>
#include <libnolphin-private/nolphin-file.h>
#include <libnolphin-private/nolphin-file-utilities.h>
#include <libnolphin-private/nolphin-file-operations.h>
#include <libnolphin-private/nolphin-mount-operation.h>
#include <libnolphin-private/nolphin-trash-monitor.h>
#include <libnolphin-private/nolphin-icon-names.h>
#include <libnolphin-private/nolphin-cell-renderer-disk.h>
#include <libnolphin-private/nolphin-places-tree-view.h>
#include <libnolphin-private/nolphin-action-manager.h>
#include <libnolphin-private/nolphin-action.h>
#include <libnolphin-private/nolphin-ui-utilities.h>

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-vfs-extensions.h>

#include "nolphin-actions.h"
#include "nolphin-application.h"
#include "nolphin-bookmark-list.h"
#include "nolphin-places-sidebar.h"
#include "nolphin-window.h"
#include "nolphin-workspace-panel.h"
#include "nolphin-phone.h"
#include "nolphin-window-slot.h"
#include "nolphin-workspace-panel.h"

#define DEBUG_FLAG NOLPHIN_DEBUG_PLACES
#include <libnolphin-private/nolphin-debug.h>

#define EXPANDER_PAD_COLUMN_WIDTH 4
#define EJECT_COLUMN_MIN_WIDTH 22
#define EJECT_COLUMN_MAX_WIDTH 60
#define DRAG_EXPAND_CATEGORY_DELAY 500
#define EJECT_PAD_COLUMN_WIDTH 14

static gint EJECT_ICON_SIZE_NOT_HOVERED = 0;
static gint EJECT_ICON_SIZE_HOVERED = GTK_ICON_SIZE_MENU;
static gint menu_icon_pixels = 16;
#define EJECT_ICON_SIZE_REDUCTION 4

#if (!GLIB_CHECK_VERSION(2,50,0))
#define g_drive_is_removable g_drive_is_media_removable
#endif

typedef struct {
	GtkScrolledWindow  parent;
	GtkTreeView        *tree_view;
    GtkTreeViewColumn  *eject_column;
    GtkCellRenderer    *eject_icon_cell_renderer;
    GtkCellRenderer    *editable_renderer;
	char 	           *uri;
	GtkTreeStore       *store;
    GtkTreeModel       *store_filter;

	NolphinWindow *window;
	NolphinBookmarkList *bookmarks;
	GVolumeMonitor *volume_monitor;

    NolphinActionManager *action_manager;
    gulong actions_changed_id;
    guint actions_changed_idle_id;

    GtkUIManager *ui_manager;

    GtkActionGroup *bookmark_action_group;
    guint bookmark_action_group_merge_id;
    GtkActionGroup *action_action_group;
    guint action_action_group_merge_id;

    gboolean actions_need_update;

	gboolean devices_header_added;
	gboolean bookmarks_header_added;

	/* DnD */
	GList     *drag_list;
	gboolean  drag_data_received;
	int       drag_data_info;
	gboolean  drop_occured;
    gboolean  in_drag;
    gchar     *desktop_dnd_source_fs;
    gboolean  desktop_dnd_can_delete_source;

	GtkWidget *popup_menu;

	/* volume mounting - delayed open process */
	gboolean mounting;
	NolphinWindowSlot *go_to_after_mount_slot;
	NolphinWindowOpenFlags go_to_after_mount_flags;

	guint bookmarks_changed_id;

    gboolean my_computer_expanded;
    gboolean bookmarks_expanded;
    gboolean devices_expanded;
    gboolean network_expanded;

    gboolean updating_sidebar;

    /* Store the positions of the last
     * entry prior to bookmarks, and
     * the first entry after bookmarks
     * to allow drag and drop creation
     * of new bookmarks */
    gchar *top_bookend_uri;
    gchar *bottom_bookend_uri;

    gint bookmark_breakpoint;
    guint expand_timeout_source;
    guint popup_menu_action_index;
    guint update_places_on_idle_id;
    guint phone_status_id;          /* prüft regelmäßig, ob die Handy-App angemeldet ist */
    gboolean phone_connected;       /* so wurde der Eintrag „Handy“ zuletzt gezeichnet */
    gchar *phone_uri;               /* URI des Eintrags „Handy“ (~/Handy) */
    gchar *phone_inbox_uri;         /* URI des Eintrags „Vom Handy“ (~/Handy/Vom Handy) */

} NolphinPlacesSidebar;

typedef struct {
	GtkScrolledWindowClass parent;
} NolphinPlacesSidebarClass;

typedef struct {
        GObject parent;
} NolphinPlacesSidebarProvider;

typedef struct {
        GObjectClass parent;
} NolphinPlacesSidebarProviderClass;

enum {
	PLACES_SIDEBAR_COLUMN_ROW_TYPE,
	PLACES_SIDEBAR_COLUMN_URI,
	PLACES_SIDEBAR_COLUMN_DRIVE,
	PLACES_SIDEBAR_COLUMN_VOLUME,
	PLACES_SIDEBAR_COLUMN_MOUNT,
	PLACES_SIDEBAR_COLUMN_NAME,
	PLACES_SIDEBAR_COLUMN_GICON,
	PLACES_SIDEBAR_COLUMN_INDEX,
	PLACES_SIDEBAR_COLUMN_EJECT,
	PLACES_SIDEBAR_COLUMN_NO_EJECT,
	PLACES_SIDEBAR_COLUMN_BOOKMARK,
    PLACES_SIDEBAR_COLUMN_TOOLTIP,
    PLACES_SIDEBAR_COLUMN_EJECT_ICON,
	PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE,
	PLACES_SIDEBAR_COLUMN_SECTION_TYPE,
	PLACES_SIDEBAR_COLUMN_HEADING_TEXT,
    PLACES_SIDEBAR_COLUMN_DF_PERCENT,
    PLACES_SIDEBAR_COLUMN_SHOW_DF,

	PLACES_SIDEBAR_COLUMN_COUNT
};

typedef enum {
	PLACES_BUILT_IN,
	PLACES_XDG_DIR,
	PLACES_MOUNTED_VOLUME,
	PLACES_BOOKMARK,
	PLACES_HEADING,
} PlaceType;

typedef enum {
    SECTION_COMPUTER,
    SECTION_XDG_BOOKMARKS,
    SECTION_BOOKMARKS,
	SECTION_DEVICES,
	SECTION_NETWORK,
} SectionType;

enum {
    POSITION_UPPER,
    POSITION_MIDDLE,
    POSITION_LOWER
};

/* URI des Ausgangsordners "~/Handy" oder NULL. Legt keine Ordner an: Das
 * geschieht erst, wenn der Eintrag geöffnet oder etwas darauf abgelegt wird. */
static gchar *
phone_outbox_uri (void)
{
    gchar *path, *uri;

    path = nolphin_phone_outbox_path ();
    uri = g_filename_to_uri (path, NULL, NULL);
    g_free (path);
    return uri;
}

static gboolean
is_phone_uri (const gchar *uri)
{
    gchar *phone = uri != NULL ? phone_outbox_uri () : NULL;
    gboolean same = phone != NULL && strcmp (uri, phone) == 0;

    g_free (phone);
    return same;
}


static void  open_selected_bookmark                    (NolphinPlacesSidebar        *sidebar,
							GtkTreeModel                 *model,
							GtkTreeIter                  *iter,
							NolphinWindowOpenFlags flags);
static void  nolphin_places_sidebar_style_set         (GtkWidget                    *widget,
							GtkStyle                     *previous_style);
static gboolean eject_or_unmount_bookmark              (NolphinPlacesSidebar *sidebar,
							GtkTreePath *path);
static gboolean eject_or_unmount_selection             (NolphinPlacesSidebar *sidebar);
static void  check_unmount_and_eject                   (GMount *mount,
							GVolume *volume,
							GDrive *drive,
							gboolean *show_unmount,
							gboolean *show_eject);

static void update_places                              (NolphinPlacesSidebar *sidebar);
static void update_places_on_idle                      (NolphinPlacesSidebar *sidebar);
static void rebuild_menu                               (NolphinPlacesSidebar *sidebar);
static void actions_changed                            (gpointer user_data);
/* Identifiers for target types */
enum {
  GTK_TREE_MODEL_ROW,
  TEXT_URI_LIST
};

/* Target types for dragging from the shortcuts list */
static const GtkTargetEntry nolphin_shortcuts_source_targets[] = {
	{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW }
};

/* Target types for dropping into the shortcuts list */
static const GtkTargetEntry nolphin_shortcuts_drop_targets [] = {
	{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW },
	{ (char *)"text/uri-list", 0, TEXT_URI_LIST }
};

/* Drag and drop interface declarations */
typedef struct {
  GtkTreeStore parent;

  NolphinPlacesSidebar *sidebar;
} NolphinShortcutsModel;

typedef struct {
  GtkTreeStoreClass parent_class;
} NolphinShortcutsModelClass;

#define NOLPHIN_TYPE_SHORTCUTS_MODEL (_nolphin_shortcuts_model_get_type ())
#define NOLPHIN_SHORTCUTS_MODEL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NOLPHIN_SHORTCUTS_MODEL_TYPE, NolphinShortcutsModel))

GType _nolphin_shortcuts_model_get_type (void);
static void _nolphin_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface);
G_DEFINE_TYPE_WITH_CODE (NolphinShortcutsModel, _nolphin_shortcuts_model, GTK_TYPE_TREE_STORE,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE,
						_nolphin_shortcuts_model_drag_source_init));
static GtkTreeStore *nolphin_shortcuts_model_new (NolphinPlacesSidebar *sidebar);

G_DEFINE_TYPE (NolphinPlacesSidebar, nolphin_places_sidebar, GTK_TYPE_SCROLLED_WINDOW);

static void
breakpoint_changed_cb (NolphinPlacesSidebar *sidebar)
{
    sidebar->bookmark_breakpoint = g_settings_get_int (nolphin_window_state, NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT);
    update_places (sidebar);
}

static void
increment_bookmark_breakpoint (NolphinPlacesSidebar *sidebar)
{
    g_signal_handlers_block_by_func (nolphin_window_state, breakpoint_changed_cb, sidebar);

    sidebar->bookmark_breakpoint ++;
    g_settings_set_int (nolphin_window_state, NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, sidebar->bookmark_breakpoint);

    g_signal_handlers_unblock_by_func (nolphin_window_state, breakpoint_changed_cb, sidebar);
}

static void
decrement_bookmark_breakpoint (NolphinPlacesSidebar *sidebar)
{
    g_signal_handlers_block_by_func (nolphin_window_state, breakpoint_changed_cb, sidebar);

    sidebar->bookmark_breakpoint --;
    g_settings_set_int (nolphin_window_state, NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, sidebar->bookmark_breakpoint);

    g_signal_handlers_unblock_by_func (nolphin_window_state, breakpoint_changed_cb, sidebar);
}

static gboolean
should_show_desktop (void)
{
	return g_settings_get_boolean (nolphin_desktop_preferences, NOLPHIN_PREFERENCES_SHOW_DESKTOP) &&
	       !g_settings_get_boolean (nolphin_preferences, NOLPHIN_PREFERENCES_DESKTOP_IS_HOME_DIR);
}

static gboolean
is_built_in_bookmark (NolphinFile *file)
{
	if (nolphin_file_is_home (file)) {
		return TRUE;
	}

    if (nolphin_file_is_desktop_directory (file) && should_show_desktop ())
        return TRUE;
    else
	    return FALSE;
}

static GtkTreeIter
add_heading (NolphinPlacesSidebar *sidebar,
	     SectionType section_type,
	     const gchar *title)
{
	GtkTreeIter cat_iter;

	gtk_tree_store_append (sidebar->store, &cat_iter, NULL);
	gtk_tree_store_set (sidebar->store, &cat_iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_HEADING,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
			    PLACES_SIDEBAR_COLUMN_HEADING_TEXT, title,
                PLACES_SIDEBAR_COLUMN_INDEX, -1,
			    PLACES_SIDEBAR_COLUMN_EJECT, FALSE,
			    PLACES_SIDEBAR_COLUMN_NO_EJECT, TRUE,
			    -1);

	return cat_iter;
}

static GtkTreeIter
check_heading_for_devices (NolphinPlacesSidebar *sidebar,
                           SectionType section_type,
			               GtkTreeIter cat_iter)
{
    if (section_type == SECTION_DEVICES) {
        if (!sidebar->devices_header_added) {
            cat_iter = add_heading (sidebar, SECTION_DEVICES,
                        _("Geräte"));
            sidebar->devices_header_added = TRUE;
        }
    }
    return cat_iter;
}

typedef struct {
       PlaceType place_type;
       SectionType section_type;
       gchar *name;
       gchar *icon_name;
       gchar *uri;
       GDrive *drive;
       GVolume *volume;
       GMount *mount;
       gint index;
       gchar *tooltip;
       gint df_percent;
       gboolean show_df_percent;
} PlaceInfo;

static gint
sort_places_func (gconstpointer a,
                  gconstpointer b)
{
    return g_utf8_collate (((PlaceInfo *) a)->name, ((PlaceInfo *) b)->name);
}

static gint
sort_mounts_func (gconstpointer a,
                  gconstpointer b)
{
    g_autofree gchar *name_a = g_mount_get_name (G_MOUNT (a));
    g_autofree gchar *name_b = g_mount_get_name (G_MOUNT (b));

    return g_utf8_collate (name_a, name_b);
}

static PlaceInfo *
new_place_info (PlaceType place_type,
                SectionType section_type,
                gchar *name,
                gchar *icon_name,
                gchar *uri,
                GDrive *drive,
                GVolume *volume,
                GMount *mount,
                gint index,
                gchar *tooltip,
                gint df_percent,
                gboolean show_df_percent)
{
    PlaceInfo *info = g_new0 (PlaceInfo, 1);

    info->place_type = place_type;
    info->section_type = section_type;
    info->name = g_utf8_make_valid (name, -1);
    info->icon_name = g_strdup (icon_name);
    info->uri = (g_strdup (uri));
    info->drive = drive ? g_object_ref (drive) : NULL;
    info->volume = volume ? g_object_ref (volume) : NULL;
    info->mount = mount ? g_object_ref (mount) : NULL;
    info->index = index;
    info->tooltip = g_strdup (tooltip);
    info->df_percent = df_percent;
    info->show_df_percent = show_df_percent;

    return info;
}

static void
free_place_info (PlaceInfo *info)
{
    g_free (info->name);
    g_free (info->icon_name);
    g_free (info->uri);
    g_clear_object (&info->drive);
    g_clear_object (&info->volume);
    g_clear_object (&info->mount);
    g_free (info->tooltip);

    g_free (info);
}

static GtkTreeIter
add_place (NolphinPlacesSidebar *sidebar,
	   PlaceType place_type,
	   SectionType section_type,
	   const char *name,
	   const char *icon_name,
	   const char *uri,
	   GDrive *drive,
	   GVolume *volume,
	   GMount *mount,
	   int index,
	   const char *tooltip,
       int df_percent,
       gboolean show_df_percent,
       GtkTreeIter cat_iter)
{
	GtkTreeIter           iter;
    GIcon *gicon;
	gboolean show_eject, show_unmount;
	gboolean show_eject_button;

	cat_iter = check_heading_for_devices (sidebar, section_type, cat_iter);

	check_unmount_and_eject (mount, volume, drive,
				 &show_unmount, &show_eject);

	if (show_unmount || show_eject) {
		g_assert (place_type != PLACES_BOOKMARK);
	}

	if (mount == NULL) {
		show_eject_button = FALSE;
	} else {
		show_eject_button = (show_unmount || show_eject);
	}

    gicon = (icon_name != NULL) ? g_themed_icon_new (icon_name) : NULL;

	gtk_tree_store_append (sidebar->store, &iter, &cat_iter);
	gtk_tree_store_set (sidebar->store, &iter,
			    PLACES_SIDEBAR_COLUMN_GICON, gicon,
			    PLACES_SIDEBAR_COLUMN_NAME, name,
			    PLACES_SIDEBAR_COLUMN_URI, uri,
			    PLACES_SIDEBAR_COLUMN_DRIVE, drive,
			    PLACES_SIDEBAR_COLUMN_VOLUME, volume,
			    PLACES_SIDEBAR_COLUMN_MOUNT, mount,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
			    PLACES_SIDEBAR_COLUMN_INDEX, index,
			    PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
			    PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
			    PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
                PLACES_SIDEBAR_COLUMN_EJECT_ICON, show_eject_button ? "xsi-media-eject-symbolic" : NULL,
			    PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, EJECT_ICON_SIZE_NOT_HOVERED,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                PLACES_SIDEBAR_COLUMN_DF_PERCENT, df_percent,
                PLACES_SIDEBAR_COLUMN_SHOW_DF, show_df_percent,
			    -1);

    g_clear_object (&gicon);

    return cat_iter;
}

typedef struct {
	const gchar *location;
	const gchar *last_uri;
	NolphinPlacesSidebar *sidebar;
	GtkTreePath *path;
} RestoreLocationData;

static gboolean
restore_selection_foreach (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   gpointer user_data)
{
	RestoreLocationData *data = user_data;
	gchar *uri;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_URI, &uri,
			    -1);
	if (g_strcmp0 (uri, data->last_uri) == 0 ||
	    g_strcmp0 (uri, data->location) == 0) {
		data->path = gtk_tree_path_copy (path);
	}

	g_free (uri);

	return (data->path != NULL);
}

static gboolean
restore_expand_state_foreach (GtkTreeModel *model,
                              GtkTreePath *path,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    PlaceType place_type;
    SectionType section_type;
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    gtk_tree_model_get (model, iter,
                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                -1);

    if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_COMPUTER) {
            if (sidebar->my_computer_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_BOOKMARKS) {
            if (sidebar->bookmarks_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_DEVICES) {
            if (sidebar->devices_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_NETWORK) {
            if (sidebar->network_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        }
    }
    return FALSE;
}

static void
restore_expand_state (NolphinPlacesSidebar *sidebar)
{
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store_filter),
                restore_expand_state_foreach, sidebar);
}


static void expand_or_collapse_category (NolphinPlacesSidebar *sidebar,
                             SectionType section_type, gboolean expand)
{
    switch (section_type) {
        case SECTION_COMPUTER:
            sidebar->my_computer_expanded = expand;
            break;
        case SECTION_BOOKMARKS:
            sidebar->bookmarks_expanded = expand;
            break;
        case SECTION_DEVICES:
            sidebar->devices_expanded = expand;
            break;
        case SECTION_NETWORK:
            sidebar->network_expanded = expand;
            break;
        case SECTION_XDG_BOOKMARKS:
        default:
            break;
    }

    restore_expand_state (sidebar);
}

static void
sidebar_update_restore_selection (NolphinPlacesSidebar *sidebar,
				  const gchar *location,
				  const gchar *last_uri)
{
	RestoreLocationData data;
	GtkTreeSelection *selection;

	data.location = location;
	data.last_uri = last_uri;
	data.sidebar = sidebar;
	data.path = NULL;

	gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store_filter),
				restore_selection_foreach, &data);

	if (data.path != NULL) {
		selection = gtk_tree_view_get_selection (sidebar->tree_view);
		gtk_tree_selection_select_path (selection, data.path);
		gtk_tree_path_free (data.path);
	}
}

static gint
get_disk_full (GFile *file, gchar **tooltip_info)
{
    GFileInfo *info;
    GError *error;
    guint64 k_used, k_total, k_free;
    gint df_percent;
    float fraction;
    int prefix;
    gchar *size_string;
    gchar *out_string;

    error = NULL;
    df_percent = -1;
    out_string = NULL;

    info = g_file_query_filesystem_info (file,
                                         "filesystem::*",
                                         NULL,
                                         &error);

    if (info != NULL) {
        k_used = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
        k_total = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
        k_free = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);

        if (k_total > 0) {
            fraction = ((float) k_used / (float) k_total) * 100.0;

            df_percent = (gint) rintf(fraction);

            prefix = nolphin_global_preferences_get_size_prefix_preference ();
            size_string = g_format_size_full (k_free, prefix);

            out_string = g_strdup_printf (_("Freier Speicherplatz: %s"), size_string);

            g_free (size_string);
        }

        g_object_unref (info);
    }

    if (error != NULL) {
        g_warning ("Couldn't get disk full info for: %s", error->message);
        g_clear_error (&error);
    }

    if (out_string == NULL) {
        out_string = g_strdup (" ");
    }

    *tooltip_info = out_string;

    return df_percent;
}

static gboolean
home_on_different_fs (const gchar *home_uri)
{
    GFile *home = g_file_new_for_uri (home_uri);
    GFile *root = g_file_new_for_uri ("file:///");
    GFileInfo *home_info, *root_info;
    const gchar *home_id, *root_id;
    gboolean res;

    res = FALSE;
    home_info = g_file_query_info (home,
                                   G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   NULL, NULL);
    root_info = g_file_query_info (root,
                                   G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   NULL, NULL);

    if (home_info && root_info) {
        home_id = g_file_info_get_attribute_string (home_info, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
        root_id = g_file_info_get_attribute_string (root_info, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
        res = g_strcmp0 (home_id, root_id) != 0;
        g_object_unref (home_info);
        g_object_unref (root_info);
    } else {
        if (home_info)
            g_object_unref (home_info);
        if (root_info)
            g_object_unref (root_info);
    }
    g_object_unref (home);
    g_object_unref (root);
    return res;
}

static gchar *
get_icon_name (const gchar *uri)
{
    NolphinFile *file = nolphin_file_get_by_uri (uri);
    gchar *icon_name;

    icon_name = nolphin_file_get_control_icon_name (file);
    nolphin_file_unref (file);

    return icon_name;
}

static void
update_places (NolphinPlacesSidebar *sidebar)
{
	NolphinBookmark *bookmark;
	GtkTreeSelection *selection;
	GtkTreeIter last_iter, cat_iter;
	GtkTreeModel *model;
	GVolumeMonitor *volume_monitor;
	GList *mounts, *l, *ll;
	GMount *mount;
	GList *drives;
	GDrive *drive;
	GList *volumes;
	GVolume *volume;
	int bookmark_count, bookmark_index;
	char *location, *mount_uri, *name, *desktop_path, *last_uri, *identifier;
	const gchar *bookmark_name;
	gchar *icon;
	GFile *root, *df_file;
	NolphinWindowSlot *slot;
	char *tooltip;
    gchar *tooltip_info;
	GList *network_mounts, *network_volumes;
    gint full;

	DEBUG ("Updating places sidebar");

    sidebar->updating_sidebar = TRUE;

	model = NULL;
	last_uri = NULL;

    g_clear_pointer (&sidebar->top_bookend_uri, g_free);
    g_clear_pointer (&sidebar->bottom_bookend_uri, g_free);

	selection = gtk_tree_view_get_selection (sidebar->tree_view);
	if (gtk_tree_selection_get_selected (selection, &model, &last_iter)) {
		gtk_tree_model_get (model,
				    &last_iter,
				    PLACES_SIDEBAR_COLUMN_URI, &last_uri, -1);
	}
	gtk_tree_store_clear (sidebar->store);

	sidebar->devices_header_added = FALSE;
	sidebar->bookmarks_header_added = FALSE;

	slot = nolphin_window_get_active_slot (sidebar->window);
	location = nolphin_window_slot_get_current_uri (slot);

	network_mounts = network_volumes = NULL;
	volume_monitor = sidebar->volume_monitor;

    cat_iter = add_heading (sidebar, SECTION_COMPUTER,
                                    _("Mein Rechner"));
    /* add built in bookmarks */

    /* home folder */
    mount_uri = nolphin_get_home_directory_uri ();
    icon = get_icon_name (mount_uri);

    df_file = g_file_new_for_uri (mount_uri);
    full = get_disk_full (df_file, &tooltip_info);
    g_clear_object (&df_file);

    tooltip = g_strdup_printf (_("Persönlichen Ordner öffnen\n%s"), tooltip_info);
    g_free (tooltip_info);
    cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_COMPUTER,
                           _("Persönlicher Ordner"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           tooltip,
                           full, home_on_different_fs (mount_uri) && full > -1,
                           cat_iter);
    g_free (icon);
    sidebar->top_bookend_uri = g_strdup (mount_uri);
    g_free (mount_uri);
    g_free (tooltip);

    if (should_show_desktop ()) {
        /* desktop */
        desktop_path = nolphin_get_desktop_directory ();
        mount_uri = g_filename_to_uri (desktop_path, NULL, NULL);
        icon = get_icon_name (mount_uri);
        cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                               SECTION_COMPUTER,
                               _("Schreibtisch"), icon,
                               mount_uri, NULL, NULL, NULL, 0,
                               _("Den Inhalt des Schreibtisches in einem Ordner öffnen"), 0, FALSE,
                               cat_iter);
        g_free (icon);
        g_free (sidebar->top_bookend_uri);
        sidebar->top_bookend_uri = g_strdup (mount_uri);
        g_free (mount_uri);
        g_free (desktop_path);
    }

    /* add bookmarks */
    bookmark_count = nolphin_bookmark_list_length (sidebar->bookmarks);
    /* in certain situations (i.e. removed a bookmark), the breakpoint is smaller than
     * the number of bookmarks - make sure to fix this before iterating through a list of them.
     * We don't overwrite the stored breakpoint because the bookmark list could simply be reloading,
     * and we want the original number still when we update places again.
     */
    gint temp_breakpoint = sidebar->bookmark_breakpoint;

    if (temp_breakpoint < 0 ||
        temp_breakpoint > bookmark_count) {
        temp_breakpoint = bookmark_count;
    }

    for (bookmark_index = 0; bookmark_index < temp_breakpoint; ++bookmark_index) {
        bookmark = nolphin_bookmark_list_item_at (sidebar->bookmarks, bookmark_index);

        root = nolphin_bookmark_get_location (bookmark);

        bookmark_name = nolphin_bookmark_get_name (bookmark);
        icon = nolphin_bookmark_get_icon_name (bookmark);
        mount_uri = nolphin_bookmark_get_uri (bookmark);
        tooltip = g_file_get_parse_name (root);

        cat_iter = add_place (sidebar, PLACES_BOOKMARK,
                               SECTION_XDG_BOOKMARKS,
                               bookmark_name, icon, mount_uri,
                               NULL, NULL, NULL, bookmark_index,
                               tooltip, 0, FALSE,
                               cat_iter);
        g_object_unref (root);
        g_free (icon);
        g_free (mount_uri);
        g_free (tooltip);
    }

    if (eel_vfs_supports_uri_scheme ("favorites")) {
        gint n = xapp_favorites_get_n_favorites (xapp_favorites_get_default ());

        if (n > 0) {
            mount_uri = (char *)"favorites:///"; /* No need to strdup */
            icon = "xsi-user-favorites-symbolic";
            cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                                  SECTION_COMPUTER,
                                  _("Favoriten"), icon, mount_uri,
                                  NULL, NULL, NULL, 0,
                                  _("Favorisierte Dateien"), 0, FALSE, cat_iter);

            sidebar->bottom_bookend_uri = g_strdup (mount_uri);
        }
    }

    gboolean recent_enabled;
    recent_enabled = g_settings_get_boolean (cinnamon_privacy_preferences,
                                             NOLPHIN_PREFERENCES_RECENT_ENABLED);

    if (recent_enabled && eel_vfs_supports_uri_scheme ("recent")) {
        mount_uri = (char *)"recent:///"; /* No need to strdup */
        icon = NOLPHIN_ICON_SYMBOLIC_FOLDER_RECENT;
        cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                              SECTION_COMPUTER,
                              _("Kürzlich"), icon, mount_uri,
                              NULL, NULL, NULL, 0,
                              _("Kürzlich verwendete Dateien"), 0, FALSE, cat_iter);

        if (sidebar->bottom_bookend_uri == NULL) {
            sidebar->bottom_bookend_uri = g_strdup (mount_uri);
        }
    }

    /* file system root */
    mount_uri = (char *)"file:///"; /* No need to strdup */
    icon = NOLPHIN_ICON_SYMBOLIC_FILESYSTEM;

    df_file = g_file_new_for_uri (mount_uri);
    full = get_disk_full (df_file, &tooltip_info);
    g_clear_object (&df_file);

    tooltip = g_strdup_printf (_("Die Inhalte des Dateisystems öffnen\n%s"), tooltip_info);
    g_free (tooltip_info);
    cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_COMPUTER,
                           _("Dateisystem"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           tooltip,
                           full, full > -1,
                           cat_iter);
    g_free (tooltip);

    if (sidebar->bottom_bookend_uri == NULL) {
        sidebar->bottom_bookend_uri = g_strdup (mount_uri);
    }

    /* Handy: Der Eintrag zeigt auf den Ausgangsordner "~/Handy". Dateien, die
     * man darauf zieht, werden dorthin kopiert und kann das Handy laden. Ein Klick öffnet
     * den Ordner; der Handy-Austausch liegt unter Datei ▸ Handy-Austausch. */
    {
        GtkIconTheme *theme = gtk_icon_theme_get_default ();
        const gchar *phone_icon = gtk_icon_theme_has_icon (theme, "phone-symbolic") ? "phone-symbolic" :
                                  gtk_icon_theme_has_icon (theme, "smartphone-symbolic") ? "smartphone-symbolic" :
                                  "network-wireless-symbolic";
        gchar *phone_uri = phone_outbox_uri ();
        gboolean have_phone = phone_uri != NULL;

        if (phone_uri != NULL) {
            g_free (sidebar->phone_uri);
            sidebar->phone_uri = g_strdup (phone_uri);
            sidebar->phone_connected = nolphin_phone_is_connected ();
            cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                                   SECTION_COMPUTER,
                                   _("Handy"), phone_icon,
                                   phone_uri, NULL, NULL, NULL, 0,
                                   _("Bilder und Dateien mit dem Handy austauschen. Dateien hierher ziehen, um sie aufs Handy zu schicken."), 0, FALSE,
                                   cat_iter);
            g_free (phone_uri);
        }
        /* Direkt darunter der Eingangsordner ~/Handy/Vom Handy: dorthin kommt alles, was vom Handy gesendet wird */
        {
            gchar *inbox = nolphin_phone_inbox_path ();
            gchar *inbox_uri = g_filename_to_uri (inbox, NULL, NULL);
            const gchar *inbox_icon = gtk_icon_theme_has_icon (theme, "folder-download-symbolic") ? "folder-download-symbolic" :
                                      "folder-symbolic";

            g_free (sidebar->phone_inbox_uri);
            sidebar->phone_inbox_uri = g_strdup (inbox_uri);
            if (have_phone && inbox_uri != NULL) {
                cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                                       SECTION_COMPUTER,
                                       _("Vom Handy"), inbox_icon,
                                       inbox_uri, NULL, NULL, NULL, 0,
                                       _("Dateien, Fotos und Ordner, die vom Handy gesendet wurden."), 0, FALSE,
                                       cat_iter);
            }
            g_free (inbox_uri);
            g_free (inbox);
        }
    }

    if (eel_vfs_supports_uri_scheme("trash")) {
        mount_uri = (char *)"trash:///"; /* No need to strdup */
        icon = nolphin_trash_monitor_get_symbolic_icon_name ();
        cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                               SECTION_COMPUTER,
                               _("Papierkorb"), icon, mount_uri,
                               NULL, NULL, NULL, 0,
                               _("Papierkorb öffnen"), 0, FALSE,
                               cat_iter);
        g_free (icon);
    }

    cat_iter = add_heading (sidebar, SECTION_BOOKMARKS,
                                    _("Lesezeichen"));

    while (bookmark_index < bookmark_count) {
        bookmark = nolphin_bookmark_list_item_at (sidebar->bookmarks, bookmark_index);

        root = nolphin_bookmark_get_location (bookmark);

        bookmark_name = nolphin_bookmark_get_name (bookmark);
        icon = nolphin_bookmark_get_icon_name (bookmark);
        mount_uri = nolphin_bookmark_get_uri (bookmark);
        tooltip = g_file_get_parse_name (root);

        cat_iter = add_place (sidebar, PLACES_BOOKMARK,
                              SECTION_BOOKMARKS,
                              bookmark_name, icon, mount_uri,
                              NULL, NULL, NULL, bookmark_index,
                              tooltip, 0, FALSE,
                              cat_iter);
        g_object_unref (root);
        g_free (icon);
        g_free (mount_uri);
        g_free (tooltip);
        ++bookmark_index;
    }

    GList *place_infos = NULL;
    PlaceInfo *place_info;

    /* add mounts that has no volume (/etc/mtab mounts, ftp, sftp,...) */
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

        icon = nolphin_get_mount_icon_name (mount);
        mount_uri = g_file_get_uri (root);
        name = g_mount_get_name (mount);
        tooltip = g_file_get_parse_name (root);
        place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                     SECTION_DEVICES,
                                     name, icon, mount_uri,
                                     NULL, NULL, mount, 0, tooltip, 0, FALSE);
        place_infos = g_list_prepend (place_infos, place_info);

        g_free (icon);
        g_free (name);
        g_free (mount_uri);
        g_free (tooltip);

        g_object_unref (root);
        g_object_unref (mount);
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
                    if (g_mount_is_shadowed (mount)) {
                        g_object_unref (mount);
                        g_object_unref (volume);
                        continue;
                    }

                    gchar *full_display_name, *volume_id;
                    /* Show mounted volume in the sidebar */
                    icon = nolphin_get_mount_icon_name (mount);
                    root = g_mount_get_default_location (mount);
                    mount_uri = g_file_get_uri (root);
                    name = g_mount_get_name (mount);

                    volume_id = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                    full_display_name = g_file_get_parse_name (root);

                    df_file = g_file_new_for_uri (mount_uri);
                    full = get_disk_full (df_file, &tooltip_info);
                    g_clear_object (&df_file);

                    tooltip = g_strdup_printf (_("%s (%s)\n%s"),
                                               full_display_name,
                                               volume_id,
                                               tooltip_info);
                    g_free (tooltip_info);
                    place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                                 SECTION_DEVICES,
                                                 name, icon, mount_uri,
                                                 drive, volume, mount, 0, tooltip, full, full > -1);
                    place_infos = g_list_prepend (place_infos, place_info);
                    g_object_unref (root);
                    g_object_unref (mount);
                    g_free (icon);
                    g_free (tooltip);
                    g_free (name);
                    g_free (mount_uri);
                    g_free (volume_id);
                    g_free (full_display_name);
                } else {
                    /* Do show the unmounted volumes in the sidebar;
                     * this is so the user can mount it (in case automounting
                     * is off).
                     *
                     * Also, even if automounting is enabled, this gives a visual
                     * cue that the user should remember to yank out the media if
                     * he just unmounted it.
                     */
                    gchar *volume_id;
                    icon = nolphin_get_volume_icon_name (volume);
                    name = g_volume_get_name (volume);

                    volume_id = g_volume_get_identifier (volume,
                                                         G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                    tooltip = g_strdup_printf (_("%s (%s) einhängen und öffnen"), name, volume_id);

                    place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                                 SECTION_DEVICES,
                                                 name, icon, NULL,
                                                 drive, volume, NULL, 0, tooltip, 0, FALSE);
                    place_infos = g_list_prepend (place_infos, place_info);

                    g_free (icon);
                    g_free (name);
                    g_free (tooltip);
                    g_free (volume_id);
                }
                g_object_unref (volume);
            }
            g_list_free (volumes);
        } else {
            if (g_drive_is_removable (drive) && !g_drive_is_media_check_automatic (drive)) {
                /* If the drive has no mountable volumes and we cannot detect media change.. we
                 * display the drive in the sidebar so the user can manually poll the drive by
                 * right clicking and selecting "Rescan..."
                 *
                 * This is mainly for drives like floppies where media detection doesn't
                 * work.. but it's also for human beings who like to turn off media detection
                 * in the OS to save battery juice.
                 */
                icon = nolphin_get_drive_icon_name (drive);
                name = g_drive_get_name (drive);
                tooltip = g_strdup_printf (_("%s einhängen und öffnen"), name);

                place_info = new_place_info (PLACES_BUILT_IN,
                                             SECTION_DEVICES,
                                             name, icon, NULL,
                                             drive, NULL, NULL, 0, tooltip, 0, FALSE);
                place_infos = g_list_prepend (place_infos, place_info);

                g_free (icon);
                g_free (tooltip);
                g_free (name);
            }
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
            if (g_mount_is_shadowed (mount)) {
                g_object_unref (mount);
                g_object_unref (volume);
                continue;
            }

            g_autofree gchar *parse_name = NULL;
            icon = nolphin_get_mount_icon_name (mount);
            root = g_mount_get_default_location (mount);
            mount_uri = g_file_get_uri (root);

            df_file = g_file_new_for_uri (mount_uri);
            full = get_disk_full (df_file, &tooltip_info);
            g_clear_object (&df_file);

            parse_name = g_file_get_parse_name (root);
            tooltip = g_strdup_printf (_("%s\n%s"), parse_name, tooltip_info);

            g_free (tooltip_info);
            g_object_unref (root);
            name = g_mount_get_name (mount);

            place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                         SECTION_DEVICES,
                                         name, icon, mount_uri,
                                         NULL, volume, mount, 0, tooltip, full, full > -1);
            place_infos = g_list_prepend (place_infos, place_info);

            g_object_unref (mount);
            g_free (icon);
            g_free (name);
            g_free (tooltip);
            g_free (mount_uri);
        } else {
            /* see comment above in why we add an icon for an unmounted mountable volume */
            icon = nolphin_get_volume_icon_name (volume);
            name = g_volume_get_name (volume);

            place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                         SECTION_DEVICES,
                                         name, icon, NULL,
                                         NULL, volume, NULL, 0, name, 0, FALSE);
            place_infos = g_list_prepend (place_infos, place_info);

            g_free (icon);
            g_free (name);
        }
        g_object_unref (volume);
    }
    g_list_free (volumes);

    place_infos = g_list_sort (place_infos, (GCompareFunc) sort_places_func);

    for (l = place_infos; l != NULL; l = l->next) {
        PlaceInfo *info = (PlaceInfo *) l->data;

        cat_iter = add_place (sidebar,
                              info->place_type,
                              info->section_type,
                              info->name,
                              info->icon_name,
                              info->uri,
                              info->drive,
                              info->volume,
                              info->mount,
                              info->index,
                              info->tooltip,
                              info->df_percent,
                              info->show_df_percent,
                              cat_iter);

        free_place_info (info);
    }

    g_list_free (place_infos);

	/* network */
	cat_iter = add_heading (sidebar, SECTION_NETWORK,
		     _("Netzwerk"));

	network_volumes = g_list_reverse (network_volumes);
	for (l = network_volumes; l != NULL; l = l->next) {
		volume = l->data;
		mount = g_volume_get_mount (volume);

		if (mount != NULL) {
			network_mounts = g_list_prepend (network_mounts, mount);
			continue;
		} else {
			icon = nolphin_get_volume_icon_name (volume);
			name = g_volume_get_name (volume);
			tooltip = g_strdup_printf (_("%s einhängen und öffnen"), name);

			cat_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                				   SECTION_NETWORK,
                				   name, icon, NULL,
                				   NULL, volume, NULL, 0, tooltip, 0, FALSE,
                                   cat_iter);
			g_free (icon);
			g_free (name);
			g_free (tooltip);
		}
	}

	g_list_free_full (network_volumes, g_object_unref);

	network_mounts = g_list_sort(network_mounts, sort_mounts_func);
	for (l = network_mounts; l != NULL; l = l->next) {
		mount = l->data;
		root = g_mount_get_default_location (mount);
		icon = nolphin_get_mount_icon_name (mount);
		mount_uri = g_file_get_uri (root);
		name = g_mount_get_name (mount);
		tooltip = g_file_get_parse_name (root);
		cat_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                			   SECTION_NETWORK,
                			   name, icon, mount_uri,
                			   NULL, NULL, mount, 0, tooltip, 0, FALSE,
                               cat_iter);
		g_object_unref (root);
		g_free (icon);
		g_free (name);
		g_free (mount_uri);
		g_free (tooltip);
	}

	g_list_free_full (network_mounts, g_object_unref);

	/* network:// */
 	mount_uri = (char *)"network:///"; /* No need to strdup */
	icon = NOLPHIN_ICON_SYMBOLIC_NETWORK;
	cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                		   SECTION_NETWORK,
                		   _("Netzwerk"), icon,
                		   mount_uri, NULL, NULL, NULL, 0,
                		   _("Inhalt des Netzwerks durchsuchen"), 0, FALSE,
                           cat_iter);

	/* restore selection */
    restore_expand_state (sidebar);
	sidebar_update_restore_selection (sidebar, location, last_uri);

    actions_changed (sidebar);

    sidebar->updating_sidebar = FALSE;

	g_free (location);
	g_free (last_uri);
}

static void
mount_added_callback (GVolumeMonitor *volume_monitor,
		      GMount *mount,
		      NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
mount_removed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
mount_changed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_added_callback (GVolumeMonitor *volume_monitor,
		       GVolume *volume,
		       NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_removed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_changed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_disconnected_callback (GVolumeMonitor *volume_monitor,
			     GDrive         *drive,
			     NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_connected_callback (GVolumeMonitor *volume_monitor,
			  GDrive         *drive,
			  NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_changed_callback (GVolumeMonitor *volume_monitor,
			GDrive         *drive,
			NolphinPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static gboolean
over_eject_button (NolphinPlacesSidebar *sidebar,
		   gint x,
		   gint y,
		   GtkTreePath **path)
{
    GtkTreeViewColumn *column;
    int width, col_x_offset, cell_x_offset;
    gboolean show_eject;
    GtkTreeIter iter;
    GtkTreeModel *model;

    *path = NULL;
    model = gtk_tree_view_get_model (sidebar->tree_view);

    if (gtk_tree_view_get_path_at_pos (sidebar->tree_view,
                                       x, y,
                                       path, &column, &col_x_offset, NULL)) {

        gtk_tree_model_get_iter (model, &iter, *path);
        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_EJECT, &show_eject,
                            -1);

        if (!show_eject) {
            goto out;
        }

        if (column == sidebar->eject_column) {
            gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);

            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->eject_icon_cell_renderer,
                                                    &cell_x_offset, &width);
            if ((col_x_offset >= cell_x_offset) && (col_x_offset < cell_x_offset + width)) {
                return TRUE;
            }
        }
    }

    out:
    if (*path != NULL) {
        gtk_tree_path_free (*path);
        *path = NULL;
    }

    return FALSE;
}

static gboolean
clicked_eject_button (NolphinPlacesSidebar *sidebar,
		      GtkTreePath **path)
{
	GdkEvent *event;

	event = gtk_get_current_event ();

	if (event) {
		GdkEventButton *button_event = (GdkEventButton *) event;
		if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
		    over_eject_button (sidebar, button_event->x, button_event->y, path)) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
desktop_setting_changed_callback (gpointer user_data)
{
	NolphinPlacesSidebar *sidebar;

	sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

	update_places (sidebar);
}

static void
loading_uri_callback (NolphinWindow *window,
                            char *location,
               NolphinPlacesSidebar *sidebar)
{
    GtkTreeSelection *selection;
    GtkTreeIter       iter_cat, iter_child;
    gboolean          valid_cat, valid_child;
    char              *uri;
    gboolean found = FALSE;

    if (strcmp (sidebar->uri, location) != 0) {
        g_free (sidebar->uri);
                sidebar->uri = g_strdup (location);

        /* set selection if any place matches location */
        selection = gtk_tree_view_get_selection (sidebar->tree_view);
        gtk_tree_selection_unselect_all (selection);
        valid_cat = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sidebar->store_filter),
                                                    &iter_cat);

        while (valid_cat) {
            valid_child = gtk_tree_model_iter_children (GTK_TREE_MODEL (sidebar->store_filter),
                                                        &iter_child,
                                                        &iter_cat);
            while (valid_child) {
                gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter_child,
                               	    PLACES_SIDEBAR_COLUMN_URI, &uri,
                                    -1);
                if (uri != NULL) {
                    if (strcmp (uri, location) == 0) {
                        g_free (uri);
                        gtk_tree_selection_select_iter (selection, &iter_child);
                        found = TRUE;
                        break;
                    }
                    g_free (uri);
                }
                valid_child = gtk_tree_model_iter_next (GTK_TREE_MODEL (sidebar->store_filter),
                                                        &iter_child);
            }
            if (found) {
                break;
            }
            valid_cat = gtk_tree_model_iter_next (GTK_TREE_MODEL (sidebar->store_filter),
							                         &iter_cat);
		}
    }
}

typedef struct {
    NolphinPlacesSidebar *sidebar;
    GdkRectangle rect;
    SectionType section_type;
} CategoryExpandPayload;

static gboolean
pointer_is_still_in_cell (gint x,
                          gint y,
                  GdkRectangle rect)
{
    gint max_x = rect.x + rect.width;
    gint max_y = rect.y + rect.height;
    if ((x >= rect.x && x <= max_x) &&
        (y >= rect.y && y <= max_y)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
maybe_expand_category (gpointer data)
{
    CategoryExpandPayload *payload = (CategoryExpandPayload *) data;
    GdkDeviceManager *manager;
    GdkDevice *pointer;
    GdkWindow *window;
    int x, y;
    g_assert (GTK_IS_WIDGET (payload->sidebar->tree_view));
    window = gtk_widget_get_window (GTK_WIDGET (payload->sidebar->tree_view));

    manager = gdk_display_get_device_manager (gtk_widget_get_display (GTK_WIDGET (payload->sidebar->tree_view)));
    pointer = gdk_device_manager_get_client_pointer (manager);
    gdk_window_get_device_position (window, pointer,
                                    &x, &y, NULL);
    if (pointer_is_still_in_cell (x, y, payload->rect)) {
        expand_or_collapse_category (payload->sidebar, payload->section_type, TRUE);
    }

    g_source_remove (payload->sidebar->expand_timeout_source);
    payload->sidebar->expand_timeout_source = 0;
    return FALSE;
}


static gboolean
cat_is_expanded (NolphinPlacesSidebar *sidebar,
                 SectionType section_type)
{
    switch (section_type) {
        case SECTION_COMPUTER:
            return sidebar->my_computer_expanded;
        case SECTION_BOOKMARKS:
            return sidebar->bookmarks_expanded;
        case SECTION_DEVICES:
            return sidebar->devices_expanded;
        case SECTION_NETWORK:
            return sidebar->network_expanded;
        case SECTION_XDG_BOOKMARKS:
        default:
            return TRUE;
    }
}


static GtkTreeViewDropPosition
get_drag_type (NolphinPlacesSidebar *sidebar,
                           gchar *drop_target_uri,
                     GdkRectangle rect,
                              int y,
                      SectionType section_type)
{
    gint zone;
    gint upper_bound = rect.y + (rect.height / 4);
    gint lower_bound = rect.y + rect.height - (rect.height / 4);

    if (y <= upper_bound) {
        zone = POSITION_UPPER;
    } else if (y > upper_bound && y < lower_bound) {
        return GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
    } else {
        zone = POSITION_LOWER;
    }

    if (g_strcmp0 (drop_target_uri, sidebar->top_bookend_uri) == 0 &&
        zone == POSITION_LOWER) {
        return GTK_TREE_VIEW_DROP_AFTER;
    } else  if (g_strcmp0 (drop_target_uri, sidebar->bottom_bookend_uri) == 0 &&
        zone == POSITION_UPPER) {
        return GTK_TREE_VIEW_DROP_BEFORE;
    }

    if ((section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)
        && zone == POSITION_UPPER) {
        return GTK_TREE_VIEW_DROP_BEFORE;
    } else if ((section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)
        && zone == POSITION_LOWER) {
        return GTK_TREE_VIEW_DROP_AFTER;
    } else {
        /* or else you want to drag items INTO the existing bookmarks */
        return GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
    }
}


/* Computes the appropriate row and position for dropping */
static gboolean
compute_drop_position (GtkTreeView *tree_view,
		       int                      x,
		       int                      y,
		       GtkTreePath            **path,
		       GtkTreeViewDropPosition *pos,
		       NolphinPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	PlaceType place_type;
	SectionType section_type;
    gchar *drop_target_uri = NULL;

	if (!gtk_tree_view_get_dest_row_at_pos (tree_view,
						x, y,
						path, pos)) {
		return FALSE;
	}
	model = gtk_tree_view_get_model (tree_view);

	gtk_tree_model_get_iter (model, &iter, *path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                PLACES_SIDEBAR_COLUMN_URI, &drop_target_uri,
			    -1);
	if (!cat_is_expanded (sidebar, section_type) && place_type == PLACES_HEADING) {
        if (sidebar->expand_timeout_source > 0) {
            goto fail;
        }
        CategoryExpandPayload *payload;
        GtkTreeViewColumn *col;
        col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree_view), 2);
        payload = g_new0 (CategoryExpandPayload, 1);
        payload->sidebar = sidebar;
        gtk_tree_view_get_cell_area (tree_view,
                                     *path,
                                     col,
                                     &payload->rect);
        payload->section_type = section_type;
        sidebar->expand_timeout_source = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                             DRAG_EXPAND_CATEGORY_DELAY,
                                                             (GSourceFunc) maybe_expand_category,
                                                             payload,
                                                             (GDestroyNotify) g_free);
        goto fail;
	} else if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_BOOKMARKS &&
            (int)nolphin_bookmark_list_length (sidebar->bookmarks) == sidebar->bookmark_breakpoint) {
            *pos = GTK_TREE_VIEW_DROP_AFTER;
            g_free (drop_target_uri);
            return TRUE;
        } else {
            goto fail;
        }
    }

	if (section_type != SECTION_XDG_BOOKMARKS &&
        section_type != SECTION_BOOKMARKS &&
	    sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW &&
        g_strcmp0 (drop_target_uri, sidebar->top_bookend_uri) != 0) {
		/* don't allow dropping bookmarks into non-bookmark areas */

        goto fail;
	}

    if (g_strcmp0 (drop_target_uri, "recent:///") == 0) {
        goto fail;
    }

    GdkRectangle rect;
    GtkTreeViewColumn *col;
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree_view), 1);
    gtk_tree_view_get_cell_area (tree_view,
                                     *path,
                                     col,
                                     &rect);

    *pos = get_drag_type (sidebar, drop_target_uri, rect, y, section_type);

	if (*pos != GTK_TREE_VIEW_DROP_BEFORE &&
	    sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
		/* bookmark rows are never dragged into other bookmark rows */
		*pos = GTK_TREE_VIEW_DROP_AFTER;
	}

	return TRUE;

fail:
    g_free (drop_target_uri);
    gtk_tree_path_free (*path);
    *path = NULL;
    return FALSE;
}

static gboolean
get_drag_data (GtkTreeView *tree_view,
	       GdkDragContext *context,
	       unsigned int time)
{
	GdkAtom target;

	target = gtk_drag_dest_find_target (GTK_WIDGET (tree_view),
					    context,
					    NULL);

	if (target == GDK_NONE) {
		return FALSE;
	}

	gtk_drag_get_data (GTK_WIDGET (tree_view),
			   context, target, time);

	return TRUE;
}

static void
free_drag_data (NolphinPlacesSidebar *sidebar)
{
	sidebar->drag_data_received = FALSE;

	if (sidebar->drag_list) {
		nolphin_drag_destroy_selection_list (sidebar->drag_list);
		sidebar->drag_list = NULL;
	}

    g_clear_pointer (&sidebar->desktop_dnd_source_fs, g_free);
    sidebar->desktop_dnd_can_delete_source = FALSE;
}

static gboolean
can_accept_file_as_bookmark (NolphinFile *file)
{
	return (nolphin_file_is_directory (file) &&
		!is_built_in_bookmark (file));
}

static gboolean
can_accept_items_as_bookmarks (const GList *items)
{
	int max;
	char *uri;
	NolphinFile *file;

	/* Iterate through selection checking if item will get accepted as a bookmark.
	 * If more than 100 items selected, return an over-optimistic result.
	 */
	for (max = 100; items != NULL && max >= 0; items = items->next, max--) {
		uri = ((NolphinDragSelectionItem *)items->data)->uri;
		file = nolphin_file_get_by_uri (uri);
		if (!can_accept_file_as_bookmark (file)) {
			nolphin_file_unref (file);
			return FALSE;
		}
		nolphin_file_unref (file);
	}

	return TRUE;
}

static gboolean
drag_motion_callback (GtkTreeView *tree_view,
		      GdkDragContext *context,
		      int x,
		      int y,
		      unsigned int time,
		      NolphinPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeViewDropPosition pos;
	int action;
	GtkTreeIter iter;
	char *uri;
	gboolean res;

    action = 0;

	if (!sidebar->drag_data_received) {
		if (!get_drag_data (tree_view, context, time)) {
			return FALSE;
		}
	}

    if (!sidebar->in_drag) {
        sidebar->in_drag = TRUE;
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->store_filter));
    }

	path = NULL;
	res = compute_drop_position (tree_view, x, y, &path, &pos, sidebar);

	if (!res) {
        gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_BEFORE);
		goto out;
	}

	if (pos == GTK_TREE_VIEW_DROP_BEFORE ||
	    pos == GTK_TREE_VIEW_DROP_AFTER ) {
		if (sidebar->drag_data_received &&
		    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
			action = GDK_ACTION_MOVE;
		} else if (can_accept_items_as_bookmarks (sidebar->drag_list)) {
			action = GDK_ACTION_COPY;
		} else {
			action = 0;
		}
	} else {
		if (sidebar->drag_list == NULL) {
			action = 0;
		} else {
			gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store_filter),
						 &iter, path);
			gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter),
					    &iter,
					    PLACES_SIDEBAR_COLUMN_URI, &uri,
					    -1);
            nolphin_drag_default_drop_action_for_icons (context,
                                                     uri,
                                                     sidebar->drag_list,
                                                     &action,
                                                     &sidebar->desktop_dnd_source_fs,
                                                     &sidebar->desktop_dnd_can_delete_source);
            if (is_phone_uri (uri) && action != 0) {
                /* aufs Handy wird immer kopiert, nie verschoben */
                action = GDK_ACTION_COPY;
            }
            g_free (uri);
		}
	}

	if (action != 0) {
		gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
	}

	if (path != NULL) {
		gtk_tree_path_free (path);
	}

 out:
	g_signal_stop_emission_by_name (tree_view, "drag-motion");

	if (action != 0) {
		gdk_drag_status (context, action, time);
	} else {
		gdk_drag_status (context, 0, time);
	}

	return TRUE;
}

static void
drag_leave_callback (GtkTreeView *tree_view,
		     GdkDragContext *context,
		     unsigned int time,
		     NolphinPlacesSidebar *sidebar)
{
	free_drag_data (sidebar);
	gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_BEFORE);
	g_signal_stop_emission_by_name (tree_view, "drag-leave");
}

/* Parses a "text/uri-list" string and inserts its URIs as bookmarks */
static void
bookmarks_drop_uris (NolphinPlacesSidebar *sidebar,
                     GtkSelectionData  *selection_data,
                                  int   position,
                          SectionType   section_type)
{
	NolphinBookmark *bookmark;
	NolphinFile *file;
	char *uri;
	char **uris;
	int i;
	GFile *location;

	uris = gtk_selection_data_get_uris (selection_data);
	if (!uris)
		return;

	for (i = 0; uris[i]; i++) {
		uri = uris[i];
		file = nolphin_file_get_by_uri (uri);

		if (!can_accept_file_as_bookmark (file)) {
			nolphin_file_unref (file);
			continue;
		}

		uri = nolphin_file_get_drop_target_uri (file);
		location = g_file_new_for_uri (uri);
		nolphin_file_unref (file);

		bookmark = nolphin_bookmark_new (location, NULL, NULL, NULL);

		if (!nolphin_bookmark_list_contains (sidebar->bookmarks, bookmark)) {
            if (position < sidebar->bookmark_breakpoint ||
                (position == sidebar->bookmark_breakpoint && (section_type == SECTION_XDG_BOOKMARKS ||
                                                              section_type == SECTION_COMPUTER))) {
                increment_bookmark_breakpoint (sidebar);
            }
			nolphin_bookmark_list_insert_item (sidebar->bookmarks, bookmark, position++);
		}

		g_object_unref (location);
		g_object_unref (bookmark);
		g_free (uri);
	}

	g_strfreev (uris);
}

static GList *
uri_list_from_selection (GList *selection)
{
	NolphinDragSelectionItem *item;
	GList *ret;
	GList *l;

	ret = NULL;
	for (l = selection; l != NULL; l = l->next) {
		item = l->data;
		ret = g_list_prepend (ret, item->uri);
	}

	return g_list_reverse (ret);
}

static GList*
build_selection_list (const char *data)
{
	NolphinDragSelectionItem *item;
	GList *result;
	char **uris;
	char *uri;
	int i;

	uris = g_uri_list_extract_uris (data);

	result = NULL;
	for (i = 0; uris[i]; i++) {
		uri = uris[i];
		item = nolphin_drag_selection_item_new ();
		item->uri = g_strdup (uri);
		item->got_icon_position = FALSE;
		result = g_list_prepend (result, item);
	}

	g_strfreev (uris);

	return g_list_reverse (result);
}

static gboolean
get_selected_iter (NolphinPlacesSidebar *sidebar,
		   GtkTreeIter *iter)
{
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (sidebar->tree_view);

	return gtk_tree_selection_get_selected (selection, NULL, iter);
}

static void
update_bookmark_breakpoint (NolphinPlacesSidebar *sidebar,
                                  SectionType  old_type,
                                  SectionType  new_type)
{
    if (old_type != new_type) {
        if (old_type == SECTION_XDG_BOOKMARKS && new_type != SECTION_COMPUTER)
            decrement_bookmark_breakpoint (sidebar);
        else if (old_type == SECTION_BOOKMARKS)
            increment_bookmark_breakpoint (sidebar);
    }
}

/* Reorders the selected bookmark to the specified position */
static void
reorder_bookmarks (NolphinPlacesSidebar *sidebar,
                                 int  new_position,
                         SectionType  new_section_type)
{
	GtkTreeIter iter;
	PlaceType type;
    SectionType old_section_type;
	int old_position;

	/* Get the selected path */
	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &old_section_type,
			    PLACES_SIDEBAR_COLUMN_INDEX, &old_position,
			    -1);

	if (type != PLACES_BOOKMARK ||
	    old_position < 0 ||
	    old_position >= (int)nolphin_bookmark_list_length (sidebar->bookmarks)) {
		return;
	}

    update_bookmark_breakpoint (sidebar, old_section_type, new_section_type);

	nolphin_bookmark_list_move_item (sidebar->bookmarks, old_position,
					  new_position);

    if (old_position == new_position)
        update_places (sidebar);
}

static gboolean
idle_hide_bookmarks (gpointer user_data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    if (sidebar->in_drag) {
        sidebar->in_drag = FALSE;
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->store_filter));
    }

    return FALSE;
}

static void
drag_data_received_callback (GtkWidget *widget,
			     GdkDragContext *context,
			     int x,
			     int y,
			     GtkSelectionData *selection_data,
			     unsigned int info,
			     unsigned int time,
			     NolphinPlacesSidebar *sidebar)
{
	GtkTreeView *tree_view;
	GtkTreePath *tree_path;
	GtkTreeViewDropPosition tree_pos;
	GtkTreeIter iter;
	int position;
	GtkTreeModel *model;
	char *drop_uri;
	GList *selection_list, *uris;
	PlaceType place_type;
	SectionType section_type;
	gboolean success;

	tree_view = GTK_TREE_VIEW (widget);

	if (!sidebar->drag_data_received) {
		if (gtk_selection_data_get_target (selection_data) != GDK_NONE &&
		    info == TEXT_URI_LIST) {
			sidebar->drag_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
		} else {
			sidebar->drag_list = NULL;
		}
		sidebar->drag_data_received = TRUE;
		sidebar->drag_data_info = info;
	}

	g_signal_stop_emission_by_name (widget, "drag-data-received");

	if (!sidebar->drop_occured) {
		return;
	}

	/* Compute position */
	success = compute_drop_position (tree_view, x, y, &tree_path, &tree_pos, sidebar);
	if (!success) {
		goto out;
	}

	success = FALSE;

	if (tree_pos == GTK_TREE_VIEW_DROP_BEFORE ||
	    tree_pos == GTK_TREE_VIEW_DROP_AFTER) {
		model = gtk_tree_view_get_model (tree_view);
		if (!gtk_tree_model_get_iter (model, &iter, tree_path)) {
			goto out;
		}

		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
    				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    PLACES_SIDEBAR_COLUMN_INDEX, &position,
				    -1);

        if (section_type == SECTION_COMPUTER && tree_pos == GTK_TREE_VIEW_DROP_BEFORE) {
            position = nolphin_bookmark_list_length(sidebar->bookmarks);
        } else if (section_type == SECTION_BOOKMARKS && position == -1) {
            /* Check for (temporarily) visible Bookmarks heading, only drop-reactive when
             * it has no children, so we can assume that (bookmark_breakpoint == bookmark_count)
             */
            position = nolphin_bookmark_list_length (sidebar->bookmarks);
        } else if (tree_pos == GTK_TREE_VIEW_DROP_AFTER &&
                   (section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)) {
            position++;
        }

		switch (info) {
		case TEXT_URI_LIST:
			bookmarks_drop_uris (sidebar, selection_data, position, section_type);
			success = TRUE;
			break;
		case GTK_TREE_MODEL_ROW:
			reorder_bookmarks (sidebar, position, section_type);
			success = TRUE;
			break;
		default:
			g_assert_not_reached ();
			break;
		}
	} else {
		GdkDragAction real_action;

		/* file transfer requested */
		real_action = gdk_drag_context_get_selected_action (context);

		if (real_action == GDK_ACTION_ASK) {
			real_action =
				nolphin_drag_drop_action_ask (GTK_WIDGET (tree_view),
							       gdk_drag_context_get_actions (context));
		}

		if (real_action > 0) {
			model = gtk_tree_view_get_model (tree_view);

			gtk_tree_model_get_iter (model, &iter, tree_path);
			gtk_tree_model_get (model, &iter,
					    PLACES_SIDEBAR_COLUMN_URI, &drop_uri,
					    -1);

			switch (info) {
			case TEXT_URI_LIST:
				selection_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
				uris = uri_list_from_selection (selection_list);

                GList *l;

                if (g_strcmp0 (drop_uri, "favorites:///") == 0) {
                    for (l = uris; l != NULL; l = l->next) {
                        gchar *uri = (gchar *) l->data;
                        NolphinFile *source_file = nolphin_file_get_by_uri (uri);
                        nolphin_file_set_is_favorite (source_file, TRUE);
                        nolphin_file_unref (source_file);
                    }
                } else {
                    if (is_phone_uri (drop_uri)) {
                        nolphin_phone_ensure_folders ();
                    }
                    nolphin_file_operations_copy_move (uris, NULL, drop_uri,
                                        real_action, GTK_WIDGET (tree_view),
                                        NULL, NULL);
                }

				nolphin_drag_destroy_selection_list (selection_list);
				g_list_free (uris);
				success = TRUE;
				break;
			case GTK_TREE_MODEL_ROW:
				success = FALSE;
				break;
			default:
				g_assert_not_reached ();
				break;
			}

			g_free (drop_uri);
		}
	}

out:
	sidebar->drop_occured = FALSE;
	free_drag_data (sidebar);
	gtk_drag_finish (context, success, FALSE, time);
	gtk_tree_path_free (tree_path);

    g_timeout_add (250, (GSourceFunc) idle_hide_bookmarks, sidebar);
}

static gboolean
drag_drop_callback (GtkTreeView *tree_view,
		    GdkDragContext *context,
		    int x,
		    int y,
		    unsigned int time,
		    NolphinPlacesSidebar *sidebar)
{
	gboolean retval = FALSE;
	sidebar->drop_occured = TRUE;
	retval = get_drag_data (tree_view, context, time);
	g_signal_stop_emission_by_name (tree_view, "drag-drop");
	return retval;
}

static void
check_unmount_and_eject (GMount *mount,
			 GVolume *volume,
			 GDrive *drive,
			 gboolean *show_unmount,
			 gboolean *show_eject)
{
	*show_unmount = FALSE;
	*show_eject = FALSE;

	if (drive != NULL) {
		*show_eject = g_drive_can_eject (drive);
	}

	if (volume != NULL) {
		*show_eject |= g_volume_can_eject (volume);
	}
	if (mount != NULL) {
		*show_eject |= g_mount_can_eject (mount);
		*show_unmount = g_mount_can_unmount (mount) && !*show_eject;
	}
}

static void
check_visibility (GMount           *mount,
		  GVolume          *volume,
		  GDrive           *drive,
		  gboolean         *show_mount,
		  gboolean         *show_unmount,
		  gboolean         *show_eject,
		  gboolean         *show_rescan,
		  gboolean         *show_start,
		  gboolean         *show_stop)
{
	*show_mount = FALSE;
	*show_rescan = FALSE;
	*show_start = FALSE;
	*show_stop = FALSE;

	check_unmount_and_eject (mount, volume, drive, show_unmount, show_eject);

	if (drive != NULL) {
		if (g_drive_is_removable (drive) &&
		    !g_drive_is_media_check_automatic (drive) &&
		    g_drive_can_poll_for_media (drive))
			*show_rescan = TRUE;

		*show_start = g_drive_can_start (drive) || g_drive_can_start_degraded (drive);
		*show_stop  = g_drive_can_stop (drive);

		if (*show_stop)
			*show_unmount = FALSE;
	}

	if (volume != NULL) {
		if (mount == NULL)
			*show_mount = g_volume_can_mount (volume);
	}
}

static void
set_action_visible (GtkActionGroup *action_group,
                    const gchar    *name,
                    gboolean        visible)
{
    GtkAction *action;

    action = gtk_action_group_get_action (action_group, name);
    gtk_action_set_visible (action, visible);
}

static void
update_menu_states (NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type;
	GDrive *drive = NULL;
	GVolume *volume = NULL;
	GMount *mount = NULL;
	GFile *location;
	NolphinDirectory *directory = NULL;
	gboolean show_mount;
	gboolean show_unmount;
	gboolean show_eject;
	gboolean show_rescan;
	gboolean show_start;
	gboolean show_stop;
	gboolean show_empty_trash;
	gboolean show_properties;
	char *uri = NULL;

	type = PLACES_BUILT_IN;

	if (sidebar->popup_menu == NULL) {
		return;
	}

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
				    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
 				    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
				    PLACES_SIDEBAR_COLUMN_URI, &uri,
				    -1);
	}

    if (uri) {
        NolphinFile *file = nolphin_file_get_by_uri (uri);
        NolphinFile *parent = nolphin_file_get_parent (file);

        GList *selection = g_list_prepend (NULL, file);

        nolphin_action_manager_update_action_states (sidebar->action_manager,
                                                  sidebar->action_action_group,
                                                  selection,
                                                  parent,
                                                  TRUE,
                                                  FALSE,
                                                  GTK_WINDOW (sidebar->window));
        nolphin_file_list_free (selection);
        nolphin_file_unref (parent);
    }

    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_ADD_BOOKMARK, (type == PLACES_MOUNTED_VOLUME));
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_SIDEBAR_REMOVE, (type == PLACES_BOOKMARK));
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_RENAME, (type == PLACES_BOOKMARK));
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_EMPTY_TRASH_CONDITIONAL, !nolphin_trash_monitor_is_empty ());

 	check_visibility (mount, volume, drive,
 			  &show_mount, &show_unmount, &show_eject, &show_rescan, &show_start, &show_stop);

	/* We actually want both eject and unmount since eject will unmount all volumes.
	 * TODO: hide unmount if the drive only has a single mountable volume
	 */
	show_empty_trash = (uri != NULL) &&
			   (!strcmp (uri, "trash:///"));

    g_free (uri);

	/* Only show properties for local mounts */
	show_properties = (mount != NULL);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);
		directory = nolphin_directory_get (location);

		show_properties = nolphin_directory_is_local (directory);

		nolphin_directory_unref (directory);
		g_object_unref (location);
	}

    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_MOUNT_VOLUME, show_mount);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_UNMOUNT_VOLUME, show_unmount);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_EJECT_VOLUME, show_eject);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_SIDEBAR_DETECT_MEDIA, show_rescan);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_START_VOLUME, show_start);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_STOP_VOLUME, show_stop);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_EMPTY_TRASH_CONDITIONAL, show_empty_trash);
    set_action_visible (sidebar->bookmark_action_group, NOLPHIN_ACTION_PROPERTIES, show_properties);

	/* Adjust start/stop items to reflect the type of the drive */
    GtkAction *start_action, *stop_action;

    start_action = gtk_action_group_get_action (sidebar->bookmark_action_group, NOLPHIN_ACTION_START_VOLUME);
    stop_action = gtk_action_group_get_action (sidebar->bookmark_action_group, NOLPHIN_ACTION_STOP_VOLUME);

	gtk_action_set_label (start_action, _("_Start"));
	gtk_action_set_label (stop_action, _("_Anhalten"));
	if ((show_start || show_stop) && drive != NULL) {
		switch (g_drive_get_start_stop_type (drive)) {
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			/* start() for type G_DRIVE_START_STOP_TYPE_SHUTDOWN is normally not used */
			gtk_action_set_label (start_action, _("_Einschalten"));
			gtk_action_set_label (stop_action, _("Laufwerk _sicher entfernen"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (start_action, _("Laufwerk _verbinden"));
			gtk_action_set_label (stop_action, _("Laufwerk _trennen"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (start_action, _("Multimedienlaufwerk _starten"));
			gtk_action_set_label (stop_action, _("Multimedienlaufwerk _anhalten"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			/* stop() for type G_DRIVE_START_STOP_TYPE_PASSWORD is normally not used */
			gtk_action_set_label (start_action, _("Laufwerk _entsperren"));
			gtk_action_set_label (stop_action, _("Laufwerk _sperren"));
			break;

		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			/* uses defaults set above */
			break;
		}
	}

    g_clear_object (&drive);
    g_clear_object (&volume);
    g_clear_object (&mount);
}

/* Callback used when the selection in the shortcuts tree changes */
static void
bookmarks_selection_changed_cb (GtkTreeSelection      *selection,
				NolphinPlacesSidebar *sidebar)
{
    update_menu_states (sidebar);
}

static void
volume_mounted_cb (GVolume *volume,
		   gboolean success,
		   GObject *user_data)
{
	GMount *mount;
	NolphinPlacesSidebar *sidebar;
	GFile *location;

	sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

	sidebar->mounting = FALSE;

	mount = g_volume_get_mount (volume);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);

		if (sidebar->go_to_after_mount_slot != NULL) {
			if ((sidebar->go_to_after_mount_flags & NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
				nolphin_window_slot_open_location (sidebar->go_to_after_mount_slot, location,
								    sidebar->go_to_after_mount_flags);
			} else {
				NolphinWindow *new, *cur;

				cur = NOLPHIN_WINDOW (sidebar->window);
				new = nolphin_application_create_window (nolphin_application_get_singleton (),
									  gtk_window_get_screen (GTK_WINDOW (cur)));
				nolphin_window_go_to (new, location);
			}
		}

		g_object_unref (G_OBJECT (location));
		g_object_unref (G_OBJECT (mount));
	}

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}
}

static void
drive_start_from_bookmark_cb (GObject      *source_object,
			      GAsyncResult *res,
			      gpointer      user_data)
{
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Starten von %s nicht möglich"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
open_selected_bookmark (NolphinPlacesSidebar *sidebar,
			GtkTreeModel	      *model,
			GtkTreeIter	      *iter,
			NolphinWindowOpenFlags	      flags)
{
	NolphinWindowSlot *slot;
	GFile *location;
	char *uri;

	if (!iter) {
		return;
	}

	gtk_tree_model_get (model, iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

	if (uri != NULL) {
		DEBUG ("Activating bookmark %s", uri);

		/* ~/Handy und ~/Handy/Vom Handy erst beim Öffnen anlegen */
		if (is_phone_uri (uri) || g_strcmp0 (uri, sidebar->phone_inbox_uri) == 0) {
			nolphin_phone_ensure_folders ();
		}

		location = g_file_new_for_uri (uri);
		/* Navigate to the clicked location */
		if ((flags & NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
			slot = nolphin_window_get_active_slot (sidebar->window);
			nolphin_window_slot_open_location (slot, location, flags);
		} else {
			NolphinWindow *cur, *new;

			cur = NOLPHIN_WINDOW (sidebar->window);
			new = nolphin_application_create_window (nolphin_application_get_singleton (),
								  gtk_window_get_screen (GTK_WINDOW (cur)));
			nolphin_window_go_to (new, location);
		}
		g_object_unref (location);
		g_free (uri);

	} else {
		GDrive *drive;
		GVolume *volume;
		NolphinWindowSlot *slt;

		gtk_tree_model_get (model, iter,
				    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
				    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
				    -1);

		if (volume != NULL && !sidebar->mounting) {
			sidebar->mounting = TRUE;

			g_assert (sidebar->go_to_after_mount_slot == NULL);

			slt = nolphin_window_get_active_slot (sidebar->window);
			sidebar->go_to_after_mount_slot = slt;
			g_object_add_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
						   (gpointer *) &sidebar->go_to_after_mount_slot);

			sidebar->go_to_after_mount_flags = flags | NOLPHIN_WINDOW_OPEN_FLAG_MOUNT;

			nolphin_file_operations_mount_volume_full (NULL, volume,
								    volume_mounted_cb,
								    G_OBJECT (sidebar));
		} else if (volume == NULL && drive != NULL &&
			   (g_drive_can_start (drive) || g_drive_can_start_degraded (drive))) {
			GMountOperation *mount_op;

			mount_op = nolphin_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
			g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_from_bookmark_cb, NULL);
			g_object_unref (mount_op);
		}

		if (drive != NULL)
			g_object_unref (drive);
		if (volume != NULL)
			g_object_unref (volume);
	}
}

static void
open_shortcut_from_menu (NolphinPlacesSidebar *sidebar,
			 NolphinWindowOpenFlags	       flags)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;

	model = gtk_tree_view_get_model (sidebar->tree_view);
	gtk_tree_view_get_cursor (sidebar->tree_view, &path, NULL);

	if (path != NULL && gtk_tree_model_get_iter (model, &iter, path)) {
		open_selected_bookmark (sidebar, model, &iter, flags);
	}

	gtk_tree_path_free (path);
}

static void
open_shortcut_cb (GtkAction		*item,
		  NolphinPlacesSidebar	*sidebar)
{
	open_shortcut_from_menu (sidebar, 0);
}

static void
open_shortcut_in_new_window_cb (GtkAction	      *item,
				NolphinPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

static void
open_shortcut_in_new_tab_cb (GtkAction	      *item,
				NolphinPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB);
}

/* Add bookmark for the selected item */
static void
add_bookmark (NolphinPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *uri;
	GFile *location;
	NolphinBookmark *bookmark;

	model = gtk_tree_view_get_model (sidebar->tree_view);

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

		if (uri == NULL) {
			return;
		}

		location = g_file_new_for_uri (uri);
		bookmark = nolphin_bookmark_new (location, NULL, NULL, NULL);

		if (!nolphin_bookmark_list_contains (sidebar->bookmarks, bookmark)) {
			nolphin_bookmark_list_append (sidebar->bookmarks, bookmark);
		}

		g_object_unref (location);
		g_object_unref (bookmark);
		g_free (uri);
	}
}

static void
add_shortcut_cb (GtkAction           *item,
		 NolphinPlacesSidebar *sidebar)
{
	add_bookmark (sidebar);
}

/* Rename the selected bookmark */
static void
rename_selected_bookmark (NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	PlaceType type;

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    -1);

		if (type != PLACES_BOOKMARK) {
			return;
		}

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
		column = gtk_tree_view_get_column (GTK_TREE_VIEW (sidebar->tree_view), 2);
		g_object_set (sidebar->editable_renderer, "editable", TRUE, NULL);
		gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sidebar->tree_view),
						path, column, sidebar->editable_renderer, TRUE);
		gtk_tree_path_free (path);
	}
}

static void
rename_shortcut_cb (GtkAction           *item,
		    NolphinPlacesSidebar *sidebar)
{
	rename_selected_bookmark (sidebar);
}

/* Removes the selected bookmarks */
static void
remove_selected_bookmarks (NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type;
	int index;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type != PLACES_BOOKMARK) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_INDEX, &index,
			    -1);

    if (index < sidebar->bookmark_breakpoint)
       decrement_bookmark_breakpoint (sidebar);

    nolphin_bookmark_list_delete_item_at (sidebar->bookmarks, index);
}

static void
remove_shortcut_cb (GtkAction           *item,
		    NolphinPlacesSidebar *sidebar)
{
	remove_selected_bookmarks (sidebar);
}

static void
mount_shortcut_cb (GtkAction           *item,
		   NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GVolume *volume;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    -1);

	if (volume != NULL) {
		nolphin_file_operations_mount_volume (NULL, volume);
		g_object_unref (volume);
	}
}

static void
unmount_done (gpointer data)
{
	NolphinWindow *window;

	window = data;
	g_object_unref (window);
}

static void
show_unmount_progress_cb (GMountOperation *op,
                              const gchar *message,
                                    gint64 time_left,
                                    gint64 bytes_left,
                                  gpointer user_data)
{
    NolphinApplication *app = NOLPHIN_APPLICATION (g_application_get_default ());

    if (bytes_left == 0) {
        nolphin_application_notify_unmount_done (app, message);
    } else {
        nolphin_application_notify_unmount_show (app, message);
    }
}

static void
show_unmount_progress_aborted_cb (GMountOperation *op,
                                  gpointer user_data)
{
    NolphinApplication *app = NOLPHIN_APPLICATION (g_application_get_default ());
    nolphin_application_notify_unmount_done (app, NULL);
}

static GMountOperation *
get_unmount_operation (NolphinPlacesSidebar *sidebar)
{
    GMountOperation *mount_op;

    mount_op = nolphin_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
    g_signal_connect (mount_op, "show-unmount-progress",
                      G_CALLBACK (show_unmount_progress_cb), sidebar);
    g_signal_connect (mount_op, "aborted",
                      G_CALLBACK (show_unmount_progress_aborted_cb), sidebar);

    return mount_op;
}

static void
do_unmount (GMount *mount,
	    NolphinPlacesSidebar *sidebar)
{
    GMountOperation *mount_op;

	if (mount != NULL) {
        mount_op = get_unmount_operation (sidebar);
        nolphin_file_operations_unmount_mount_full (NULL, mount, mount_op, FALSE, TRUE,
                                                 unmount_done,
                                                 g_object_ref (sidebar->window));
        g_object_unref (mount_op);
	}
}

static void
do_unmount_selection (NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    -1);

	if (mount != NULL) {
		do_unmount (mount, sidebar);
		g_object_unref (mount);
	}
}

static void
unmount_shortcut_cb (GtkAction           *item,
		     NolphinPlacesSidebar *sidebar)
{
	do_unmount_selection (sidebar);
}

static void
handle_mount_unmount_failure (const gchar *primary,
                              GError      *error)
{
    const gchar *message = NULL;

    if (error && error->code == G_IO_ERROR_FAILED_HANDLED) {
        return;
    }

    if (error) {
        message = error->message;
    }

    eel_show_error_dialog (primary,
                           message,
                           NULL);
}

static void
drive_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NolphinWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_drive_eject_with_operation_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Auswerfen von %s nicht möglich"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
volume_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NolphinWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_volume_eject_with_operation_finish (G_VOLUME (source_object), res, &error)) {
        char *name, *primary;

        name = g_volume_get_name (G_VOLUME (source_object));
        primary = g_strdup_printf (_("Auswerfen von %s nicht möglich"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
mount_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NolphinWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_mount_eject_with_operation_finish (G_MOUNT (source_object), res, &error)) {
        char *name, *primary;

        name = g_mount_get_name (G_MOUNT (source_object));
        primary = g_strdup_printf (_("Auswerfen von %s nicht möglich"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
do_eject (GMount *mount,
	  GVolume *volume,
	  GDrive *drive,
	  NolphinPlacesSidebar *sidebar)
{
    GMountOperation *mount_op = get_unmount_operation (sidebar);

	if (mount != NULL) {
		g_mount_eject_with_operation (mount, 0, mount_op, NULL, mount_eject_cb,
					      g_object_ref (sidebar->window));
	} else if (volume != NULL) {
		g_volume_eject_with_operation (volume, 0, mount_op, NULL, volume_eject_cb,
					      g_object_ref (sidebar->window));
	} else if (drive != NULL) {
		g_drive_eject_with_operation (drive, 0, mount_op, NULL, drive_eject_cb,
					      g_object_ref (sidebar->window));
	}
	g_object_unref (mount_op);
}

static void
eject_shortcut_cb (GtkAction           *item,
		   NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	do_eject (mount, volume, drive, sidebar);

    g_clear_object (&mount);
    g_clear_object (&volume);
    g_clear_object (&drive);
}

static gboolean
eject_or_unmount_bookmark (NolphinPlacesSidebar *sidebar,
			   GtkTreePath *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean can_unmount, can_eject;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;
	gboolean ret;

	model = GTK_TREE_MODEL (sidebar->store_filter);

	if (!path) {
		return FALSE;
	}
	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return FALSE;
	}

	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	ret = FALSE;

	check_unmount_and_eject (mount, volume, drive, &can_unmount, &can_eject);
	/* if we can eject, it has priority over unmount */
	if (can_eject) {
		do_eject (mount, volume, drive, sidebar);
		ret = TRUE;
	} else if (can_unmount) {
		do_unmount (mount, sidebar);
		ret = TRUE;
	}

    g_clear_object (&mount);
    g_clear_object (&volume);
    g_clear_object (&drive);

	return ret;
}

static gboolean
eject_or_unmount_selection (NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean ret;

	if (!get_selected_iter (sidebar, &iter)) {
		return FALSE;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
	if (path == NULL) {
		return FALSE;
	}

	ret = eject_or_unmount_bookmark (sidebar, path);

	gtk_tree_path_free (path);

	return ret;
}

static void
drive_poll_for_media_cb (GObject *source_object,
			 GAsyncResult *res,
			 gpointer user_data)
{
	GError *error;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("%s konnte nicht nach Datenträgeränderungen befragt werden"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
rescan_shortcut_cb (GtkAction           *item,
		    NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		g_drive_poll_for_media (drive, NULL, drive_poll_for_media_cb, NULL);
        g_object_unref (drive);
	}
}

static void
drive_start_cb (GObject      *source_object,
		GAsyncResult *res,
		gpointer      user_data)
{
	GError *error;

	error = NULL;

	if (!g_drive_start_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Starten von %s nicht möglich"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
start_shortcut_cb (GtkAction           *item,
		   NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		GMountOperation *mount_op;

		mount_op = nolphin_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));

		g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_cb, NULL);

		g_object_unref (mount_op);
        g_object_unref (drive);
	}
}

static void
drive_stop_cb (GObject *source_object,
	       GAsyncResult *res,
	       gpointer user_data)
{
	NolphinWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;

    if (!g_drive_stop_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Anhalten von %s nicht möglich"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
stop_shortcut_cb (GtkAction           *item,
		  NolphinPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
        GMountOperation *mount_op = get_unmount_operation (sidebar);
		g_drive_stop (drive, G_MOUNT_UNMOUNT_NONE, mount_op, NULL, drive_stop_cb,
			      g_object_ref (sidebar->window));
		g_object_unref (mount_op);
        g_object_unref (drive);
	}
}

static void
empty_trash_cb (GtkAction           *item,
		NolphinPlacesSidebar *sidebar)
{
	nolphin_file_operations_empty_trash (GTK_WIDGET (sidebar->window));
}

static gboolean
find_prev_or_next_row (NolphinPlacesSidebar *sidebar,
		       GtkTreeIter *iter,
		       gboolean go_up)
{
	GtkTreeModel *model = GTK_TREE_MODEL (sidebar->store_filter);
	gboolean res;
	int place_type;

	if (go_up) {
		res = gtk_tree_model_iter_previous (model, iter);
	} else {
		res = gtk_tree_model_iter_next (model, iter);
	}

	if (res) {
		gtk_tree_model_get (model, iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    -1);
		if (place_type == PLACES_HEADING) {
			if (go_up) {
				res = gtk_tree_model_iter_previous (model, iter);
			} else {
				res = gtk_tree_model_iter_next (model, iter);
			}
		}
	}

	return res;
}

static gboolean
find_prev_row (NolphinPlacesSidebar *sidebar, GtkTreeIter *iter)
{
	return find_prev_or_next_row (sidebar, iter, TRUE);
}

static gboolean
find_next_row (NolphinPlacesSidebar *sidebar, GtkTreeIter *iter)
{
	return find_prev_or_next_row (sidebar, iter, FALSE);
}

static void
properties_cb (GtkAction           *item,
	       NolphinPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GList *list;
	NolphinFile *file;
	char *uri;

	model = gtk_tree_view_get_model (sidebar->tree_view);
	gtk_tree_view_get_cursor (sidebar->tree_view, &path, NULL);

	if (path == NULL || !gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

	if (uri != NULL) {

		GtkWidget *toplevel;

		file = nolphin_file_get_by_uri (uri);
		list = g_list_prepend (NULL, nolphin_file_ref (file));

		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (sidebar));
		if (NOLPHIN_IS_WINDOW (toplevel)) {
			nolphin_workspace_panel_show_properties (nolphin_window_get_workspace_panel (NOLPHIN_WINDOW (toplevel)),
								 NOLPHIN_WINDOW (toplevel), list);
		} else {
			nolphin_workspace_panel_show_properties_anywhere (list, GTK_WIDGET (sidebar));
		}

		nolphin_file_list_free (list);
		g_free (uri);
	}

	gtk_tree_path_free (path);
}

static gboolean
nolphin_places_sidebar_focus (GtkWidget *widget,
			       GtkDirectionType direction)
{
	NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (widget);
	GtkTreePath *path;
	GtkTreeIter iter, child_iter;
	gboolean res;

	res = get_selected_iter (sidebar, &iter);
	if (!res) {
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sidebar->store_filter), &iter);
        gtk_tree_model_iter_children (GTK_TREE_MODEL (sidebar->store_filter), &child_iter, &iter);
		res = find_next_row (sidebar, &child_iter);
		if (res) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
			gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
			gtk_tree_path_free (path);
		}
	}

	return GTK_WIDGET_CLASS (nolphin_places_sidebar_parent_class)->focus (widget, direction);
}

/* Handler for GtkWidget::key-press-event on the shortcuts list */
static gboolean
bookmarks_key_press_event_cb (GtkWidget             *widget,
			      GdkEventKey           *event,
			      NolphinPlacesSidebar *sidebar)
{
  guint modifiers;
  GtkTreeIter selected_iter;
  GtkTreePath *path;

  if (event->keyval == GDK_KEY_slash ||
    event->keyval == GDK_KEY_KP_Divide ||
    event->keyval == GDK_KEY_asciitilde) {
    if (gtk_bindings_activate_event (G_OBJECT (sidebar->window), event)) {
        return GDK_EVENT_STOP;
    }
  }

  if (!get_selected_iter (sidebar, &selected_iter)) {
	  return FALSE;
  }

  modifiers = gtk_accelerator_get_default_mod_mask ();

  if ((event->keyval == GDK_KEY_Return ||
       event->keyval == GDK_KEY_KP_Enter ||
       event->keyval == GDK_KEY_ISO_Enter ||
       event->keyval == GDK_KEY_space)) {
      NolphinWindowOpenFlags flags = 0;

      if ((event->state & modifiers) == GDK_SHIFT_MASK) {
          flags = NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB;
      } else if ((event->state & modifiers) == GDK_CONTROL_MASK) {
          flags = NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW;
      }

      open_selected_bookmark (sidebar, GTK_TREE_MODEL (sidebar->store_filter),
			      &selected_iter, flags);
      return TRUE;
  }

  if (event->keyval == GDK_KEY_Down &&
      (event->state & modifiers) == GDK_MOD1_MASK) {
      return eject_or_unmount_selection (sidebar);
  }

  if (event->keyval == GDK_KEY_Up) {
      if (find_prev_row (sidebar, &selected_iter)) {
	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &selected_iter);
	      gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
	      gtk_tree_path_free (path);
      }
      return TRUE;
  }

  if (event->keyval == GDK_KEY_Down) {
      if (find_next_row (sidebar, &selected_iter)) {
	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &selected_iter);
	      gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
	      gtk_tree_path_free (path);
      }
      return TRUE;
  }

  if ((event->keyval == GDK_KEY_F2)
      && (event->state & modifiers) == 0) {
      rename_selected_bookmark (sidebar);
      return TRUE;
  }

  return FALSE;
}

static void
run_action_callback (GtkAction *action, gpointer user_data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);
    gchar *uri = NULL;
    GtkTreeIter iter;

    if (get_selected_iter (sidebar, &iter)) {
        gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
                    PLACES_SIDEBAR_COLUMN_URI, &uri,
                    -1);
    }

    if (!uri) {
        return;
    }

    NolphinFile *file = nolphin_file_get_by_uri (uri);
    NolphinFile *parent = nolphin_file_get_parent (file);
    GList *selection = g_list_prepend (NULL, file);

    nolphin_action_activate (NOLPHIN_ACTION (action), selection, parent, GTK_WINDOW (sidebar->window));

    nolphin_file_list_free (selection);
    nolphin_file_unref (parent);

    g_free (uri);
}

#if GTK_CHECK_VERSION (3, 24, 8)
static void
moved_to_rect_cb (GdkWindow          *window,
                  const GdkRectangle *flipped_rect,
                  const GdkRectangle *final_rect,
                  gboolean            flipped_x,
                  gboolean            flipped_y,
                  GtkMenu            *menu)
{
    g_signal_emit_by_name (menu,
                           "popped-up",
                           0,
                           flipped_rect,
                           final_rect,
                           flipped_x,
                           flipped_y);

    // Don't let the emission run in gtkmenu.c
    g_signal_stop_emission_by_name (window, "moved-to-rect");
}

static void
popup_menu_realized (GtkWidget    *menu,
                     gpointer      user_data)
{
    GdkWindow *toplevel;

    toplevel = gtk_widget_get_window (gtk_widget_get_toplevel (menu));

    g_signal_handlers_disconnect_by_func (toplevel, moved_to_rect_cb, menu);

    g_signal_connect (toplevel, "moved-to-rect", G_CALLBACK (moved_to_rect_cb),
                      menu);
}
#endif

static void
bookmarks_popup_menu (NolphinPlacesSidebar *sidebar,
		      GdkEventButton        *event)
{
    update_menu_states (sidebar);
    eel_pop_up_context_menu (GTK_MENU(sidebar->popup_menu),
                             (GdkEvent *) event,
                             GTK_WIDGET (sidebar));
}

/* Callback used for the GtkWidget::popup-menu signal of the shortcuts list */
static gboolean
bookmarks_popup_menu_cb (GtkWidget *widget,
			 NolphinPlacesSidebar *sidebar)
{
	bookmarks_popup_menu (sidebar, NULL);
	return TRUE;
}

static void
reset_menu (NolphinPlacesSidebar *sidebar)
{
    sidebar->actions_need_update = TRUE;
    rebuild_menu (sidebar);
}

static gboolean
actions_changed_idle_cb (gpointer user_data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    reset_menu (sidebar);

    sidebar->actions_changed_idle_id = 0;
    return G_SOURCE_REMOVE;
}

static void
actions_changed (gpointer user_data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    g_clear_handle_id (&sidebar->actions_changed_idle_id, g_source_remove);
    sidebar->actions_changed_idle_id = g_idle_add (actions_changed_idle_cb, sidebar);
}

static void
add_action_to_ui (NolphinActionManager    *manager,
                  GtkAction            *action,
                  GtkUIManagerItemType  type,
                  const gchar          *path,
                  const gchar          *accelerator,
                  gpointer              user_data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    static const gchar *roots[] = {
        "/selection/PlacesSidebarActionsPlaceholder",
        NULL,
    };

    nolphin_action_manager_add_action_ui (manager,
                                       sidebar->ui_manager,
                                       action,
                                       path,
                                       accelerator,
                                       sidebar->action_action_group,
                                       sidebar->action_action_group_merge_id,
                                       roots,
                                       type,
                                       G_CALLBACK (run_action_callback),
                                       sidebar);
}

static void
clear_ui (NolphinPlacesSidebar *sidebar)
{

    nolphin_ui_unmerge_ui (sidebar->ui_manager,
                        &sidebar->bookmark_action_group_merge_id,
                        &sidebar->bookmark_action_group);

    nolphin_ui_unmerge_ui (sidebar->ui_manager,
                        &sidebar->action_action_group_merge_id,
                        &sidebar->action_action_group);

}

static const GtkActionEntry bookmark_action_entries[] = {
    { NOLPHIN_ACTION_OPEN,                    "xsi-folder-open-symbolic", N_("_Öffnen"),                NULL, NULL, G_CALLBACK (open_shortcut_cb)               },
    { NOLPHIN_ACTION_OPEN_IN_NEW_TAB,         NULL,                   N_("In neuem _Reiter öffnen"),     NULL, NULL, G_CALLBACK (open_shortcut_in_new_tab_cb)    },
    { NOLPHIN_ACTION_OPEN_ALTERNATE,          NULL,                   N_("In neuem _Fenster öffnen"),  NULL, NULL, G_CALLBACK (open_shortcut_in_new_window_cb) },
    { NOLPHIN_ACTION_ADD_BOOKMARK,            NULL,                   N_("Lesezeichen _hinzufügen"),        NULL, NULL, G_CALLBACK (add_shortcut_cb)                },
    { NOLPHIN_ACTION_SIDEBAR_REMOVE,          "xsi-list-remove-symbolic", N_("Entfernen"),               NULL, NULL, G_CALLBACK (remove_shortcut_cb)             },
    { NOLPHIN_ACTION_RENAME,                  NULL,                   N_("_Umbenennen …"),           NULL, NULL, G_CALLBACK (rename_shortcut_cb)             },
    { NOLPHIN_ACTION_MOUNT_VOLUME,            NULL,                   N_("_Einhängen"),               NULL, NULL, G_CALLBACK (mount_shortcut_cb)              },
    { NOLPHIN_ACTION_UNMOUNT_VOLUME,          NULL,                   N_("_Aushängen"),             NULL, NULL, G_CALLBACK (unmount_shortcut_cb)            },
    { NOLPHIN_ACTION_EJECT_VOLUME,            NULL,                   N_("_Auswerfen"),               NULL, NULL, G_CALLBACK (eject_shortcut_cb)              },
    { NOLPHIN_ACTION_SIDEBAR_DETECT_MEDIA,    NULL,                   N_("Me_dien erkennen"),        NULL, NULL, G_CALLBACK (rescan_shortcut_cb)             },
    { NOLPHIN_ACTION_START_VOLUME,            NULL,                   N_("_Start"),               NULL, NULL, G_CALLBACK (start_shortcut_cb)              },
    { NOLPHIN_ACTION_STOP_VOLUME,             NULL,                   N_("_Anhalten"),                NULL, NULL, G_CALLBACK (stop_shortcut_cb)               },
    { NOLPHIN_ACTION_EMPTY_TRASH_CONDITIONAL, NULL,                   N_("_Papierkorb leeren"),        NULL, NULL, G_CALLBACK (empty_trash_cb)                 },
    { NOLPHIN_ACTION_PROPERTIES,              NULL,                   N_("_Eigenschaften"),          NULL, NULL, G_CALLBACK (properties_cb)                  },
};

static void
rebuild_menu (NolphinPlacesSidebar *sidebar)
{
    if (!gtk_widget_get_realized (GTK_WIDGET (sidebar))) {
        return;
    }

    if (!sidebar->actions_need_update) {
        return;
    }

    clear_ui (sidebar);

    nolphin_ui_prepare_merge_ui (sidebar->ui_manager,
                              "NolphinPlacesSidebarBookmarkActions",
                              &sidebar->bookmark_action_group_merge_id,
                              &sidebar->bookmark_action_group);

    nolphin_ui_prepare_merge_ui (sidebar->ui_manager,
                              "NolphinPlacesSidebarActionActions",
                              &sidebar->action_action_group_merge_id,
                              &sidebar->action_action_group);

    sidebar->bookmark_action_group_merge_id =
            gtk_ui_manager_add_ui_from_resource (sidebar->ui_manager, "/org/nolphin/nolphin-places-sidebar-ui.xml", NULL);

    gtk_action_group_add_actions (sidebar->bookmark_action_group,
                                  bookmark_action_entries,
                                  G_N_ELEMENTS (bookmark_action_entries),
                                  sidebar);

    nolphin_action_manager_iterate_actions (sidebar->action_manager,
                                         (NolphinActionManagerIterFunc) add_action_to_ui,
                                         sidebar);

    if (sidebar->popup_menu == NULL) {
        GtkWidget *menu = gtk_ui_manager_get_widget (sidebar->ui_manager, "/selection");
        gtk_menu_set_screen (GTK_MENU (menu), gtk_widget_get_screen (GTK_WIDGET (sidebar->window)));
        sidebar->popup_menu = menu;

#if GTK_CHECK_VERSION (3, 24, 8)
        g_signal_connect (sidebar->popup_menu, "realize",
                          G_CALLBACK (popup_menu_realized),
                          sidebar);
        gtk_widget_realize (sidebar->popup_menu);
#endif

        gtk_widget_show (menu);
    }

    sidebar->actions_need_update = FALSE;
}

static gboolean
bookmarks_button_release_event_cb (GtkWidget *widget,
				   GdkEventButton *event,
				   NolphinPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	gboolean res;

	path = NULL;

	if (event->type != GDK_BUTTON_RELEASE) {
		return TRUE;
	}

	if (event->button == GDK_BUTTON_PRIMARY && clicked_eject_button (sidebar, &path)) {
		eject_or_unmount_bookmark (sidebar, path);
		gtk_tree_path_free (path);

		return FALSE;
	}

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (event->button == GDK_BUTTON_PRIMARY) {

		if (event->window != gtk_tree_view_get_bin_window (tree_view)) {
			return FALSE;
		}

		res = gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
						     &path, NULL, NULL, NULL);

		if (!res) {
			return FALSE;
		}

		gtk_tree_model_get_iter (model, &iter, path);

		open_selected_bookmark (sidebar, model, &iter, 0);

		gtk_tree_path_free (path);
	}

	return FALSE;
}

static gboolean
bookmarks_button_press_event_cb (GtkWidget             *widget,
				 GdkEventButton        *event,
				 NolphinPlacesSidebar *sidebar)

{
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;
	gboolean retval = FALSE;
	PlaceType row_type;

	if (event->type != GDK_BUTTON_PRESS) {
		/* ignore multiple clicks */
		return TRUE;
	}

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);
	gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
				       &path, NULL, NULL, NULL);

	if (path == NULL || !gtk_tree_model_get_iter (model, &iter, path)) {
		return FALSE;
	}

	if (event->button == GDK_BUTTON_SECONDARY) {
		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
				    -1);

		if (row_type != PLACES_HEADING) {
			bookmarks_popup_menu (sidebar, event);
		}
	} else if (event->button == GDK_BUTTON_MIDDLE) {
		NolphinWindowOpenFlags flags = 0;

		if (g_settings_get_boolean (nolphin_preferences,
					    NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER)) {
			flags = (event->state & GDK_CONTROL_MASK) ?
				NOLPHIN_WINDOW_OPEN_FLAG_NEW_WINDOW :
				NOLPHIN_WINDOW_OPEN_FLAG_NEW_TAB;
		} else {
			flags = NOLPHIN_WINDOW_OPEN_FLAG_CLOSE_BEHIND;
		}

		open_selected_bookmark (sidebar, model, &iter, flags);
		retval = TRUE;
	}

	gtk_tree_path_free (path);

	return retval;
}

typedef struct {
    NolphinPlacesSidebar *sidebar;
    GtkTreePath *hovered_path;
} ClearHoverData;

static gboolean
clear_eject_hover (GtkTreeModel *model,
                    GtkTreePath  *path,
                    GtkTreeIter  *iter,
                    gpointer      data)
{
    ClearHoverData *hdata = data;
    gint size;
    gboolean can_eject = FALSE;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_EJECT, &can_eject,
                        -1);

    if (can_eject && hdata->hovered_path != NULL && gtk_tree_path_compare (path, hdata->hovered_path) == 0) {
        size = EJECT_ICON_SIZE_HOVERED;

    } else {
        size = EJECT_ICON_SIZE_NOT_HOVERED;
    }

    gtk_tree_store_set (hdata->sidebar->store, iter,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, size,
                        -1);

    return FALSE;
}

static gboolean
motion_notify_cb (GtkWidget         *widget,
                  GdkEventMotion    *event,
                  NolphinPlacesSidebar *sidebar)
{
    GtkTreeModel *model;
    GtkTreePath *path = NULL;
    GtkTreePath *store_path = NULL;
    gboolean editing;

    if (event->type != GDK_MOTION_NOTIFY) {
        return TRUE;
    }

    g_object_get (sidebar->editable_renderer, "editing", &editing, NULL);
    if (editing) {
        return GDK_EVENT_PROPAGATE;
    }

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (sidebar->tree_view));

    if (over_eject_button (sidebar, event->x, event->y, &path)) {
        store_path = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (model), path);
    }

    ClearHoverData data = { sidebar, store_path };
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store), (GtkTreeModelForeachFunc) clear_eject_hover, &data);

    if (store_path != NULL) {
        gtk_tree_path_free (store_path);
    }

    gtk_tree_path_free (path);

    return FALSE;
}

static gboolean
leave_notify_cb (GtkWidget         *widget,
                 GdkEventCrossing  *event,
                 NolphinPlacesSidebar *sidebar)
{
    gboolean editing;

    if (event->type != GDK_LEAVE_NOTIFY) {
        return TRUE;
    }

    g_object_get (sidebar->editable_renderer, "editing", &editing, NULL);
    if (editing) {
        return GDK_EVENT_PROPAGATE;
    }

    ClearHoverData data = { sidebar, NULL };
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store), (GtkTreeModelForeachFunc) clear_eject_hover, &data);

    return FALSE;
}


static gboolean
query_tooltip_callback (GtkWidget *widget,
                        gint x,
                        gint y,
                        gboolean kb_mode,
                        GtkTooltip *tooltip,
                        gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreePath *path = NULL;
    GtkTreeModel *model;

    if (gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y,
                                           kb_mode,
                                           &model, &path, &iter)) {
        gboolean can_eject;
        gint icon_size, type;

        gtk_tree_model_get (model,
                            &iter,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                            PLACES_SIDEBAR_COLUMN_EJECT, &can_eject,
                            // HACK: If we store a bool 'hovered' we still need to have an
                            // icon size column or else make a cell_data_func to render the icon
                            // manually. This is simpler.
                            PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, &icon_size,
                            -1);

        if (type == PLACES_HEADING) {
            gtk_tree_path_free (path);
            return FALSE;
        }

        g_autofree gchar *tooltip_markup = NULL;

        if (can_eject && icon_size == EJECT_ICON_SIZE_HOVERED) {
            g_autoptr(GMount) mount = NULL;
            g_autoptr(GDrive) drive = NULL;
            g_autoptr(GVolume) volume = NULL;

            gtk_tree_model_get (model,
                                &iter,
                                PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                                PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                                PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                                -1);
            if (mount != NULL) {
                tooltip_markup = g_strdup (_("Aushängen"));
            }
            else
            if (drive != NULL) {
                tooltip_markup = g_strdup (_("Auswerfen"));
            }
            else
            if (volume != NULL) {
                tooltip_markup = g_strdup (_("Anhalten"));
            }
        } else {
            gtk_tree_model_get (model,
                                &iter,
                                PLACES_SIDEBAR_COLUMN_TOOLTIP, &tooltip_markup,
                                -1);
        }

        gtk_tooltip_set_markup (tooltip, tooltip_markup);
        gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip, path, NULL, NULL);

        gtk_tree_path_free (path);
        return TRUE;
    }

    return FALSE;
}


static void
update_expanded_state (GtkTreeView *tree_view,
                       GtkTreeIter *iter,
                       GtkTreePath *path,
                           gpointer user_data,
                           gboolean expanded)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);

    if (sidebar->updating_sidebar)
        return;

    SectionType type;
    GtkTreeIter heading_iter;
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &heading_iter, path);
    gtk_tree_model_get (model, iter,
                    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &type,
                    -1);
    if (type == SECTION_COMPUTER) {
        sidebar->my_computer_expanded = expanded;
        g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_MY_COMPUTER_EXPANDED, expanded);
    } else if (type == SECTION_BOOKMARKS) {
        sidebar->bookmarks_expanded = expanded;
        g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_BOOKMARKS_EXPANDED, expanded);
    } else if (type == SECTION_DEVICES) {
        sidebar->devices_expanded = expanded;
        g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_DEVICES_EXPANDED, expanded);
    } else if (type == SECTION_NETWORK) {
        sidebar->network_expanded = expanded;
        g_settings_set_boolean (nolphin_window_state, NOLPHIN_WINDOW_STATE_NETWORK_EXPANDED, expanded);
    }
}

static void
row_collapsed_cb (GtkTreeView *tree_view,
                              GtkTreeIter *iter,
                              GtkTreePath *path,
                              gpointer user_data)
{
    update_expanded_state (tree_view,
                           iter,
                           path,
                           user_data,
                           FALSE);
}


static void
row_expanded_cb (GtkTreeView *tree_view,
                              GtkTreeIter *iter,
                              GtkTreePath *path,
                              gpointer user_data)
{
    update_expanded_state (tree_view,
                           iter,
                           path,
                           user_data,
                           TRUE);
}

static void
row_activated_cb (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  gpointer           user_data)
{
    GtkTreeIter iter;
    SectionType section_type;
    PlaceType place_type;

    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (user_data);
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
    gtk_tree_model_get (model, &iter,
                    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                    -1);
    if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_COMPUTER) {
            sidebar->my_computer_expanded = !sidebar->my_computer_expanded;
        } else if (section_type == SECTION_BOOKMARKS) {
            sidebar->bookmarks_expanded = !sidebar->bookmarks_expanded;
        } else if (section_type == SECTION_DEVICES) {
            sidebar->devices_expanded = !sidebar->devices_expanded;
        } else if (section_type == SECTION_NETWORK) {
            sidebar->network_expanded = !sidebar->network_expanded;
        }
        restore_expand_state (sidebar);
    }
}

static void
bookmarks_edited (GtkCellRenderer       *cell,
		  gchar                 *path_string,
		  gchar                 *new_text,
		  NolphinPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	NolphinBookmark *bookmark;
	int index;

	g_object_set (cell, "editable", FALSE, NULL);

	path = gtk_tree_path_new_from_string (path_string);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store_filter), &iter, path);
	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
		            PLACES_SIDEBAR_COLUMN_INDEX, &index,
		            -1);
	gtk_tree_path_free (path);
	bookmark = nolphin_bookmark_list_item_at (sidebar->bookmarks, index);

	if (bookmark != NULL) {
		nolphin_bookmark_set_custom_name (bookmark, new_text);
	}
}

static void
bookmarks_editing_canceled (GtkCellRenderer       *cell,
			    NolphinPlacesSidebar *sidebar)
{
	g_object_set (cell, "editable", FALSE, NULL);
}

static void
trash_state_changed_cb (NolphinTrashMonitor *trash_monitor,
			gboolean             state,
			gpointer             data)
{
	NolphinPlacesSidebar *sidebar;

	sidebar = NOLPHIN_PLACES_SIDEBAR (data);

	/* The trash icon changed, update the sidebar */
	update_places (sidebar);

	reset_menu (sidebar);
}

#ifdef HAVE_XAPP
static void
favorites_changed_cb (gpointer data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (data);

    update_places (sidebar);
}
#endif

static gboolean
tree_selection_func (GtkTreeSelection *selection,
		     GtkTreeModel *model,
		     GtkTreePath *path,
		     gboolean path_currently_selected,
		     gpointer user_data)
{
	GtkTreeIter iter;
	PlaceType row_type;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
			    -1);

	if (row_type == PLACES_HEADING) {
		return FALSE;
	}

	return TRUE;
}

static void
icon_cell_renderer_func (GtkTreeViewColumn *column,
			 GtkCellRenderer *cell,
			 GtkTreeModel *model,
			 GtkTreeIter *iter,
			 gpointer user_data)
{
	PlaceType type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type == PLACES_HEADING) {
		g_object_set (cell,
			      "visible", FALSE,
			      NULL);
	} else {
		g_object_set (cell,
			      "visible", TRUE,
                  "xpad", 3,
                  "ypad", 2,
			      NULL);
	}
}

static void
padding_cell_renderer_func (GtkTreeViewColumn *column,
			    GtkCellRenderer *cell,
			    GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer user_data)
{
	PlaceType type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type == PLACES_HEADING) {
		g_object_set (cell,
			      "visible", FALSE,
			      "xpad", 0,
			      "ypad", 0,
			      NULL);
	} else {
		g_object_set (cell,
			      "visible", TRUE,
			      "ypad", 3,
			      NULL);
	}
}

static void
heading_cell_renderer_func (GtkTreeViewColumn *column,
			    GtkCellRenderer *cell,
			    GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer user_data)
{
	PlaceType type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type == PLACES_HEADING) {
		g_object_set (cell,
			      "visible", TRUE,
			      NULL);
	} else {
		g_object_set (cell,
			      "visible", FALSE,
			      NULL);
	}
}

static gboolean
row_visibility_function (GtkTreeModel *model,
                         GtkTreeIter  *iter,
                         gpointer      data)
{
    NolphinPlacesSidebar *sidebar = NOLPHIN_PLACES_SIDEBAR (data);

    SectionType section_type;
    PlaceType type;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        -1);

    if (type != PLACES_HEADING || section_type != SECTION_BOOKMARKS)
        return TRUE;

    if (sidebar->in_drag)
        return TRUE;

    if ((int)nolphin_bookmark_list_length (sidebar->bookmarks) > sidebar->bookmark_breakpoint)
        return TRUE;

    return FALSE;
}

/* Zeichnet beim Eintrag „Handy“ einen grünen Punkt, solange die Handy-App angemeldet ist. */
static void
name_cell_data_func (GtkTreeViewColumn *column,
		     GtkCellRenderer   *cell,
		     GtkTreeModel      *model,
		     GtkTreeIter       *iter,
		     gpointer           user_data)
{
	NolphinPlacesSidebar *sidebar = user_data;
	gchar *uri = NULL, *name = NULL;

	gtk_tree_model_get (model, iter, PLACES_SIDEBAR_COLUMN_URI, &uri, PLACES_SIDEBAR_COLUMN_NAME, &name, -1);
	if (sidebar->phone_connected && uri != NULL && name != NULL && sidebar->phone_uri != NULL &&
	    strcmp (uri, sidebar->phone_uri) == 0) {
		gchar *escaped = g_markup_escape_text (name, -1);
		gchar *markup = g_strdup_printf ("%s  <span foreground=\"#2ecc71\">●</span>", escaped);

		g_object_set (cell, "markup", markup, NULL);
		g_free (markup);
		g_free (escaped);
	} else {
		g_object_set (cell, "attributes", NULL, NULL);
	}
	g_free (uri);
	g_free (name);
}

/* Zeichnet den Eintrag „Handy“ neu, sobald sich ändert, ob die Handy-App angemeldet ist. */
static gboolean
phone_status_tick (gpointer user_data)
{
	NolphinPlacesSidebar *sidebar = user_data;

	if (nolphin_phone_is_connected () != sidebar->phone_connected) {
		update_places (sidebar);
	}
	return G_SOURCE_CONTINUE;
}

static void
nolphin_places_sidebar_init (NolphinPlacesSidebar *sidebar)
{
	GtkTreeView       *tree_view;
	GtkTreeViewColumn *primary_column, *expander_column, *expander_pad_column;
	GtkCellRenderer   *cell;
	GtkTreeSelection  *selection;
	GtkStyleContext   *style_context;

    sidebar->action_manager = nolphin_action_manager_new ();
    sidebar->actions_changed_id = g_signal_connect_swapped (sidebar->action_manager,
                                                            "changed",
                                                            G_CALLBACK (actions_changed),
                                                            sidebar);
    sidebar->ui_manager = gtk_ui_manager_new ();

    sidebar->in_drag = FALSE;

    sidebar->desktop_dnd_source_fs = NULL;
    sidebar->desktop_dnd_can_delete_source = FALSE;

	sidebar->volume_monitor = g_volume_monitor_get ();

    sidebar->update_places_on_idle_id = 0;

    sidebar->my_computer_expanded = g_settings_get_boolean (nolphin_window_state,
                                                            NOLPHIN_WINDOW_STATE_MY_COMPUTER_EXPANDED);
    sidebar->bookmarks_expanded = g_settings_get_boolean (nolphin_window_state,
                                                             NOLPHIN_WINDOW_STATE_BOOKMARKS_EXPANDED);
    sidebar->devices_expanded = g_settings_get_boolean (nolphin_window_state,
                                                        NOLPHIN_WINDOW_STATE_DEVICES_EXPANDED);
    sidebar->network_expanded = g_settings_get_boolean (nolphin_window_state,
                                                        NOLPHIN_WINDOW_STATE_NETWORK_EXPANDED);

    gtk_widget_set_size_request (GTK_WIDGET (sidebar), 140, -1);
    sidebar->phone_status_id = g_timeout_add_seconds (3, phone_status_tick, sidebar);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sidebar),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
	gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sidebar), GTK_SHADOW_IN);

	style_context = gtk_widget_get_style_context (GTK_WIDGET (sidebar));
	gtk_style_context_set_junction_sides (style_context, GTK_JUNCTION_RIGHT | GTK_JUNCTION_LEFT);

	/* Make it easier for theme authors to style the sidebar */
	gtk_style_context_add_class (style_context, "nolphin-places-sidebar");
	/* Nolphin ist ein Nemo-Fork: Themes wie Mint-Y gestalten die Orte-Leiste
	 * nur über die Nemo-Klassen. Ohne sie fehlen z. B. Zeilenabstand und Hover. */
	gtk_style_context_add_class (style_context, "nemo-places-sidebar");

  	/* tree view */
	tree_view = GTK_TREE_VIEW (nolphin_places_tree_view_new ());

	gtk_tree_view_set_headers_visible (tree_view, FALSE);

    primary_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (primary_column), NOLPHIN_ICON_SIZE_SMALLER);
    gtk_tree_view_column_set_expand (primary_column, TRUE);

	/* initial padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);

	/* headings */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (primary_column, cell,
					     "text", PLACES_SIDEBAR_COLUMN_HEADING_TEXT,
					     NULL);
	g_object_set (cell,
		      "weight", PANGO_WEIGHT_BOLD,
		      "weight-set", TRUE,
		      "ypad", 0,
		      "xpad", 0,
		      NULL);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 heading_cell_renderer_func,
						 sidebar, NULL);

	/* icon padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 padding_cell_renderer_func,
						 sidebar, NULL);

	/* icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
    g_object_set (cell,
                  "follow-state", TRUE,
                  NULL);
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (primary_column, cell,
					     "gicon", PLACES_SIDEBAR_COLUMN_GICON,
					     NULL);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 icon_cell_renderer_func,
						 sidebar, NULL);

    /* normal text renderer */
    cell = sidebar->editable_renderer = nolphin_cell_renderer_disk_new ();
    NOLPHIN_CELL_RENDERER_DISK (cell)->direction = gtk_widget_get_direction (GTK_WIDGET (tree_view));
    gtk_tree_view_column_pack_start (primary_column, cell, TRUE);
    g_object_set (G_OBJECT (cell), "editable", FALSE, NULL);
    gtk_tree_view_column_set_attributes (primary_column, cell,
                         "text", PLACES_SIDEBAR_COLUMN_NAME,
                         "editable-set", PLACES_SIDEBAR_COLUMN_BOOKMARK,
                         "disk-full-percent", PLACES_SIDEBAR_COLUMN_DF_PERCENT,
                         "show-disk-full-percent", PLACES_SIDEBAR_COLUMN_SHOW_DF,
                         NULL);
    gtk_tree_view_column_set_cell_data_func (primary_column, cell, name_cell_data_func, sidebar, NULL);
    g_object_set (cell,
              "ellipsize", PANGO_ELLIPSIZE_END,
              "ellipsize-set", TRUE,
              NULL);

    g_signal_connect (cell, "edited",
              G_CALLBACK (bookmarks_edited), sidebar);
    g_signal_connect (cell, "editing-canceled",
              G_CALLBACK (bookmarks_editing_canceled), sidebar);

    /* eject column */
    sidebar->eject_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_sizing (sidebar->eject_column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
    gtk_tree_view_column_set_min_width (sidebar->eject_column, EJECT_COLUMN_MIN_WIDTH);
    gtk_tree_view_column_set_max_width (sidebar->eject_column, EJECT_COLUMN_MAX_WIDTH);

    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
    g_object_set (cell, "width", 5, NULL);

	/* eject icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
	sidebar->eject_icon_cell_renderer = cell;
	g_object_set (cell,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      "yalign", 0.8,
              "width", menu_icon_pixels,
		      NULL);
	gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (sidebar->eject_column, cell,
					     "visible", PLACES_SIDEBAR_COLUMN_EJECT,
					     "icon-name", PLACES_SIDEBAR_COLUMN_EJECT_ICON,
                         "stock-size", PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE,
                         NULL);

    /* eject icon trailing padding (adjusts to always avoid overlay-scrollbars) */
    gboolean overlay_scrolling;
    GtkSettings *gtksettings = gtk_settings_get_default ();
    g_object_get (gtksettings,
                  "gtk-overlay-scrolling", &overlay_scrolling,
                  NULL);

    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
    gtk_tree_view_column_set_attributes (sidebar->eject_column, cell,
                                         "visible", PLACES_SIDEBAR_COLUMN_EJECT,
                                         NULL);

    if (overlay_scrolling) {
        GtkWidget *vscrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sidebar));
        gint nat_width;

        gtk_widget_get_preferred_width (vscrollbar, NULL, &nat_width);
        g_object_set (cell, "width", nat_width, NULL);
    } else {
        g_object_set (cell, "width", 2, NULL);
    }

    expander_pad_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new());
    gtk_tree_view_column_set_sizing (expander_pad_column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (expander_pad_column, EXPANDER_PAD_COLUMN_WIDTH);

    expander_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_sizing (expander_column, GTK_TREE_VIEW_COLUMN_FIXED);

    gint expander_size;
    gtk_widget_style_get (GTK_WIDGET (tree_view), "expander-size", &expander_size, NULL);
    gtk_tree_view_column_set_fixed_width (expander_column, expander_size);

    gtk_tree_view_append_column (tree_view, expander_pad_column);
    gtk_tree_view_append_column (tree_view, expander_column);
	gtk_tree_view_append_column (tree_view, primary_column);
    gtk_tree_view_append_column (tree_view, sidebar->eject_column);

    gtk_tree_view_set_expander_column (tree_view, expander_column);

	sidebar->store = nolphin_shortcuts_model_new (sidebar);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sidebar->store),
                                          GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                          GTK_SORT_ASCENDING);

    sidebar->store_filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (sidebar->store), NULL);

    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (sidebar->store_filter),
                                            (GtkTreeModelFilterVisibleFunc) row_visibility_function,
                                            sidebar, NULL);

    gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (sidebar->store_filter));

    gtk_container_add (GTK_CONTAINER (sidebar), GTK_WIDGET (tree_view));
    gtk_widget_show (GTK_WIDGET (tree_view));

	gtk_widget_show (GTK_WIDGET (sidebar));
	sidebar->tree_view = tree_view;

    /* On by default, and its search window positions itself with absolute root
     * coordinates - which a Wayland client cannot know, so it lands on the
     * wrong monitor. */
    gtk_tree_view_set_enable_search (tree_view, FALSE);

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	gtk_tree_selection_set_select_function (selection,
						tree_selection_func,
						sidebar,
						NULL);

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
						GDK_BUTTON1_MASK,
						nolphin_shortcuts_source_targets,
						G_N_ELEMENTS (nolphin_shortcuts_source_targets),
						GDK_ACTION_MOVE);
	gtk_drag_dest_set (GTK_WIDGET (tree_view),
			   0,
			   nolphin_shortcuts_drop_targets, G_N_ELEMENTS (nolphin_shortcuts_drop_targets),
			   GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);

	g_signal_connect (tree_view, "key-press-event",
			  G_CALLBACK (bookmarks_key_press_event_cb), sidebar);

	g_signal_connect (tree_view, "drag-motion",
			  G_CALLBACK (drag_motion_callback), sidebar);
	g_signal_connect (tree_view, "drag-leave",
			  G_CALLBACK (drag_leave_callback), sidebar);
	g_signal_connect (tree_view, "drag-data-received",
			  G_CALLBACK (drag_data_received_callback), sidebar);
	g_signal_connect (tree_view, "drag-drop",
			  G_CALLBACK (drag_drop_callback), sidebar);

	g_signal_connect (selection, "changed",
			  G_CALLBACK (bookmarks_selection_changed_cb), sidebar);
	g_signal_connect (tree_view, "popup-menu",
			  G_CALLBACK (bookmarks_popup_menu_cb), sidebar);
	g_signal_connect (tree_view, "button-press-event",
			  G_CALLBACK (bookmarks_button_press_event_cb), sidebar);
	g_signal_connect (tree_view, "button-release-event",
			  G_CALLBACK (bookmarks_button_release_event_cb), sidebar);
    g_signal_connect (tree_view, "row-expanded",
              G_CALLBACK (row_expanded_cb), sidebar);
    g_signal_connect (tree_view, "row-collapsed",
              G_CALLBACK (row_collapsed_cb), sidebar);
    g_signal_connect (tree_view, "row-activated",
              G_CALLBACK (row_activated_cb), sidebar);
    g_signal_connect (tree_view, "motion-notify-event",
              G_CALLBACK (motion_notify_cb), sidebar);
    g_signal_connect (tree_view, "leave-notify-event",
              G_CALLBACK (leave_notify_cb), sidebar);

    g_signal_connect_object (GTK_WIDGET (tree_view), "query-tooltip",
                             G_CALLBACK (query_tooltip_callback), sidebar, 0);
    gtk_widget_set_has_tooltip (GTK_WIDGET (tree_view), TRUE);

	g_signal_connect_swapped (nolphin_preferences, "changed::" NOLPHIN_PREFERENCES_DESKTOP_IS_HOME_DIR,
				  G_CALLBACK(desktop_setting_changed_callback),
				  sidebar);

	g_signal_connect_swapped (nolphin_desktop_preferences, "changed::" NOLPHIN_PREFERENCES_SHOW_DESKTOP,
				  G_CALLBACK(desktop_setting_changed_callback),
				  sidebar);

    g_signal_connect_swapped (cinnamon_privacy_preferences, "changed::" NOLPHIN_PREFERENCES_RECENT_ENABLED,
                  G_CALLBACK(desktop_setting_changed_callback),
                  sidebar);

	g_signal_connect_object (nolphin_trash_monitor_get (),
				 "trash_state_changed",
				 G_CALLBACK (trash_state_changed_cb),
				 sidebar, 0);

#ifdef HAVE_XAPP
    g_signal_connect_swapped (xapp_favorites_get_default (),
                              "changed",
                              G_CALLBACK (favorites_changed_cb),
                              sidebar);
#endif
}

static void
nolphin_places_sidebar_dispose (GObject *object)
{
	NolphinPlacesSidebar *sidebar;

	sidebar = NOLPHIN_PLACES_SIDEBAR (object);

	g_clear_handle_id (&sidebar->phone_status_id, g_source_remove);
	g_clear_pointer (&sidebar->phone_uri, g_free);
	g_clear_pointer (&sidebar->phone_inbox_uri, g_free);
	sidebar->window = NULL;
	sidebar->tree_view = NULL;

	g_free (sidebar->uri);
	sidebar->uri = NULL;

	free_drag_data (sidebar);

    g_clear_handle_id (&sidebar->actions_changed_idle_id, g_source_remove);

	if (sidebar->bookmarks_changed_id != 0) {
		g_signal_handler_disconnect (sidebar->bookmarks,
					     sidebar->bookmarks_changed_id);
		sidebar->bookmarks_changed_id = 0;
	}

    if (sidebar->actions_changed_id != 0) {
        g_signal_handler_disconnect (sidebar->action_manager,
                                     sidebar->actions_changed_id);
        sidebar->actions_changed_id = 0;
    }

    g_clear_object (&sidebar->action_manager);

    if (sidebar->update_places_on_idle_id != 0) {
        g_source_remove (sidebar->update_places_on_idle_id);
        sidebar->update_places_on_idle_id = 0;
    }

	g_clear_object (&sidebar->store);

    g_clear_pointer (&sidebar->top_bookend_uri, g_free);
    g_clear_pointer (&sidebar->bottom_bookend_uri, g_free);

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}

    g_signal_handlers_disconnect_by_func (nolphin_window_state,
                                          breakpoint_changed_cb,
                                          sidebar);

	g_signal_handlers_disconnect_by_func (nolphin_preferences,
					      desktop_setting_changed_callback,
					      sidebar);

	g_signal_handlers_disconnect_by_func (gnome_background_preferences,
					      desktop_setting_changed_callback,
					      sidebar);

    g_signal_handlers_disconnect_by_func (cinnamon_privacy_preferences,
                          desktop_setting_changed_callback,
                          sidebar);

#ifdef HAVE_XAPP
    g_signal_handlers_disconnect_by_func (xapp_favorites_get_default (),
                                          favorites_changed_cb,
                                          sidebar);
#endif

	if (sidebar->volume_monitor != NULL) {
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_disconnected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_connected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_changed_callback, sidebar);

		g_clear_object (&sidebar->volume_monitor);
	}

	G_OBJECT_CLASS (nolphin_places_sidebar_parent_class)->dispose (object);
}

static void
nolphin_places_sidebar_class_init (NolphinPlacesSidebarClass *class)
{
    GObjectClass *oclass = G_OBJECT_CLASS (class);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

    oclass->dispose = nolphin_places_sidebar_dispose;

	widget_class->style_set = nolphin_places_sidebar_style_set;
	widget_class->focus = nolphin_places_sidebar_focus;

    gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &menu_icon_pixels, NULL);
    EJECT_ICON_SIZE_NOT_HOVERED = gtk_icon_size_register ("menu-icon-size-small",
                                                          menu_icon_pixels - EJECT_ICON_SIZE_REDUCTION,
                                                          menu_icon_pixels - EJECT_ICON_SIZE_REDUCTION);
}

static gboolean
update_places_on_idle_callback (NolphinPlacesSidebar *sidebar)
{
    sidebar->update_places_on_idle_id = 0;

    update_places (sidebar);

    return FALSE;
}

static void
update_places_on_idle (NolphinPlacesSidebar *sidebar)
{
    if (sidebar->update_places_on_idle_id != 0) {
        g_source_remove (sidebar->update_places_on_idle_id);
        sidebar->update_places_on_idle_id = 0;
    }

    sidebar->update_places_on_idle_id = g_idle_add_full (G_PRIORITY_LOW,
                                                         (GSourceFunc) update_places_on_idle_callback,
                                                         sidebar, NULL);
}

static void
nolphin_places_sidebar_set_parent_window (NolphinPlacesSidebar *sidebar,
					   NolphinWindow *window)
{
	NolphinWindowSlot *slot;
    gint breakpoint;

	sidebar->window = window;

	slot = nolphin_window_get_active_slot (window);

	sidebar->bookmarks = nolphin_bookmark_list_get_default ();
	sidebar->uri = nolphin_window_slot_get_current_uri (slot);

    breakpoint = g_settings_get_int (nolphin_window_state, NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT);

    if (breakpoint < 0) {     // Default gsettings value is -1 (which translates to 'not previously set')
        breakpoint = nolphin_bookmark_list_length (sidebar->bookmarks);
        g_settings_set_int (nolphin_window_state, NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, breakpoint);
    }

    sidebar->bookmark_breakpoint = breakpoint;
    g_signal_connect_swapped (nolphin_window_state, "changed::" NOLPHIN_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT,
                              G_CALLBACK (breakpoint_changed_cb), sidebar);

	sidebar->bookmarks_changed_id =
		g_signal_connect_swapped (sidebar->bookmarks, "changed",
					  G_CALLBACK (update_places_on_idle),
					  sidebar);

	g_signal_connect_object (window, "loading_uri",
				 G_CALLBACK (loading_uri_callback),
				 sidebar, 0);

	g_signal_connect_object (sidebar->volume_monitor, "volume_added",
				 G_CALLBACK (volume_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume_removed",
				 G_CALLBACK (volume_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume_changed",
				 G_CALLBACK (volume_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_added",
				 G_CALLBACK (mount_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_removed",
				 G_CALLBACK (mount_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_changed",
				 G_CALLBACK (mount_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_disconnected",
				 G_CALLBACK (drive_disconnected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_connected",
				 G_CALLBACK (drive_connected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_changed",
				 G_CALLBACK (drive_changed_callback), sidebar, 0);

	g_signal_connect_swapped (nolphin_preferences, "changed::" NOLPHIN_PREFERENCES_ALWAYS_USE_BROWSER,
				 G_CALLBACK (reset_menu), sidebar);
	update_places (sidebar);
}

static void
nolphin_places_sidebar_style_set (GtkWidget *widget,
				   GtkStyle  *previous_style)
{
	NolphinPlacesSidebar *sidebar;

	sidebar = NOLPHIN_PLACES_SIDEBAR (widget);
	update_places (sidebar);
}

GtkWidget *
nolphin_places_sidebar_new (NolphinWindow *window)
{
	NolphinPlacesSidebar *sidebar;

	sidebar = g_object_new (NOLPHIN_TYPE_PLACES_SIDEBAR, NULL);
	nolphin_places_sidebar_set_parent_window (sidebar, window);

	return GTK_WIDGET (sidebar);
}


/* Drag and drop interfaces */

/* GtkTreeDragSource::row_draggable implementation for the shortcuts filter model */

static gboolean
nolphin_shortcuts_model_row_draggable (GtkTreeDragSource *drag_source,
					GtkTreePath       *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	PlaceType place_type;
	SectionType section_type;

	model = GTK_TREE_MODEL (drag_source);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
			    -1);

	if (place_type != PLACES_HEADING &&
        (section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS))
		return TRUE;

	return FALSE;
}

static void
_nolphin_shortcuts_model_class_init (NolphinShortcutsModelClass *klass)
{

}

static void
_nolphin_shortcuts_model_init (NolphinShortcutsModel *model)
{
	model->sidebar = NULL;
}

static void
_nolphin_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface)
{
	iface->row_draggable = nolphin_shortcuts_model_row_draggable;
}

static GtkTreeStore *
nolphin_shortcuts_model_new (NolphinPlacesSidebar *sidebar)
{
	NolphinShortcutsModel *model;
	GType model_types[PLACES_SIDEBAR_COLUMN_COUNT] = {
        G_TYPE_INT,
		G_TYPE_STRING,
		G_TYPE_DRIVE,
		G_TYPE_VOLUME,
		G_TYPE_MOUNT,
		G_TYPE_STRING,
		G_TYPE_ICON,
		G_TYPE_INT,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_STRING,
		G_TYPE_STRING,
        G_TYPE_INT,
		G_TYPE_INT,
		G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_BOOLEAN
	};

	model = g_object_new (NOLPHIN_TYPE_SHORTCUTS_MODEL, NULL);
	model->sidebar = sidebar;

	gtk_tree_store_set_column_types (GTK_TREE_STORE (model),
					 PLACES_SIDEBAR_COLUMN_COUNT,
					 model_types);

	return GTK_TREE_STORE (model);
}
