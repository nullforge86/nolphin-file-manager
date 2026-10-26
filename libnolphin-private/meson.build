
dbusBuiltSources = gnome.gdbus_codegen('nolphin-generated',
  join_paths(meson.project_source_root(), 'data', 'dbus-interfaces.xml'),
  interface_prefix: 'org.Nolphin.',
  namespace: 'NolphinDBus',
  object_manager: true
)

nolphin_private_sources = [
  dbusBuiltSources,
  'nolphin-acl.c',
  'nolphin-action-manager.c',
  'nolphin-action.c',
  'nolphin-archive.c',
  'nolphin-archive-manage.c',
  'nolphin-bookmark.c',
  'nolphin-cad.c',
  'nolphin-cell-renderer-disk.c',
  'nolphin-centered-placement-grid.c',
  'nolphin-checksum.c',
  'nolphin-clipboard-monitor.c',
  'nolphin-encryption.c',
  'nolphin-clipboard.c',
  'nolphin-column-chooser.c',
  'nolphin-column-utilities.c',
  'nolphin-dbus-manager.c',
  'nolphin-debug.c',
  'nolphin-default-file-icon.c',
  'nolphin-desktop-directory-file.c',
  'nolphin-desktop-directory.c',
  'nolphin-desktop-icon-file.c',
  'nolphin-desktop-link-monitor.c',
  'nolphin-desktop-link.c',
  'nolphin-desktop-metadata.c',
  'nolphin-desktop-utils.c',
  'nolphin-directory-async.c',
  'nolphin-directory.c',
  'nolphin-dnd.c',
  'nolphin-entry.c',
  'fzy-match.c',
  'nolphin-fzy-utils.c',
  'nolphin-file-changes-queue.c',
  'nolphin-file-conflict-dialog.c',
  'nolphin-file-dnd.c',
  'nolphin-file-operations.c',
  'nolphin-file-queue.c',
  'nolphin-file-undo-manager.c',
  'nolphin-file-undo-operations.c',
  'nolphin-file-utilities.c',
  'nolphin-file.c',
  'nolphin-git.c',
  'nolphin-git-overlay.c',
  'nolphin-global-preferences.c',
  'nolphin-icon-canvas-item.c',
  'nolphin-icon-container.c',
  'nolphin-icon-dnd.c',
  'nolphin-icon-fallback.c',
  'nolphin-icon-info.c',
  'nolphin-job-queue.c',
  'nolphin-lib-self-check-functions.c',
  'nolphin-malloc-utils.c',
  'nolphin-link.c',
  'nolphin-merged-directory.c',
  'nolphin-media.c',
  'nolphin-file-chooser-preview.c',
  'nolphin-search-expression.c',
  'nolphin-metadata.c',
  'nolphin-mime-application-chooser.c',
  'nolphin-module.c',
  'nolphin-monitor.c',
  'nolphin-mount-operation.c',
  'nolphin-placement-grid.c',
  'nolphin-places-tree-view.c',
  'nolphin-program-choosing.c',
  'nolphin-progress-info-manager.c',
  'nolphin-progress-info.c',
  'nolphin-query.c',
  'nolphin-recent.c',
  'nolphin-saved-selections.c',
  'nolphin-search-directory-file.c',
  'nolphin-search-directory.c',
  'nolphin-search-engine-advanced.c',
  'nolphin-search-engine.c',
  'nolphin-selection-canvas-item.c',
  'nolphin-separator-action.c',
  'nolphin-signaller.c',
  'nolphin-thumbnailer.c',
  'nolphin-thumbnails.c',
  'nolphin-tool-progress.c',
  'nolphin-trash-cleanup.c',
  'nolphin-trash-monitor.c',
  'nolphin-tree-view-drag-dest.c',
  'nolphin-ui-utilities.c',
  'nolphin-undo-manager.c',
  'nolphin-undo-signal-handlers.c',
  'nolphin-undo-transaction.c',
  'nolphin-undo.c',
  'nolphin-vfs-directory.c',
  'nolphin-vfs-file.c',
  'nolphin-widget-action.c',
  'nolphin-widget-menu-item.c',
]

libgsf = dependency('libgsf-1', required: true)

nolphin_private_deps = [
  cinnamon, eel, gail, gio_unix, glib, gmodule, gtk, json, libgsf, math, nolphin_extension, x11, xapp
]

if libexif_enabled
  nolphin_private_deps += libexif
endif

if tracker_enabled
  nolphin_private_sources += 'nolphin-search-engine-tracker.c'
  nolphin_private_deps += tracker_sparql
endif

if libselinux_enabled
  nolphin_private_deps += libselinux
endif

if gtk_layer_shell_enabled
  nolphin_private_deps += gtk_layer_shell
endif

nolphin_private_lib = static_library('nolphin-private',
  nolphin_private_sources,
  dependencies: nolphin_private_deps,
  include_directories: [ rootInclude, ],
  c_args: nolphin_definitions,
)

nolphin_private = declare_dependency(
  include_directories: include_directories('.'),
  link_with: [ nolphin_private_lib ],
  dependencies: nolphin_private_deps,
)

install_data('org.nolphin.gschema.xml',
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

# Compiles org.nolphin.gschema.xml into build/libnolphin-private/
# gschemas.compiled on every build (independent of `ninja install`),
# so an uninstalled/dev binary can run with
# GSETTINGS_SCHEMA_DIR=<builddir>/libnolphin-private instead of
# requiring root to install the schema system-wide first.
gnome.compile_schemas(depend_files: files('org.nolphin.gschema.xml'))
