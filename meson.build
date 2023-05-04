project('ld53', 'c', 'cpp', default_options: ['cpp_std=c++17'])

fs = import('fs')
cmake = import('cmake')

sources = []
includedirs = []
subdir('src')
subdir('shaders')
subdir('textures')
subdir('thirdparty')
subdir('audio')

portaudio_cmake = cmake.subproject('portaudio')

depends = [
  dependency('glfw3'),
  dependency('GL'),
  dependency('glm'),
  # dependency('portaudio-2.0'),
  dependency('ogg'),
  dependency('vorbisfile'),
  portaudio_cmake.dependency('PortAudio')
]

executable('ld53', sources, include_directories: includedirs, dependencies: depends, build_rpath: 'lib')
