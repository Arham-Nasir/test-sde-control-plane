# Integration tests

test_integration_sources = files(
  'test_integration.c',
)

test_integration = executable('test_integration',
  test_integration_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra, libbgp],
  install : false,
)

test('Integration Tests', test_integration,
  timeout : 300,
  is_parallel : false,
)
