project('ld53', 'c', 'cpp', default_options: ['cpp_std=c++17'])

fs = import('fs')

sources = []
includedirs = []
subdir('src')
subdir('shaders')
subdir('textures')
subdir('thirdparty')

depends = [
  dependency('glfw3'),
  dependency('GL')
]

executable('ld53', sources, include_directories: includedirs, dependencies: depends)
