# Principal Assessment Tests

test_rr_sources = files(
  'test_route_reflector.c',
)

test_rr = executable('test_route_reflector',
  test_rr_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra, libbgp, libbgp_rr],
  install : false,
)

test('BGP Route Reflector Tests', test_rr,
  timeout : 120,
  is_parallel : false,
)
