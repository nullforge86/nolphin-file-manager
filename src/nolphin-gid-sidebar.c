/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nolphin-gid-sidebar.c: Bereich "GID-Projekte" der Seitenleiste
 */

#include <config.h>

#include "nolphin-gid-sidebar.h"
#include "nolphin-gid-projects.h"
#include "nolphin-workspace-panel.h"

#include <libnolphin-private/nolphin-global-preferences.h>

#include <glib/gi18n.h>

typedef struct {
	NolphinWindow *window;
	GtkWidget     *list;
	GtkWidget     *scroller;   /* enthält @list */
	GtkWidget     *empty_label;
	gchar         *selected;   /* zuletzt gezeigtes Projekt */
} GidSidebar;

static void
gid_sidebar_free (gpointer data)
{
	GidSidebar *sb = data;

	g_free (sb->selected);
	g_free (sb);
}

static GtkWidget *
make_row (const gchar *path)
{
	GtkWidget *row = gtk_list_box_row_new ();
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *icon, *name;
	gchar *base = g_path_get_basename (path);
	gchar *branch = nolphin_gid_project_get_branch (path);
	gboolean available = g_file_test (path, G_FILE_TEST_IS_DIR);

	gtk_container_set_border_width (GTK_CONTAINER (box), 4);
	icon = gtk_image_new_from_icon_name (available ? "folder-symbolic" : "dialog-warning-symbolic",
					     GTK_ICON_SIZE_MENU);
	gtk_box_pack_start (GTK_BOX (box), icon, FALSE, FALSE, 0);

	name = gtk_label_new (base);
	gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
	gtk_widget_set_halign (name, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), name, TRUE, TRUE, 0);

	if (!available) {
		gtk_style_context_add_class (gtk_widget_get_style_context (name), "dim-label");
		gtk_widget_set_tooltip_text (row, _("Der Projektordner ist nicht verfügbar."));
	} else {
		gtk_widget_set_tooltip_text (row, path);
	}
	if (branch != NULL) {
		GtkWidget *b = gtk_label_new (branch);

		gtk_label_set_ellipsize (GTK_LABEL (b), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (b), 14);
		gtk_style_context_add_class (gtk_widget_get_style_context (b), "dim-label");
		gtk_box_pack_end (GTK_BOX (box), b, FALSE, FALSE, 0);
	}
	gtk_container_add (GTK_CONTAINER (row), box);
	g_object_set_data_full (G_OBJECT (row), "gid-project-path", g_strdup (path), g_free);
	gtk_widget_show_all (row);
	g_free (branch);
	g_free (base);
	return row;
}

static void
rebuild (GidSidebar *sb)
{
	GList *children, *l;
	gchar **projects = nolphin_gid_projects_get ();
	guint i;
	GtkListBoxRow *to_select = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (sb->list));
	for (l = children; l != NULL; l = l->next)
		gtk_widget_destroy (l->data);
	g_list_free (children);

	for (i = 0; projects[i] != NULL; i++) {
		GtkWidget *row = make_row (projects[i]);

		gtk_list_box_insert (GTK_LIST_BOX (sb->list), row, -1);
		if (g_strcmp0 (projects[i], sb->selected) == 0)
			to_select = GTK_LIST_BOX_ROW (row);
	}
	if (to_select != NULL)
		gtk_list_box_select_row (GTK_LIST_BOX (sb->list), to_select);
	gtk_widget_set_visible (sb->empty_label, projects[0] == NULL);
	gtk_widget_set_visible (sb->scroller, projects[0] != NULL);
	g_strfreev (projects);
}

static void
show_project (GidSidebar *sb, const gchar *path)
{
	GFile *folder = g_file_new_for_path (path);

	g_free (sb->selected);
	sb->selected = g_strdup (path);
	nolphin_workspace_panel_show_gid (nolphin_window_get_workspace_panel (sb->window), sb->window, folder);
	g_object_unref (folder);
}

static void
on_row_activated (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
	GidSidebar *sb = user_data;
	const gchar *path = g_object_get_data (G_OBJECT (row), "gid-project-path");

	if (path != NULL) {
		gchar *copy = g_strdup (path);

		show_project (sb, copy);
		rebuild (sb); /* aktualisiert u. a. die Branch-Anzeige */
		g_free (copy);
	}
}

static void
on_show_changed (GSettings *settings, const gchar *key, gpointer user_data)
{
	gtk_widget_set_visible (GTK_WIDGET (user_data), g_settings_get_boolean (settings, key));
}

static void
on_projects_changed (GSettings *settings, const gchar *key, gpointer user_data)
{
	GidSidebar *sb = g_object_get_data (G_OBJECT (user_data), "gid-sidebar-data");

	if (sb != NULL)
		rebuild (sb);
}

/* --- Kontextmenü ------------------------------------------------------------------ */

static GidSidebar *
item_sidebar (GtkMenuItem *item)
{
	return g_object_get_data (G_OBJECT (item), "gid-sidebar");
}

static const gchar *
item_path (GtkMenuItem *item)
{
	return g_object_get_data (G_OBJECT (item), "gid-path");
}

static void
on_menu_show_readme (GtkMenuItem *item, gpointer user_data)
{
	show_project (item_sidebar (item), item_path (item));
}

static void
on_menu_open_folder (GtkMenuItem *item, gpointer user_data)
{
	GFile *folder = g_file_new_for_path (item_path (item));

	nolphin_window_go_to (item_sidebar (item)->window, folder);
	g_object_unref (folder);
}

static void
on_menu_remove (GtkMenuItem *item, gpointer user_data)
{
	GidSidebar *sb = item_sidebar (item);
	gchar *path = g_strdup (item_path (item));

	if (g_strcmp0 (sb->selected, path) == 0)
		g_clear_pointer (&sb->selected, g_free);
	nolphin_gid_projects_remove (path);
	g_free (path);
}

static void
on_menu_add (GtkMenuItem *item, gpointer user_data)
{
	nolphin_gid_choose_and_add_project (((GidSidebar *) user_data)->window);
}

static void
add_project_item (GtkWidget *menu, const gchar *label, GCallback cb, GidSidebar *sb, const gchar *path)
{
	GtkWidget *item = gtk_menu_item_new_with_mnemonic (label);

	g_object_set_data (G_OBJECT (item), "gid-sidebar", sb);
	g_object_set_data_full (G_OBJECT (item), "gid-path", g_strdup (path), g_free);
	g_signal_connect (item, "activate", cb, NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

/* Das Menü erst nach der Aktivierung des Eintrags zerstören. */
static gboolean
destroy_menu_idle (gpointer menu)
{
	gtk_widget_destroy (GTK_WIDGET (menu));
	g_object_unref (menu);
	return G_SOURCE_REMOVE;
}

static void
on_menu_done (GtkMenuShell *menu, gpointer user_data)
{
	g_idle_add (destroy_menu_idle, g_object_ref (menu));
}

static void
popup_for_row (GidSidebar *sb, GtkListBoxRow *row, GdkEvent *event)
{
	GtkWidget *menu = gtk_menu_new ();
	const gchar *path = row != NULL ? g_object_get_data (G_OBJECT (row), "gid-project-path") : NULL;

	if (path != NULL) {
		add_project_item (menu, _("README an_zeigen"), G_CALLBACK (on_menu_show_readme), sb, path);
		add_project_item (menu, _("Im Dateimanager _öffnen"), G_CALLBACK (on_menu_open_folder), sb, path);
		add_project_item (menu, _("Aus GID-Projekten _entfernen"), G_CALLBACK (on_menu_remove), sb, path);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
	}
	{
		GtkWidget *add = gtk_menu_item_new_with_mnemonic (_("Projekt _hinzufügen …"));

		g_signal_connect (add, "activate", G_CALLBACK (on_menu_add), sb);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), add);
	}
	gtk_widget_show_all (menu);
	g_signal_connect (menu, "selection-done", G_CALLBACK (on_menu_done), NULL);
	gtk_menu_popup_at_pointer (GTK_MENU (menu), event);
}

static gboolean
on_button_press (GtkWidget *list, GdkEventButton *event, gpointer user_data)
{
	GidSidebar *sb = user_data;

	if (event->button != GDK_BUTTON_SECONDARY)
		return GDK_EVENT_PROPAGATE;
	popup_for_row (sb, gtk_list_box_get_row_at_y (GTK_LIST_BOX (list), (gint) event->y), (GdkEvent *) event);
	return GDK_EVENT_STOP;
}

static gboolean
on_popup_menu (GtkWidget *list, gpointer user_data)
{
	popup_for_row (user_data, gtk_list_box_get_selected_row (GTK_LIST_BOX (list)), NULL);
	return TRUE;
}

static void
on_add_clicked (GtkButton *button, gpointer user_data)
{
	nolphin_gid_choose_and_add_project (((GidSidebar *) user_data)->window);
}

void
nolphin_gid_choose_and_add_project (NolphinWindow *window)
{
	GtkWidget *dialog;

	g_return_if_fail (NOLPHIN_IS_WINDOW (window));

	dialog = gtk_file_chooser_dialog_new (_("GID-Projekt hinzufügen"), GTK_WINDOW (window),
					      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
					      _("_Abbrechen"), GTK_RESPONSE_CANCEL,
					      _("_Hinzufügen"), GTK_RESPONSE_ACCEPT, NULL);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

		if (path != NULL && !nolphin_gid_projects_add (path)) {
			GtkWidget *msg = gtk_message_dialog_new (GTK_WINDOW (window), GTK_DIALOG_MODAL,
								 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
								 _("Dieser Ordner ist bereits ein GID-Projekt oder nicht verfügbar."));
			gtk_dialog_run (GTK_DIALOG (msg));
			gtk_widget_destroy (msg);
		}
		g_free (path);
	}
	gtk_widget_destroy (dialog);
}

GtkWidget *
nolphin_gid_sidebar_new (NolphinWindow *window)
{
	GidSidebar *sb = g_new0 (GidSidebar, 1);
	GtkWidget *expander, *content, *add;

	sb->window = window;

	expander = gtk_expander_new (_("GID-Projekte"));
	gtk_expander_set_expanded (GTK_EXPANDER (expander), TRUE);
	gtk_widget_set_margin_start (expander, 8);
	gtk_widget_set_margin_top (expander, 6);
	gtk_widget_set_margin_bottom (expander, 6);

	content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);

	sb->list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (sb->list), GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (sb->list), TRUE);
	g_signal_connect (sb->list, "row-activated", G_CALLBACK (on_row_activated), sb);
	g_signal_connect (sb->list, "button-press-event", G_CALLBACK (on_button_press), sb);
	g_signal_connect (sb->list, "popup-menu", G_CALLBACK (on_popup_menu), sb);

	sb->scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sb->scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sb->scroller), 40);
	gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (sb->scroller), 180);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (sb->scroller), TRUE);
	gtk_container_add (GTK_CONTAINER (sb->scroller), sb->list);
	gtk_box_pack_start (GTK_BOX (content), sb->scroller, FALSE, FALSE, 0);

	sb->empty_label = gtk_label_new (_("Noch keine Projekte."));
	gtk_widget_set_halign (sb->empty_label, GTK_ALIGN_START);
	gtk_label_set_ellipsize (GTK_LABEL (sb->empty_label), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class (gtk_widget_get_style_context (sb->empty_label), "dim-label");
	gtk_box_pack_start (GTK_BOX (content), sb->empty_label, FALSE, FALSE, 0);

	/* Beschriftung kürzbar, damit die Seitenleiste wie bei Nemo bis 140 px
	 * schmal gezogen werden kann (gtk_button_set_image erlaubt das nicht). */
	add = gtk_button_new ();
	{
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
		GtkWidget *label = gtk_label_new (_("Projekt hinzufügen …"));

		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		gtk_box_pack_start (GTK_BOX (box),
				    gtk_image_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_BUTTON),
				    FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
		gtk_container_add (GTK_CONTAINER (add), box);
		gtk_widget_show_all (box);
	}
	gtk_widget_set_tooltip_text (add, _("Projekt hinzufügen …"));
	gtk_button_set_relief (GTK_BUTTON (add), GTK_RELIEF_NONE);
	gtk_widget_set_halign (add, GTK_ALIGN_FILL);
	g_signal_connect (add, "clicked", G_CALLBACK (on_add_clicked), sb);
	gtk_box_pack_start (GTK_BOX (content), add, FALSE, FALSE, 0);

	gtk_container_add (GTK_CONTAINER (expander), content);
	g_object_set_data_full (G_OBJECT (expander), "gid-sidebar-data", sb, gid_sidebar_free);
	g_signal_connect_object (nolphin_preferences, "changed::" NOLPHIN_PREFERENCES_GID_PROJECTS,
				 G_CALLBACK (on_projects_changed), expander, 0);
	g_signal_connect_object (nolphin_window_state, "changed::" NOLPHIN_WINDOW_STATE_SHOW_GID_PROJECTS,
				 G_CALLBACK (on_show_changed), expander, 0);
	gtk_widget_show_all (expander);
	gtk_widget_set_visible (expander, g_settings_get_boolean (nolphin_window_state,
								  NOLPHIN_WINDOW_STATE_SHOW_GID_PROJECTS));
	rebuild (sb);
	return expander;
}
