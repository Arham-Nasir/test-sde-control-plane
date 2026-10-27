# Zebra library - RIB and inter-daemon communication
# This provides the Routing Information Base abstraction and
# Zebra API for communication between routing daemons.

zebra_sources = files(
  'rib.c',
  'zapi.c',
)

zebra_headers = files(
  'rib.h',
  'zapi.h',
)

libzebra = static_library('zebra',
  zebra_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon],
  install : false,
)

zebra_dep = declare_dependency(
  link_with : libzebra,
  include_directories : [root_inc, lib_inc],
  dependencies : [common_dep],
)
