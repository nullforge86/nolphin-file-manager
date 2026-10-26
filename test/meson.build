
test('Search Engine test',
  executable('test-nolphin-search-engine',
    [ 'test-nolphin-search-engine.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Directory Async test',
  executable('test-nolphin-directory-async',
    [ 'test-nolphin-directory-async.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Copy test',
  executable('test-nolphin-copy',
    [ 'test-copy.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
    c_args: nolphin_definitions,
  ),
  args: []
)

test('Archive test',
  executable('test-nolphin-archive',
    [ 'test-nolphin-archive.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Tool progress test',
  executable('test-nolphin-tool-progress',
    [ 'test-nolphin-tool-progress.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('CAD test',
  executable('test-nolphin-cad',
    [ 'test-nolphin-cad.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Recursive ownership test',
  executable('test-nolphin-ownership',
    [ 'test-nolphin-ownership.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Grouping test',
  executable('test-nolphin-grouping',
    [ 'test-nolphin-grouping.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Type selection test',
  executable('test-nolphin-type-selection',
    [ 'test-nolphin-type-selection.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Checksum test',
  executable('test-nolphin-checksum',
    [ 'test-nolphin-checksum.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Encryption test',
  executable('test-nolphin-encryption',
    [ 'test-nolphin-encryption.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('ACL test',
  executable('test-nolphin-acl',
    [ 'test-nolphin-acl.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Git test',
  executable('test-nolphin-git',
    [ 'test-nolphin-git.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Trash cleanup test',
  executable('test-nolphin-trash-cleanup',
    [ 'test-nolphin-trash-cleanup.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Saved selections test',
  executable('test-nolphin-saved-selections',
    [ 'test-nolphin-saved-selections.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Metadata/Tags registration test',
  executable('test-nolphin-metadata-tags',
    [ 'test-nolphin-metadata-tags.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Archive manager test',
  executable('test-nolphin-archive-manage',
    [ 'test-nolphin-archive-manage.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Git overlay test',
  executable('test-nolphin-git-overlay',
    [ 'test-nolphin-git-overlay.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Media info test',
  executable('test-nolphin-media',
    [ 'test-nolphin-media.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Search expression test',
  executable('test-nolphin-search-expression',
    [ 'test-nolphin-search-expression.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Markdown renderer test',
  executable('test-nolphin-markdown',
    [ 'test-nolphin-markdown.c', '../src/nolphin-markdown-view.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  ),
  args: []
)

test('GID projects test',
  executable('test-nolphin-gid-projects',
    [ 'test-nolphin-gid-projects.c', '../src/nolphin-gid-projects.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
    c_args: nolphin_definitions,
  ),
  env: [
    'GSETTINGS_BACKEND=memory',
    'GSETTINGS_SCHEMA_DIR=' + meson.current_build_dir() + '/../libnolphin-private',
  ],
  args: []
)

test('Phone exchange test',
  executable('test-nolphin-phone-server',
    [ 'test-nolphin-phone-server.c', '../src/nolphin-phone-server.c', '../src/nolphin-phone-tls.c', '../src/nolphin-qr.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
    c_args: nolphin_definitions,
  ),
  args: []
)

test('Thumbnailer test',
  executable('test-nolphin-thumbnailer',
    [ 'test-nolphin-thumbnailer.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Settings schema test',
  executable('test-nolphin-settings-schema',
    [ 'test-nolphin-settings-schema.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: []
)

test('Icon fallback test',
  executable('test-nolphin-icon-fallback',
    [ 'test-nolphin-icon-fallback.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nolphin_private ],
  ),
  args: [ meson.project_source_root() ]
)

# Werkzeug, kein Test: zeigt, welche xsi-Symbole je Symbolthema nach dem Ersatz fehlen
executable('icon-theme-coverage',
  [ 'icon-theme-coverage.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nolphin_private ],
)
