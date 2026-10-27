project('test-sde-control-plane', 'c',
  version : '1.0.0',
  license : 'Apache-2.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
    'b_sanitize=address,undefined',
    'b_lundef=false',
  ],
  meson_version : '>= 0.56.0'
)

# =============================================================================
# Control Plane Technical Assessment
# =============================================================================
# This repository contains technical assessments for Control Plane Software
# Engineers. The codebase mimics patterns from FRR (Free Range Routing) and
# SONiC (Software for Open Networking in the Cloud).
#
# Build Instructions:
#   meson setup builddir
#   cd builddir && ninja
#   ninja test
#
# Assessment Tracks:
#   - SE Level: BGP Graceful Restart Implementation
#   - Principal Level: BGP Route Reflector with Cluster Optimization
# =============================================================================

cc = meson.get_compiler('c')

# Common dependencies
thread_dep = dependency('threads')
math_dep = cc.find_library('m', required : false)

# Configuration
conf_data = configuration_data()
conf_data.set_quoted('VERSION', meson.project_version())
conf_data.set_quoted('PACKAGE_NAME', meson.project_name())
conf_data.set('HAVE_PTHREAD', thread_dep.found())

configure_file(
  output : 'config.h',
  configuration : conf_data
)

# Include directories
root_inc = include_directories('.')
lib_inc = include_directories('lib')

# Build options
assessment_level = get_option('assessment_level')
enable_integration_tests = get_option('enable_integration_tests')

# Subdirectories
subdir('lib')
subdir('protocols')
subdir('assessments')
subdir('tests')

# Summary
summary({
  'Assessment Level': assessment_level,
  'Integration Tests': enable_integration_tests,
  'Sanitizers': get_option('b_sanitize'),
}, section: 'Configuration')
