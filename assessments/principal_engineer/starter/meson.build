# Principal Assessment Starter Code

principal_rr_sources = files(
  'bgp_route_reflector.c',
)

libbgp_rr = static_library('bgp_rr',
  principal_rr_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra, libbgp],
  install : false,
)

bgp_rr_dep = declare_dependency(
  link_with : libbgp_rr,
  include_directories : [root_inc, lib_inc],
  dependencies : [common_dep, zebra_dep, bgp_dep],
)
