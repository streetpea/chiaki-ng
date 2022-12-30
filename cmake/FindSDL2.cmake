
find_package(SDL2 NO_MODULE QUIET)

# Adapted from libsdl-org/SDL_ttf: https://github.com/libsdl-org/SDL_ttf/blob/main/cmake/FindPrivateSDL2.cmake#L19-L31
# Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
# Licensed under the zlib license (https://github.com/libsdl-org/SDL_ttf/blob/main/LICENSE.txt)
set(SDL2_VERSION_MAJOR)
set(SDL2_VERSION_MINOR)
set(SDL2_VERSION_PATCH)
set(SDL2_VERSION)
if(SDL2_INCLUDE_DIR)
    file(READ "${SDL2_INCLUDE_DIR}/SDL_version.h" _sdl_version_h)
    string(REGEX MATCH "#define[ \t]+SDL_MAJOR_VERSION[ \t]+([0-9]+)" _sdl2_major_re "${_sdl_version_h}")
    set(SDL2_VERSION_MAJOR "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+SDL_MINOR_VERSION[ \t]+([0-9]+)" _sdl2_minor_re "${_sdl_version_h}")
    set(SDL2_VERSION_MINOR "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define[ \t]+SDL_PATCHLEVEL[ \t]+([0-9]+)" _sdl2_patch_re "${_sdl_version_h}")
    set(SDL2_VERSION_PATCH "${CMAKE_MATCH_1}")
	if(_sdl2_major_re AND _sdl2_minor_re AND _sdl2_patch_re)
		set(SDL2_VERSION "${SDL2_VERSION_MAJOR}.${SDL2_VERSION_MINOR}.${SDL2_VERSION_PATCH}")
    endif()
endif()

if(SDL2_FOUND AND (NOT TARGET SDL2::SDL2))
	add_library(SDL2::SDL2 UNKNOWN IMPORTED GLOBAL)
	if(NOT SDL2_LIBDIR)
		set(SDL2_LIBDIR "${libdir}")
	endif()
	find_library(SDL2_LIBRARY SDL2 PATHS "${SDL2_LIBDIR}" NO_DEFAULT_PATH)
	if(SDL2_LIBRARY)
		string(STRIP "${SDL2_LIBRARIES}" SDL2_LIBRARIES)
		set_target_properties(SDL2::SDL2 PROPERTIES
				IMPORTED_LOCATION "${SDL2_LIBRARY}"
				IMPORTED_LINK_INTERFACE_LIBRARIES "${SDL2_LIBRARIES}"
				INTERFACE_LINK_DIRECTORIES "${SDL2_LIBDIR}"
				INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}")
	else()
		set(SDL2_FOUND FALSE)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 CONFIG_MODE)
