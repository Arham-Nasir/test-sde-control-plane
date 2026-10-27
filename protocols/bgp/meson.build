# BGP Daemon
# This provides the BGP protocol implementation skeleton that candidates
# will extend in their assessments.

bgp_sources = files(
  'bgpd.c',
  'bgp_fsm.c',
  'bgp_packet.c',
  'bgp_neighbor.c',
  'bgp_attr.c',
  'bgp_route.c',
)

bgp_headers = files(
  'bgpd.h',
  'bgp_fsm.h',
  'bgp_packet.h',
  'bgp_neighbor.h',
  'bgp_attr.h',
  'bgp_route.h',
)

libbgp = static_library('bgp',
  bgp_sources,
  include_directories : [root_inc, lib_inc],
  link_with : [libcommon, libzebra],
  install : false,
)

bgp_dep = declare_dependency(
  link_with : libbgp,
  include_directories : [root_inc, lib_inc],
  dependencies : [common_dep, zebra_dep],
)
