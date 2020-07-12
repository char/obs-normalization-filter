project('obs_normalize_filter', 'c', default_options : ['buildtype=release', 'warning_level=2'])

include = include_directories('include', 'libebur128/ebur128')
obs = dependency('libobs')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

sources = ['src/main.c', 'src/normalisation.c', 'libebur128/ebur128/ebur128.c']

shared_library(meson.project_name(), sources, include_directories : include, dependencies : [obs, m_dep])
