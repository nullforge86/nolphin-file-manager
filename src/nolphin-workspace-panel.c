/* nolphin-workspace-panel.c
 *
 * Baut die rechte Arbeitsbereich-Leiste als GtkNotebook mit einem Reiter
 * pro Werkzeug. Fuer Eigenschaften, Archiv/Komprimieren, Git und den
 * .deb-Paket-Ersteller existiert die eigentliche Funktion bereits an
 * anderer Stelle im Programm (nolphin-view.c bzw. nolphin-window-menus.c)
 * - dieses Modul baut sie nicht neu, sondern loest die vorhandene
 * GtkAction aus. Nur die Vorschau (uebergebenes Widget aus
 * nolphin-preview.c) und die Terminal-/Suche-Kurzbefehle greifen direkt
 * auf bereits oeffentliche NolphinWindow-Funktionen zu.
 */

#include <config.h>

#include "nolphin-workspace-panel.h"

#include <libnolphin-private/nolphin-file-chooser-preview.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libnolphin-private/nolphin-archive.h>
#include <libnolphin-private/nolphin-archive-manage.h>
#include <libnolphin-private/nolphin-file.h>
#include <libnolphin-private/nolphin-git.h>

#include "nolphin-actions.h"
#include "nolphin-properties-panel.h"
#include "nolphin-deb-builder.h"
#include "nolphin-application.h"
#include "nolphin-diagnostics.h"
#include "nolphin-tools.h"
#include "nolphin-gid.h"
#include "nolphin-phone.h"
#include "nolphin-terminal.h"
#include "nolphin-view.h"
#include "nolphin-window-pane.h"
#include "nolphin-window-slot.h"

static NolphinWindowSlot *
workspace_active_slot (NolphinWindow *window)
{
	return nolphin_window_get_active_slot (window);
}

static NolphinView *
workspace_active_view (NolphinWindow *window)
{
	NolphinWindowSlot *slot = workspace_active_slot (window);

	if (slot == NULL || slot->content_view == NULL) {
		return NULL;
	}

	return NOLPHIN_VIEW (slot->content_view);
}

/* Jeder aktivierte Reiter (Eigenschaften/Archiv/Git/.deb-Paket) bekommt
 * diesen Knopf oben, um zurück zur Vorschau-Ruhelage zu wechseln - ohne
 * ihn gäbe es keinen Weg zurück, sobald eine Funktion einmal aktiviert
 * wurde. */
static void
on_back_to_preview_clicked (GtkButton *button, gpointer user_data)
{
	NolphinWindow *window = NOLPHIN_WINDOW (user_data);
	GtkWidget *workspace_panel = nolphin_window_get_workspace_panel (window);

	gtk_stack_set_visible_child_name (GTK_STACK (workspace_panel), "preview");
}

/* Einheitlicher Stil für Knöpfe in den Panel-Seiten: Symbol links vom Text,
 * Hauptaktion in der Akzentfarbe (siehe auch build_git_tab()). */
static void
panel_decorate_button (GtkWidget *button, const gchar *icon_name, gboolean primary)
{
	if (icon_name != NULL) {
		gtk_button_set_image (GTK_BUTTON (button),
				      gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));
		gtk_button_set_always_show_image (GTK_BUTTON (button), TRUE);
	}
	if (primary) {
		gtk_style_context_add_class (gtk_widget_get_style_context (button), "suggested-action");
	}
}

/* Beschreibungstexte dezent grau, damit die Bedienelemente im Vordergrund stehen. */
static void
panel_dim_label (GtkWidget *label)
{
	gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
}

static GtkWidget *
build_back_to_preview_button (NolphinWindow *window)
{
	GtkWidget *button = gtk_button_new_with_label (_("Zur Vorschau"));

	panel_decorate_button (button, "go-previous-symbolic", FALSE);
	gtk_widget_set_halign (button, GTK_ALIGN_START);
	g_signal_connect (button, "clicked", G_CALLBACK (on_back_to_preview_clicked), window);

	return button;
}

/* --- Eigenschaften -------------------------------------------------------
 * Eigenes Panel (nolphin-properties-panel.c) im Layout der Vorschau, statt
 * den früheren Eigenschaften-Dialog einzubetten. */

typedef struct {
	NolphinWindow *window;
	GtkWidget     *panel;
} PropertiesTabData;

typedef struct {
	NolphinWindow *window;
	GtkWidget     *filename_entry;
	GtkWidget     *format_combo;
	/* Nur die erstellbaren Formate ("Erstellung & Komprimierung")
	 * stehen im Dropdown - format_map[Dropdown-Index] ist der jeweils
	 * dahinterstehende NolphinArchiveFormat-Wert, da diese wegen der
	 * "Nur Lesen"-Formate nicht mehr lückenlos mit dem Dropdown-Index
	 * übereinstimmen. */
	NolphinArchiveFormat format_map[32];
	GtkWidget     *folder_chooser;
	GtkWidget     *password_entry;
	GtkWidget     *split_check;
	GtkWidget     *split_spin;
	GtkWidget     *status_label;
	GtkWidget     *summary_label;
} ArchiveTabData;

static NolphinArchiveFormat
archive_tab_get_selected_format (ArchiveTabData *data)
{
	gint index = gtk_combo_box_get_active (GTK_COMBO_BOX (data->format_combo));

	if (index < 0 || (guint) index >= G_N_ELEMENTS (data->format_map)) {
		return NOLPHIN_ARCHIVE_FORMAT_ZIP;
	}
	return data->format_map[index];
}

/* Passwort/Teilarchiv-Felder nur bedienbar lassen, wenn das gerade
 * gewählte Format sie wirklich unterstützt (nur ZIP/7-Zip) - sonst
 * würden sie eine Funktion vortäuschen, die es für TAR-Varianten gar
 * nicht gibt. */
static void
on_archive_format_changed (GtkComboBox *combo, gpointer user_data)
{
	ArchiveTabData *data = user_data;
	NolphinArchiveFormat format = archive_tab_get_selected_format (data);
	gboolean password_ok = nolphin_archive_format_supports_password (format);
	gboolean split_ok = nolphin_archive_format_supports_split (format);

	gtk_widget_set_sensitive (data->password_entry, password_ok);
	gtk_widget_set_sensitive (data->split_check, split_ok);
	gtk_widget_set_sensitive (data->split_spin, split_ok && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->split_check)));
}

static void
on_archive_split_toggled (GtkToggleButton *toggle, gpointer user_data)
{
	ArchiveTabData *data = user_data;

	gtk_widget_set_sensitive (data->split_spin, gtk_toggle_button_get_active (toggle));
}

static void
archive_compress_finished_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ArchiveTabData *data = user_data;
	GError *error = NULL;
	gboolean success = nolphin_archive_compress_finish (result, &error);

	if (success) {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Archiv wurde erstellt."));
	} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Abgebrochen."));
	} else {
		gchar *msg = g_strdup_printf (_("Fehlgeschlagen: %s"), error != NULL ? error->message : "");
		gtk_label_set_text (GTK_LABEL (data->status_label), msg);
		g_free (msg);
	}
	g_clear_error (&error);
}

static void
on_archive_create_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveTabData *data = user_data;
	NolphinView *view = workspace_active_view (data->window);
	GList *selection;
	GList *sources = NULL;
	GList *l;
	const gchar *filename;
	gchar *folder;
	NolphinArchiveFormat format;
	gchar *full_name;
	gchar *dest_path;
	GFile *destination;

	if (view == NULL) {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Kein aktiver Ordner."));
		return;
	}

	selection = nolphin_view_get_selection (view);
	if (selection == NULL) {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Keine Dateien ausgewählt."));
		return;
	}

	filename = gtk_entry_get_text (GTK_ENTRY (data->filename_entry));
	if (filename == NULL || *filename == '\0') {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Bitte einen Dateinamen angeben."));
		nolphin_file_list_free (selection);
		return;
	}

	folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (data->folder_chooser));
	if (folder == NULL) {
		gtk_label_set_text (GTK_LABEL (data->status_label), _("Bitte einen Zielordner wählen."));
		nolphin_file_list_free (selection);
		return;
	}

	format = archive_tab_get_selected_format (data);

	if (!nolphin_archive_format_is_available (format)) {
		gchar *msg = g_strdup_printf (_("Das Werkzeug für %s ist nicht installiert."),
					      nolphin_archive_format_get_label (format));
		gtk_label_set_text (GTK_LABEL (data->status_label), msg);
		g_free (msg);
		g_free (folder);
		nolphin_file_list_free (selection);
		return;
	}

	for (l = selection; l != NULL; l = l->next) {
		sources = g_list_prepend (sources, nolphin_file_get_location (NOLPHIN_FILE (l->data)));
	}
	sources = g_list_reverse (sources);
	nolphin_file_list_free (selection);

	if (g_str_has_suffix (filename, nolphin_archive_format_get_extension (format))) {
		full_name = g_strdup (filename);
	} else {
		full_name = g_strconcat (filename, nolphin_archive_format_get_extension (format), NULL);
	}

	dest_path = g_build_filename (folder, full_name, NULL);
	destination = g_file_new_for_path (dest_path);

	{
		const gchar *password = gtk_entry_get_text (GTK_ENTRY (data->password_entry));
		guint split_mb = 0;

		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->split_check))) {
			split_mb = (guint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->split_spin));
		}

		gtk_label_set_text (GTK_LABEL (data->status_label), _("Archiv wird erstellt …"));
		nolphin_archive_compress_async (sources, destination, format,
						(password != NULL && *password != '\0') ? password : NULL,
						split_mb,
						NULL, archive_compress_finished_cb, data);
	}

	g_free (dest_path);
	g_free (full_name);
	g_free (folder);
	g_object_unref (destination);
	g_list_free_full (sources, g_object_unref);
}

/* --- Git ----------------------------------------------------------------
 * Wirkt auf den Ordner der aktiven Ansicht (bzw. die dort ausgewählten
 * Objekte) über das Systemwerkzeug git, direkt eingebettet statt
 * als eigenes Dialogfenster (vorher: show_git_text_dialog() in
 * nolphin-view.c). */

typedef struct {
	NolphinWindow *window;
	GtkWidget     *output_view;
	GtkWidget     *commit_entry;
} GitTabData;

static void
git_tab_set_output (GitTabData *d, const gchar *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (d->output_view));

	gtk_text_buffer_set_text (buffer, (text != NULL && *text != '\0') ? text : _("(keine Ausgabe)"), -1);
}

typedef void (*GitTabRootFunc) (GitTabData *d, GFile *repo_root, GList *selected_files /* GFile*, nullable */);

typedef struct {
	GitTabData     *tab;
	GitTabRootFunc  func;
} GitTabRootLookup;

static void
git_tab_root_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabRootLookup *lookup = user_data;
	GError *error = NULL;
	GFile *root = nolphin_git_find_repository_root_finish (result, &error);

	if (error != NULL) {
		gchar *msg = g_strdup_printf (_("Fehler: %s"), error->message);
		git_tab_set_output (lookup->tab, msg);
		g_free (msg);
		g_clear_error (&error);
	} else if (root == NULL) {
		git_tab_set_output (lookup->tab, _("Dieser Ort befindet sich nicht in einem Git-Repository."));
	} else {
		NolphinView *view = workspace_active_view (lookup->tab->window);
		GList *selection = (view != NULL) ? nolphin_view_get_selection (view) : NULL;
		GList *selected_files = NULL;
		GList *l;

		for (l = selection; l != NULL; l = l->next) {
			selected_files = g_list_prepend (selected_files, nolphin_file_get_location (NOLPHIN_FILE (l->data)));
		}
		selected_files = g_list_reverse (selected_files);
		nolphin_file_list_free (selection);

		lookup->func (lookup->tab, root, selected_files);

		g_list_free_full (selected_files, g_object_unref);
		g_object_unref (root);
	}

	g_free (lookup);
}

static void
git_tab_resolve_root (GitTabData *d, GitTabRootFunc func)
{
	NolphinView *view = workspace_active_view (d->window);
	GFile *start_file;
	GitTabRootLookup *lookup;

	if (view == NULL) {
		git_tab_set_output (d, _("Kein aktiver Ordner."));
		return;
	}

	start_file = nolphin_file_get_location (nolphin_view_get_directory_as_file (view));

	lookup = g_new0 (GitTabRootLookup, 1);
	lookup->tab = d;
	lookup->func = func;

	git_tab_set_output (d, _("Arbeite …"));
	nolphin_git_find_repository_root_async (start_file, NULL, git_tab_root_ready_cb, lookup);
	g_object_unref (start_file);
}

static void
git_tab_status_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	GHashTable *table = nolphin_git_get_status_finish (result, &error);
	GString *text = g_string_new (NULL);

	if (table == NULL) {
		g_string_assign (text, error ? error->message : _("Unbekannter Fehler"));
		g_clear_error (&error);
	} else if (g_hash_table_size (table) == 0) {
		g_string_assign (text, _("Arbeitsverzeichnis ist sauber - keine Änderungen."));
	} else {
		GHashTableIter iter;
		gpointer key, value;

		g_hash_table_iter_init (&iter, table);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			NolphinGitFileStatus st = (NolphinGitFileStatus) GPOINTER_TO_INT (value);
			g_string_append_printf (text, "%-14s %s\n", nolphin_git_status_get_label (st), (const gchar *) key);
		}
	}
	if (table != NULL) {
		g_hash_table_unref (table);
	}

	git_tab_set_output (d, text->str);
	g_string_free (text, TRUE);
}

static void
git_tab_status_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	nolphin_git_get_status_async (repo_root, NULL, git_tab_status_ready_cb, d);
}

static void
on_git_status_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_status_found);
}

static void
git_tab_add_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gboolean success = nolphin_git_add_finish (result, &error);

	git_tab_set_output (d, success ? _("Hinzugefügt.") : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_clear_error (&error);
}

static void
git_tab_add_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	GList *files = (selected_files != NULL) ? selected_files : g_list_prepend (NULL, repo_root);

	nolphin_git_add_async (repo_root, files, NULL, git_tab_add_ready_cb, d);

	if (selected_files == NULL) {
		g_list_free (files);
	}
}

static void
on_git_add_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_add_found);
}

static void
git_tab_commit_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gboolean success = nolphin_git_commit_finish (result, &error);

	git_tab_set_output (d, success ? _("Commit erstellt.") : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_clear_error (&error);
}

static void
git_tab_commit_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	const gchar *message = gtk_entry_get_text (GTK_ENTRY (d->commit_entry));

	if (message == NULL || *message == '\0') {
		git_tab_set_output (d, _("Bitte eine Commit-Nachricht eingeben."));
		return;
	}

	nolphin_git_commit_async (repo_root, message, NULL, git_tab_commit_ready_cb, d);
}

static void
on_git_commit_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_commit_found);
}

static void
git_tab_pull_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gchar *output = nolphin_git_pull_finish (result, &error);

	git_tab_set_output (d, output != NULL ? output : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_free (output);
	g_clear_error (&error);
}

static void
git_tab_pull_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	nolphin_git_pull_async (repo_root, NULL, git_tab_pull_ready_cb, d);
}

static void
on_git_pull_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_pull_found);
}

static void
git_tab_push_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gchar *output = nolphin_git_push_finish (result, &error);

	git_tab_set_output (d, output != NULL ? output : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_free (output);
	g_clear_error (&error);
}

static void
git_tab_push_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	nolphin_git_push_async (repo_root, NULL, git_tab_push_ready_cb, d);
}

static void
on_git_push_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_push_found);
}

static void
git_tab_log_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gchar *output = nolphin_git_log_finish (result, &error);

	git_tab_set_output (d, output != NULL ? output : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_free (output);
	g_clear_error (&error);
}

static void
git_tab_log_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	GFile *path = (selected_files != NULL && selected_files->next == NULL) ? selected_files->data : NULL;

	nolphin_git_log_async (repo_root, path, 50, NULL, git_tab_log_ready_cb, d);
}

static void
on_git_log_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_log_found);
}

static void
git_tab_diff_ready_cb (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GitTabData *d = user_data;
	GError *error = NULL;
	gchar *output = nolphin_git_diff_finish (result, &error);

	git_tab_set_output (d, output != NULL ? output : (error != NULL ? error->message : _("Fehlgeschlagen.")));
	g_free (output);
	g_clear_error (&error);
}

static void
git_tab_diff_found (GitTabData *d, GFile *repo_root, GList *selected_files)
{
	GFile *path = (selected_files != NULL && selected_files->next == NULL) ? selected_files->data : NULL;

	nolphin_git_diff_async (repo_root, path, NULL, git_tab_diff_ready_cb, d);
}

static void
on_git_diff_clicked (GtkButton *button, gpointer user_data)
{
	git_tab_resolve_root ((GitTabData *) user_data, git_tab_diff_found);
}

/* --- .deb-Paket erstellen ----------------------------------------------
 * Lebt als Formular direkt in seinem Reiter (kein Dialogfenster mehr).
 * Der Hilfe-Menüeintrag "Build Deb Package" zeigt nur noch diesen Reiter
 * an, siehe nolphin_workspace_panel_focus_deb_builder() unten. */

enum {
	DEB_FILES_COL_SOURCE,
	DEB_FILES_COL_NAME,
	DEB_FILES_COL_TARGET,
	DEB_FILES_COL_EXECUTABLE,
	DEB_FILES_N_COLS
};

typedef struct {
	NolphinWindow *window;
	GtkWidget    *package_entry;
	GtkWidget    *version_entry;
	GtkWidget    *arch_combo;
	GtkWidget    *maintainer_entry;
	GtkWidget    *email_entry;
	GtkWidget    *description_entry;
	GtkWidget    *section_entry;
	GtkWidget    *priority_combo;
	GtkWidget    *depends_entry;
	GtkWidget    *homepage_entry;
	GtkWidget    *output_name_entry;
	GtkWidget    *output_chooser;
	GtkWidget    *files_view;
	GtkListStore *files_store;
	GtkWidget    *status_label;
} DebBuilderTab;

static void
deb_builder_add_files_clicked (GtkButton *button, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	GtkWidget *chooser;
	gint response;

	chooser = gtk_file_chooser_dialog_new (_("Dateien auswählen"),
					       GTK_WINDOW (d->window),
					       GTK_FILE_CHOOSER_ACTION_OPEN,
					       _("Abbrechen"), GTK_RESPONSE_CANCEL,
					       _("Hinzufügen"), GTK_RESPONSE_ACCEPT,
					       NULL);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (chooser), TRUE);
	nolphin_file_chooser_add_preview (GTK_FILE_CHOOSER (chooser));

	response = gtk_dialog_run (GTK_DIALOG (chooser));

	if (response == GTK_RESPONSE_ACCEPT) {
		GSList *filenames = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (chooser));
		GSList *l;

		for (l = filenames; l != NULL; l = l->next) {
			gchar *path = l->data;
			gchar *basename = g_path_get_basename (path);
			gchar *target = g_strdup_printf ("/usr/bin/%s", basename);
			gboolean executable = g_file_test (path, G_FILE_TEST_IS_EXECUTABLE);
			GtkTreeIter iter;

			gtk_list_store_append (d->files_store, &iter);
			gtk_list_store_set (d->files_store, &iter,
					    DEB_FILES_COL_SOURCE, path,
					    DEB_FILES_COL_NAME, basename,
					    DEB_FILES_COL_TARGET, target,
					    DEB_FILES_COL_EXECUTABLE, executable,
					    -1);

			g_free (target);
			g_free (basename);
			g_free (path);
		}

		g_slist_free (filenames);
	}

	gtk_widget_destroy (chooser);
}

static void
deb_builder_add_folder_clicked (GtkButton *button, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	GtkWidget *chooser;
	gint response;

	chooser = gtk_file_chooser_dialog_new (_("Ordner auswählen"),
					       GTK_WINDOW (d->window),
					       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
					       _("Abbrechen"), GTK_RESPONSE_CANCEL,
					       _("Hinzufügen"), GTK_RESPONSE_ACCEPT,
					       NULL);

	response = gtk_dialog_run (GTK_DIALOG (chooser));

	if (response == GTK_RESPONSE_ACCEPT) {
		gchar *folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
		gchar *folder_basename = g_path_get_basename (folder);

		gchar *display_name = g_strdup_printf ("%s/", folder_basename);
		gchar *target = g_strdup_printf ("/opt/%s", folder_basename);
		GtkTreeIter iter;

		/* Ein Ordner ist eine Zeile; der Inhalt wird erst beim Erstellen
		 * rekursiv eingepackt (Dateirechte bleiben erhalten). */
		gtk_list_store_append (d->files_store, &iter);
		gtk_list_store_set (d->files_store, &iter,
				    DEB_FILES_COL_SOURCE, folder,
				    DEB_FILES_COL_NAME, display_name,
				    DEB_FILES_COL_TARGET, target,
				    DEB_FILES_COL_EXECUTABLE, FALSE,
				    -1);

		g_free (display_name);
		g_free (target);

		g_free (folder_basename);
		g_free (folder);
	}

	gtk_widget_destroy (chooser);
}

static void
deb_builder_remove_file_clicked (GtkButton *button, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (d->files_view));
	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_list_store_remove (d->files_store, &iter);
	}
}

static void
deb_builder_target_edited (GtkCellRendererText *renderer, gchar *path_string,
			   gchar *new_text, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (d->files_store), &iter, path_string)) {
		gtk_list_store_set (d->files_store, &iter, DEB_FILES_COL_TARGET, new_text, -1);
	}
}

static void
deb_builder_executable_toggled (GtkCellRendererToggle *renderer, gchar *path_string, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (d->files_store), &iter, path_string)) {
		gboolean current;

		gtk_tree_model_get (GTK_TREE_MODEL (d->files_store), &iter, DEB_FILES_COL_EXECUTABLE, &current, -1);
		gtk_list_store_set (d->files_store, &iter, DEB_FILES_COL_EXECUTABLE, !current, -1);
	}
}

static GtkWidget *
deb_builder_add_row (GtkWidget *grid, gint row, const gchar *label_text)
{
	GtkWidget *label;
	GtkWidget *entry;

	label = gtk_label_new (label_text);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

	entry = gtk_entry_new ();
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);

	return entry;
}

typedef struct {
	DebBuilderTab *tab;
	gchar *output_deb_path;
	gchar *deb_filename;
	gchar *package_name;
	gchar *version;
	gchar *architecture;
	gchar *maintainer;
	gchar *description;
	gchar *section;
	gchar *priority;
	gchar *depends;
	gchar *homepage;
	GList *files;
	GtkWidget *button;
} DebBuildJob;

static void
deb_build_job_free (DebBuildJob *job)
{
	g_free (job->output_deb_path);
	g_free (job->deb_filename);
	g_free (job->package_name);
	g_free (job->version);
	g_free (job->architecture);
	g_free (job->maintainer);
	g_free (job->description);
	g_free (job->section);
	g_free (job->priority);
	g_free (job->depends);
	g_free (job->homepage);
	g_list_free_full (job->files, (GDestroyNotify) nolphin_deb_builder_file_free);
	g_free (job);
}

static void
deb_build_thread (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	DebBuildJob *job = task_data;
	GError *error = NULL;

	if (nolphin_deb_builder_create (job->output_deb_path, job->package_name, job->version,
					job->architecture, job->maintainer, job->description,
					job->section, job->priority, job->depends, job->homepage,
					job->files, &error)) {
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_error (task, error);
	}
}

static void
deb_build_finished (GObject *source, GAsyncResult *result, gpointer user_data)
{
	DebBuildJob *job = g_task_get_task_data (G_TASK (result));
	DebBuilderTab *d = job->tab;
	GError *error = NULL;
	gchar *msg;

	if (g_task_propagate_boolean (G_TASK (result), &error)) {
		msg = g_strdup_printf (_("Paket wurde erstellt: %s"), job->deb_filename);
	} else {
		msg = g_strdup_printf (_("Paket konnte nicht erstellt werden: %s"),
				       error != NULL ? error->message : "");
		g_clear_error (&error);
	}

	gtk_label_set_text (GTK_LABEL (d->status_label), msg);
	gtk_widget_set_sensitive (job->button, TRUE);
	g_free (msg);
}

static void
on_deb_build_clicked (GtkButton *button, gpointer user_data)
{
	DebBuilderTab *d = user_data;
	const gchar *package_name = gtk_entry_get_text (GTK_ENTRY (d->package_entry));
	const gchar *version = gtk_entry_get_text (GTK_ENTRY (d->version_entry));
	gchar *architecture = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (d->arch_combo));
	const gchar *maintainer_name = gtk_entry_get_text (GTK_ENTRY (d->maintainer_entry));
	const gchar *email = gtk_entry_get_text (GTK_ENTRY (d->email_entry));
	const gchar *description = gtk_entry_get_text (GTK_ENTRY (d->description_entry));
	const gchar *section = gtk_entry_get_text (GTK_ENTRY (d->section_entry));
	gchar *priority = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (d->priority_combo));
	const gchar *depends = gtk_entry_get_text (GTK_ENTRY (d->depends_entry));
	const gchar *homepage = gtk_entry_get_text (GTK_ENTRY (d->homepage_entry));
	const gchar *output_name = gtk_entry_get_text (GTK_ENTRY (d->output_name_entry));
	gchar *output_folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d->output_chooser));
	gchar *maintainer;
	GList *files = NULL;
	GtkTreeIter iter;
	gboolean valid;

	maintainer = (email != NULL && *email != '\0')
		? g_strdup_printf ("%s <%s>", maintainer_name, email)
		: g_strdup (maintainer_name);

	if (package_name == NULL || *package_name == '\0' ||
	    version == NULL || *version == '\0' ||
	    output_folder == NULL) {
		gtk_label_set_text (GTK_LABEL (d->status_label),
				    _("Name, Versionsnummer und Speicherort werden benötigt."));
	} else {
		gchar *deb_filename;
		gchar *output_deb_path;
		DebBuildJob *job;
		GTask *task;

		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->files_store), &iter);
		while (valid) {
			gchar *source;
			gchar *target;
			gboolean executable;
			NolphinDebBuilderFile *f;

			gtk_tree_model_get (GTK_TREE_MODEL (d->files_store), &iter,
					    DEB_FILES_COL_SOURCE, &source,
					    DEB_FILES_COL_TARGET, &target,
					    DEB_FILES_COL_EXECUTABLE, &executable,
					    -1);

			f = nolphin_deb_builder_file_new (source, target, executable);
			files = g_list_append (files, f);

			g_free (source);
			g_free (target);

			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->files_store), &iter);
		}

		deb_filename = (output_name != NULL && *output_name != '\0')
			? g_strdup (output_name)
			: g_strdup_printf ("%s_%s_%s.deb", package_name, version, architecture);
		output_deb_path = g_build_filename (output_folder, deb_filename, NULL);

		job = g_new0 (DebBuildJob, 1);
		job->tab = d;
		job->button = GTK_WIDGET (button);
		job->output_deb_path = output_deb_path;
		job->deb_filename = deb_filename;
		job->package_name = g_strdup (package_name);
		job->version = g_strdup (version);
		job->architecture = g_strdup (architecture);
		job->maintainer = g_strdup (maintainer);
		job->description = g_strdup (description);
		job->section = g_strdup (section);
		job->priority = g_strdup (priority);
		job->depends = g_strdup (depends);
		job->homepage = g_strdup (homepage);
		job->files = files;

		gtk_label_set_text (GTK_LABEL (d->status_label), _("Paket wird erstellt … bitte warten."));
		gtk_widget_set_sensitive (job->button, FALSE);

		task = g_task_new (NULL, NULL, deb_build_finished, NULL);
		g_task_set_task_data (task, job, (GDestroyNotify) deb_build_job_free);
		g_task_run_in_thread (task, deb_build_thread);
		g_object_unref (task);
	}

	g_free (maintainer);
	g_free (architecture);
	g_free (priority);
	g_free (output_folder);
}

static GtkWidget *
build_properties_tab (NolphinWindow *window)
{
	GtkWidget *box;
	GtkWidget *back;
	PropertiesTabData *d;

	d = g_new0 (PropertiesTabData, 1);
	d->window = window;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	back = build_back_to_preview_button (window);
	gtk_widget_set_margin_start (back, 12);
	gtk_widget_set_margin_top (back, 12);
	gtk_box_pack_start (GTK_BOX (box), back, FALSE, FALSE, 0);

	d->panel = nolphin_properties_panel_new ();
	gtk_box_pack_start (GTK_BOX (box), d->panel, TRUE, TRUE, 0);

	g_object_set_data_full (G_OBJECT (box), "properties-tab-data", d, g_free);

	return box;
}

/* Zeigt, was gepackt wird, und schlägt einen passenden Dateinamen vor
 * (Name des einzelnen Objekts, sonst Name des aktuellen Ordners). Wird
 * aufgerufen, wenn die Archiv-Seite angezeigt wird. */
static void
archive_tab_refresh_from_selection (ArchiveTabData *data)
{
	NolphinView *view = workspace_active_view (data->window);
	GList *selection;
	guint count;
	gchar *summary = NULL;
	gchar *suggestion = NULL;

	if (view == NULL) {
		return;
	}

	selection = nolphin_view_get_selection (view);
	count = g_list_length (selection);

	if (count == 0) {
		summary = g_strdup (_("Nichts ausgewählt – markiere im Hauptfenster Dateien oder Ordner."));
	} else {
		gchar *first = nolphin_file_get_display_name (NOLPHIN_FILE (selection->data));

		if (count == 1) {
			summary = g_strdup_printf (_("Wird gepackt: %s"), first);
			suggestion = g_strdup (first);
			if (!nolphin_file_is_directory (NOLPHIN_FILE (selection->data))) {
				gchar *dot = strrchr (suggestion, '.');

				if (dot != NULL && dot != suggestion) {
					*dot = '\0';
				}
			}
		} else {
			summary = g_strdup_printf (_("Wird gepackt: %s und %u weitere Objekte"), first, count - 1);
			suggestion = nolphin_file_get_display_name (nolphin_view_get_directory_as_file (view));
		}
		g_free (first);
	}
	nolphin_file_list_free (selection);

	gtk_label_set_text (GTK_LABEL (data->summary_label), summary);
	if (suggestion != NULL && *suggestion != '\0') {
		gtk_entry_set_text (GTK_ENTRY (data->filename_entry), suggestion);
	}
	g_free (summary);
	g_free (suggestion);
}

static GtkWidget *
build_archive_tab (NolphinWindow *window)
{
	GtkWidget *scroller;
	GtkWidget *box;
	GtkWidget *desc_label;
	GtkWidget *grid;
	GtkWidget *filename_label;
	GtkWidget *format_label;
	GtkWidget *folder_label;
	GtkWidget *create_button;
	ArchiveTabData *data;
	NolphinArchiveFormat fmt;

	/* Ohne diesen Scroller hat diese Seite (anders als die uebrigen Panel-
	 * Seiten) keinen Weg, mit weniger als ihrer natuerlichen Breite
	 * auszukommen - ihr Inhalt ragt dann beim Schmaler-Ziehen des Panels
	 * einfach unerreichbar ueber den rechten Rand hinaus. */
	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_container_add (GTK_CONTAINER (scroller), box);
	gtk_box_pack_start (GTK_BOX (box), build_back_to_preview_button (window), FALSE, FALSE, 0);

	desc_label = gtk_label_new (_("Ein Archiv aus den im Hauptfenster ausgewählten "
				     "Dateien und Ordnern erstellen."));
	gtk_label_set_line_wrap (GTK_LABEL (desc_label), TRUE);
	panel_dim_label (desc_label);
	gtk_label_set_max_width_chars (GTK_LABEL (desc_label), 30);
	gtk_label_set_xalign (GTK_LABEL (desc_label), 0.0);
	gtk_box_pack_start (GTK_BOX (box), desc_label, FALSE, FALSE, 0);

	data = g_new0 (ArchiveTabData, 1);
	data->window = window;

	data->summary_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (data->summary_label), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (data->summary_label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (data->summary_label), 30);
	gtk_box_pack_start (GTK_BOX (box), data->summary_label, FALSE, FALSE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	filename_label = gtk_label_new (_("Dateiname:"));
	gtk_label_set_xalign (GTK_LABEL (filename_label), 0.0);
	gtk_grid_attach (GTK_GRID (grid), filename_label, 0, 0, 1, 1);

	data->filename_entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (data->filename_entry), _("archiv"));
	gtk_widget_set_hexpand (data->filename_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), data->filename_entry, 1, 0, 1, 1);

	format_label = gtk_label_new (_("Format:"));
	gtk_label_set_xalign (GTK_LABEL (format_label), 0.0);
	gtk_grid_attach (GTK_GRID (grid), format_label, 0, 1, 1, 1);

	data->format_combo = gtk_combo_box_text_new ();
	{
		gint combo_index = 0;

		/* Alle erstellbaren Formate (plus die Nemo/file-roller-Liste) - die
		 * reinen "Nur Lesen"-Formate (RAR, CAB, ARJ ...) haben kein
		 * create_tool und tauchen hier bewusst nicht auf. */
		for (fmt = NOLPHIN_ARCHIVE_FORMAT_ZIP; fmt < NOLPHIN_ARCHIVE_FORMAT_UNKNOWN; fmt++) {
			if (!nolphin_archive_format_can_create (fmt)) {
				continue;
			}
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (data->format_combo),
							nolphin_archive_format_get_label (fmt));
			if ((guint) combo_index < G_N_ELEMENTS (data->format_map)) {
				data->format_map[combo_index] = fmt;
			}
			combo_index++;
		}
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (data->format_combo), 0);
	gtk_grid_attach (GTK_GRID (grid), data->format_combo, 1, 1, 1, 1);

	folder_label = gtk_label_new (_("Ort:"));
	gtk_label_set_xalign (GTK_LABEL (folder_label), 0.0);
	gtk_grid_attach (GTK_GRID (grid), folder_label, 0, 2, 1, 1);

	data->folder_chooser = gtk_file_chooser_button_new (_("Zielordner wählen"),
							     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_widget_set_hexpand (data->folder_chooser, TRUE);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (data->folder_chooser), g_get_home_dir ());
	gtk_grid_attach (GTK_GRID (grid), data->folder_chooser, 1, 2, 1, 1);

	{
		GtkWidget *password_label;
		GtkWidget *split_label;
		GtkWidget *split_box;
		GtkAdjustment *split_adjustment;
		GtkWidget *adv_expander = gtk_expander_new (_("Erweiterte Optionen (Passwort, Teilarchive)"));
		GtkWidget *adv_grid = gtk_grid_new ();

		gtk_grid_set_row_spacing (GTK_GRID (adv_grid), 8);
		gtk_grid_set_column_spacing (GTK_GRID (adv_grid), 10);
		gtk_widget_set_margin_top (adv_grid, 8);
		gtk_container_add (GTK_CONTAINER (adv_expander), adv_grid);
		gtk_box_pack_start (GTK_BOX (box), adv_expander, FALSE, FALSE, 0);

		password_label = gtk_label_new (_("Passwort:"));
		gtk_label_set_xalign (GTK_LABEL (password_label), 0.0);
		gtk_grid_attach (GTK_GRID (adv_grid), password_label, 0, 0, 1, 1);

		data->password_entry = gtk_entry_new ();
		gtk_entry_set_visibility (GTK_ENTRY (data->password_entry), FALSE);
		gtk_widget_set_hexpand (data->password_entry, TRUE);
		gtk_grid_attach (GTK_GRID (adv_grid), data->password_entry, 1, 0, 1, 1);

		split_label = gtk_label_new (_("Teilarchive:"));
		gtk_label_set_xalign (GTK_LABEL (split_label), 0.0);
		gtk_grid_attach (GTK_GRID (adv_grid), split_label, 0, 1, 1, 1);

		split_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_grid_attach (GTK_GRID (adv_grid), split_box, 1, 1, 1, 1);

		data->split_check = gtk_check_button_new_with_label (_("Aufteilen zu je"));
		gtk_box_pack_start (GTK_BOX (split_box), data->split_check, FALSE, FALSE, 0);
		g_signal_connect (data->split_check, "toggled", G_CALLBACK (on_archive_split_toggled), data);

		split_adjustment = gtk_adjustment_new (100, 1, 100000, 1, 10, 0);
		data->split_spin = gtk_spin_button_new (split_adjustment, 1, 0);
		gtk_widget_set_sensitive (data->split_spin, FALSE);
		gtk_box_pack_start (GTK_BOX (split_box), data->split_spin, FALSE, FALSE, 0);

		gtk_box_pack_start (GTK_BOX (split_box), gtk_label_new (_("MB")), FALSE, FALSE, 0);

		/* Nur ZIP/7-Zip können beides - beim Start ist ZIP aktiv,
		 * also bleiben die Felder zunächst bedienbar. */
		g_signal_connect (data->format_combo, "changed", G_CALLBACK (on_archive_format_changed), data);
	}

	create_button = gtk_button_new_with_label (_("Archiv erstellen"));
	panel_decorate_button (create_button, "document-save-symbolic", TRUE);
	gtk_widget_set_halign (create_button, GTK_ALIGN_START);
	g_signal_connect (create_button, "clicked", G_CALLBACK (on_archive_create_clicked), data);
	gtk_box_pack_start (GTK_BOX (box), create_button, FALSE, FALSE, 0);

	data->status_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (data->status_label), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (data->status_label), TRUE);
	gtk_box_pack_start (GTK_BOX (box), data->status_label, FALSE, FALSE, 0);

	g_object_set_data_full (G_OBJECT (box), "archive-tab-data", data, g_free);
	/* Zeiger ohne Eigentum: der Scroller lebt genau so lange wie sein Inhalt. */
	g_object_set_data (G_OBJECT (scroller), "archive-tab-ptr", data);

	return scroller;
}

/* Knopf einer Git-Aktion der Ansicht (Synchronisieren, Klonen, ...): löst
 * dieselbe Aktion aus wie der Eintrag im Rechtsklick-Menü. */
static void
on_git_view_action_clicked (GtkButton *button, gpointer user_data)
{
	GitTabData *d = user_data;
	NolphinView *view = workspace_active_view (d->window);
	const gchar *action_name = g_object_get_data (G_OBJECT (button), "view-action");

	if (view != NULL && action_name != NULL) {
		nolphin_view_activate_action_by_name (view, action_name);
	}
}

static GtkWidget *
git_tab_new_section (GtkWidget *box, const gchar *caption)
{
	GtkWidget *label = gtk_label_new (NULL);
	GtkWidget *flow;
	gchar *markup = g_markup_printf_escaped ("<small><b>%s</b></small>", caption);

	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);
	gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_widget_set_margin_top (label, 10);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

	/* GtkFlowBox: bricht Knöpfe bei schmalem Panel in die nächste Zeile um. */
	flow = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), FALSE);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 6);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 6);
	gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 1);
	gtk_box_pack_start (GTK_BOX (box), flow, FALSE, FALSE, 0);

	return flow;
}

static GtkWidget *
git_tab_add_button (GtkWidget *flow, const gchar *icon_name, const gchar *label, const gchar *tooltip,
		    GCallback callback, gpointer data, const gchar *view_action)
{
	GtkWidget *button = gtk_button_new_with_label (label);

	/* Symbol links vom Text, damit man Aktionen auf einen Blick erkennt. */
	if (icon_name != NULL) {
		gtk_button_set_image (GTK_BUTTON (button),
				      gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));
		gtk_button_set_always_show_image (GTK_BUTTON (button), TRUE);
	}

	gtk_widget_set_tooltip_text (button, tooltip);
	if (view_action != NULL) {
		g_object_set_data (G_OBJECT (button), "view-action", (gpointer) view_action);
	}
	g_signal_connect (button, "clicked", callback, data);
	gtk_container_add (GTK_CONTAINER (flow), button);

	return button;
}

static GtkWidget *
build_git_tab (NolphinWindow *window)
{
	GtkWidget *scroller;
	GtkWidget *box;
	GtkWidget *flow;
	GtkWidget *output_scroller;
	GtkWidget *commit_box;
	GtkWidget *commit_button;
	GtkWidget *sync_button;
	GitTabData *d;

	d = g_new0 (GitTabData, 1);
	d->window = window;

	/* Ohne Scroller ragt der Inhalt beim Schmaler-Ziehen des Panels
	 * unerreichbar über den rechten Rand hinaus. */
	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_container_add (GTK_CONTAINER (scroller), box);
	gtk_box_pack_start (GTK_BOX (box), build_back_to_preview_button (window), FALSE, FALSE, 0);

	/* Reihenfolge entspricht dem Arbeitsablauf: ansehen, speichern,
	 * mit dem Server abgleichen. */
	flow = git_tab_new_section (box, _("1. Änderungen"));
	git_tab_add_button (flow, "view-refresh-symbolic", _("Status anzeigen"),
			    _("Zeigt, welche Dateien geändert, neu oder gelöscht sind"),
			    G_CALLBACK (on_git_status_clicked), d, NULL);
	git_tab_add_button (flow, "list-add-symbolic", _("Hinzufügen"),
			    _("Merkt die im Hauptfenster markierten Dateien (ohne Markierung: alle) für den nächsten Commit vor"),
			    G_CALLBACK (on_git_add_clicked), d, NULL);

	{
		GtkWidget *caption = gtk_label_new (NULL);

		gtk_label_set_markup (GTK_LABEL (caption), "<small><b>2. Speichern</b></small>");
		gtk_style_context_add_class (gtk_widget_get_style_context (caption), "dim-label");
		gtk_label_set_xalign (GTK_LABEL (caption), 0.0);
		gtk_widget_set_margin_top (caption, 10);
		gtk_box_pack_start (GTK_BOX (box), caption, FALSE, FALSE, 0);
	}
	commit_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start (GTK_BOX (box), commit_box, FALSE, FALSE, 0);

	d->commit_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (d->commit_entry), _("Kurze Beschreibung der Änderungen …"));
	gtk_widget_set_hexpand (d->commit_entry, TRUE);
	gtk_box_pack_start (GTK_BOX (commit_box), d->commit_entry, TRUE, TRUE, 0);

	commit_button = gtk_button_new_with_label (_("Speichern"));
	gtk_button_set_image (GTK_BUTTON (commit_button),
			      gtk_image_new_from_icon_name ("document-save-symbolic", GTK_ICON_SIZE_BUTTON));
	gtk_button_set_always_show_image (GTK_BUTTON (commit_button), TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (commit_button), "suggested-action");
	gtk_widget_set_tooltip_text (commit_button, _("Legt die vorgemerkten Änderungen als neuen Stand ab (Commit)"));
	g_signal_connect (commit_button, "clicked", G_CALLBACK (on_git_commit_clicked), d);
	gtk_box_pack_start (GTK_BOX (commit_box), commit_button, FALSE, FALSE, 0);

	flow = git_tab_new_section (box, _("3. Mit dem Server (z. B. GitHub)"));
	sync_button = git_tab_add_button (flow, "emblem-synchronizing-symbolic", _("Synchronisieren"),
			    _("Holt Neues vom Server und lädt deine Änderungen hoch"),
			    G_CALLBACK (on_git_view_action_clicked), d, NOLPHIN_ACTION_GIT_SYNC);
	gtk_style_context_add_class (gtk_widget_get_style_context (sync_button), "suggested-action");
	git_tab_add_button (flow, "edit-find-symbolic", _("Abgleichen"),
			    _("Zeigt, was lokal und auf dem Server unterschiedlich ist, ohne etwas zu ändern"),
			    G_CALLBACK (on_git_view_action_clicked), d, NOLPHIN_ACTION_GIT_COMPARE);
	git_tab_add_button (flow, "go-down-symbolic", _("Herunterladen"),
			    _("Holt Änderungen vom Server (Pull)"),
			    G_CALLBACK (on_git_pull_clicked), d, NULL);
	git_tab_add_button (flow, "go-up-symbolic", _("Hochladen"),
			    _("Sendet deine gespeicherten Änderungen zum Server (Push)"),
			    G_CALLBACK (on_git_push_clicked), d, NULL);
	git_tab_add_button (flow, "network-server-symbolic", _("Server eintragen …"),
			    _("Trägt die Adresse eines Servers ein, z. B. https://github.com/name/projekt.git (Remote)"),
			    G_CALLBACK (on_git_view_action_clicked), d, NOLPHIN_ACTION_GIT_REMOTE_ADD);
	git_tab_add_button (flow, "folder-download-symbolic", _("Repository klonen …"),
			    _("Lädt ein Repository vom Server in den aktuellen Ordner herunter"),
			    G_CALLBACK (on_git_view_action_clicked), d, NOLPHIN_ACTION_GIT_CLONE);

	flow = git_tab_new_section (box, _("Ansehen"));
	git_tab_add_button (flow, "document-open-recent-symbolic", _("Verlauf"),
			    _("Zeigt die bisherigen Commits (Log)"),
			    G_CALLBACK (on_git_log_clicked), d, NULL);
	git_tab_add_button (flow, "view-dual-symbolic", _("Unterschiede"),
			    _("Zeigt die noch nicht gespeicherten Änderungen im Detail (Diff)"),
			    G_CALLBACK (on_git_diff_clicked), d, NULL);

	d->output_view = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (d->output_view), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (d->output_view), TRUE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (d->output_view), GTK_WRAP_WORD_CHAR);
	git_tab_set_output (d, _("Bereit."));

	output_scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (output_scroller),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (output_scroller), GTK_SHADOW_IN);
	gtk_widget_set_size_request (output_scroller, -1, 180);
	gtk_widget_set_margin_top (output_scroller, 8);
	gtk_container_add (GTK_CONTAINER (output_scroller), d->output_view);
	gtk_box_pack_start (GTK_BOX (box), output_scroller, TRUE, TRUE, 0);

	g_object_set_data_full (G_OBJECT (scroller), "git-tab-data", d, g_free);

	return scroller;
}

/* --- Suche (Pflicht-Panel) --------------------------------------
 * Baut keine zweite Ergebnisliste nach - die vorhandene Suchleiste
 * (NolphinQueryEditor, bereits ein GtkBox-Widget, kein Dialog) zeigt die
 * Ergebnisse schon in der Hauptansicht an. Dieser Reiter ist der im
 * rechten Arbeitsbereich geforderte Einstiegspunkt dafür: er löst die
 * vorhandene Suche/Filterleiste aus und wechselt danach selbst zurück
 * zur Vorschau, damit die Ergebnisse in der Hauptansicht nicht vom
 * Arbeitsbereich verdeckt werden. */

static void
on_search_trigger_clicked (GtkButton *button, gpointer user_data)
{
	NolphinWindow *window = NOLPHIN_WINDOW (user_data);
	NolphinWindowSlot *slot = nolphin_window_get_active_slot (window);
	NolphinWindowPane *pane = (slot != NULL) ? slot->pane : NULL;
	GtkAction *action;

	if (pane == NULL || pane->action_group == NULL) {
		return;
	}

	/* Die Suchleiste zieht danach selbst in diese Seite um (siehe
	 * nolphin_window_slot_set_query_editor_visible()). */
	action = gtk_action_group_get_action (pane->action_group, NOLPHIN_ACTION_SEARCH);
	if (action != NULL) {
		gtk_action_activate (action);
	}
}

static void
on_filter_trigger_clicked (GtkButton *button, gpointer user_data)
{
	NolphinWindow *window = NOLPHIN_WINDOW (user_data);
	NolphinView *view = workspace_active_view (window);

	if (view != NULL) {
		nolphin_view_grab_focus (view);
	}

	nolphin_workspace_panel_show_preview (nolphin_window_get_workspace_panel (window));
}

/* Hinweis und Startknöpfe nur zeigen, solange keine Suchleiste in der Seite steckt. */
static void
search_host_child_added (GtkContainer *host, GtkWidget *child, gpointer idle_box)
{
	gtk_widget_hide (GTK_WIDGET (idle_box));
}

static void
search_host_child_removed (GtkContainer *host, GtkWidget *child, gpointer idle_box)
{
	gtk_widget_show (GTK_WIDGET (idle_box));
}

static GtkWidget *
build_search_tab (NolphinWindow *window)
{
	GtkWidget *scroller;
	GtkWidget *box;
	GtkWidget *host;
	GtkWidget *idle_box;
	GtkWidget *desc_label;
	GtkWidget *search_button;
	GtkWidget *filter_button;

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_container_add (GTK_CONTAINER (scroller), box);
	gtk_box_pack_start (GTK_BOX (box), build_back_to_preview_button (window), FALSE, FALSE, 0);

	/* Hier landet die Suchleiste, sobald eine Suche läuft. */
	host = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (box), host, FALSE, FALSE, 0);
	g_object_set_data (G_OBJECT (scroller), "search-host", host);

	idle_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start (GTK_BOX (box), idle_box, FALSE, FALSE, 0);
	g_signal_connect (host, "add", G_CALLBACK (search_host_child_added), idle_box);
	g_signal_connect (host, "remove", G_CALLBACK (search_host_child_removed), idle_box);

	desc_label = gtk_label_new (_("Vollständige Suche im aktuellen Ordner starten "
				     "oder die Ansicht sofort nach Namen filtern. Die "
				     "Ergebnisse erscheinen in der Hauptansicht."));
	gtk_label_set_line_wrap (GTK_LABEL (desc_label), TRUE);
	panel_dim_label (desc_label);
	gtk_label_set_max_width_chars (GTK_LABEL (desc_label), 30);
	gtk_label_set_xalign (GTK_LABEL (desc_label), 0.0);
	gtk_box_pack_start (GTK_BOX (idle_box), desc_label, FALSE, FALSE, 0);

	search_button = gtk_button_new_with_label (_("Suchen … (Strg+F)"));
	panel_decorate_button (search_button, "edit-find-symbolic", TRUE);
	gtk_widget_set_halign (search_button, GTK_ALIGN_START);
	g_signal_connect (search_button, "clicked", G_CALLBACK (on_search_trigger_clicked), window);
	gtk_box_pack_start (GTK_BOX (idle_box), search_button, FALSE, FALSE, 0);

	filter_button = gtk_button_new_with_label (_("Filterleiste (Strg+I)"));
	panel_decorate_button (filter_button, "view-list-symbolic", FALSE);
	gtk_widget_set_halign (filter_button, GTK_ALIGN_START);
	g_signal_connect (filter_button, "clicked", G_CALLBACK (on_filter_trigger_clicked), window);
	gtk_box_pack_start (GTK_BOX (idle_box), filter_button, FALSE, FALSE, 0);

	return scroller;
}

/* (Massenumbenennung): Suchen/Ersetzen, Nummerierung, Groß-/
 * Kleinschreibung, Vorschau vor dem Ausführen, rückgängig machbar (jede
 * Umbenennung laeuft ueber nolphin_file_rename(), das intern automatisch
 * einen Undo-Eintrag anlegt - siehe nolphin-file.c). */
enum {
	BR_COL_ORIG = 0,
	BR_COL_NEW,
	BR_COL_FILE,
	BR_COL_IS_DIR,
	BR_N_COLS
};

typedef struct {
	NolphinWindow *window;
	GtkListStore  *store;
	GtkWidget     *search_entry;
	GtkWidget     *replace_entry;
	GtkWidget     *case_sensitive_check;
	GtkWidget     *numbering_check;
	GtkWidget     *numbering_position_combo;
	GtkSpinButton *numbering_start_spin;
	GtkSpinButton *numbering_digits_spin;
	GtkWidget     *case_mode_combo;
	GtkWidget     *apply_button;
	GtkWidget     *status_label;
	guint          pending_renames;
	guint          failed_renames;
} BatchRenameTab;

static void
split_stem_extension (const gchar *name, gboolean is_dir, gchar **stem, gchar **ext)
{
	const gchar *dot;

	if (is_dir) {
		*stem = g_strdup (name);
		*ext = g_strdup ("");
		return;
	}

	dot = strrchr (name, '.');
	if (dot == NULL || dot == name) {
		*stem = g_strdup (name);
		*ext = g_strdup ("");
	} else {
		*stem = g_strndup (name, dot - name);
		*ext = g_strdup (dot);
	}
}

/* Einfacher, byte-basierter Ersetzer (kein GRegex noetig fuer reines
 * Suchen/Ersetzen) - g_ascii_strncasecmp fuer "Groß-/Kleinschreibung nicht
 * beachten", sonst strncmp. */
static gchar *
str_replace_all (const gchar *haystack, const gchar *needle, const gchar *replacement, gboolean case_sensitive)
{
	GString *result;
	gsize needle_len;
	const gchar *p;

	if (haystack == NULL) {
		return g_strdup ("");
	}
	if (needle == NULL || *needle == '\0') {
		return g_strdup (haystack);
	}

	needle_len = strlen (needle);
	result = g_string_new (NULL);
	p = haystack;

	while (*p != '\0') {
		gboolean matches = case_sensitive ?
			(strncmp (p, needle, needle_len) == 0) :
			(g_ascii_strncasecmp (p, needle, needle_len) == 0);

		if (matches && strlen (p) >= needle_len) {
			g_string_append (result, replacement);
			p += needle_len;
		} else {
			g_string_append_c (result, *p);
			p += 1;
		}
	}

	return g_string_free (result, FALSE);
}

static gchar *
apply_case_mode (const gchar *stem, gint mode)
{
	switch (mode) {
	case 1:
		return g_utf8_strup (stem, -1);
	case 2:
		return g_utf8_strdown (stem, -1);
	case 3: {
		gchar *down;
		gunichar first;
		gchar first_upper[7] = { 0 };
		gint len;
		const gchar *rest;
		gchar *result;

		if (stem[0] == '\0') {
			return g_strdup (stem);
		}

		down = g_utf8_strdown (stem, -1);
		first = g_utf8_get_char (down);
		len = g_unichar_to_utf8 (g_unichar_toupper (first), first_upper);
		first_upper[len] = '\0';
		rest = g_utf8_next_char (down);
		result = g_strconcat (first_upper, rest, NULL);
		g_free (down);
		return result;
	}
	default:
		return g_strdup (stem);
	}
}

static void
batch_rename_update_preview (BatchRenameTab *d)
{
	GtkTreeIter iter;
	gboolean valid;
	gint index;
	const gchar *search_text = gtk_entry_get_text (GTK_ENTRY (d->search_entry));
	const gchar *replace_text = gtk_entry_get_text (GTK_ENTRY (d->replace_entry));
	gboolean case_sensitive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->case_sensitive_check));
	gboolean numbering = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->numbering_check));
	gint numbering_position = gtk_combo_box_get_active (GTK_COMBO_BOX (d->numbering_position_combo));
	gint numbering_start = gtk_spin_button_get_value_as_int (d->numbering_start_spin);
	gint numbering_digits = gtk_spin_button_get_value_as_int (d->numbering_digits_spin);
	gint case_mode = gtk_combo_box_get_active (GTK_COMBO_BOX (d->case_mode_combo));

	index = numbering_start;
	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->store), &iter);
	while (valid) {
		gchar *orig_name;
		gboolean is_dir;
		gchar *stem, *ext, *replaced, *cased, *numbered_stem, *new_name;

		gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter,
				     BR_COL_ORIG, &orig_name,
				     BR_COL_IS_DIR, &is_dir,
				     -1);

		split_stem_extension (orig_name, is_dir, &stem, &ext);
		replaced = str_replace_all (stem, search_text, replace_text, case_sensitive);
		cased = apply_case_mode (replaced, case_mode);

		if (numbering) {
			gchar *num_str = g_strdup_printf ("%0*d", numbering_digits, index);
			numbered_stem = (numbering_position == 1) ?
				g_strconcat (cased, "_", num_str, NULL) :
				g_strconcat (num_str, "_", cased, NULL);
			g_free (num_str);
		} else {
			numbered_stem = g_strdup (cased);
		}

		new_name = g_strconcat (numbered_stem, ext, NULL);
		gtk_list_store_set (d->store, &iter, BR_COL_NEW, new_name, -1);

		g_free (orig_name);
		g_free (stem);
		g_free (ext);
		g_free (replaced);
		g_free (cased);
		g_free (numbered_stem);
		g_free (new_name);

		index++;
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->store), &iter);
	}
}

static void
on_batch_rename_options_changed (GtkWidget *widget, gpointer user_data)
{
	batch_rename_update_preview ((BatchRenameTab *) user_data);
}

static void
batch_rename_file_done_cb (NolphinFile *file, GFile *result_location, GError *error, gpointer callback_data)
{
	BatchRenameTab *d = callback_data;

	if (error != NULL) {
		d->failed_renames++;
	}

	d->pending_renames--;
	if (d->pending_renames == 0) {
		gchar *msg = (d->failed_renames > 0) ?
			g_strdup_printf (_("Fertig, %u Datei(en) konnten nicht umbenannt werden."), d->failed_renames) :
			g_strdup (_("Alle Dateien erfolgreich umbenannt."));

		gtk_label_set_text (GTK_LABEL (d->status_label), msg);
		g_free (msg);
		gtk_widget_set_sensitive (d->apply_button, TRUE);
	}
}

static void
on_batch_rename_apply_clicked (GtkButton *button, gpointer user_data)
{
	BatchRenameTab *d = user_data;
	GtkTreeIter iter;
	gboolean valid;

	d->pending_renames = 0;
	d->failed_renames = 0;

	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->store), &iter);
	while (valid) {
		gchar *orig_name, *new_name;

		gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter,
				     BR_COL_ORIG, &orig_name, BR_COL_NEW, &new_name, -1);
		if (g_strcmp0 (orig_name, new_name) != 0 && *new_name != '\0') {
			d->pending_renames++;
		}
		g_free (orig_name);
		g_free (new_name);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->store), &iter);
	}

	if (d->pending_renames == 0) {
		gtk_label_set_text (GTK_LABEL (d->status_label), _("Keine Änderungen zum Anwenden."));
		return;
	}

	gtk_widget_set_sensitive (d->apply_button, FALSE);
	gtk_label_set_text (GTK_LABEL (d->status_label), _("Wird umbenannt …"));

	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->store), &iter);
	while (valid) {
		gchar *orig_name, *new_name;
		NolphinFile *file;

		gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter,
				     BR_COL_ORIG, &orig_name, BR_COL_NEW, &new_name,
				     BR_COL_FILE, &file, -1);
		if (g_strcmp0 (orig_name, new_name) != 0 && *new_name != '\0') {
			nolphin_file_rename (file, new_name, batch_rename_file_done_cb, d);
		}
		g_free (orig_name);
		g_free (new_name);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->store), &iter);
	}
}

static GtkWidget *
build_batch_rename_tab (NolphinWindow *window)
{
	GtkWidget *box, *grid, *label, *scroller, *tree_view;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	BatchRenameTab *d;
	gint row = 0;

	d = g_new0 (BatchRenameTab, 1);
	d->window = window;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_box_pack_start (GTK_BOX (box), build_back_to_preview_button (window), FALSE, FALSE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

	label = gtk_label_new (_("Suchen:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->search_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (d->search_entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), d->search_entry, 1, row, 2, 1);
	row++;

	label = gtk_label_new (_("Ersetzen durch:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->replace_entry = gtk_entry_new ();
	gtk_grid_attach (GTK_GRID (grid), d->replace_entry, 1, row, 2, 1);
	row++;

	d->case_sensitive_check = gtk_check_button_new_with_label (_("Groß-/Kleinschreibung beim Suchen beachten"));
	gtk_grid_attach (GTK_GRID (grid), d->case_sensitive_check, 0, row, 3, 1);
	row++;

	d->numbering_check = gtk_check_button_new_with_label (_("Nummerierung hinzufügen"));
	gtk_grid_attach (GTK_GRID (grid), d->numbering_check, 0, row, 3, 1);
	row++;

	label = gtk_label_new (_("Position:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->numbering_position_combo = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->numbering_position_combo), _("Vor dem Namen"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->numbering_position_combo), _("Nach dem Namen"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (d->numbering_position_combo), 0);
	gtk_grid_attach (GTK_GRID (grid), d->numbering_position_combo, 1, row, 2, 1);
	row++;

	label = gtk_label_new (_("Start bei:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->numbering_start_spin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (0, 99999, 1));
	gtk_spin_button_set_value (d->numbering_start_spin, 1);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (d->numbering_start_spin), 1, row, 1, 1);
	row++;

	label = gtk_label_new (_("Stellen (Ziffern):"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->numbering_digits_spin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (1, 6, 1));
	gtk_spin_button_set_value (d->numbering_digits_spin, 2);
	gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (d->numbering_digits_spin), 1, row, 1, 1);
	row++;

	label = gtk_label_new (_("Groß-/Kleinschreibung:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
	d->case_mode_combo = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->case_mode_combo), _("Unverändert"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->case_mode_combo), _("GROSSBUCHSTABEN"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->case_mode_combo), _("kleinbuchstaben"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->case_mode_combo), _("Erster Buchstabe groß"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (d->case_mode_combo), 0);
	gtk_grid_attach (GTK_GRID (grid), d->case_mode_combo, 1, row, 2, 1);
	row++;

	d->store = gtk_list_store_new (BR_N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN);

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scroller, TRUE);
	gtk_widget_set_size_request (scroller, -1, 200);

	tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->store));

	/* ellipsize statt die volle Textbreite einzufordern - sonst zwingt ein
	 * einziger langer Dateiname die ganze Spalte (und damit das Panel)
	 * zu einer Mindestbreite, die sich beim Schmaler-Ziehen nicht mehr
	 * anpasst, sondern über den sichtbaren Rand hinausragt. */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Aktueller Name"), renderer, "text", BR_COL_ORIG, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Neuer Name"), renderer, "text", BR_COL_NEW, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

	gtk_container_add (GTK_CONTAINER (scroller), tree_view);
	gtk_box_pack_start (GTK_BOX (box), scroller, TRUE, TRUE, 0);

	d->apply_button = gtk_button_new_with_label (_("Anwenden"));
	panel_decorate_button (d->apply_button, "object-select-symbolic", TRUE);
	gtk_widget_set_halign (d->apply_button, GTK_ALIGN_START);
	g_signal_connect (d->apply_button, "clicked", G_CALLBACK (on_batch_rename_apply_clicked), d);
	gtk_box_pack_start (GTK_BOX (box), d->apply_button, FALSE, FALSE, 0);

	d->status_label = gtk_label_new ("");
	gtk_widget_set_halign (d->status_label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), d->status_label, FALSE, FALSE, 0);

	g_signal_connect (d->search_entry, "changed", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->replace_entry, "changed", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->case_sensitive_check, "toggled", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->numbering_check, "toggled", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->numbering_position_combo, "changed", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->numbering_start_spin, "value-changed", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->numbering_digits_spin, "value-changed", G_CALLBACK (on_batch_rename_options_changed), d);
	g_signal_connect (d->case_mode_combo, "changed", G_CALLBACK (on_batch_rename_options_changed), d);

	g_object_set_data_full (G_OBJECT (box), "batch-rename-tab-data", d, g_free);

	return box;
}

static GtkWidget *
build_deb_builder_tab (NolphinWindow *window)
{
	GtkWidget *scroller;
	GtkWidget *outer_box;
	GtkWidget *grid;
	GtkWidget *files_label;
	GtkWidget *files_scroller;
	GtkWidget *files_button_box;
	GtkWidget *add_files_button;
	GtkWidget *add_folder_button;
	GtkWidget *remove_button;
	GtkWidget *build_button;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	DebBuilderTab *d;
	gint row = 0;

	d = g_new0 (DebBuilderTab, 1);
	d->window = window;

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	outer_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width (GTK_CONTAINER (outer_box), 12);
	gtk_container_add (GTK_CONTAINER (scroller), outer_box);
	gtk_box_pack_start (GTK_BOX (outer_box), build_back_to_preview_button (window), FALSE, FALSE, 0);

	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	gtk_box_pack_start (GTK_BOX (outer_box), grid, FALSE, FALSE, 0);

	/* Oben nur das, was jedes Paket braucht; Seltenes steckt eingeklappt
	 * unter "Weitere Angaben", damit die Dateiliste und der Knopf zum
	 * Erstellen ohne Scrollen sichtbar bleiben. */
	d->package_entry = deb_builder_add_row (grid, row++, _("Name des Programms:"));
	d->version_entry = deb_builder_add_row (grid, row++, _("Versionsnummer:"));
	gtk_entry_set_text (GTK_ENTRY (d->version_entry), "1.0.0");
	d->description_entry = deb_builder_add_row (grid, row++, _("Kurzbeschreibung:"));
	d->maintainer_entry = deb_builder_add_row (grid, row++, _("Ersteller (Name):"));

	{
		GtkWidget *label = gtk_label_new (_("Speichern in:"));
		gtk_label_set_xalign (GTK_LABEL (label), 0.0);
		gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

		d->output_chooser = gtk_file_chooser_button_new (_("Zielordner wählen"),
								 GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
		gtk_widget_set_hexpand (d->output_chooser, TRUE);
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (d->output_chooser), g_get_home_dir ());
		gtk_grid_attach (GTK_GRID (grid), d->output_chooser, 1, row, 1, 1);
		row++;
	}

	{
		GtkWidget *more_expander = gtk_expander_new (_("Weitere Angaben"));
		GtkWidget *more_grid = gtk_grid_new ();
		gint mrow = 0;

		gtk_grid_set_row_spacing (GTK_GRID (more_grid), 8);
		gtk_grid_set_column_spacing (GTK_GRID (more_grid), 10);
		gtk_widget_set_margin_top (more_grid, 8);
		gtk_container_add (GTK_CONTAINER (more_expander), more_grid);
		gtk_box_pack_start (GTK_BOX (outer_box), more_expander, FALSE, FALSE, 0);

		{
			GtkWidget *label = gtk_label_new (_("Computertyp:"));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0);
			gtk_grid_attach (GTK_GRID (more_grid), label, 0, mrow, 1, 1);

			d->arch_combo = gtk_combo_box_text_new ();
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->arch_combo), "amd64");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->arch_combo), "arm64");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->arch_combo), "armhf");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->arch_combo), "i386");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->arch_combo), "all");
			gtk_combo_box_set_active (GTK_COMBO_BOX (d->arch_combo), 0);
			gtk_widget_set_hexpand (d->arch_combo, TRUE);
			gtk_grid_attach (GTK_GRID (more_grid), d->arch_combo, 1, mrow, 1, 1);
			mrow++;
		}

		d->email_entry = deb_builder_add_row (more_grid, mrow++, _("E-Mail des Erstellers:"));
		d->section_entry = deb_builder_add_row (more_grid, mrow++, _("Kategorie:"));
		gtk_entry_set_text (GTK_ENTRY (d->section_entry), "utils");

		{
			GtkWidget *label = gtk_label_new (_("Wichtigkeit:"));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0);
			gtk_grid_attach (GTK_GRID (more_grid), label, 0, mrow, 1, 1);

			d->priority_combo = gtk_combo_box_text_new ();
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->priority_combo), "optional");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->priority_combo), "standard");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->priority_combo), "important");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->priority_combo), "required");
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->priority_combo), "extra");
			gtk_combo_box_set_active (GTK_COMBO_BOX (d->priority_combo), 0);
			gtk_widget_set_hexpand (d->priority_combo, TRUE);
			gtk_grid_attach (GTK_GRID (more_grid), d->priority_combo, 1, mrow, 1, 1);
			mrow++;
		}

		d->depends_entry = deb_builder_add_row (more_grid, mrow++, _("Benötigte Programme:"));
		d->homepage_entry = deb_builder_add_row (more_grid, mrow++, _("Webseite:"));
		d->output_name_entry = deb_builder_add_row (more_grid, mrow++, _("Dateiname (optional):"));
	}

	files_label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (files_label), _("<b>Inhalt des Pakets</b>"));
	gtk_label_set_xalign (GTK_LABEL (files_label), 0.0);
	gtk_widget_set_margin_top (files_label, 6);
	gtk_box_pack_start (GTK_BOX (outer_box), files_label, FALSE, FALSE, 0);

	d->files_store = gtk_list_store_new (DEB_FILES_N_COLS,
					     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	d->files_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->files_store));
	g_object_unref (d->files_store);

	/* ellipsize statt die volle Textbreite einzufordern - sonst zwingt ein
	 * einziger langer Pfad die ganze Spalte (und damit das Panel) zu
	 * einer Mindestbreite, die sich beim Schmaler-Ziehen nicht mehr
	 * anpasst, sondern über den sichtbaren Rand hinausragt. */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Datei / Ordner"), renderer, "text", DEB_FILES_COL_NAME, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->files_view), column);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
	g_signal_connect (renderer, "edited", G_CALLBACK (deb_builder_target_edited), d);
	column = gtk_tree_view_column_new_with_attributes (_("Installieren nach"), renderer, "text", DEB_FILES_COL_TARGET, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->files_view), column);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (deb_builder_executable_toggled), d);
	column = gtk_tree_view_column_new_with_attributes (_("Startbar"), renderer, "active", DEB_FILES_COL_EXECUTABLE, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->files_view), column);

	files_scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (files_scroller),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (files_scroller), GTK_SHADOW_IN);
	gtk_widget_set_size_request (files_scroller, -1, 220);
	gtk_container_add (GTK_CONTAINER (files_scroller), d->files_view);
	gtk_box_pack_start (GTK_BOX (outer_box), files_scroller, TRUE, TRUE, 0);

	/* GtkFlowBox statt GtkButtonBox: bricht die Knoepfe automatisch in
	 * eine neue Zeile um, wenn der Platz nicht fuer alle auf einmal
	 * reicht, statt sie ueber den Rand hinausragen zu lassen. */
	files_button_box = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (files_button_box), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (files_button_box), FALSE);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (files_button_box), 6);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (files_button_box), 6);
	gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (files_button_box), 1);
	gtk_box_pack_start (GTK_BOX (outer_box), files_button_box, FALSE, FALSE, 0);

	add_files_button = gtk_button_new_with_label (_("Datei hinzufügen"));
	panel_decorate_button (add_files_button, "list-add-symbolic", FALSE);
	g_signal_connect (add_files_button, "clicked", G_CALLBACK (deb_builder_add_files_clicked), d);
	gtk_container_add (GTK_CONTAINER (files_button_box), add_files_button);

	add_folder_button = gtk_button_new_with_label (_("Ordner hinzufügen"));
	panel_decorate_button (add_folder_button, "folder-new-symbolic", FALSE);
	g_signal_connect (add_folder_button, "clicked", G_CALLBACK (deb_builder_add_folder_clicked), d);
	gtk_container_add (GTK_CONTAINER (files_button_box), add_folder_button);

	remove_button = gtk_button_new_with_label (_("Entfernen"));
	panel_decorate_button (remove_button, "list-remove-symbolic", FALSE);
	g_signal_connect (remove_button, "clicked", G_CALLBACK (deb_builder_remove_file_clicked), d);
	gtk_container_add (GTK_CONTAINER (files_button_box), remove_button);

	build_button = gtk_button_new_with_label (_("DEB erstellen"));
	panel_decorate_button (build_button, "document-save-symbolic", TRUE);
	g_signal_connect (build_button, "clicked", G_CALLBACK (on_deb_build_clicked), d);
	gtk_container_add (GTK_CONTAINER (files_button_box), build_button);

	d->status_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (d->status_label), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (d->status_label), TRUE);
	gtk_box_pack_start (GTK_BOX (outer_box), d->status_label, FALSE, FALSE, 0);

	g_object_set_data_full (G_OBJECT (scroller), "deb-builder-data", d, g_free);

	return scroller;
}

/* --- Archiv verwalten ----------------------------------------------
 * Seite im Archiv-Panel: Inhalt anzeigen, einzelne Einträge entpacken,
 * Dateien hinzufügen/ersetzen/entfernen, Archiv testen, Informationen. */

enum { AM_COL_NAME, AM_COL_SIZE, AM_COL_DATE, AM_COL_PATH, AM_COL_IS_DIR, AM_N_COLS };

typedef struct {
	NolphinWindow *window;
	GtkWidget *title_label;
	GtkWidget *info_label;
	GtkWidget *status_label;
	GtkWidget *spinner;
	GtkWidget *tree;
	GtkListStore *store;
	GtkWidget *buttons[8];   /* extract, add files, add folder, replace, remove, test, refresh */
	GFile *archive;
	NolphinArchiveFormat format;
	gboolean busy;
} ArchiveManagerTab;

/* Größe ab der vor dem Neupacken (TAR-Formate) gewarnt wird */
#define AM_REPACK_WARN_BYTES (50 * 1024 * 1024)

static void archive_manager_reload (ArchiveManagerTab *d);

static void
am_set_busy (ArchiveManagerTab *d, gboolean busy, const gchar *status)
{
	guint i;

	d->busy = busy;
	for (i = 0; i < 7; i++) {
		if (d->buttons[i] != NULL) {
			gtk_widget_set_sensitive (d->buttons[i], !busy && d->archive != NULL);
		}
	}
	if (busy) {
		gtk_spinner_start (GTK_SPINNER (d->spinner));
	} else {
		gtk_spinner_stop (GTK_SPINNER (d->spinner));
	}
	gtk_label_set_text (GTK_LABEL (d->status_label), status != NULL ? status : "");
}

static void
am_show_error (ArchiveManagerTab *d, const gchar *message)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (d->tree);
	GtkWidget *dialog = gtk_message_dialog_new (GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
						    GTK_DIALOG_DESTROY_WITH_PARENT,
						    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/* Gewählte Einträge als GList von Pfaden (Ordner mit "/" am Ende). */
static GList *
am_selected_paths (ArchiveManagerTab *d, guint *n_dirs, guint *n_files)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (d->tree));
	GList *rows = gtk_tree_selection_get_selected_rows (sel, NULL), *l, *paths = NULL;

	*n_dirs = *n_files = 0;
	for (l = rows; l != NULL; l = l->next) {
		GtkTreeIter iter;
		gchar *path = NULL;
		gboolean is_dir = FALSE;

		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (d->store), &iter, l->data)) {
			gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter, AM_COL_PATH, &path, AM_COL_IS_DIR, &is_dir, -1);
			if (is_dir) {
				paths = g_list_append (paths, g_strconcat (path, "/", NULL));
				(*n_dirs)++;
			} else {
				paths = g_list_append (paths, g_strdup (path));
				(*n_files)++;
			}
			g_free (path);
		}
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	return paths;
}

static void
am_fill (ArchiveManagerTab *d, GPtrArray *entries)
{
	goffset total = 0, archive_size = 0;
	guint files = 0, dirs = 0, i;
	GFileInfo *info;
	gchar *text, *size_text, *total_text, *fmt_label;

	gtk_list_store_clear (d->store);
	for (i = 0; i < entries->len; i++) {
		NolphinArchiveEntry *e = entries->pdata[i];
		GtkTreeIter iter;
		gchar *size = (!e->is_dir && e->size >= 0) ? g_format_size (e->size) : NULL;

		gtk_list_store_append (d->store, &iter);
		gtk_list_store_set (d->store, &iter,
				    AM_COL_NAME, e->path,
				    AM_COL_SIZE, size != NULL ? size : "",
				    AM_COL_DATE, e->modified != NULL ? e->modified : "",
				    AM_COL_PATH, e->path,
				    AM_COL_IS_DIR, e->is_dir, -1);
		g_free (size);
		if (e->is_dir) {
			dirs++;
		} else {
			files++;
			if (e->size > 0) {
				total += e->size;
			}
		}
	}

	info = g_file_query_info (d->archive, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info != NULL) {
		archive_size = g_file_info_get_size (info);
		g_object_unref (info);
	}
	size_text = g_format_size (archive_size);
	total_text = g_format_size (total);
	fmt_label = g_strdup (nolphin_archive_format_get_label (d->format));
	text = g_strdup_printf (_("Format: %s · Größe: %s · %u Dateien, %u Ordner · entpackt: %s%s"),
				fmt_label, size_text, files, dirs, total_text,
				nolphin_archive_manage_can_modify (d->format) ? "" : _(" · nur lesbar"));
	gtk_label_set_text (GTK_LABEL (d->info_label), text);
	g_free (text);
	g_free (size_text);
	g_free (total_text);
	g_free (fmt_label);
}

static void
am_list_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	GError *error = NULL;
	GPtrArray *entries = nolphin_archive_list_finish (result, &error);

	if (entries != NULL) {
		am_fill (d, entries);
		g_ptr_array_unref (entries);
		am_set_busy (d, FALSE, NULL);
	} else {
		gtk_list_store_clear (d->store);
		gtk_label_set_text (GTK_LABEL (d->info_label), "");
		am_set_busy (d, FALSE, error != NULL ? error->message : _("Der Inhalt konnte nicht gelesen werden."));
		g_clear_error (&error);
	}
}

static void
archive_manager_reload (ArchiveManagerTab *d)
{
	if (d->archive == NULL) {
		return;
	}
	am_set_busy (d, TRUE, _("Inhalt wird gelesen …"));
	nolphin_archive_list_async (d->archive, NULL, am_list_ready, d);
}

static void
am_op_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	GError *error = NULL;
	gboolean ok = nolphin_archive_manage_finish (result, &error);

	if (ok) {
		archive_manager_reload (d);
		gtk_label_set_text (GTK_LABEL (d->status_label), _("Fertig."));
	} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		/* Im Fenster „Dateivorgänge“ abgebrochen: kein Fehlerdialog. */
		am_set_busy (d, FALSE, _("Abgebrochen."));
		g_clear_error (&error);
		archive_manager_reload (d);
	} else {
		am_set_busy (d, FALSE, error != NULL ? error->message : _("Fehlgeschlagen."));
		if (error != NULL) {
			am_show_error (d, error->message);
		}
		g_clear_error (&error);
		archive_manager_reload (d);
	}
}

/* TAR-Formate: Ändern heißt Neupacken - bei großen Archiven vorher fragen. */
static gboolean
am_confirm_repack (ArchiveManagerTab *d)
{
	GFileInfo *info;
	goffset size = 0;
	GtkWidget *toplevel, *dialog;
	gboolean proceed = TRUE;

	if (!nolphin_archive_manage_needs_repack (d->format)) {
		return TRUE;
	}
	info = g_file_query_info (d->archive, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info != NULL) {
		size = g_file_info_get_size (info);
		g_object_unref (info);
	}
	if (size < AM_REPACK_WARN_BYTES) {
		return TRUE;
	}

	toplevel = gtk_widget_get_toplevel (d->tree);
	dialog = gtk_message_dialog_new (GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
					 GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
					 "%s", _("Archiv muss neu gepackt werden"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
		_("Bei TAR-Archiven ist zum Ändern ein vollständiges Neupacken nötig. Das kann bei großen Archiven lange dauern und braucht temporär Platz für den ganzen Inhalt."));
	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Abbrechen"), GTK_RESPONSE_CANCEL,
				_("_Fortfahren"), GTK_RESPONSE_OK, NULL);
	proceed = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;
	gtk_widget_destroy (dialog);
	return proceed;
}

static GFile *
am_choose_folder (ArchiveManagerTab *d, const gchar *title, const gchar *accept)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (d->tree);
	GtkWidget *chooser = gtk_file_chooser_dialog_new (title,
							  GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
							  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
							  _("_Abbrechen"), GTK_RESPONSE_CANCEL,
							  accept, GTK_RESPONSE_ACCEPT, NULL);
	GFile *result = NULL;

	if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
		result = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (chooser));
	}
	gtk_widget_destroy (chooser);
	return result;
}

static GList *
am_choose_files (ArchiveManagerTab *d, const gchar *title, gboolean multiple)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (d->tree);
	GtkWidget *chooser = gtk_file_chooser_dialog_new (title,
							  GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
							  GTK_FILE_CHOOSER_ACTION_OPEN,
							  _("_Abbrechen"), GTK_RESPONSE_CANCEL,
							  _("_Auswählen"), GTK_RESPONSE_ACCEPT, NULL);
	GSList *chosen = NULL, *s;
	GList *result = NULL;

	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (chooser), multiple);
	nolphin_file_chooser_add_preview (GTK_FILE_CHOOSER (chooser));
	if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
		chosen = gtk_file_chooser_get_files (GTK_FILE_CHOOSER (chooser));
	}
	gtk_widget_destroy (chooser);
	for (s = chosen; s != NULL; s = s->next) {
		result = g_list_append (result, s->data);
	}
	g_slist_free (chosen);
	return result;
}

static void
am_extract_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	guint nd, nf;
	GList *paths = am_selected_paths (d, &nd, &nf);
	GFile *dest = am_choose_folder (d, _("Entpacken nach …"), _("_Entpacken"));

	if (dest != NULL) {
		am_set_busy (d, TRUE, _("Wird entpackt …"));
		if (paths != NULL) {
			nolphin_archive_extract_entries_async (d->archive, paths, dest, NULL, am_op_ready, d);
		} else {
			nolphin_archive_extract_async (d->archive, dest, NULL, am_op_ready, d);
		}
		g_object_unref (dest);
	}
	g_list_free_full (paths, g_free);
}

/* Zielordner im Archiv: der gewählte Ordner, sonst oberste Ebene */
static gchar *
am_target_dir (ArchiveManagerTab *d)
{
	guint nd, nf;
	GList *paths = am_selected_paths (d, &nd, &nf);
	gchar *target = NULL;

	if (nd == 1 && nf == 0 && paths != NULL) {
		target = g_strdup (paths->data);
	}
	g_list_free_full (paths, g_free);
	return target;
}

static void
am_add_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	gboolean folder = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "am-folder"));
	GList *sources = NULL;

	if (!nolphin_archive_manage_can_modify (d->format)) {
		am_show_error (d, _("Dieses Archivformat kann nur gelesen, aber nicht geändert werden."));
		return;
	}
	if (folder) {
		GFile *f = am_choose_folder (d, _("Ordner hinzufügen"), _("_Hinzufügen"));

		if (f != NULL) {
			sources = g_list_append (NULL, f);
		}
	} else {
		sources = am_choose_files (d, _("Dateien hinzufügen"), TRUE);
	}
	if (sources != NULL && am_confirm_repack (d)) {
		gchar *target = am_target_dir (d);

		am_set_busy (d, TRUE, _("Wird hinzugefügt …"));
		nolphin_archive_add_files_async (d->archive, sources, target, NULL, am_op_ready, d);
		g_free (target);
	}
	g_list_free_full (sources, g_object_unref);
}

static void
am_replace_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	guint nd, nf;
	GList *paths = am_selected_paths (d, &nd, &nf), *files;

	if (!nolphin_archive_manage_can_modify (d->format)) {
		am_show_error (d, _("Dieses Archivformat kann nur gelesen, aber nicht geändert werden."));
	} else if (nf != 1 || nd != 0) {
		am_show_error (d, _("Zum Ersetzen genau eine Datei im Archiv auswählen."));
	} else {
		files = am_choose_files (d, _("Ersetzen durch …"), FALSE);
		if (files != NULL && am_confirm_repack (d)) {
			am_set_busy (d, TRUE, _("Wird ersetzt …"));
			nolphin_archive_replace_entry_async (d->archive, paths->data, files->data, NULL, am_op_ready, d);
		}
		g_list_free_full (files, g_object_unref);
	}
	g_list_free_full (paths, g_free);
}

static void
am_remove_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	guint nd, nf;
	GList *paths = am_selected_paths (d, &nd, &nf);

	if (!nolphin_archive_manage_can_modify (d->format)) {
		am_show_error (d, _("Dieses Archivformat kann nur gelesen, aber nicht geändert werden."));
	} else if (paths == NULL) {
		am_show_error (d, _("Keine Einträge ausgewählt."));
	} else {
		GtkWidget *toplevel = gtk_widget_get_toplevel (d->tree);
		GtkWidget *dialog = gtk_message_dialog_new (GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
							    GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
							    GTK_BUTTONS_NONE, "%s", _("Einträge aus dem Archiv entfernen?"));
		gboolean go;

		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
			ngettext ("%u Datei und %u Ordner werden aus dem Archiv gelöscht. Das lässt sich nicht rückgängig machen.",
				  "%u Dateien und %u Ordner werden aus dem Archiv gelöscht. Das lässt sich nicht rückgängig machen.", nf),
			nf, nd);
		gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Abbrechen"), GTK_RESPONSE_CANCEL,
					_("_Entfernen"), GTK_RESPONSE_OK, NULL);
		go = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK;
		gtk_widget_destroy (dialog);

		if (go && am_confirm_repack (d)) {
			am_set_busy (d, TRUE, _("Wird entfernt …"));
			nolphin_archive_remove_entries_async (d->archive, paths, NULL, am_op_ready, d);
		}
	}
	g_list_free_full (paths, g_free);
}

static void
am_test_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;
	GError *error = NULL;

	if (nolphin_archive_test_finish (result, &error)) {
		am_set_busy (d, FALSE, _("Archiv in Ordnung: keine Fehler gefunden."));
	} else {
		am_set_busy (d, FALSE, error != NULL ? error->message : _("Der Test ist fehlgeschlagen."));
		g_clear_error (&error);
	}
}

static void
am_test_clicked (GtkButton *button, gpointer user_data)
{
	ArchiveManagerTab *d = user_data;

	am_set_busy (d, TRUE, _("Archiv wird geprüft …"));
	nolphin_archive_test_async (d->archive, NULL, am_test_ready, d);
}

static void
am_refresh_clicked (GtkButton *button, gpointer user_data)
{
	archive_manager_reload (user_data);
}

static void
am_free (gpointer data)
{
	ArchiveManagerTab *d = data;

	g_clear_object (&d->archive);
	g_free (d);
}

static GtkWidget *
am_button (ArchiveManagerTab *d, GtkWidget *box, guint index, const gchar *label, const gchar *icon,
	   GCallback cb, gboolean folder)
{
	GtkWidget *b = gtk_button_new_with_label (label);

	panel_decorate_button (b, icon, FALSE);
	g_object_set_data (G_OBJECT (b), "am-folder", GINT_TO_POINTER (folder));
	g_signal_connect (b, "clicked", cb, d);
	gtk_container_add (GTK_CONTAINER (box), b);
	d->buttons[index] = b;
	return b;
}

static GtkWidget *
build_archive_manager_tab (NolphinWindow *window)
{
	ArchiveManagerTab *d = g_new0 (ArchiveManagerTab, 1);
	GtkWidget *outer, *scroller, *tree_scroller, *flow, *status_row;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	d->window = window;
	d->store = gtk_list_store_new (AM_N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width (GTK_CONTAINER (outer), 12);
	gtk_box_pack_start (GTK_BOX (outer), build_back_to_preview_button (window), FALSE, FALSE, 0);

	d->title_label = gtk_label_new ("");
	gtk_widget_set_halign (d->title_label, GTK_ALIGN_START);
	gtk_label_set_ellipsize (GTK_LABEL (d->title_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->title_label), "heading");
	gtk_box_pack_start (GTK_BOX (outer), d->title_label, FALSE, FALSE, 0);

	d->info_label = gtk_label_new ("");
	gtk_widget_set_halign (d->info_label, GTK_ALIGN_START);
	gtk_label_set_line_wrap (GTK_LABEL (d->info_label), TRUE);
	gtk_label_set_selectable (GTK_LABEL (d->info_label), TRUE);
	panel_dim_label (d->info_label);
	gtk_box_pack_start (GTK_BOX (outer), d->info_label, FALSE, FALSE, 0);

	/* Knöpfe in einer umbrechenden Leiste, damit sie im schmalen Panel nicht abgeschnitten werden */
	flow = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), FALSE);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 4);
	gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 4);
	gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 4);
	gtk_box_pack_start (GTK_BOX (outer), flow, FALSE, FALSE, 0);
	am_button (d, flow, 0, _("Entpacken …"), "document-save-symbolic", G_CALLBACK (am_extract_clicked), FALSE);
	am_button (d, flow, 1, _("Dateien hinzufügen …"), "list-add-symbolic", G_CALLBACK (am_add_clicked), FALSE);
	am_button (d, flow, 2, _("Ordner hinzufügen …"), "folder-new-symbolic", G_CALLBACK (am_add_clicked), TRUE);
	am_button (d, flow, 3, _("Ersetzen …"), "view-refresh-symbolic", G_CALLBACK (am_replace_clicked), FALSE);
	am_button (d, flow, 4, _("Entfernen"), "list-remove-symbolic", G_CALLBACK (am_remove_clicked), FALSE);
	am_button (d, flow, 5, _("Prüfen"), "emblem-ok-symbolic", G_CALLBACK (am_test_clicked), FALSE);
	am_button (d, flow, 6, _("Neu laden"), "view-refresh-symbolic", G_CALLBACK (am_refresh_clicked), FALSE);

	d->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->store));
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (d->tree)), GTK_SELECTION_MULTIPLE);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer, "text", AM_COL_NAME, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->tree), column);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "xalign", 1.0f, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Größe"), renderer, "text", AM_COL_SIZE, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->tree), column);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Geändert"), renderer, "text", AM_COL_DATE, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->tree), column);

	tree_scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tree_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (tree_scroller, -1, 260);
	gtk_container_add (GTK_CONTAINER (tree_scroller), d->tree);
	gtk_box_pack_start (GTK_BOX (outer), tree_scroller, TRUE, TRUE, 0);

	status_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	d->spinner = gtk_spinner_new ();
	gtk_box_pack_start (GTK_BOX (status_row), d->spinner, FALSE, FALSE, 0);
	d->status_label = gtk_label_new ("");
	gtk_widget_set_halign (d->status_label, GTK_ALIGN_START);
	gtk_label_set_line_wrap (GTK_LABEL (d->status_label), TRUE);
	gtk_label_set_selectable (GTK_LABEL (d->status_label), TRUE);
	gtk_box_pack_start (GTK_BOX (status_row), d->status_label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (outer), status_row, FALSE, FALSE, 0);

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroller), outer);
	g_object_set_data_full (G_OBJECT (scroller), "archive-manager-data", d, am_free);

	return scroller;
}

/* Die DEB-Paket-Erstellung ist Teil des Archiv-Panels, kein
 * eigenes Panel - beide Formulare (Komprimieren, .deb-Paket erstellen)
 * teilen sich deshalb denselben aeusseren "archive"-Platz, statt zwei
 * getrennte oberste Stack-Seiten zu sein. */
static GtkWidget *
build_archive_panel (NolphinWindow *window, GtkWidget **out_inner_stack)
{
	GtkWidget *inner_stack;

	/* Kein manueller Umschalter zwischen "Komprimieren" und ".deb-Paket
	 * erstellen" - wie jedes andere Panel wird die jeweilige Seite
	 * ausschliesslich ueber ihren eigenen Ausloeser erreicht
	 * (Kontextmenue/Menueleiste), siehe nolphin_workspace_panel_show_archive()
	 * bzw. _show_deb_builder(). Der innere Stack existiert nur, damit
	 * beide Formulare denselben aeusseren "archive"-Platz teilen. */
	inner_stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (inner_stack), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_set_hhomogeneous (GTK_STACK (inner_stack), FALSE);

	gtk_stack_add_named (GTK_STACK (inner_stack), build_archive_tab (window), "compress");
	gtk_stack_add_named (GTK_STACK (inner_stack), build_deb_builder_tab (window), "deb");
	gtk_stack_add_named (GTK_STACK (inner_stack), build_archive_manager_tab (window), "manage");
	gtk_stack_set_visible_child_name (GTK_STACK (inner_stack), "compress");

	if (out_inner_stack != NULL) {
		*out_inner_stack = inner_stack;
	}

	return inner_stack;
}

/* --- Arbeitsbereiche -----------------------------------------------
 * Benannte Schnappschüsse der Sitzung (Reiter, Teilung, Fenstergröße,
 * Panels), abgelegt unter ~/.config/nolphin/workspaces/. Speichern, Laden,
 * Duplizieren und Löschen laufen in dieser Panel-Seite, ohne eigenes Fenster. */

enum { WS_COL_NAME, WS_COL_PATH, WS_COL_INFO, WS_N_COLS };

typedef struct {
	NolphinWindow *window;
	GtkWidget *name_entry;
	GtkWidget *tree;
	GtkListStore *store;
	GtkWidget *status_label;
} WorkspacesTab;

static gchar *
ws_directory (void)
{
	gchar *dir = g_build_filename (g_get_user_config_dir (), "nolphin", "workspaces", NULL);

	g_mkdir_with_parents (dir, 0700);
	return dir;
}

static void
ws_set_status (WorkspacesTab *d, const gchar *text)
{
	gtk_label_set_text (GTK_LABEL (d->status_label), text != NULL ? text : "");
}

static void
ws_reload (WorkspacesTab *d)
{
	gchar *dir = ws_directory ();
	GDir *gd = g_dir_open (dir, 0, NULL);
	const gchar *file;
	GList *names = NULL, *l;

	gtk_list_store_clear (d->store);
	if (gd != NULL) {
		while ((file = g_dir_read_name (gd)) != NULL) {
			if (g_str_has_suffix (file, ".ini")) {
				names = g_list_prepend (names, g_strdup (file));
			}
		}
		g_dir_close (gd);
	}
	names = g_list_sort (names, (GCompareFunc) g_utf8_collate);

	for (l = names; l != NULL; l = l->next) {
		gchar *path = g_build_filename (dir, l->data, NULL);
		GKeyFile *kf = g_key_file_new ();

		if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
			gchar *name = g_key_file_get_string (kf, "Workspace", "Name", NULL);
			gchar **left = g_key_file_get_string_list (kf, "Workspace", "tabs-left", NULL, NULL);
			gchar **right = g_key_file_get_string_list (kf, "Workspace", "tabs-right", NULL, NULL);
			guint nl = left != NULL ? g_strv_length (left) : 0, nr = right != NULL ? g_strv_length (right) : 0;
			gchar *info = (nr > 0) ? g_strdup_printf (_("%u + %u Reiter, geteilt"), nl, nr)
					       : g_strdup_printf (ngettext ("%u Reiter", "%u Reiter", nl), nl);
			GtkTreeIter iter;

			gtk_list_store_append (d->store, &iter);
			gtk_list_store_set (d->store, &iter, WS_COL_NAME, name != NULL ? name : (const gchar *) l->data,
					    WS_COL_PATH, path, WS_COL_INFO, info, -1);
			g_free (name);
			g_free (info);
			g_strfreev (left);
			g_strfreev (right);
		}
		g_key_file_free (kf);
		g_free (path);
	}
	g_list_free_full (names, g_free);
	g_free (dir);
}

static gchar *
ws_selected_path (WorkspacesTab *d, gchar **out_name)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	gchar *path = NULL;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (d->tree)), &model, &iter)) {
		gtk_tree_model_get (model, &iter, WS_COL_PATH, &path, WS_COL_NAME, out_name, -1);
	}
	return path;
}

/* Eindeutiger Dateiname für @name im Arbeitsbereichs-Ordner */
static gchar *
ws_new_path (const gchar *name)
{
	gchar *dir = ws_directory (), *safe = g_strdup (name), *path = NULL;
	guint i;

	for (i = 0; safe[i] != '\0'; i++) {
		if (safe[i] == '/' || safe[i] == '\\' || g_ascii_iscntrl (safe[i])) {
			safe[i] = '_';
		}
	}
	for (i = 0; i < 1000; i++) {
		gchar *file = i == 0 ? g_strdup_printf ("%s.ini", safe) : g_strdup_printf ("%s-%u.ini", safe, i);

		g_free (path);
		path = g_build_filename (dir, file, NULL);
		g_free (file);
		if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
			break;
		}
	}
	g_free (safe);
	g_free (dir);
	return path;
}

static void
ws_write (WorkspacesTab *d, GKeyFile *kf, const gchar *name)
{
	gchar *path = NULL, *data;
	GtkTreeIter iter;
	GError *error = NULL;
	gboolean found = FALSE;
	gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->store), &iter);

	/* Gleicher Name überschreibt den vorhandenen Arbeitsbereich */
	while (valid) {
		gchar *n = NULL, *p = NULL;

		gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter, WS_COL_NAME, &n, WS_COL_PATH, &p, -1);
		if (g_strcmp0 (n, name) == 0) {
			path = p;
			found = TRUE;
		} else {
			g_free (p);
		}
		g_free (n);
		if (found) {
			break;
		}
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->store), &iter);
	}
	if (path == NULL) {
		path = ws_new_path (name);
	}

	g_key_file_set_string (kf, "Workspace", "Name", name);
	data = g_key_file_to_data (kf, NULL, NULL);
	if (g_file_set_contents (path, data, -1, &error)) {
		ws_set_status (d, found ? _("Arbeitsbereich aktualisiert.") : _("Arbeitsbereich gespeichert."));
	} else {
		ws_set_status (d, error->message);
		g_clear_error (&error);
	}
	g_free (data);
	g_free (path);
	ws_reload (d);
}

static void
ws_save_clicked (GtkButton *button, gpointer user_data)
{
	WorkspacesTab *d = user_data;
	gchar *name = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (d->name_entry))));
	GKeyFile *kf;

	if (name[0] == '\0') {
		ws_set_status (d, _("Bitte einen Namen eingeben."));
		g_free (name);
		return;
	}
	kf = g_key_file_new ();
	nolphin_window_workspace_capture (d->window, kf);
	ws_write (d, kf, name);
	g_key_file_free (kf);
	g_free (name);
}

static void
ws_load_clicked (GtkButton *button, gpointer user_data)
{
	WorkspacesTab *d = user_data;
	gchar *name = NULL, *path = ws_selected_path (d, &name);
	GKeyFile *kf = g_key_file_new ();

	if (path == NULL) {
		ws_set_status (d, _("Bitte einen Arbeitsbereich in der Liste auswählen."));
	} else if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
		ws_set_status (d, _("Der Arbeitsbereich konnte nicht gelesen werden."));
	} else {
		gchar *text;

		nolphin_window_workspace_apply (d->window, kf);
		text = g_strdup_printf (_("Arbeitsbereich »%s« geladen."), name);
		ws_set_status (d, text);
		g_free (text);
	}
	g_key_file_free (kf);
	g_free (name);
	g_free (path);
}

static void
ws_duplicate_clicked (GtkButton *button, gpointer user_data)
{
	WorkspacesTab *d = user_data;
	gchar *name = NULL, *path = ws_selected_path (d, &name);
	GKeyFile *kf = g_key_file_new ();

	if (path == NULL) {
		ws_set_status (d, _("Bitte einen Arbeitsbereich in der Liste auswählen."));
	} else if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
		gchar *copy_name = g_strdup_printf (_("%s (Kopie)"), name);

		ws_write (d, kf, copy_name);
		g_free (copy_name);
	}
	g_key_file_free (kf);
	g_free (name);
	g_free (path);
}

static void
ws_delete_clicked (GtkButton *button, gpointer user_data)
{
	WorkspacesTab *d = user_data;
	gchar *name = NULL, *path = ws_selected_path (d, &name);

	if (path == NULL) {
		ws_set_status (d, _("Bitte einen Arbeitsbereich in der Liste auswählen."));
	} else {
		GtkWidget *toplevel = gtk_widget_get_toplevel (d->tree);
		GtkWidget *dialog = gtk_message_dialog_new (GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL,
							    GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
							    GTK_BUTTONS_NONE, _("Arbeitsbereich »%s« löschen?"), name);

		gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Abbrechen"), GTK_RESPONSE_CANCEL,
					_("_Löschen"), GTK_RESPONSE_OK, NULL);
		if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
			if (g_remove (path) == 0) {
				ws_set_status (d, _("Arbeitsbereich gelöscht."));
			} else {
				ws_set_status (d, _("Der Arbeitsbereich konnte nicht gelöscht werden."));
			}
			ws_reload (d);
		}
		gtk_widget_destroy (dialog);
	}
	g_free (name);
	g_free (path);
}

static void
ws_row_activated (GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
	ws_load_clicked (NULL, user_data);
}

static GtkWidget *
build_workspaces_tab (NolphinWindow *window)
{
	WorkspacesTab *d = g_new0 (WorkspacesTab, 1);
	GtkWidget *outer, *row, *label, *scroller, *tree_scroller, *b;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	d->window = window;
	d->store = gtk_list_store_new (WS_N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width (GTK_CONTAINER (outer), 12);
	gtk_box_pack_start (GTK_BOX (outer), build_back_to_preview_button (window), FALSE, FALSE, 0);

	label = gtk_label_new (_("Arbeitsbereiche"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_style_context_add_class (gtk_widget_get_style_context (label), "heading");
	gtk_box_pack_start (GTK_BOX (outer), label, FALSE, FALSE, 0);

	label = gtk_label_new (_("Ein Arbeitsbereich merkt sich Reiter, Teilung, Fenstergröße und die Panels."));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	panel_dim_label (label);
	gtk_box_pack_start (GTK_BOX (outer), label, FALSE, FALSE, 0);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	d->name_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (d->name_entry), _("Name des Arbeitsbereichs"));
	gtk_box_pack_start (GTK_BOX (row), d->name_entry, TRUE, TRUE, 0);
	b = gtk_button_new_with_label (_("Speichern"));
	panel_decorate_button (b, "document-save-symbolic", TRUE);
	g_signal_connect (b, "clicked", G_CALLBACK (ws_save_clicked), d);
	g_signal_connect (d->name_entry, "activate", G_CALLBACK (ws_save_clicked), d);
	gtk_box_pack_start (GTK_BOX (row), b, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (outer), row, FALSE, FALSE, 0);

	d->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->store));
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer, "text", WS_COL_NAME, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->tree), column);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Inhalt"), renderer, "text", WS_COL_INFO, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (d->tree), column);
	g_signal_connect (d->tree, "row-activated", G_CALLBACK (ws_row_activated), d);

	tree_scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tree_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (tree_scroller, -1, 200);
	gtk_container_add (GTK_CONTAINER (tree_scroller), d->tree);
	gtk_box_pack_start (GTK_BOX (outer), tree_scroller, TRUE, TRUE, 0);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	b = gtk_button_new_with_label (_("Laden"));
	panel_decorate_button (b, "document-open-symbolic", FALSE);
	g_signal_connect (b, "clicked", G_CALLBACK (ws_load_clicked), d);
	gtk_box_pack_start (GTK_BOX (row), b, FALSE, FALSE, 0);
	b = gtk_button_new_with_label (_("Duplizieren"));
	panel_decorate_button (b, "edit-copy-symbolic", FALSE);
	g_signal_connect (b, "clicked", G_CALLBACK (ws_duplicate_clicked), d);
	gtk_box_pack_start (GTK_BOX (row), b, FALSE, FALSE, 0);
	b = gtk_button_new_with_label (_("Löschen"));
	panel_decorate_button (b, "edit-delete-symbolic", FALSE);
	g_signal_connect (b, "clicked", G_CALLBACK (ws_delete_clicked), d);
	gtk_box_pack_start (GTK_BOX (row), b, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (outer), row, FALSE, FALSE, 0);

	d->status_label = gtk_label_new ("");
	gtk_widget_set_halign (d->status_label, GTK_ALIGN_START);
	gtk_label_set_line_wrap (GTK_LABEL (d->status_label), TRUE);
	gtk_box_pack_start (GTK_BOX (outer), d->status_label, FALSE, FALSE, 0);

	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroller), outer);
	g_object_set_data_full (G_OBJECT (scroller), "workspaces-data", d, g_free);
	ws_reload (d);

	return scroller;
}

/* Das Panel ist ein GtkStack ohne Reiterleiste, keine Dauer-Werkzeugleiste
 * mit sieben immer sichtbaren Reitern: "preview" ist die Ruhelage (die
 * bisherige F11-Vorschau) und bleibt sichtbar, solange nichts anderes
 * angefordert wurde. Eigenschaften/Archiv/Git/.deb-Paket blenden sich nur
 * ein, wenn ihre jeweilige Funktion tatsächlich ausgelöst wird (Menü,
 * Kontextmenü, Alt+Enter, …) - über die nolphin_workspace_panel_show_*()
 * Funktionen unten - und nicht als Dauerzustand. Terminal und Suche
 * brauchen keine eigene Seite: F4 bzw. Strg+F/Strg+I bedienen sie längst
 * direkt. */
static void
add_stack_page (GtkStack *stack, GtkWidget *content, const gchar *name)
{
	gtk_widget_show_all (content);
	gtk_stack_add_named (stack, content, name);
}

/* Ein Farbton dunkler als der Hauptbereich, wie es die rechte Leiste
 * ursprünglich hatte - nur auf dieses Panel begrenzt (widget-eigener
 * GtkCssProvider), nicht global fürs ganze Fenster. */
static void
apply_workspace_panel_background (GtkWidget *stack)
{
	GtkCssProvider *provider = gtk_css_provider_new ();

	gtk_css_provider_load_from_data (provider,
					 "stack.nolphin-workspace-panel, "
					 "stack.nolphin-workspace-panel > * { "
					 "  background-image: none; "
					 "  background-color: shade(@theme_bg_color, 0.92); "
					 "}",
					 -1, NULL);
	gtk_style_context_add_class (gtk_widget_get_style_context (stack), "nolphin-workspace-panel");
	gtk_style_context_add_provider (gtk_widget_get_style_context (stack),
					GTK_STYLE_PROVIDER (provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}

/* Terminal-Seite: derselbe "← Zur Vorschau"-Knopf oben wie bei den
 * anderen Reitern, darunter das echte, uebergebene Terminal-Widget
 * (kein zweites Terminal - dasselbe, das frueher im unteren
 * Fensterbereich lag). */
static void
on_terminal_copy_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_copy (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_paste_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_paste (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_copy_html_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_copy_html (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_select_all_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_select_all (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_zoom_in_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_zoom_in (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_zoom_out_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_zoom_out (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_zoom_reset_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_zoom_reset (NOLPHIN_TERMINAL (user_data));
}

static void
on_terminal_reset_activate (GtkMenuItem *item, gpointer user_data)
{
	nolphin_terminal_reset (NOLPHIN_TERMINAL (user_data));
}

/* Menüleiste ins Panel eingebaut (kein eigenes Fenster) - nur Einträge,
 * die auch tatsächlich etwas tun: "Datei"/"Suchen"/"Hilfe" aus
 * einem gewöhnlichen Terminalfenster fehlen bewusst, dafür gibt es hier
 * (noch) keine echte Funktion (Tabs, Scrollback-Suche). */
typedef struct {
	GtkWidget *terminal_widget;
	NolphinWindow *window;
} TerminalTabData;

/* Referenz-Reihenfolge "Form der Eingabemarke": 0 = Rechteck (Block),
 * 1 = Unterstrich, 2 = Senkrechter Balken (I-Beam) - alle drei sind
 * echte VTE-Cursorformen (vte_terminal_set_cursor_shape()). */
static void
on_terminal_cursor_style_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;
	NolphinCursorShape shape;

	switch (gtk_combo_box_get_active (combo)) {
	case 0: shape = NOLPHIN_CURSOR_SHAPE_BLOCK; break;
	case 2: shape = NOLPHIN_CURSOR_SHAPE_IBEAM; break;
	default: shape = NOLPHIN_CURSOR_SHAPE_UNDERLINE; break;
	}
	nolphin_terminal_set_cursor_shape (NOLPHIN_TERMINAL (terminal_widget), shape);
}

/* "Blinkende Eingabemarke": Immer/Nie/Vorgabe - VTE kennt alle drei
 * Zustaende wirklich (VTE_CURSOR_BLINK_ON/OFF/SYSTEM), "Vorgabe" folgt
 * also tatsaechlich der GTK-Systemeinstellung statt eines Alias. */
static void
on_terminal_cursor_blink_combo_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;
	NolphinCursorBlinkMode mode;

	switch (gtk_combo_box_get_active (combo)) {
	case 0: mode = NOLPHIN_CURSOR_BLINK_ALWAYS; break;
	case 1: mode = NOLPHIN_CURSOR_BLINK_NEVER; break;
	default: mode = NOLPHIN_CURSOR_BLINK_SYSTEM; break;
	}
	nolphin_terminal_set_cursor_blink_mode (NOLPHIN_TERMINAL (terminal_widget), mode);
}

/* "Blinkenden Text erlauben" hat in der Referenz i. d. R. mehr Eintraege;
 * VTE kennt dafuer intern vier Blink-Modi (Never/Focused/Unfocused/
 * Always) - fuer diese einfache Ein/Aus-Checkbox werden nur die beiden
 * Extremwerte verwendet. */
static void
on_terminal_text_blink_combo_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_text_blink (NOLPHIN_TERMINAL (terminal_widget),
					 gtk_combo_box_get_active (combo) == 0);
}

typedef struct {
	GtkWidget *terminal_widget;
	GtkWidget *font_button;
} CustomFontData;

/* "Benutzerdefinierte Schrift": Familie UND Groesse aus der
 * GtkFontButton-Auswahl wirken sich wirklich aus (vte_terminal_set_font()). */
static void
on_terminal_custom_font_set (GtkFontButton *font_button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;
	PangoFontDescription *desc = pango_font_description_from_string (
		gtk_font_chooser_get_font (GTK_FONT_CHOOSER (font_button)));
	gint size_pt = pango_font_description_get_size (desc) / PANGO_SCALE;
	const gchar *family = pango_font_description_get_family (desc);

	if (family != NULL) {
		nolphin_terminal_set_font_family (NOLPHIN_TERMINAL (terminal_widget), family);
	}
	if (size_pt > 0) {
		nolphin_terminal_set_font_size (NOLPHIN_TERMINAL (terminal_widget), size_pt);
	}
	pango_font_description_free (desc);
}

static void
on_terminal_custom_font_toggled (GtkToggleButton *button, gpointer user_data)
{
	CustomFontData *cfd = user_data;
	gboolean active = gtk_toggle_button_get_active (button);

	gtk_widget_set_sensitive (cfd->font_button, active);
	if (active) {
		on_terminal_custom_font_set (GTK_FONT_BUTTON (cfd->font_button), cfd->terminal_widget);
	} else {
		nolphin_terminal_set_font_family (NOLPHIN_TERMINAL (cfd->terminal_widget), "Monospace");
	}
}

static void
on_terminal_color_scheme_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_solarized (NOLPHIN_TERMINAL (terminal_widget),
					gtk_combo_box_get_active (combo) == 1);
}

static void
on_terminal_use_system_colors_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_use_system_colors (NOLPHIN_TERMINAL (terminal_widget),
						gtk_toggle_button_get_active (button));
}

static void
on_terminal_bold_is_bright_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_bold_is_bright (NOLPHIN_TERMINAL (terminal_widget),
					     gtk_toggle_button_get_active (button));
}

static void
on_terminal_backspace_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_backspace_binding (NOLPHIN_TERMINAL (terminal_widget),
						(NolphinEraseBinding) gtk_combo_box_get_active (combo));
}

static void
on_terminal_delete_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_delete_binding (NOLPHIN_TERMINAL (terminal_widget),
					     (NolphinEraseBinding) gtk_combo_box_get_active (combo));
}

typedef struct {
	GtkWidget *terminal_widget;
	GtkWidget *width_spin;
	GtkWidget *height_spin;
} CellSpacingData;

static void
on_terminal_cell_spacing_changed (GtkSpinButton *spin, gpointer user_data)
{
	CellSpacingData *csd = user_data;

	(void) spin;
	nolphin_terminal_set_cell_spacing (NOLPHIN_TERMINAL (csd->terminal_widget),
					   gtk_spin_button_get_value (GTK_SPIN_BUTTON (csd->width_spin)),
					   gtk_spin_button_get_value (GTK_SPIN_BUTTON (csd->height_spin)));
}

static void
on_terminal_bell_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_bell_enabled (NOLPHIN_TERMINAL (terminal_widget),
					   gtk_toggle_button_get_active (button));
}

static void
on_terminal_show_scrollbar_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_show_scrollbar (NOLPHIN_TERMINAL (terminal_widget),
					     gtk_toggle_button_get_active (button));
}

static void
on_terminal_scroll_on_paste_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_scroll_on_paste (NOLPHIN_TERMINAL (terminal_widget),
					      gtk_toggle_button_get_active (button));
}

typedef struct {
	GtkWidget *terminal_widget;
	GtkWidget *cols_spin;
	GtkWidget *rows_spin;
} InitialSizeData;

static void
on_terminal_initial_size_changed (GtkSpinButton *spin, gpointer user_data)
{
	InitialSizeData *isd = user_data;

	(void) spin;
	nolphin_terminal_set_initial_size (
		NOLPHIN_TERMINAL (isd->terminal_widget),
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (isd->cols_spin)),
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (isd->rows_spin)));
}

static void
on_terminal_initial_size_reset_clicked (GtkButton *button, gpointer user_data)
{
	InitialSizeData *isd = user_data;

	(void) button;
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (isd->cols_spin), 80);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (isd->rows_spin), 24);
}

static void
on_terminal_cell_spacing_reset_clicked (GtkButton *button, gpointer user_data)
{
	CellSpacingData *csd = user_data;

	(void) button;
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (csd->width_spin), 1.0);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (csd->height_spin), 1.0);
}

static void
on_terminal_scrollback_lines_changed (GtkSpinButton *spin, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_scrollback_lines (NOLPHIN_TERMINAL (terminal_widget),
					       gtk_spin_button_get_value_as_int (spin));
}

static void
on_terminal_scroll_on_output_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_scroll_on_output (NOLPHIN_TERMINAL (terminal_widget),
					       gtk_toggle_button_get_active (button));
}

static void
on_terminal_scroll_on_keystroke_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_scroll_on_keystroke (NOLPHIN_TERMINAL (terminal_widget),
						  gtk_toggle_button_get_active (button));
}

static void
on_terminal_login_shell_toggled (GtkToggleButton *button, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_login_shell (NOLPHIN_TERMINAL (terminal_widget),
					  gtk_toggle_button_get_active (button));
}

typedef struct {
	GtkWidget *terminal_widget;
	GtkWidget *use_custom_check;
	GtkWidget *command_entry;
} CustomCommandData;

static void
on_terminal_custom_command_changed (GtkWidget *widget, gpointer user_data)
{
	CustomCommandData *ccd = user_data;

	(void) widget;
	nolphin_terminal_set_custom_command (
		NOLPHIN_TERMINAL (ccd->terminal_widget),
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ccd->use_custom_check)),
		gtk_entry_get_text (GTK_ENTRY (ccd->command_entry)));
	gtk_widget_set_sensitive (ccd->command_entry,
				  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ccd->use_custom_check)));
}

static void
on_terminal_follow_location_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_follow_location (NOLPHIN_TERMINAL (terminal_widget),
					       gtk_combo_box_get_active (combo) == 1);
}

static void
on_terminal_exit_action_changed (GtkComboBox *combo, gpointer user_data)
{
	GtkWidget *terminal_widget = user_data;

	nolphin_terminal_set_exit_action (NOLPHIN_TERMINAL (terminal_widget),
		gtk_combo_box_get_active (combo) == 1 ?
			NOLPHIN_TERMINAL_EXIT_RESTART : NOLPHIN_TERMINAL_EXIT_HOLD);
}

/* Terminal-Einstellungen als eigenes Fenster mit Seitenleiste (Global:
 * Allgemein / Tastenkombinationen; Profile: aktuelles Profil), Layout
 * und Aufbau exakt nach der vom Benutzer vorgegebenen Referenz. Aus-
 * druckliche Ausnahme (auf expliziten Wunsch): Einstellungen
 * zaehlt nicht zu den Pflicht-Panels, ein eigenes Fenster ist hier
 * zulaessig. Der Profil-Teil (Text/Farben/Bildlauf/Befehl/Kompatibi-
 * litaet) wirkt sofort auf das echte Terminal - keine Attrappen. Die
 * "Global"-Regler (Menueleiste/Mnemonics/Menuetastenkombination/Standard-
 * terminal-Pruefung/Tastenkombinationen) sind ueber org.nolphin.terminal
 * in GSettings/dconf persistiert (siehe prefs_get_terminal_settings()),
 * nicht nur Sitzungszustand. Die Reiterposition-Combobox hat dagegen
 * bewusst keinen Schalter dahinter - siehe deren eigener Tooltip.
 * "Duplizieren" ist deaktiviert, weil Nolphins Terminal noch keine
 * Mehrfach-Profile kennt (nur ein Profil "Unbenannt" pro Terminal-
 * Instanz); "Umbenennen" benennt real die angezeigte Kennung um,
 * "Loeschen" und "Als Vorgabe festlegen" sind wie in der Referenz
 * deaktiviert, solange es nur ein (bereits Standard-) Profil gibt. */

static gchar *prefs_profile_name = NULL;
static gchar *prefs_profile_uuid = NULL;

static GSettings *
prefs_get_terminal_settings (void)
{
    static GSettings *settings = NULL;

    if (settings == NULL) {
        settings = g_settings_new ("org.nolphin.terminal");
    }
    return settings;
}

static void
on_prefs_keybindings_enabled_toggled (GtkToggleButton *button, gpointer user_data)
{
    GtkWidget *tree_view = user_data;

    gtk_widget_set_sensitive (tree_view, gtk_toggle_button_get_active (button));
}

static GtkWidget *
prefs_section_heading (const gchar *text)
{
    GtkWidget *label = gtk_label_new (NULL);
    gchar *markup = g_markup_printf_escaped ("<b>%s</b>", text);

    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_margin_top (label, 10);
    return label;
}

static GtkWidget *
build_prefs_text_page (GtkWidget *terminal_widget)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *grid, *label, *reset_button;
    GtkWidget *cell_width_spin, *cell_height_spin;
    GtkWidget *custom_font_check, *font_button;
    GtkWidget *cursor_style_combo, *cursor_blink_combo, *text_blink_combo;
    GtkWidget *bell_check;
    CellSpacingData *csd;
    InitialSizeData *isd;
    CustomFontData *cfd;
    gchar *font_desc_text;

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    gtk_box_pack_start (GTK_BOX (page), prefs_section_heading (_("Text-Erscheinungsbild")), FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_top (grid, 6);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    /* Zeile 0: Anfaengliche Groesse des Terminals + Zuruecksetzen */
    label = gtk_label_new (_("Anfängliche Größe des Terminals:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    isd = g_new0 (InitialSizeData, 1);
    isd->terminal_widget = terminal_widget;
    {
        GtkWidget *size_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *cols_spin, *rows_spin;

        cols_spin = gtk_spin_button_new_with_range (10, 400, 1);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cols_spin), 80);
        gtk_box_pack_start (GTK_BOX (size_box), cols_spin, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (size_box), gtk_label_new (_("Spalten")), FALSE, FALSE, 0);

        rows_spin = gtk_spin_button_new_with_range (5, 200, 1);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (rows_spin), 24);
        gtk_box_pack_start (GTK_BOX (size_box), rows_spin, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (size_box), gtk_label_new (_("Zeilen")), FALSE, FALSE, 0);

        gtk_grid_attach (GTK_GRID (grid), size_box, 1, 0, 1, 1);

        isd->cols_spin = cols_spin;
        isd->rows_spin = rows_spin;
        g_signal_connect (cols_spin, "value-changed", G_CALLBACK (on_terminal_initial_size_changed), isd);
        g_signal_connect (rows_spin, "value-changed", G_CALLBACK (on_terminal_initial_size_changed), isd);
    }
    g_object_set_data_full (G_OBJECT (page), "initial-size-data", isd, g_free);

    reset_button = gtk_button_new_with_label (_("Zurücksetzen"));
    g_signal_connect (reset_button, "clicked", G_CALLBACK (on_terminal_initial_size_reset_clicked), isd);
    gtk_grid_attach (GTK_GRID (grid), reset_button, 2, 0, 1, 1);

    /* Zeile 1: Benutzerdefinierte Schrift - Familie UND Groesse wirken
     * sich wirklich aus (vte_terminal_set_font()). */
    {
        const gchar *current_family = nolphin_terminal_get_font_family (NOLPHIN_TERMINAL (terminal_widget));
        gboolean is_custom = g_strcmp0 (current_family, "Monospace") != 0;

        custom_font_check = gtk_check_button_new ();
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom_font_check), is_custom);
        {
            GtkWidget *check_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

            label = gtk_label_new (_("Benutzerdefinierte Schrift:"));
            gtk_label_set_xalign (GTK_LABEL (label), 0.0);
            gtk_box_pack_start (GTK_BOX (check_box), custom_font_check, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (check_box), label, FALSE, FALSE, 0);
            gtk_grid_attach (GTK_GRID (grid), check_box, 0, 1, 1, 1);
        }

        font_desc_text = g_strdup_printf ("%s %d", current_family,
            (int) nolphin_terminal_get_font_size (NOLPHIN_TERMINAL (terminal_widget)));
        font_button = gtk_font_button_new_with_font (font_desc_text);
        g_free (font_desc_text);
        gtk_widget_set_sensitive (font_button, is_custom);
        gtk_grid_attach (GTK_GRID (grid), font_button, 1, 1, 1, 1);
    }

    cfd = g_new0 (CustomFontData, 1);
    cfd->terminal_widget = terminal_widget;
    cfd->font_button = font_button;
    g_signal_connect (custom_font_check, "toggled", G_CALLBACK (on_terminal_custom_font_toggled), cfd);
    g_signal_connect (font_button, "font-set", G_CALLBACK (on_terminal_custom_font_set), terminal_widget);
    g_object_set_data_full (G_OBJECT (page), "custom-font-data", cfd, g_free);

    /* Zeile 2: Zellenabstand + Zuruecksetzen */
    label = gtk_label_new (_("Zellenabstand:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);

    {
        GtkWidget *spacing_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

        cell_width_spin = gtk_spin_button_new_with_range (0.5, 2.0, 0.1);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cell_width_spin),
                                    nolphin_terminal_get_cell_width_scale (NOLPHIN_TERMINAL (terminal_widget)));
        gtk_box_pack_start (GTK_BOX (spacing_box), cell_width_spin, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (spacing_box), gtk_label_new (_("× Breite")), FALSE, FALSE, 0);

        cell_height_spin = gtk_spin_button_new_with_range (0.5, 2.0, 0.1);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (cell_height_spin),
                                    nolphin_terminal_get_cell_height_scale (NOLPHIN_TERMINAL (terminal_widget)));
        gtk_box_pack_start (GTK_BOX (spacing_box), cell_height_spin, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (spacing_box), gtk_label_new (_("× Höhe")), FALSE, FALSE, 0);

        gtk_grid_attach (GTK_GRID (grid), spacing_box, 1, 2, 1, 1);
    }

    csd = g_new0 (CellSpacingData, 1);
    csd->terminal_widget = terminal_widget;
    csd->width_spin = cell_width_spin;
    csd->height_spin = cell_height_spin;
    g_signal_connect (cell_width_spin, "value-changed",
                       G_CALLBACK (on_terminal_cell_spacing_changed), csd);
    g_signal_connect (cell_height_spin, "value-changed",
                       G_CALLBACK (on_terminal_cell_spacing_changed), csd);
    g_object_set_data_full (G_OBJECT (page), "cell-spacing-data", csd, g_free);

    reset_button = gtk_button_new_with_label (_("Zurücksetzen"));
    g_signal_connect (reset_button, "clicked", G_CALLBACK (on_terminal_cell_spacing_reset_clicked), csd);
    gtk_grid_attach (GTK_GRID (grid), reset_button, 2, 2, 1, 1);

    /* Zeile 3: Blinkenden Text erlauben (Dropdown - Kern kennt nur Ein/Aus) */
    label = gtk_label_new (_("Blinkenden Text erlauben:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);

    text_blink_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (text_blink_combo), _("Immer"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (text_blink_combo), _("Nie"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (text_blink_combo),
                               nolphin_terminal_get_text_blink (NOLPHIN_TERMINAL (terminal_widget)) ? 0 : 1);
    g_signal_connect (text_blink_combo, "changed",
                       G_CALLBACK (on_terminal_text_blink_combo_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), text_blink_combo, 1, 3, 1, 1);

    gtk_box_pack_start (GTK_BOX (page), prefs_section_heading (_("Eingabemarke")), FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_top (grid, 6);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Form der Eingabemarke:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    cursor_style_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_style_combo), _("Rechteck"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_style_combo), _("Unterstrich"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_style_combo), _("Senkrechter Balken"));
    {
        NolphinCursorShape shape = nolphin_terminal_get_cursor_shape (NOLPHIN_TERMINAL (terminal_widget));
        gtk_combo_box_set_active (GTK_COMBO_BOX (cursor_style_combo), (gint) shape);
    }
    g_signal_connect (cursor_style_combo, "changed",
                       G_CALLBACK (on_terminal_cursor_style_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), cursor_style_combo, 1, 0, 1, 1);

    label = gtk_label_new (_("Blinkende Eingabemarke:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    cursor_blink_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_blink_combo), _("Immer"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_blink_combo), _("Nie"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cursor_blink_combo), _("Vorgabe"));
    {
        NolphinCursorBlinkMode mode = nolphin_terminal_get_cursor_blink_mode (NOLPHIN_TERMINAL (terminal_widget));
        gint active = mode == NOLPHIN_CURSOR_BLINK_ALWAYS ? 0 : mode == NOLPHIN_CURSOR_BLINK_NEVER ? 1 : 2;
        gtk_combo_box_set_active (GTK_COMBO_BOX (cursor_blink_combo), active);
    }
    g_signal_connect (cursor_blink_combo, "changed",
                       G_CALLBACK (on_terminal_cursor_blink_combo_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), cursor_blink_combo, 1, 1, 1, 1);

    gtk_box_pack_start (GTK_BOX (page), prefs_section_heading (_("Klang")), FALSE, FALSE, 0);

    bell_check = gtk_check_button_new_with_label (_("Terminalglocke"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bell_check),
                                   nolphin_terminal_get_bell_enabled (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (bell_check, "toggled",
                       G_CALLBACK (on_terminal_bell_toggled), terminal_widget);
    gtk_widget_set_margin_start (bell_check, 12);
    gtk_widget_set_margin_top (bell_check, 6);
    gtk_box_pack_start (GTK_BOX (page), bell_check, FALSE, FALSE, 0);

    return page;
}

/* Anzeige-/Vorgabewerte fuer die beiden "Integrierte Schemata" - dieselben
 * Zahlen wie die beiden Paletten in nolphin-terminal.c
 * (palette_standard/palette_solarized); der echte, gerade aktive Wert je
 * Swatch kommt aus nolphin_terminal_get_palette_color(). */
static const GdkRGBA prefs_palette_standard[8] = {
    {0.00,0.00,0.00,1.0},{0.80,0.20,0.20,1.0},{0.20,0.80,0.20,1.0},{0.80,0.80,0.20,1.0},
    {0.30,0.50,0.90,1.0},{0.70,0.30,0.80,1.0},{0.20,0.80,0.80,1.0},{0.90,0.90,0.90,1.0}
};
static const GdkRGBA prefs_palette_solarized[8] = {
    {0.027,0.212,0.259,1.0},{0.863,0.196,0.184,1.0},{0.522,0.600,0.000,1.0},{0.710,0.537,0.000,1.0},
    {0.149,0.545,0.824,1.0},{0.827,0.212,0.510,1.0},{0.165,0.631,0.596,1.0},{0.933,0.910,0.835,1.0}
};

typedef struct {
    GtkWidget *swatches[8];
} PaletteSwatchData;

static void
on_terminal_palette_combo_changed (GtkComboBox *combo, gpointer user_data)
{
    PaletteSwatchData *psd = user_data;
    const GdkRGBA *pal = gtk_combo_box_get_active (combo) == 1 ?
        prefs_palette_solarized : prefs_palette_standard;
    int i;

    /* Schemawechsel setzt eine zuvor bearbeitete Palette zurueck (siehe
     * nolphin_terminal_set_solarized()) - die Swatches hier zeigen das
     * nur nach, ohne selbst noch einmal zu schreiben. */
    for (i = 0; i < 8; ++i) {
        gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (psd->swatches[i]), &pal[i]);
    }
}

typedef struct {
    GtkWidget *terminal_widget;
    int index;
} PaletteSwatchIndexData;

static void
on_terminal_palette_swatch_changed (GtkColorButton *button, gpointer user_data)
{
    PaletteSwatchIndexData *d = user_data;
    GdkRGBA rgba;

    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (button), &rgba);
    nolphin_terminal_set_palette_color (NOLPHIN_TERMINAL (d->terminal_widget), d->index, &rgba);
}

typedef struct {
    GtkWidget *terminal_widget;
    GtkWidget *fg_button;
    GtkWidget *bg_button;
} DefaultColorData;

static void
on_terminal_default_color_changed (GtkWidget *widget, gpointer user_data)
{
    DefaultColorData *d = user_data;
    GdkRGBA fg, bg;

    (void) widget;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->fg_button), &fg);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->bg_button), &bg);
    nolphin_terminal_set_custom_default_colors (NOLPHIN_TERMINAL (d->terminal_widget), TRUE, &fg, &bg);
}

/* "Standardfarbe" hat in der Referenz keinen eigenen Umschalter - jede
 * Farbwahl hier aktiviert die eigene Vorgabefarbe sofort (nur im
 * Standardschema, siehe ot_terminal_set_custom_default_colors()). */
static void
prefs_build_default_color_row (GtkWidget *grid, GtkWidget *terminal_widget)
{
    GtkWidget *label, *row_box, *fg_button, *bg_button;
    GdkRGBA fg, bg;
    DefaultColorData *d;

    if (!nolphin_terminal_get_custom_default_colors (NOLPHIN_TERMINAL (terminal_widget), &fg, &bg)) {
        fg.red = fg.green = fg.blue = 0.9; fg.alpha = 1.0;
        bg.red = bg.green = bg.blue = 0.02; bg.alpha = 1.0;
    }

    label = gtk_label_new (_("Standardfarbe:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    fg_button = gtk_color_button_new_with_rgba (&fg);
    bg_button = gtk_color_button_new_with_rgba (&bg);
    gtk_widget_set_tooltip_text (fg_button, _("Textfarbe"));
    gtk_widget_set_tooltip_text (bg_button, _("Hintergrundfarbe"));
    gtk_box_pack_start (GTK_BOX (row_box), fg_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), bg_button, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), row_box, 1, 1, 1, 1);

    d = g_new0 (DefaultColorData, 1);
    d->terminal_widget = terminal_widget;
    d->fg_button = fg_button;
    d->bg_button = bg_button;
    g_signal_connect (fg_button, "color-set", G_CALLBACK (on_terminal_default_color_changed), d);
    g_signal_connect (bg_button, "color-set", G_CALLBACK (on_terminal_default_color_changed), d);
    g_object_set_data_full (G_OBJECT (row_box), "default-color-data", d, g_free);
}

typedef struct {
    GtkWidget *terminal_widget;
    GtkWidget *check;
    GtkWidget *color_button;
} BoldColorData;

static void
on_terminal_bold_color_changed (GtkWidget *widget, gpointer user_data)
{
    BoldColorData *d = user_data;
    GdkRGBA fg;
    gboolean enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->check));

    (void) widget;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->color_button), &fg);
    nolphin_terminal_set_custom_bold_color (NOLPHIN_TERMINAL (d->terminal_widget), enabled, &fg);
    gtk_widget_set_sensitive (d->color_button, enabled);
}

static void
prefs_build_bold_color_row (GtkWidget *grid, GtkWidget *terminal_widget)
{
    GtkWidget *label, *row_box, *check, *color_button;
    GdkRGBA fg;
    gboolean enabled = nolphin_terminal_get_custom_bold_color (NOLPHIN_TERMINAL (terminal_widget), &fg);
    BoldColorData *d;

    if (!enabled) { fg.red = fg.green = fg.blue = 1.0; fg.alpha = 1.0; }

    label = gtk_label_new (_("Farbe für fetten Text:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);

    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    check = gtk_check_button_new ();
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), enabled);
    color_button = gtk_color_button_new_with_rgba (&fg);
    gtk_widget_set_sensitive (color_button, enabled);
    gtk_box_pack_start (GTK_BOX (row_box), check, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), color_button, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), row_box, 1, 2, 1, 1);

    d = g_new0 (BoldColorData, 1);
    d->terminal_widget = terminal_widget;
    d->check = check;
    d->color_button = color_button;
    g_signal_connect (check, "toggled", G_CALLBACK (on_terminal_bold_color_changed), d);
    g_signal_connect (color_button, "color-set", G_CALLBACK (on_terminal_bold_color_changed), d);
    g_object_set_data_full (G_OBJECT (row_box), "bold-color-data", d, g_free);
}

typedef struct {
    GtkWidget *terminal_widget;
    GtkWidget *check;
    GtkWidget *fg_button;
    GtkWidget *bg_button;
} CheckedColorPairData;

static void
on_terminal_cursor_color_changed (GtkWidget *widget, gpointer user_data)
{
    CheckedColorPairData *d = user_data;
    GdkRGBA fg, bg;
    gboolean enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->check));

    (void) widget;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->fg_button), &fg);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->bg_button), &bg);
    nolphin_terminal_set_custom_cursor_colors (NOLPHIN_TERMINAL (d->terminal_widget), enabled, &fg, &bg);
    gtk_widget_set_sensitive (d->fg_button, enabled);
    gtk_widget_set_sensitive (d->bg_button, enabled);
}

/* "Farbe der Eingabemarke": Hintergrund faerbt die Cursorform selbst ein
 * (alle drei Formen), Text ist nur beim Rechteck-Cursor sichtbar - dort
 * wird das verdeckte Zeichen in dieser Farbe neu gezeichnet (siehe
 * draw_cb()). */
static void
prefs_build_cursor_color_row (GtkWidget *grid, GtkWidget *terminal_widget)
{
    GtkWidget *label, *row_box, *check, *fg_button, *bg_button;
    GdkRGBA fg, bg;
    gboolean enabled = nolphin_terminal_get_custom_cursor_colors (NOLPHIN_TERMINAL (terminal_widget), &fg, &bg);
    CheckedColorPairData *d;

    if (!enabled) {
        fg.red = fg.green = fg.blue = 0.0; fg.alpha = 1.0;
        bg.red = bg.green = bg.blue = 0.85; bg.alpha = 1.0;
    }

    label = gtk_label_new (_("Farbe der Eingabemarke:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);

    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    check = gtk_check_button_new ();
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), enabled);
    fg_button = gtk_color_button_new_with_rgba (&fg);
    bg_button = gtk_color_button_new_with_rgba (&bg);
    gtk_widget_set_tooltip_text (fg_button, _("Textfarbe (nur bei Rechteck-Form sichtbar)"));
    gtk_widget_set_tooltip_text (bg_button, _("Farbe der Cursorform"));
    gtk_widget_set_sensitive (fg_button, enabled);
    gtk_widget_set_sensitive (bg_button, enabled);
    gtk_box_pack_start (GTK_BOX (row_box), check, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), fg_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), bg_button, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), row_box, 1, 3, 1, 1);

    d = g_new0 (CheckedColorPairData, 1);
    d->terminal_widget = terminal_widget;
    d->check = check;
    d->fg_button = fg_button;
    d->bg_button = bg_button;
    g_signal_connect (check, "toggled", G_CALLBACK (on_terminal_cursor_color_changed), d);
    g_signal_connect (fg_button, "color-set", G_CALLBACK (on_terminal_cursor_color_changed), d);
    g_signal_connect (bg_button, "color-set", G_CALLBACK (on_terminal_cursor_color_changed), d);
    g_object_set_data_full (G_OBJECT (row_box), "cursor-color-data", d, g_free);
}

static void
on_terminal_highlight_color_changed (GtkWidget *widget, gpointer user_data)
{
    CheckedColorPairData *d = user_data;
    GdkRGBA fg, bg;
    gboolean enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->check));

    (void) widget;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->fg_button), &fg);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->bg_button), &bg);
    nolphin_terminal_set_custom_highlight_colors (NOLPHIN_TERMINAL (d->terminal_widget), enabled, &fg, &bg);
    gtk_widget_set_sensitive (d->fg_button, enabled);
    gtk_widget_set_sensitive (d->bg_button, enabled);
}

static void
prefs_build_highlight_color_row (GtkWidget *grid, GtkWidget *terminal_widget)
{
    GtkWidget *label, *row_box, *check, *fg_button, *bg_button;
    GdkRGBA fg, bg;
    gboolean enabled = nolphin_terminal_get_custom_highlight_colors (NOLPHIN_TERMINAL (terminal_widget), &fg, &bg);
    CheckedColorPairData *d;

    if (!enabled) {
        fg.red = fg.green = fg.blue = 0.9; fg.alpha = 1.0;
        bg.red = 0.25; bg.green = 0.45; bg.blue = 0.75; bg.alpha = 1.0;
    }

    label = gtk_label_new (_("Hervorhebungsfarbe:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 4, 1, 1);

    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    check = gtk_check_button_new ();
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), enabled);
    fg_button = gtk_color_button_new_with_rgba (&fg);
    bg_button = gtk_color_button_new_with_rgba (&bg);
    gtk_widget_set_tooltip_text (fg_button, _("Textfarbe der Auswahl"));
    gtk_widget_set_tooltip_text (bg_button, _("Hintergrundfarbe der Auswahl"));
    gtk_widget_set_sensitive (fg_button, enabled);
    gtk_widget_set_sensitive (bg_button, enabled);
    gtk_box_pack_start (GTK_BOX (row_box), check, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), fg_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row_box), bg_button, FALSE, FALSE, 0);
    gtk_grid_attach (GTK_GRID (grid), row_box, 1, 4, 1, 1);

    d = g_new0 (CheckedColorPairData, 1);
    d->terminal_widget = terminal_widget;
    d->check = check;
    d->fg_button = fg_button;
    d->bg_button = bg_button;
    g_signal_connect (check, "toggled", G_CALLBACK (on_terminal_highlight_color_changed), d);
    g_signal_connect (fg_button, "color-set", G_CALLBACK (on_terminal_highlight_color_changed), d);
    g_signal_connect (bg_button, "color-set", G_CALLBACK (on_terminal_highlight_color_changed), d);
    g_object_set_data_full (G_OBJECT (row_box), "highlight-color-data", d, g_free);
}

static GtkWidget *
build_prefs_colors_page (GtkWidget *terminal_widget)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *grid, *label, *check, *combo, *button;

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    gtk_box_pack_start (GTK_BOX (page), prefs_section_heading (_("Text- und Hintergrundfarbe")), FALSE, FALSE, 0);

    check = gtk_check_button_new_with_label (_("Farben vom System-Thema verwenden"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
        nolphin_terminal_get_use_system_colors (NOLPHIN_TERMINAL (terminal_widget)));
    gtk_widget_set_tooltip_text (check,
        _("Liest Vorder-/Hintergrundfarbe aus dem aktiven GTK-Systemthema - hat "
          "Vorrang vor der „Standardfarbe“ weiter unten."));
    g_signal_connect (check, "toggled",
                       G_CALLBACK (on_terminal_use_system_colors_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_top (grid, 6);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Integrierte Schemata:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
    combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Tango (dunkel)"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
    gtk_widget_set_sensitive (combo, FALSE);
    gtk_widget_set_tooltip_text (combo,
        _("Benannte, vom GTK-Systemthema abgeleitete Schemata kennt der Kern nicht - "
          "die tatsächlich wirksame Auswahl steht unten unter „Farbpalette“."));
    gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);

    /* Standardfarbe, Farbe fuer fetten Text, Farbe der Eingabemarke und
     * Hervorhebungsfarbe wirken wirklich (vte_terminal_set_color_*()) -
     * ebenso die Palette unten (frei editierbar) und "Fetten Text auch in
     * helleren Farben" (vte_terminal_set_bold_is_bright()). Nur
     * Transparenz bleibt deaktiviert (siehe deren eigener Tooltip). */
    prefs_build_default_color_row (grid, terminal_widget);
    prefs_build_bold_color_row (grid, terminal_widget);
    prefs_build_cursor_color_row (grid, terminal_widget);
    prefs_build_highlight_color_row (grid, terminal_widget);

    check = gtk_check_button_new_with_label (_("Durchsichtigen Hintergrund benutzen"));
    gtk_widget_set_sensitive (check, FALSE);
    gtk_widget_set_margin_top (check, 8);
    gtk_widget_set_tooltip_text (check,
        _("VTE kann transparent zeichnen, aber nur sinnvoll in einem eigenen "
          "Fenster, das zum Desktop durchscheint. Nolphins Terminal ist ein "
          "eingebettetes Panel im Hauptfenster - ein durchsichtiger Hintergrund "
          "würde zufällige Pixel des Desktops statt des Hauptfensters zeigen."));
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);
    {
        GtkWidget *slider_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01);

        gtk_widget_set_sensitive (scale, FALSE);
        gtk_widget_set_hexpand (scale, TRUE);
        gtk_box_pack_start (GTK_BOX (slider_box), gtk_label_new (_("Keine")), FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (slider_box), scale, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (slider_box), gtk_label_new (_("vollständig")), FALSE, FALSE, 0);
        gtk_widget_set_margin_start (slider_box, 12);
        gtk_box_pack_start (GTK_BOX (page), slider_box, FALSE, FALSE, 0);
    }

    check = gtk_check_button_new_with_label (_("Transparenz aus dem System Theme benutzen"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
    gtk_widget_set_sensitive (check, FALSE);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (page), prefs_section_heading (_("Farbpalette")), FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_top (grid, 6);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Integrierte Schemata:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    /* Dies ist die tatsaechlich wirksame Auswahl - wirkt sofort auf den
     * echten Terminal-Kern (ot_terminal_set_color_scheme()). */
    combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Standard"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Solarisiert"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
                               nolphin_terminal_get_solarized (NOLPHIN_TERMINAL (terminal_widget)) ? 1 : 0);
    g_signal_connect (combo, "changed", G_CALLBACK (on_terminal_color_scheme_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);

    label = gtk_label_new (_("Farbpalette:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    {
        GtkWidget *palette_grid = gtk_grid_new ();
        PaletteSwatchData *psd = g_new0 (PaletteSwatchData, 1);
        int i;

        gtk_grid_set_row_spacing (GTK_GRID (palette_grid), 4);
        gtk_grid_set_column_spacing (GTK_GRID (palette_grid), 4);
        for (i = 0; i < 8; ++i) {
            GdkRGBA rgba;
            GtkWidget *swatch;
            PaletteSwatchIndexData *isd;

            nolphin_terminal_get_palette_color (NOLPHIN_TERMINAL (terminal_widget), i, &rgba);
            swatch = gtk_color_button_new_with_rgba (&rgba);
            gtk_color_button_set_title (GTK_COLOR_BUTTON (swatch), _("Palettenfarbe wählen"));

            isd = g_new0 (PaletteSwatchIndexData, 1);
            isd->terminal_widget = terminal_widget;
            isd->index = i;
            g_signal_connect (swatch, "color-set", G_CALLBACK (on_terminal_palette_swatch_changed), isd);
            g_object_set_data_full (G_OBJECT (swatch), "palette-swatch-index-data", isd, g_free);

            gtk_grid_attach (GTK_GRID (palette_grid), swatch, i % 8, i / 8, 1, 1);
            psd->swatches[i] = swatch;
        }
        g_signal_connect (combo, "changed", G_CALLBACK (on_terminal_palette_combo_changed), psd);
        g_object_set_data_full (G_OBJECT (page), "palette-swatch-data", psd, g_free);
        gtk_grid_attach (GTK_GRID (grid), palette_grid, 1, 1, 1, 1);
    }

    button = gtk_check_button_new_with_label (_("Fetten Text auch in helleren Farben darstellen"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
        nolphin_terminal_get_bold_is_bright (NOLPHIN_TERMINAL (terminal_widget)));
    gtk_widget_set_margin_top (button, 8);
    g_signal_connect (button, "toggled",
                       G_CALLBACK (on_terminal_bold_is_bright_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), button, FALSE, FALSE, 0);

    return page;
}

static GtkWidget *
build_prefs_scroll_page (GtkWidget *terminal_widget)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *grid, *label;
    GtkWidget *scrollback_spin, *scroll_output_check, *scroll_keystroke_check;
    GtkWidget *show_scrollbar_check, *scroll_on_paste_check;

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    show_scrollbar_check = gtk_check_button_new_with_label (_("Bildlaufleiste anzeigen"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_scrollbar_check),
                                   nolphin_terminal_get_show_scrollbar (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (show_scrollbar_check, "toggled",
                       G_CALLBACK (on_terminal_show_scrollbar_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), show_scrollbar_check, FALSE, FALSE, 0);

    scroll_output_check = gtk_check_button_new_with_label (_("Bildlauf bei Ausgabe"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scroll_output_check),
                                   nolphin_terminal_get_scroll_on_output (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (scroll_output_check, "toggled",
                       G_CALLBACK (on_terminal_scroll_on_output_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), scroll_output_check, FALSE, FALSE, 0);

    scroll_keystroke_check = gtk_check_button_new_with_label (_("Bildlauf bei Tastendruck"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scroll_keystroke_check),
                                   nolphin_terminal_get_scroll_on_keystroke (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (scroll_keystroke_check, "toggled",
                       G_CALLBACK (on_terminal_scroll_on_keystroke_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), scroll_keystroke_check, FALSE, FALSE, 0);

    scroll_on_paste_check = gtk_check_button_new_with_label (_("Bildlauf beim Einfügen"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scroll_on_paste_check),
                                   nolphin_terminal_get_scroll_on_paste (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (scroll_on_paste_check, "toggled",
                       G_CALLBACK (on_terminal_scroll_on_paste_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), scroll_on_paste_check, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_top (grid, 4);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Zeilenpuffer limitieren auf:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    scrollback_spin = gtk_spin_button_new_with_range (0, 100000, 100);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (scrollback_spin), 10000);
    g_signal_connect (scrollback_spin, "value-changed",
                       G_CALLBACK (on_terminal_scrollback_lines_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), scrollback_spin, 1, 0, 1, 1);

    label = gtk_label_new (_("Zeilen"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 2, 0, 1, 1);

    {
        GtkWidget *warning = gtk_label_new (
            _("Warnung! Große Scrollback-Puffer können zu einer Erschöpfung der Systemressourcen führen."));
        gtk_label_set_line_wrap (GTK_LABEL (warning), TRUE);
        gtk_widget_set_margin_top (warning, 10);
        gtk_style_context_add_class (gtk_widget_get_style_context (warning), "app-notification");
        gtk_box_pack_start (GTK_BOX (page), warning, FALSE, FALSE, 0);
    }

    return page;
}

static GtkWidget *
build_prefs_command_page (GtkWidget *terminal_widget)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *grid, *label;
    GtkWidget *login_shell_check, *use_custom_command_check, *custom_command_entry;
    CustomCommandData *ccd;

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    login_shell_check = gtk_check_button_new_with_label (_("Befehl als Login-Shell starten"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (login_shell_check),
                                   nolphin_terminal_get_login_shell (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (login_shell_check, "toggled",
                       G_CALLBACK (on_terminal_login_shell_toggled), terminal_widget);
    gtk_box_pack_start (GTK_BOX (page), login_shell_check, FALSE, FALSE, 0);

    use_custom_command_check = gtk_check_button_new_with_label (
        _("Einen benutzerdefinierten Befehl statt meiner Befehlszeile starten"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (use_custom_command_check),
                                   nolphin_terminal_get_use_custom_command (NOLPHIN_TERMINAL (terminal_widget)));
    gtk_box_pack_start (GTK_BOX (page), use_custom_command_check, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_top (grid, 4);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Benutzerdefinierter Befehl:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    custom_command_entry = gtk_entry_new ();
    {
        const gchar *existing = nolphin_terminal_get_custom_command (NOLPHIN_TERMINAL (terminal_widget));
        if (existing != NULL) {
            gtk_entry_set_text (GTK_ENTRY (custom_command_entry), existing);
        }
    }
    gtk_widget_set_sensitive (custom_command_entry,
                               nolphin_terminal_get_use_custom_command (NOLPHIN_TERMINAL (terminal_widget)));
    gtk_widget_set_hexpand (custom_command_entry, TRUE);
    gtk_grid_attach (GTK_GRID (grid), custom_command_entry, 1, 0, 1, 1);

    ccd = g_new0 (CustomCommandData, 1);
    ccd->terminal_widget = terminal_widget;
    ccd->use_custom_check = use_custom_command_check;
    ccd->command_entry = custom_command_entry;
    g_signal_connect (use_custom_command_check, "toggled",
                       G_CALLBACK (on_terminal_custom_command_changed), ccd);
    g_signal_connect (custom_command_entry, "changed",
                       G_CALLBACK (on_terminal_custom_command_changed), ccd);
    g_object_set_data_full (G_OBJECT (page), "custom-command-data", ccd, g_free);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_top (grid, 8);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Arbeitsordner beibehalten:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    {
        GtkWidget *combo = gtk_combo_box_text_new ();
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Nie"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Nur Shell"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
            nolphin_terminal_get_follow_location (NOLPHIN_TERMINAL (terminal_widget)) ? 1 : 0);
        gtk_widget_set_tooltip_text (combo,
            _("„Nur Shell“ (Vorgabe) laesst das Terminal dem im Dateimanager geoeffneten "
              "Ordner folgen; „Nie“ schaltet das automatische Mitwandern ab. „Immer“ "
              "(ueber mehrere Terminal-Reiter hinweg) gibt es nicht, weil Nolphins "
              "Terminal-Panel bisher nur einen Reiter pro Fenster kennt."));
        g_signal_connect (combo, "changed",
                           G_CALLBACK (on_terminal_follow_location_changed), terminal_widget);
        gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);
    }

    label = gtk_label_new (_("Wenn Befehl beendet:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    {
        GtkWidget *combo = gtk_combo_box_text_new ();
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Halten"));
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Befehl neu starten"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
            nolphin_terminal_get_exit_action (NOLPHIN_TERMINAL (terminal_widget)) == NOLPHIN_TERMINAL_EXIT_RESTART ? 1 : 0);
        gtk_widget_set_tooltip_text (combo,
            _("„Halten“ (Vorgabe) laesst den letzten Bildschirminhalt stehen. „Das "
              "Terminal schliessen“ aus der Referenz gibt es hier nicht, weil das "
              "Terminal ein eingebettetes Panel ohne eigenes Fenster ist."));
        g_signal_connect (combo, "changed",
                           G_CALLBACK (on_terminal_exit_action_changed), terminal_widget);
        gtk_grid_attach (GTK_GRID (grid), combo, 1, 1, 1, 1);
    }

    label = gtk_label_new (_("(Gilt ab dem nächsten neuen Terminal-Reiter.)"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    gtk_widget_set_margin_top (label, 8);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);

    return page;
}

typedef struct {
    GtkWidget *terminal_widget;
    GtkWidget *backspace_combo;
    GtkWidget *delete_combo;
    GtkWidget *ambiguous_combo;
} CompatResetData;

static void
on_terminal_ambiguous_width_changed (GtkComboBox *combo, gpointer user_data)
{
    GtkWidget *terminal_widget = user_data;

    nolphin_terminal_set_ambiguous_width_wide (NOLPHIN_TERMINAL (terminal_widget),
                                               gtk_combo_box_get_active (combo) == 1);
}

static void
on_terminal_compat_reset_clicked (GtkButton *button, gpointer user_data)
{
    CompatResetData *crd = user_data;

    (void) button;
    /* Setzt die Combos zurueck; deren vorhandene "changed"-Handler
     * schreiben den Vorgabewert direkt in das echte VTE-Widget. */
    gtk_combo_box_set_active (GTK_COMBO_BOX (crd->backspace_combo), NOLPHIN_ERASE_AUTO);
    gtk_combo_box_set_active (GTK_COMBO_BOX (crd->delete_combo), NOLPHIN_ERASE_DELETE_SEQUENCE);
    gtk_combo_box_set_active (GTK_COMBO_BOX (crd->ambiguous_combo), 0);
}

static GtkWidget *
build_prefs_compat_page (GtkWidget *terminal_widget)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *grid, *label, *backspace_combo, *delete_combo, *ambiguous_combo, *button;

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, FALSE, 0);

    label = gtk_label_new (_("Rücktaste erzeugt:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    /* Reihenfolge entspricht NolphinEraseBinding / VteEraseBinding. */
    backspace_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (backspace_combo), _("Automatisch"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (backspace_combo), _("Strg+H"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (backspace_combo), _("ASCII DEL"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (backspace_combo), _("Escape-Sequenz"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (backspace_combo), _("TTY-Einstellung"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (backspace_combo),
                               nolphin_terminal_get_backspace_binding (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (backspace_combo, "changed",
                       G_CALLBACK (on_terminal_backspace_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), backspace_combo, 1, 0, 1, 1);

    label = gtk_label_new (_("Entfernen-Taste erzeugt:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    delete_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (delete_combo), _("Automatisch"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (delete_combo), _("Strg+H"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (delete_combo), _("ASCII DEL"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (delete_combo), _("Escape-Sequenz"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (delete_combo), _("TTY-Einstellung"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (delete_combo),
                               nolphin_terminal_get_delete_binding (NOLPHIN_TERMINAL (terminal_widget)));
    g_signal_connect (delete_combo, "changed",
                       G_CALLBACK (on_terminal_delete_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), delete_combo, 1, 1, 1, 1);

    label = gtk_label_new (_("Zeichenkodierung:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);

    {
        GtkWidget *combo = gtk_combo_box_text_new ();
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Unicode — UTF-8"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
        gtk_widget_set_sensitive (combo, FALSE);
        gtk_widget_set_tooltip_text (combo,
            _("VTE unterstützt seit mehreren Jahren ausschließlich UTF-8 - eine "
              "andere Kodierung bietet die Bibliothek selbst nicht mehr an."));
        gtk_grid_attach (GTK_GRID (grid), combo, 1, 2, 1, 1);
    }

    label = gtk_label_new (_("Zeichen mit unbekannter Breite:"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);

    ambiguous_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (ambiguous_combo), _("Schmal"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (ambiguous_combo), _("Breit"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (ambiguous_combo),
        nolphin_terminal_get_ambiguous_width_wide (NOLPHIN_TERMINAL (terminal_widget)) ? 1 : 0);
    g_signal_connect (ambiguous_combo, "changed",
                       G_CALLBACK (on_terminal_ambiguous_width_changed), terminal_widget);
    gtk_grid_attach (GTK_GRID (grid), ambiguous_combo, 1, 3, 1, 1);

    button = gtk_button_new_with_label (_("Kompatibilitätseinstellungen auf Vorgabewerte zurücksetzen"));
    gtk_widget_set_halign (button, GTK_ALIGN_START);
    gtk_widget_set_margin_top (button, 8);
    {
        CompatResetData *crd = g_new0 (CompatResetData, 1);
        crd->terminal_widget = terminal_widget;
        crd->backspace_combo = backspace_combo;
        crd->delete_combo = delete_combo;
        crd->ambiguous_combo = ambiguous_combo;
        g_signal_connect (button, "clicked", G_CALLBACK (on_terminal_compat_reset_clicked), crd);
        g_object_set_data_full (G_OBJECT (page), "compat-reset-data", crd, g_free);
    }
    gtk_box_pack_start (GTK_BOX (page), button, FALSE, FALSE, 0);

    return page;
}

static GtkWidget *
build_prefs_profile_notebook (GtkWidget *terminal_widget)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *notebook = gtk_notebook_new ();
    GtkWidget *id_label;
    gchar *id_text;

    gtk_box_pack_start (GTK_BOX (box), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), build_prefs_text_page (terminal_widget), gtk_label_new (_("Text")));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), build_prefs_colors_page (terminal_widget), gtk_label_new (_("Farben")));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), build_prefs_scroll_page (terminal_widget), gtk_label_new (_("Bildlauf")));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), build_prefs_command_page (terminal_widget), gtk_label_new (_("Befehl")));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), build_prefs_compat_page (terminal_widget), gtk_label_new (_("Kompatibilität")));

    if (prefs_profile_uuid == NULL) {
        prefs_profile_uuid = g_uuid_string_random ();
    }
    id_text = g_strdup_printf (_("Profilkennung: %s"), prefs_profile_uuid);
    id_label = gtk_label_new (id_text);
    g_free (id_text);
    gtk_label_set_xalign (GTK_LABEL (id_label), 1.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (id_label), "dim-label");
    gtk_widget_set_margin_top (id_label, 4);
    gtk_widget_set_margin_end (id_label, 8);
    gtk_widget_set_margin_bottom (id_label, 4);
    gtk_box_pack_start (GTK_BOX (box), id_label, FALSE, FALSE, 0);

    return box;
}

static GtkWidget *
build_prefs_general_page (void)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *check, *combo, *label, *hbox, *button;
    GSettings *settings = prefs_get_terminal_settings ();

    gtk_container_set_border_width (GTK_CONTAINER (page), 16);

    check = gtk_check_button_new_with_label (_("Menüleiste in neuen Terminals per Vorgabe anzeigen"));
    g_settings_bind (settings, "menubar-default-visible", check, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    check = gtk_check_button_new_with_label (
        _("Menükürzelbuchstaben aktivieren (z.B. Alt+D, um das Datei-Menü zu öffnen)"));
    g_settings_bind (settings, "menu-mnemonics-enabled", check, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    check = gtk_check_button_new_with_label (_("Menütastenkombination aktivieren (Vorgabe: F10)"));
    g_settings_bind (settings, "menu-accel-enabled", check, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new (_("Themenvariante:"));
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Dunkler Stil"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Heller Stil"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Systemvorgabe"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
    gtk_widget_set_tooltip_text (combo,
        _("Wirkt auf dieses Einstellungsfenster; Nolphin erzwingt "
          "kein eigenes GTK-Theme für die restliche Anwendung."));
    gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), hbox, FALSE, FALSE, 0);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new (_("Neue Reiterposition:"));
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Letzte"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Nächste"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Erste"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
    gtk_widget_set_tooltip_text (combo,
        _("Nolphins Terminal-Panel kennt bisher genau einen Reiter pro Fenster; diese "
          "Einstellung greift erst, sobald mehrere Terminal-Reiter unterstützt werden."));
    gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), hbox, FALSE, FALSE, 0);

    check = gtk_check_button_new_with_label (_("Immer prüfen, ob es sich um das Vorgabe-Terminal handelt"));
    g_settings_bind (settings, "check-default-terminal", check, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    button = gtk_button_new_with_label (_("Als Vorgabe-Terminal festlegen"));
    gtk_widget_set_halign (button, GTK_ALIGN_START);
    gtk_widget_set_sensitive (button, FALSE);
    gtk_widget_set_tooltip_text (button,
        _("Nolphin hat noch keinen eigenständigen Terminal-Startmodus (z. B. „nolphin "
          "--terminal -e Befehl“) und kann sich deshalb noch nicht beim System als "
          "Standardterminal registrieren."));
    gtk_box_pack_start (GTK_BOX (page), button, FALSE, FALSE, 0);

    return page;
}

static void
prefs_keybindings_add_row (GtkTreeStore *store, GtkTreeIter *parent,
                            const gchar *action, const gchar *accel)
{
    GtkTreeIter iter;

    gtk_tree_store_append (store, &iter, parent);
    gtk_tree_store_set (store, &iter, 0, action, 1, accel, -1);
}

static GtkWidget *
build_prefs_keybindings_page (void)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *check, *tree_view, *scrolled;
    GtkTreeStore *store;
    GtkTreeIter category;
    GtkCellRenderer *renderer;
    GSettings *settings = prefs_get_terminal_settings ();

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);

    store = gtk_tree_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

    /* Alle Eintraege hier sind ueber die Bearbeiten-Menueleiste oder das
     * Rechtsklick-Kontextmenue des Terminals tatsaechlich erreichbar
     * (siehe build_terminal_menu_bar()/nolphin-terminal.c:build_context_
     * menu()) - keiner traegt bisher ein eigenes GTK-Tastenkuerzel,
     * "Deaktiviert" statt einer erfundenen Kombination. "Neuer Reiter"/
     * "Neues Fenster" aus einem gewoehnlichen Terminalfenster fehlen
     * bewusst (kein Mehrfach-Reiter/-Fenster-Konzept im Panel). */
    gtk_tree_store_append (store, &category, NULL);
    gtk_tree_store_set (store, &category, 0, _("Bearbeiten"), 1, "", -1);
    prefs_keybindings_add_row (store, &category, _("Kopieren"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Als HTML kopieren"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Einfügen"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Alles auswählen"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Nur lesen"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Einstellungen"), _("Deaktiviert"));

    gtk_tree_store_append (store, &category, NULL);
    gtk_tree_store_set (store, &category, 0, _("Ansicht"), 1, "", -1);
    prefs_keybindings_add_row (store, &category, _("Vergrößern"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Verkleinern"), _("Deaktiviert"));
    prefs_keybindings_add_row (store, &category, _("Normale Größe"), _("Deaktiviert"));

    gtk_tree_store_append (store, &category, NULL);
    gtk_tree_store_set (store, &category, 0, _("Terminal"), 1, "", -1);
    prefs_keybindings_add_row (store, &category, _("Zurücksetzen"), _("Deaktiviert"));

    tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
    g_object_unref (store);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), TRUE);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree_view), -1,
                                                  _("Aktion"), renderer, "text", 0, NULL);
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree_view), -1,
                                                  _("Tastenkombination"), renderer, "text", 1, NULL);
    gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));

    check = gtk_check_button_new_with_label (_("Tastenkombinationen aktivieren"));
    gtk_widget_set_sensitive (tree_view, g_settings_get_boolean (settings, "keybindings-enabled"));
    g_settings_bind (settings, "keybindings-enabled", check, "active", G_SETTINGS_BIND_DEFAULT);
    g_signal_connect (check, "toggled", G_CALLBACK (on_prefs_keybindings_enabled_toggled), tree_view);
    gtk_box_pack_start (GTK_BOX (page), check, FALSE, FALSE, 0);

    scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                     GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scrolled), tree_view);
    gtk_widget_set_vexpand (scrolled, TRUE);
    gtk_box_pack_start (GTK_BOX (page), scrolled, TRUE, TRUE, 0);

    return page;
}

/* Überschrift der Terminal-Einstellungsseite (früher der Fenstertitel). */
static void
prefs_set_title (GtkWidget *page, const gchar *text)
{
    GtkWidget *label = g_object_get_data (G_OBJECT (page), "prefs-title-label");

    if (label != NULL) {
        gtk_label_set_text (GTK_LABEL (label), text);
    }
}

static void
on_prefs_sidebar_row_selected (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    GtkWidget *window = GTK_WIDGET (user_data);
    GtkStack *stack;
    const gchar *page_name;

    if (row == NULL) {
        return;
    }

    /* "Global" (nav_list) und "Profile" (profile_list) sind zwei getrennte
     * GtkListBox-Widgets mit je eigenem Auswahlzustand - ohne diesen
     * Abgleich blieben beide dauerhaft gleichzeitig "ausgewählt" (z. B.
     * "Allgemein" UND "Unbenannt"), und ein erneuter Klick auf eine
     * bereits ausgewählten Zeile feuert in GTK kein "row-selected" mehr,
     * wodurch die Profilseite nach dem ersten Wechsel zu "Allgemein"
     * unerreichbar wurde. Die Geschwister-Listbox wird deshalb bei jeder
     * echten Auswahl explizit geleert; der Handler wird dafuer kurz
     * blockiert, damit das dadurch ausgeloeste "row-selected" (row==NULL)
     * nicht in denselben Code zurueckspringt. */
    {
        GtkWidget *sibling = g_object_get_data (G_OBJECT (box), "prefs-sibling-list");

        if (sibling != NULL) {
            g_signal_handlers_block_by_func (sibling, on_prefs_sidebar_row_selected, window);
            gtk_list_box_unselect_all (GTK_LIST_BOX (sibling));
            g_signal_handlers_unblock_by_func (sibling, on_prefs_sidebar_row_selected, window);
        }
    }

    stack = g_object_get_data (G_OBJECT (window), "prefs-stack");
    page_name = g_object_get_data (G_OBJECT (row), "prefs-page-name");
    if (stack == NULL || page_name == NULL) {
        return;
    }

    gtk_stack_set_visible_child_name (stack, page_name);

    if (g_strcmp0 (page_name, "general") == 0) {
        prefs_set_title (window, _("Einstellungen – Allgemein"));
    } else if (g_strcmp0 (page_name, "keybindings") == 0) {
        prefs_set_title (window, _("Einstellungen – Tastenkombinationen"));
    } else {
        gchar *title = g_strdup_printf (_("Einstellungen – Profil »%s«"), prefs_profile_name);
        prefs_set_title (window, title);
        g_free (title);
    }
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

/* Profil umbenennen: kleines Popover mit Eingabefeld statt eines Dialogs. */
static void
prefs_profile_rename_apply (GtkWidget *popover)
{
    GtkWidget *entry = g_object_get_data (G_OBJECT (popover), "rename-entry");
    GtkWidget *row_label = g_object_get_data (G_OBJECT (popover), "rename-row-label");
    const gchar *new_name = gtk_entry_get_text (GTK_ENTRY (entry));

    if (new_name != NULL && *new_name != '\0') {
        GtkWidget *page = g_object_get_data (G_OBJECT (row_label), "prefs-page");

        g_free (prefs_profile_name);
        prefs_profile_name = g_strdup (new_name);
        gtk_label_set_text (GTK_LABEL (row_label), prefs_profile_name);
        if (page != NULL) {
            gchar *title = g_strdup_printf (_("Einstellungen – Profil »%s«"), prefs_profile_name);

            prefs_set_title (page, title);
            g_free (title);
        }
    }
    gtk_widget_destroy (popover);
}

static void
on_prefs_rename_entry_activate (GtkEntry *entry, gpointer popover)
{
    prefs_profile_rename_apply (GTK_WIDGET (popover));
}

static void
on_prefs_rename_ok_clicked (GtkButton *button, gpointer popover)
{
    prefs_profile_rename_apply (GTK_WIDGET (popover));
}

static void
on_prefs_profile_rename_clicked (GtkMenuItem *item, gpointer user_data)
{
    GtkWidget *row_label = user_data;
    GtkWidget *popover = gtk_popover_new (row_label);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new ();
    GtkWidget *ok = gtk_button_new_with_label (_("Umbenennen"));

    (void) item;

    gtk_container_set_border_width (GTK_CONTAINER (box), 8);
    gtk_entry_set_text (GTK_ENTRY (entry), prefs_profile_name);
    gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (box), ok, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (popover), box);
    g_object_set_data (G_OBJECT (popover), "rename-entry", entry);
    g_object_set_data (G_OBJECT (popover), "rename-row-label", row_label);
    g_signal_connect (entry, "activate", G_CALLBACK (on_prefs_rename_entry_activate), popover);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_prefs_rename_ok_clicked), popover);
    gtk_widget_show_all (box);
    popover_popup_later (popover, entry);
}

static void
on_prefs_profile_menu_clicked (GtkButton *button, gpointer user_data)
{
    GtkWidget *row_label = user_data;
    GtkWidget *menu = gtk_menu_new ();
    GtkWidget *item;

    item = gtk_menu_item_new_with_label (_("Duplizieren …"));
    gtk_widget_set_sensitive (item, FALSE);
    gtk_widget_set_tooltip_text (item,
        _("Nolphins Terminal kennt bisher nur ein Profil pro Terminal-Instanz."));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Umbenennen …"));
    g_signal_connect (item, "activate", G_CALLBACK (on_prefs_profile_rename_clicked), row_label);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Löschen …"));
    gtk_widget_set_sensitive (item, FALSE);
    gtk_widget_set_tooltip_text (item, _("Das einzige Profil kann nicht gelöscht werden."));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    item = gtk_menu_item_new_with_label (_("Als Vorgabe festlegen"));
    gtk_widget_set_sensitive (item, FALSE);
    gtk_widget_set_tooltip_text (item, _("Dieses Profil ist bereits die Vorgabe."));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

    gtk_widget_show_all (menu);
    gtk_menu_popup_at_widget (GTK_MENU (menu), GTK_WIDGET (button),
                              GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static GtkWidget *
build_prefs_sidebar (GtkWidget *window)
{
    GtkWidget *sidebar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *global_label, *profile_header, *profile_add_button;
    GtkWidget *nav_list, *profile_list;
    GtkWidget *row, *row_box, *label;

    gtk_widget_set_size_request (sidebar, 170, -1);
    gtk_container_set_border_width (GTK_CONTAINER (sidebar), 8);

    global_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (global_label), _("<b>Global</b>"));
    gtk_label_set_xalign (GTK_LABEL (global_label), 0.0);
    gtk_box_pack_start (GTK_BOX (sidebar), global_label, FALSE, FALSE, 0);

    nav_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (nav_list), GTK_SELECTION_SINGLE);
    gtk_box_pack_start (GTK_BOX (sidebar), nav_list, FALSE, FALSE, 0);

    row = gtk_list_box_row_new ();
    g_object_set_data (G_OBJECT (row), "prefs-page-name", (gpointer) "general");
    gtk_container_add (GTK_CONTAINER (row), gtk_label_new (_("Allgemein")));
    gtk_widget_set_halign (gtk_bin_get_child (GTK_BIN (row)), GTK_ALIGN_START);
    gtk_container_add (GTK_CONTAINER (nav_list), row);

    row = gtk_list_box_row_new ();
    g_object_set_data (G_OBJECT (row), "prefs-page-name", (gpointer) "keybindings");
    gtk_container_add (GTK_CONTAINER (row), gtk_label_new (_("Tastenkombinationen")));
    gtk_widget_set_halign (gtk_bin_get_child (GTK_BIN (row)), GTK_ALIGN_START);
    gtk_container_add (GTK_CONTAINER (nav_list), row);

    g_signal_connect (nav_list, "row-selected", G_CALLBACK (on_prefs_sidebar_row_selected), window);

    profile_header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top (profile_header, 12);
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), _("<b>Profile</b>"));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_box_pack_start (GTK_BOX (profile_header), label, TRUE, TRUE, 0);
    profile_add_button = gtk_button_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (profile_add_button), GTK_RELIEF_NONE);
    gtk_widget_set_sensitive (profile_add_button, FALSE);
    gtk_widget_set_tooltip_text (profile_add_button,
        _("Nolphins Terminal kennt bisher nur ein Profil pro Terminal-Instanz."));
    gtk_box_pack_start (GTK_BOX (profile_header), profile_add_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (sidebar), profile_header, FALSE, FALSE, 0);

    profile_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (profile_list), GTK_SELECTION_SINGLE);
    gtk_box_pack_start (GTK_BOX (sidebar), profile_list, FALSE, FALSE, 0);

    row = gtk_list_box_row_new ();
    g_object_set_data (G_OBJECT (row), "prefs-page-name", (gpointer) "profile");
    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start (GTK_BOX (row_box), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
    label = gtk_label_new (prefs_profile_name);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_hexpand (label, TRUE);
    g_object_set_data (G_OBJECT (label), "prefs-page", window);
    gtk_box_pack_start (GTK_BOX (row_box), label, TRUE, TRUE, 0);
    {
        GtkWidget *menu_button = gtk_button_new_from_icon_name ("pan-down-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_relief (GTK_BUTTON (menu_button), GTK_RELIEF_NONE);
        g_signal_connect (menu_button, "clicked", G_CALLBACK (on_prefs_profile_menu_clicked), label);
        gtk_box_pack_start (GTK_BOX (row_box), menu_button, FALSE, FALSE, 0);
    }
    gtk_container_add (GTK_CONTAINER (row), row_box);
    gtk_container_add (GTK_CONTAINER (profile_list), row);
    g_signal_connect (profile_list, "row-selected", G_CALLBACK (on_prefs_sidebar_row_selected), window);

    /* nav_list und profile_list sollen sich gegenseitig ausschliessen
     * (siehe on_prefs_sidebar_row_selected()) - dafuer braucht jede
     * Listbox einen Verweis auf ihre Geschwister-Listbox. */
    g_object_set_data (G_OBJECT (nav_list), "prefs-sibling-list", profile_list);
    g_object_set_data (G_OBJECT (profile_list), "prefs-sibling-list", nav_list);

    /* Die Startauswahl (Profil "Unbenannt") wird hier bewusst NICHT
     * gesetzt: "prefs-stack" existiert an dieser Stelle noch nicht
     * (wird erst von on_terminal_settings_activate() nach dem Aufbau
     * der Seitenleiste angelegt), ein gtk_list_box_select_row() haette
     * also keine Wirkung auf die angezeigte Seite. Stattdessen hier nur
     * Listbox und Zeile fuer den Aufrufer hinterlegen. */
    g_object_set_data (G_OBJECT (window), "prefs-profile-list", profile_list);
    g_object_set_data (G_OBJECT (window), "prefs-profile-row", row);

    return sidebar;
}

static void
on_prefs_back_to_terminal_clicked (GtkButton *button, gpointer panel)
{
    gtk_stack_set_visible_child_name (GTK_STACK (panel), "terminal");
}

/* Terminal-Einstellungen als Seite im rechten Arbeitsbereich (kein eigenes Fenster). */
static void
on_terminal_settings_activate (GtkMenuItem *item, gpointer user_data)
{
    TerminalTabData *d = user_data;
    GtkWidget *panel, *old, *scroller, *page, *top_bar, *back, *title, *content_hbox, *sidebar, *stack;

    (void) item;

    if (d->window == NULL) {
        return;
    }
    panel = nolphin_window_get_workspace_panel (d->window);
    if (panel == NULL) {
        return;
    }

    if (prefs_profile_name == NULL) {
        prefs_profile_name = g_strdup (_("Unbenannt"));
    }

    /* Immer frisch aufbauen: eine vorhandene Seite vorher entfernen */
    old = gtk_stack_get_child_by_name (GTK_STACK (panel), "terminal-prefs");
    if (old != NULL) {
        gtk_container_remove (GTK_CONTAINER (panel), old);
    }

    page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width (GTK_CONTAINER (page), 8);

    top_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    back = gtk_button_new_with_label (_("Zum Terminal"));
    panel_decorate_button (back, "go-previous-symbolic", FALSE);
    g_signal_connect (back, "clicked", G_CALLBACK (on_prefs_back_to_terminal_clicked), panel);
    gtk_box_pack_start (GTK_BOX (top_bar), back, FALSE, FALSE, 0);
    title = gtk_label_new (_("Einstellungen – Profil »Unbenannt«"));
    gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class (gtk_widget_get_style_context (title), "heading");
    gtk_box_pack_start (GTK_BOX (top_bar), title, TRUE, TRUE, 0);
    g_object_set_data (G_OBJECT (page), "prefs-title-label", title);
    gtk_box_pack_start (GTK_BOX (page), top_bar, FALSE, FALSE, 0);

    /* Im schmalen Arbeitsbereich liegt die Auswahl über dem Inhalt statt daneben. */
    content_hbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start (GTK_BOX (page), content_hbox, TRUE, TRUE, 0);

    sidebar = build_prefs_sidebar (page);
    gtk_widget_set_size_request (sidebar, -1, -1);
    gtk_box_pack_start (GTK_BOX (content_hbox), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content_hbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    stack = gtk_stack_new ();
    gtk_widget_set_hexpand (stack, TRUE);
    gtk_widget_set_vexpand (stack, TRUE);
    gtk_stack_add_named (GTK_STACK (stack), build_prefs_general_page (), "general");
    gtk_stack_add_named (GTK_STACK (stack), build_prefs_keybindings_page (), "keybindings");
    gtk_stack_add_named (GTK_STACK (stack), build_prefs_profile_notebook (d->terminal_widget), "profile");
    gtk_stack_set_visible_child_name (GTK_STACK (stack), "profile");
    gtk_box_pack_start (GTK_BOX (content_hbox), stack, TRUE, TRUE, 0);
    g_object_set_data (G_OBJECT (page), "prefs-stack", stack);

    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scroller), page);
    gtk_widget_show_all (scroller);
    gtk_stack_add_named (GTK_STACK (panel), scroller, "terminal-prefs");
    gtk_stack_set_visible_child_name (GTK_STACK (panel), "terminal-prefs");

    /* Erst nach show_all(): GTK wählt beim ersten Anzeigen der Listbox
     * (nav_list) automatisch deren erste Zeile aus und würde eine vorher
     * gesetzte Auswahl überschreiben. */
    {
        GtkWidget *profile_list = g_object_get_data (G_OBJECT (page), "prefs-profile-list");
        GtkWidget *profile_row = g_object_get_data (G_OBJECT (page), "prefs-profile-row");

        if (profile_list != NULL && profile_row != NULL) {
            gtk_list_box_select_row (GTK_LIST_BOX (profile_list), GTK_LIST_BOX_ROW (profile_row));
        }
    }
}

static GtkWidget *
build_terminal_menu_bar (TerminalTabData *d)
{
	GtkWidget *terminal_widget = d->terminal_widget;
	GtkWidget *menu_bar;
	GtkWidget *edit_menu_item, *edit_menu, *copy_item, *copy_html_item, *paste_item, *select_all_item, *settings_item;
	GtkWidget *view_menu_item, *view_menu, *zoom_in_item, *zoom_out_item, *zoom_reset_item;
	GtkWidget *terminal_menu_item, *terminal_menu, *reset_item;

	menu_bar = gtk_menu_bar_new ();

	edit_menu_item = gtk_menu_item_new_with_label (_("Bearbeiten"));
	edit_menu = gtk_menu_new ();
	copy_item = gtk_menu_item_new_with_label (_("Kopieren"));
	g_signal_connect (copy_item, "activate", G_CALLBACK (on_terminal_copy_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), copy_item);
	copy_html_item = gtk_menu_item_new_with_label (_("Als HTML kopieren"));
	g_signal_connect (copy_html_item, "activate", G_CALLBACK (on_terminal_copy_html_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), copy_html_item);
	paste_item = gtk_menu_item_new_with_label (_("Einfügen"));
	g_signal_connect (paste_item, "activate", G_CALLBACK (on_terminal_paste_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), paste_item);
	select_all_item = gtk_menu_item_new_with_label (_("Alles auswählen"));
	g_signal_connect (select_all_item, "activate", G_CALLBACK (on_terminal_select_all_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), select_all_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), gtk_separator_menu_item_new ());
	settings_item = gtk_menu_item_new_with_label (_("Einstellungen …"));
	g_signal_connect (settings_item, "activate", G_CALLBACK (on_terminal_settings_activate), d);
	gtk_menu_shell_append (GTK_MENU_SHELL (edit_menu), settings_item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (edit_menu_item), edit_menu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), edit_menu_item);

	view_menu_item = gtk_menu_item_new_with_label (_("Ansicht"));
	view_menu = gtk_menu_new ();
	zoom_in_item = gtk_menu_item_new_with_label (_("Vergrößern"));
	g_signal_connect (zoom_in_item, "activate", G_CALLBACK (on_terminal_zoom_in_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (view_menu), zoom_in_item);
	zoom_out_item = gtk_menu_item_new_with_label (_("Verkleinern"));
	g_signal_connect (zoom_out_item, "activate", G_CALLBACK (on_terminal_zoom_out_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (view_menu), zoom_out_item);
	zoom_reset_item = gtk_menu_item_new_with_label (_("Normale Größe"));
	g_signal_connect (zoom_reset_item, "activate", G_CALLBACK (on_terminal_zoom_reset_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (view_menu), zoom_reset_item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (view_menu_item), view_menu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), view_menu_item);

	terminal_menu_item = gtk_menu_item_new_with_label (_("Terminal"));
	terminal_menu = gtk_menu_new ();
	reset_item = gtk_menu_item_new_with_label (_("Zurücksetzen"));
	g_signal_connect (reset_item, "activate", G_CALLBACK (on_terminal_reset_activate), terminal_widget);
	gtk_menu_shell_append (GTK_MENU_SHELL (terminal_menu), reset_item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (terminal_menu_item), terminal_menu);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), terminal_menu_item);

	return menu_bar;
}

static GtkWidget *
build_terminal_tab (NolphinWindow *window, GtkWidget *terminal_widget)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *back_button = build_back_to_preview_button (window);
	TerminalTabData *d = g_new0 (TerminalTabData, 1);
	GtkWidget *menu_bar;

	d->terminal_widget = terminal_widget;
	d->window = window;
	menu_bar = build_terminal_menu_bar (d);
	g_object_set_data_full (G_OBJECT (box), "terminal-tab-data", d, g_free);

	/* "Einstellungen …" im VTE-Rechtsklick-Kontextmenue (nolphin-terminal.c)
	 * oeffnet denselben Dialog wie der gleichnamige Eintrag hier in der
	 * Bearbeiten-Menueleiste. */
	g_signal_connect (terminal_widget, "settings-requested",
			  G_CALLBACK (on_terminal_settings_activate), d);

	gtk_widget_set_margin_start (back_button, 8);
	gtk_widget_set_margin_end (back_button, 8);
	gtk_widget_set_margin_top (back_button, 8);
	gtk_box_pack_start (GTK_BOX (box), back_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), menu_bar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), terminal_widget, TRUE, TRUE, 0);

	return box;
}

GtkWidget *
nolphin_workspace_panel_new (NolphinWindow *window, GtkWidget *preview_widget, GtkWidget *terminal_widget)
{
	GtkWidget *stack;

	g_return_val_if_fail (NOLPHIN_IS_WINDOW (window), NULL);
	g_return_val_if_fail (GTK_IS_WIDGET (preview_widget), NULL);
	g_return_val_if_fail (GTK_IS_WIDGET (terminal_widget), NULL);

	stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_NONE);
	/* GtkStack ist standardmaessig "homogeneous": er fordert fuer JEDE
	 * Seite die Breite der breitesten Seite an (z. B. das .deb-Formular),
	 * egal welche gerade sichtbar ist - dadurch liess sich das Panel bei
	 * kleinen Seiten (Vorschau, Git, Suche, …) nie schmaler ziehen als die
	 * breiteste Seite es erlaubt. Ausgeschaltet: jede Seite bekommt nur
	 * noch ihre eigene Breite. */
	gtk_stack_set_hhomogeneous (GTK_STACK (stack), FALSE);
	apply_workspace_panel_background (stack);

	/* GtkStack kann eine unsichtbare Seite nicht als aktuelle Seite
	 * führen - sie muss "visible" sein, auch wenn ihr Vorfahre (dieser
	 * Stack, siehe unten) es nicht ist. Erst der Stack selbst bleibt
	 * unsichtbar, bis nolphin_window_set_show_preview() (F11) ihn
	 * zeigt; das genügt, um die ganze Leiste verborgen zu halten. */
	gtk_widget_show_all (preview_widget);
	gtk_stack_add_named (GTK_STACK (stack), preview_widget, "preview");

	add_stack_page (GTK_STACK (stack), build_properties_tab (window), "properties");

	{
		GtkWidget *archive_inner_stack = NULL;
		GtkWidget *archive_page = build_archive_panel (window, &archive_inner_stack);

		add_stack_page (GTK_STACK (stack), archive_page, "archive");
		g_object_set_data (G_OBJECT (stack), "archive-inner-stack", archive_inner_stack);
	}

	add_stack_page (GTK_STACK (stack), build_search_tab (window), "search");
	add_stack_page (GTK_STACK (stack), build_batch_rename_tab (window), "rename");
	add_stack_page (GTK_STACK (stack), build_git_tab (window), "git");
	add_stack_page (GTK_STACK (stack), build_workspaces_tab (window), "workspaces");
	add_stack_page (GTK_STACK (stack), nolphin_diagnostics_page_new (window), "diagnostics");
	add_stack_page (GTK_STACK (stack), nolphin_duplicates_page_new (window), "duplicates");
	add_stack_page (GTK_STACK (stack), nolphin_versions_page_new (window), "versions");
	add_stack_page (GTK_STACK (stack), nolphin_sync_page_new (window), "sync");
	add_stack_page (GTK_STACK (stack), nolphin_rules_page_new (window), "rules");
	add_stack_page (GTK_STACK (stack), nolphin_gid_page_new (window), "gid");
	add_stack_page (GTK_STACK (stack), nolphin_phone_page_new (window), "phone");

	/* Terminal-Widget selbst kommt bereits sichtbar aus
	 * nolphin_terminal_new() (wie im frueheren unteren Bereich auch) -
	 * nur der umgebende Wrapper (Knopf) muss noch gezeigt werden. */
	gtk_widget_show_all (terminal_widget);
	add_stack_page (GTK_STACK (stack), build_terminal_tab (window, terminal_widget), "terminal");

	gtk_stack_set_visible_child_name (GTK_STACK (stack), "preview");

	return stack;
}

static void
workspace_panel_show_page (GtkWidget *workspace_panel, NolphinWindow *window, const gchar *page_name)
{
	nolphin_window_set_show_preview (window, TRUE);
	gtk_stack_set_visible_child_name (GTK_STACK (workspace_panel), page_name);
}

void
nolphin_workspace_panel_show_preview (GtkWidget *workspace_panel)
{
	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	gtk_stack_set_visible_child_name (GTK_STACK (workspace_panel), "preview");
}

void
nolphin_workspace_panel_show_terminal (GtkWidget *workspace_panel, NolphinWindow *window)
{
	workspace_panel_show_page (workspace_panel, window, "terminal");
}

/* Baut die Eigenschaften-Seite sofort für @files neu auf und zeigt sie -
 * kein zusätzlicher Klick auf "Aktualisieren" nötig, genau wie der frühere
 * Dialog sich sofort mit Inhalt öffnete. */
void
nolphin_workspace_panel_show_properties (GtkWidget *workspace_panel, NolphinWindow *window, GList *files)
{
	GtkWidget *properties_tab;
	PropertiesTabData *d;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));
	g_return_if_fail (files != NULL);

	properties_tab = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "properties");
	d = g_object_get_data (G_OBJECT (properties_tab), "properties-tab-data");

	if (d != NULL) {
		nolphin_properties_panel_set_files (NOLPHIN_PROPERTIES_PANEL (d->panel), files);
	}

	workspace_panel_show_page (workspace_panel, window, "properties");
}

void
nolphin_workspace_panel_show_archive (GtkWidget *workspace_panel, NolphinWindow *window)
{
	GtkWidget *archive_inner_stack;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	/* Beide Formulare teilen sich den "archive"-Platz: ohne diese Zeile
	 * bliebe nach dem .deb-Formular dieses auch bei "Komprimieren" stehen. */
	archive_inner_stack = g_object_get_data (G_OBJECT (workspace_panel), "archive-inner-stack");

	if (archive_inner_stack != NULL) {
		GtkWidget *page = gtk_stack_get_child_by_name (GTK_STACK (archive_inner_stack), "compress");
		ArchiveTabData *data = (page != NULL) ? g_object_get_data (G_OBJECT (page), "archive-tab-ptr") : NULL;

		gtk_stack_set_visible_child_name (GTK_STACK (archive_inner_stack), "compress");
		if (data != NULL) {
			archive_tab_refresh_from_selection (data);
		}
	}

	workspace_panel_show_page (workspace_panel, window, "archive");
}

/* Zeigt die Eigenschaften im rechten Arbeitsbereich - auch wenn kein Fenster
 * als Ausgangspunkt bekannt ist (z. B. Aufruf über D-Bus): dann das aktive
 * Nolphin-Fenster, sonst ein neues. Ersetzt den früheren Eigenschaften-Dialog. */
void
nolphin_workspace_panel_show_properties_anywhere (GList *files, GtkWidget *parent_widget)
{
	NolphinWindow *window = NULL;
	NolphinApplication *app = nolphin_application_get_singleton ();
	GtkWidget *top = parent_widget != NULL ? gtk_widget_get_toplevel (parent_widget) : NULL;

	g_return_if_fail (files != NULL);

	if (top != NULL && NOLPHIN_IS_WINDOW (top)) {
		window = NOLPHIN_WINDOW (top);
	}
	if (window == NULL) {
		GtkWindow *active = gtk_application_get_active_window (GTK_APPLICATION (app));

		if (active != NULL && NOLPHIN_IS_WINDOW (active)) {
			window = NOLPHIN_WINDOW (active);
		}
	}
	if (window == NULL) {
		window = nolphin_application_create_window (app, gdk_screen_get_default ());
		gtk_window_present (GTK_WINDOW (window));
	}

	nolphin_workspace_panel_show_properties (nolphin_window_get_workspace_panel (window), window, files);
}

/* Zeigt die Archiv-Verwaltung für @archive (Inhalt, Hinzufügen, Entfernen, …). */
void
nolphin_workspace_panel_show_archive_manager (GtkWidget *workspace_panel, NolphinWindow *window, GFile *archive)
{
	GtkWidget *inner, *page;
	ArchiveManagerTab *d;
	gchar *name;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));
	g_return_if_fail (G_IS_FILE (archive));

	inner = g_object_get_data (G_OBJECT (workspace_panel), "archive-inner-stack");
	if (inner == NULL) {
		return;
	}
	page = gtk_stack_get_child_by_name (GTK_STACK (inner), "manage");
	d = page != NULL ? g_object_get_data (G_OBJECT (page), "archive-manager-data") : NULL;
	if (d == NULL) {
		return;
	}

	g_clear_object (&d->archive);
	d->archive = g_object_ref (archive);
	d->format = nolphin_archive_detect_format (archive);
	name = g_file_get_basename (archive);
	gtk_label_set_text (GTK_LABEL (d->title_label), name);
	g_free (name);
	gtk_list_store_clear (d->store);

	gtk_stack_set_visible_child_name (GTK_STACK (inner), "manage");
	workspace_panel_show_page (workspace_panel, window, "archive");
	archive_manager_reload (d);
}

/* Zeigt Regeln/Stapelverarbeitung: @selection_paths NULL = Ordner @folder. */
void
nolphin_workspace_panel_show_rules (GtkWidget *workspace_panel, NolphinWindow *window, GFile *folder,
				    GList *selection_paths, gboolean batch_mode)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "rules");
	if (page != NULL) {
		nolphin_rules_page_open (page, folder, selection_paths, batch_mode);
	}
	workspace_panel_show_page (workspace_panel, window, "rules");
}

/* Zeigt Vergleich/Synchronisation mit @folder als lokalem Ordner. */
void
nolphin_workspace_panel_show_sync (GtkWidget *workspace_panel, NolphinWindow *window, GFile *folder)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "sync");
	if (page != NULL && folder != NULL) {
		nolphin_sync_page_open (page, folder);
	}
	workspace_panel_show_page (workspace_panel, window, "sync");
}

/* Zeigt den Handy-Austausch. */
void
nolphin_workspace_panel_show_phone (GtkWidget *workspace_panel, NolphinWindow *window, gboolean autostart)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "phone");
	nolphin_phone_page_open (page, autostart);
	workspace_panel_show_page (workspace_panel, window, "phone");
}

/* Zeigt die Versionen der Datei @file. */
void
nolphin_workspace_panel_show_versions (GtkWidget *workspace_panel, NolphinWindow *window, GFile *file)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "versions");
	if (page != NULL && file != NULL) {
		nolphin_versions_page_open (page, file);
	}
	workspace_panel_show_page (workspace_panel, window, "versions");
}

/* Zeigt die Duplikat-Suche für @folder. */
void
nolphin_workspace_panel_show_duplicates (GtkWidget *workspace_panel, NolphinWindow *window, GFile *folder)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "duplicates");
	if (page != NULL && folder != NULL) {
		nolphin_duplicates_page_open (page, folder);
	}
	workspace_panel_show_page (workspace_panel, window, "duplicates");
}

/* Zeigt die Diagnose-Seite mit dem gewünschten Reiter. */
void
nolphin_workspace_panel_show_diagnostics (GtkWidget *workspace_panel, NolphinWindow *window, gint tab)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "diagnostics");
	if (page != NULL) {
		nolphin_diagnostics_page_show_tab (page, (NolphinDiagTab) tab);
	}
	workspace_panel_show_page (workspace_panel, window, "diagnostics");
}

/* Zeigt die Arbeitsbereiche-Seite; @focus_name: Namensfeld (Speichern) statt Liste fokussieren. */
void
nolphin_workspace_panel_show_workspaces (GtkWidget *workspace_panel, NolphinWindow *window, gboolean focus_name)
{
	GtkWidget *page;
	WorkspacesTab *d;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "workspaces");
	d = page != NULL ? g_object_get_data (G_OBJECT (page), "workspaces-data") : NULL;
	if (d == NULL) {
		return;
	}
	ws_reload (d);
	ws_set_status (d, NULL);
	workspace_panel_show_page (workspace_panel, window, "workspaces");
	gtk_widget_grab_focus (focus_name ? d->name_entry : d->tree);
}

void
nolphin_workspace_panel_show_search (GtkWidget *workspace_panel, NolphinWindow *window)
{
	workspace_panel_show_page (workspace_panel, window, "search");
}

/* Massenumbenennung: baut die Vorschau-Liste fuer @files neu auf
 * (alte NolphinFile-Referenzen werden zuerst freigegeben) und zeigt die
 * Seite. Der Aufrufer behaelt das Eigentum an @files. */
void
nolphin_workspace_panel_show_batch_rename (GtkWidget *workspace_panel, NolphinWindow *window, GList *files)
{
	GtkWidget *tab;
	BatchRenameTab *d;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));
	g_return_if_fail (files != NULL);

	tab = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "rename");
	d = g_object_get_data (G_OBJECT (tab), "batch-rename-tab-data");

	if (d != NULL) {
		GtkTreeIter iter;
		gboolean valid;
		GList *l;

		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (d->store), &iter);
		while (valid) {
			NolphinFile *old_file;

			gtk_tree_model_get (GTK_TREE_MODEL (d->store), &iter, BR_COL_FILE, &old_file, -1);
			if (old_file != NULL) {
				nolphin_file_unref (old_file);
			}
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (d->store), &iter);
		}
		gtk_list_store_clear (d->store);

		for (l = files; l != NULL; l = l->next) {
			NolphinFile *file = NOLPHIN_FILE (l->data);
			gchar *name = nolphin_file_get_display_name (file);

			gtk_list_store_append (d->store, &iter);
			gtk_list_store_set (d->store, &iter,
					     BR_COL_ORIG, name,
					     BR_COL_NEW, name,
					     BR_COL_FILE, nolphin_file_ref (file),
					     BR_COL_IS_DIR, nolphin_file_is_directory (file),
					     -1);
			g_free (name);
		}

		gtk_entry_set_text (GTK_ENTRY (d->search_entry), "");
		gtk_entry_set_text (GTK_ENTRY (d->replace_entry), "");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->numbering_check), FALSE);
		gtk_combo_box_set_active (GTK_COMBO_BOX (d->case_mode_combo), 0);
		gtk_label_set_text (GTK_LABEL (d->status_label), "");
		gtk_widget_set_sensitive (d->apply_button, TRUE);

		batch_rename_update_preview (d);
	}

	workspace_panel_show_page (workspace_panel, window, "rename");
}

/* Zeigt die Git-Seite und stößt sofort eine Status-Aktualisierung an. */
void
nolphin_workspace_panel_show_git (GtkWidget *workspace_panel, NolphinWindow *window)
{
	GtkWidget *git_tab;
	GitTabData *d;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	git_tab = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "git");
	d = g_object_get_data (G_OBJECT (git_tab), "git-tab-data");

	if (d != NULL) {
		git_tab_resolve_root (d, git_tab_status_found);
	}

	workspace_panel_show_page (workspace_panel, window, "git");
}

void
nolphin_workspace_panel_show_gid (GtkWidget *workspace_panel, NolphinWindow *window, GFile *project_folder)
{
	GtkWidget *page;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "gid");
	if (page != NULL && project_folder != NULL) {
		nolphin_gid_page_open (page, project_folder);
	}
	workspace_panel_show_page (workspace_panel, window, "gid");
}

void
nolphin_workspace_panel_show_deb_builder (GtkWidget *workspace_panel, NolphinWindow *window)
{
	GtkWidget *archive_inner_stack;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	archive_inner_stack = g_object_get_data (G_OBJECT (workspace_panel), "archive-inner-stack");

	if (archive_inner_stack != NULL) {
		gtk_stack_set_visible_child_name (GTK_STACK (archive_inner_stack), "deb");
	}

	workspace_panel_show_page (workspace_panel, window, "archive");
}

/* Hält die Eigenschaften-Seite synchron mit der Auswahl im Hauptfenster,
 * solange sie sichtbar ist (ohne Auswahl: der aktuelle Ordner). */
void
nolphin_workspace_panel_sync_properties (GtkWidget *workspace_panel, GList *selection, NolphinFile *directory_as_file)
{
	GtkWidget *properties_tab;
	PropertiesTabData *d;
	GList *single = NULL;

	g_return_if_fail (GTK_IS_STACK (workspace_panel));

	if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (workspace_panel)), "properties") != 0) {
		return;
	}

	properties_tab = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "properties");
	d = (properties_tab != NULL) ? g_object_get_data (G_OBJECT (properties_tab), "properties-tab-data") : NULL;
	if (d == NULL) {
		return;
	}

	if (selection == NULL && directory_as_file != NULL) {
		single = g_list_prepend (NULL, directory_as_file);
		selection = single;
	}
	nolphin_properties_panel_set_files (NOLPHIN_PROPERTIES_PANEL (d->panel), selection);
	g_list_free (single);
}

GtkWidget *
nolphin_workspace_panel_get_search_host (GtkWidget *workspace_panel)
{
	GtkWidget *search_page;

	g_return_val_if_fail (GTK_IS_STACK (workspace_panel), NULL);

	search_page = gtk_stack_get_child_by_name (GTK_STACK (workspace_panel), "search");
	return (search_page != NULL) ? g_object_get_data (G_OBJECT (search_page), "search-host") : NULL;
}
