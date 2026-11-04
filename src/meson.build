dbusBuiltSources = gnome.gdbus_codegen(
  'nolphin-freedesktop-generated',
  join_paths(meson.project_source_root(), 'data', 'freedesktop-dbus-interfaces.xml'),
  interface_prefix: 'org.freedesktop.',
  namespace: 'NolphinFreedesktop',
  object_manager: true
)

nolphinBuiltSources = gnome.gdbus_codegen(
  'nolphin-cinnamon-dbus',
  join_paths(meson.project_source_root(), 'data', 'org.Cinnamon.xml'),
  interface_prefix: 'org.',
  namespace: 'Nolphin',
  object_manager: true
)

nolphinCommon_sources = [
  dbusBuiltSources,
  nolphinBuiltSources,
  'nolphin-action-config-widget.c',
  'nolphin-application.c',
  'nolphin-blank-desktop-window.c',
  'nolphin-bookmark-list.c',
  'nolphin-bookmarks-window.c',
  'nolphin-config-base-widget.c',
  'nolphin-connect-server-dialog-nonmain.c',
  'nolphin-connect-server-dialog.c',
  'nolphin-connect-server-operation.c',
  'nolphin-deb-builder.c',
  'nolphin-desktop-icon-grid-view.c',
  'nolphin-desktop-item-properties.c',
  'nolphin-desktop-manager.c',
  'nolphin-desktop-overlay.c',
  'nolphin-desktop-window.c',
  'nolphin-error-reporting.c',
  'nolphin-extension-config-widget.c',
  'nolphin-file-management-properties.c',
  'nolphin-filter-bar.c',
  'nolphin-floating-bar.c',
  'nolphin-freedesktop-dbus.c',
  'nolphin-icon-view-container.c',
  'nolphin-icon-view-grid-container.c',
  'nolphin-icon-view.c',
  'nolphin-image-properties-page.c',
  'nolphin-interesting-folder-bar.c',
  'nolphin-list-model.c',
  'nolphin-list-view.c',
  'nolphin-location-bar.c',
  'nolphin-diagnostics.c',
  'nolphin-help.c',
  'nolphin-duplicates.c',
  'nolphin-rules.c',
  'nolphin-sync.c',
  'nolphin-tools.c',
  'nolphin-versions.c',
  'nolphin-location-stats.c',
  'nolphin-location-entry.c',
  'nolphin-mime-actions.c',
  'nolphin-navigation-action.c',
  'nolphin-navigation-state.c',
  'nolphin-notebook.c',
  'nolphin-pathbar.c',
  'nolphin-places-sidebar.c',
  'nolphin-plugin-manager.c',
  'nolphin-previewer.c',
  'nolphin-progress-info-widget.c',
  'nolphin-progress-ui-handler.c',
  'nolphin-query-editor.c',
  'nolphin-template-config-widget.c',
  'nolphin-self-check-functions.c',
  'nolphin-statusbar.c',
  'nolphin-thumbnail-problem-bar.c',
  'nolphin-toolbar.c',
  'nolphin-trash-bar.c',
  'nolphin-tree-sidebar-model.c',
  'nolphin-tree-sidebar.c',
  'nolphin-view-dnd.c',
  'nolphin-view-factory.c',
  'nolphin-view.c',
  'nolphin-window-bookmarks.c',
  'nolphin-window-manage-views.c',
  'nolphin-window-menus.c',
  'nolphin-window-pane.c',
  'nolphin-terminal.c',
  'nolphin-preview.c',
  'nolphin-properties-panel.c',
  'nolphin-window-slot-dnd.c',
  'nolphin-window-slot.c',
  'nolphin-window.c',
  'nolphin-gid-menu.c',
  'nolphin-gid-projects.c',
  'nolphin-gid-sidebar.c',
  'nolphin-gid.c',
  'nolphin-phone.c',
  'nolphin-phone-server.c',
  'nolphin-phone-tls.c',
  'nolphin-qr.c',
  'nolphin-markdown-view.c',
  'nolphin-git-page.c',
  'nolphin-workspace-panel.c',
  'nolphin-x-content-bar.c',
]

nolphinWindow_sources = [
  'nolphin-main-application.c',
  'nolphin-main.c',
]
nolphinWindow_headers = [
  'nolphin-main-application.h',
]

nolphinDesktop_sources = [
  'nolphin-desktop-application.c',
  'nolphin-desktop-icon-view.c',
  'nolphin-desktop-main.c',
]
nolphinDesktop_headers = [
  'nolphin-desktop-application.h',
  'nolphin-desktop-overlay.h',
  'nolphin-desktop-icon-view.h',
]


if enableEmptyView
  nolphinCommon_sources += 'nolphin-empty-view.c'
endif

nolphin_deps = [ cinnamon, gail, glib, gtk, math,
  egg, nolphin_extension, nolphin_private, xapp, vte ]

if exempi_enabled
  nolphin_deps += exempi
endif

if libexif_enabled
  nolphin_deps += libexif
endif

if gtk_layer_shell_enabled
  nolphin_deps += gtk_layer_shell
  nolphin_deps += wayland_client
endif

# Bei Installation außerhalb von /usr (z. B. /usr/local) muss das Programm seine
# eigene libnolphin-extension finden, nicht die eines Systempakets in /lib.
nolphin_install_rpath = get_option('prefix') == '/usr' ? '' : join_paths(get_option('prefix'), get_option('libdir'))

nolphin = executable('nolphin',
  nolphinCommon_sources + nolphinWindow_sources,
  include_directories: [ rootInclude ],
  c_args: nolphin_definitions,
  dependencies: nolphin_deps,
  install: true,
  install_rpath: nolphin_install_rpath
)

nolphinDesktop = executable('nolphin-desktop',
  nolphinCommon_sources + nolphinDesktop_sources,
  include_directories: [ rootInclude],
  c_args: nolphin_definitions,
  dependencies: nolphin_deps,
  install: true,
  install_rpath: nolphin_install_rpath
)

nolphin_autorun_software = executable('nolphin-autorun-software',
  [ 'nolphin-autorun-software.c' ],
  include_directories: [ rootInclude, ],
  c_args: nolphin_definitions,
  dependencies: nolphin_deps,
  install: true,
  install_rpath: nolphin_install_rpath
)

nolphin_connect_server = executable('nolphin-connect-server',
  [
    'nolphin-bookmark-list.c',
    'nolphin-connect-server-dialog.c',
    'nolphin-connect-server-dialog-main.c',
    'nolphin-connect-server-operation.c'
  ],
  include_directories: [ rootInclude, ],
  c_args: nolphin_definitions,
  dependencies: nolphin_deps,
  install: true,
  install_rpath: nolphin_install_rpath
)

nolphin_open_with = executable('nolphin-open-with',
  [ 'nolphin-open-with-main.c' ],
  include_directories: [ rootInclude, ],
  c_args: nolphin_definitions,
  dependencies: nolphin_deps,
  install: true,
  install_rpath: nolphin_install_rpath
)

# LibExecs
nolphin_extensions_list = executable('nolphin-extensions-list',
  [ 'nolphin-extensions-list.c' ],
  include_directories: [ rootInclude, ],
  c_args: nolphin_definitions,
  dependencies: [ nolphin_deps ],
  install: true,
  install_dir: libExecPath,
  install_rpath: nolphin_install_rpath
)
