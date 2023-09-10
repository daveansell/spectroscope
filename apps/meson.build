


openGLDep = dependency('gl')
glewDep = dependency('glew', required: true)
gluDep = dependency('glu', required:true)
glfwDep = dependency('glfw3', required: true)
sdlDep = dependency('sdl2', required: true)
xDep = dependency('x11', required: true)
epoxyDep = dependency('epoxy', required: true)
#glutDep = dependency('glut', required: true)


libcamera_still = executable('libcamera-still', files('libcamera_still.cpp'),
                             include_directories : include_directories('..'),
                             dependencies: [libcamera_dep, boost_dep],
                             link_with : libcamera_app,
                             install : true)

libcamera_vid = executable('libcamera-vid', files('libcamera_vid.cpp'),
                           include_directories : include_directories('..', '../include'),
                           dependencies: [libcamera_dep, boost_dep, gluDep, glewDep,  sdlDep, xDep, epoxyDep],
                           link_with : [libcamera_app],
                           install : true,
			   link_args: ['-lpigpio'])

libcamera_hello = executable('libcamera-hello', files('libcamera_hello.cpp'),
                             include_directories : include_directories('..'),
                             dependencies: libcamera_dep,
                             link_with : libcamera_app,
                             install : true)

libcamera_raw = executable('libcamera-raw', files('libcamera_raw.cpp'),
                           include_directories : include_directories('..'),
                           dependencies: [libcamera_dep, boost_dep],
                           link_with : libcamera_app,
                           install : true)

libcamera_jpeg = executable('libcamera-jpeg', files('libcamera_jpeg.cpp'),
                            include_directories : include_directories('..'),
                            dependencies: [libcamera_dep, boost_dep],
                            link_with : libcamera_app,
                            install : true)

if enable_tflite
    libcamera_detect = executable('libcamera-detect', files('libcamera_detect.cpp'),
                                  include_directories : include_directories('..'),
                                  dependencies: [libcamera_dep, boost_dep],
                                  link_with : libcamera_app,
                                  install : true)
endif
