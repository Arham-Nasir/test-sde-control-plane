# SE Assessment Tests

test_gr_sources = files(
  'test_graceful_restart.c',
)

test_gr = executable('test_graceful_restart',
  test_gr_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra, libbgp, libbgp_gr],
  install : false,
)

test('BGP Graceful Restart Tests', test_gr,
  timeout : 60,
  is_parallel : false,
)
