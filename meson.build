project('ld53', 'c', 'cpp', default_options: ['cpp_std=c++17'])

fs = import('fs')

sources = []
includedirs = []
subdir('src')
subdir('shaders')
subdir('textures')
subdir('thirdparty')
subdir('audio')

depends = [
  dependency('glfw3'),
  dependency('GL'),
  dependency('portaudio-2.0'),
  dependency('vorbisfile'),
]

executable('ld53', sources, include_directories: includedirs, dependencies: depends, build_rpath: 'lib')
