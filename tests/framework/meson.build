# Test framework library

test_framework_sources = files(
  'test_framework.c',
)

libtest = static_library('test_framework',
  test_framework_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon],
  install : false,
)

test_dep = declare_dependency(
  link_with : libtest,
  include_directories : [root_inc, lib_inc],
)
