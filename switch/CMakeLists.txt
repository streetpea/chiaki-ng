
##################
# borealis dependency
##################

# do not include
# borealis/library/lib/switch_wrapper.c
# switch functions are in switch/src/main.cpp
set(BOREALIS_SOURCE
    borealis/library/lib/extern/libretro-common/features/features_cpu.c
    borealis/library/lib/extern/libretro-common/encodings/encoding_utf.c
    borealis/library/lib/extern/libretro-common/compat/compat_strl.c
    borealis/library/lib/extern/nxfmtwrapper/format.cpp
    borealis/library/lib/extern/nanovg/nanovg.c
    borealis/library/lib/extern/glad/glad.c
    borealis/library/lib/scroll_view.cpp
    borealis/library/lib/style.cpp
    borealis/library/lib/table.cpp
    borealis/library/lib/task_manager.cpp
    borealis/library/lib/progress_display.cpp
    borealis/library/lib/staged_applet_frame.cpp
    borealis/library/lib/applet_frame.cpp
    borealis/library/lib/hint.cpp
    borealis/library/lib/image.cpp
    borealis/library/lib/logger.cpp
    borealis/library/lib/swkbd.cpp
    borealis/library/lib/crash_frame.cpp
    borealis/library/lib/header.cpp
    borealis/library/lib/progress_spinner.cpp
    borealis/library/lib/layer_view.cpp
    borealis/library/lib/notification_manager.cpp
    borealis/library/lib/rectangle.cpp
    borealis/library/lib/application.cpp
    borealis/library/lib/box_layout.cpp
    borealis/library/lib/sidebar.cpp
    borealis/library/lib/dropdown.cpp
    borealis/library/lib/popup_frame.cpp
    borealis/library/lib/repeating_task.cpp
    borealis/library/lib/absolute_layout.cpp
    borealis/library/lib/i18n.cpp
    borealis/library/lib/tab_frame.cpp
    borealis/library/lib/thumbnail_frame.cpp
    borealis/library/lib/animations.cpp
    borealis/library/lib/dialog.cpp
    borealis/library/lib/view.cpp
    borealis/library/lib/list.cpp
    borealis/library/lib/button.cpp
    borealis/library/lib/label.cpp
    borealis/library/lib/theme.cpp
    borealis/library/lib/material_icon.cpp)

add_library(borealis STATIC ${BOREALIS_SOURCE})
set_property(TARGET borealis PROPERTY CXX_STANDARD 17)
target_include_directories(borealis PUBLIC
	borealis/library/include
    borealis/library/include/borealis/extern
	borealis/library/include/borealis/extern/glad
	borealis/library/include/borealis/extern/nanovg
	borealis/library/include/borealis/extern/libretro-common
	borealis/library/lib/extern/fmt/include)

find_package(glfw3 REQUIRED)
find_library(EGL EGL)
find_library(GLAPI glapi)
find_library(DRM_NOUVEAU drm_nouveau)
target_link_libraries(borealis
	glfw
	${EGL}
	${GLAPI}
	${DRM_NOUVEAU})

if(CHIAKI_IS_SWITCH)
	target_compile_definitions(borealis PUBLIC
		BOREALIS_RESOURCES="romfs:/")
else()
	target_compile_definitions(borealis PUBLIC
		BOREALIS_RESOURCES="./switch/res/")
endif()


##################
# chiaki with borealis
##################

find_package(FFMPEG REQUIRED COMPONENTS avcodec avutil swscale)
find_library(SDL2 SDL2)
find_library(SWRESAMPLE swresample)

# find -type f | grep -P '\.(h|cpp)$' | sed 's#\./#\t\t#g'
add_executable(chiaki-borealis WIN32
	src/discoverymanager.cpp
	src/settings.cpp
	src/io.cpp
	src/host.cpp
	src/main.cpp
	src/gui.cpp)

set_target_properties(chiaki-borealis PROPERTIES
	CXX_STANDARD 17
	OUTPUT_NAME chiaki)

target_include_directories(chiaki-borealis PRIVATE include)

target_link_libraries(chiaki-borealis
	chiaki-lib
	borealis
	${SDL2}
	FFMPEG::avcodec
	FFMPEG::avutil
	FFMPEG::swscale
	${SWRESAMPLE}
	${SWSCALE})

if(CHIAKI_IS_SWITCH)
	# libnx is forced by the switch toolchain
	find_library(Z z)
	find_library(GLAPI glapi) # TODO: make it transitive from borealis
	find_library(DRM_NOUVEAU drm_nouveau) # TODO: make it transitive from borealis
	target_link_libraries(chiaki-borealis ${Z} ${GLAPI} ${DRM_NOUVEAU})
endif()

install(TARGETS chiaki-borealis
	RUNTIME DESTINATION bin
	BUNDLE DESTINATION bin)

if(CHIAKI_IS_SWITCH)
	add_nro_target(chiaki
		chiaki-borealis
		"Chiaki"
		"H0neyBadger and thestr4ng3r"
		"${CHIAKI_VERSION}"
		"${CMAKE_CURRENT_SOURCE_DIR}/nro_icon.jpg"
		"${CMAKE_CURRENT_SOURCE_DIR}/res")
endif()
