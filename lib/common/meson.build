# Common library - FRR-style utilities
# This library provides the foundational data structures and utilities
# used across all routing protocol daemons.

common_sources = files(
  'memory.c',
  'linklist.c',
  'prefix.c',
  'thread.c',
  'log.c',
  'hash.c',
  'stream.c',
)

common_headers = files(
  'memory.h',
  'linklist.h',
  'prefix.h',
  'thread.h',
  'log.h',
  'hash.h',
  'stream.h',
)

libcommon = static_library('common',
  common_sources,
  include_directories : [root_inc, lib_inc],
  dependencies : [thread_dep],
  install : false,
)

common_dep = declare_dependency(
  link_with : libcommon,
  include_directories : [root_inc, lib_inc],
  dependencies : [thread_dep],
)
