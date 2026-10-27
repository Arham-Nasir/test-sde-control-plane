# SE Assessment Starter Code

se_gr_sources = files(
  'bgp_graceful_restart.c',
)

# Build as a module that links with the main BGP library
libbgp_gr = static_library('bgp_gr',
  se_gr_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra, libbgp],
  install : false,
)

bgp_gr_dep = declare_dependency(
  link_with : libbgp_gr,
  include_directories : [root_inc, lib_inc],
  dependencies : [common_dep, zebra_dep, bgp_dep],
)
