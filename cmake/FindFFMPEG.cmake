#[==[
Provides the following variables:

  * `FFMPEG_INCLUDE_DIRS`: Include directories necessary to use FFMPEG.
  * `FFMPEG_LIBRARIES`: Libraries necessary to use FFMPEG. Note that this only
    includes libraries for the components requested.
  * `FFMPEG_VERSION`: The version of FFMPEG found.

The following components are supported:

  * `avcodec`
  * `avdevice`
  * `avfilter`
  * `avformat`
  * `avresample`
  * `avutil`
  * `swresample`
  * `swscale`

For each component, the following are provided:

  * `FFMPEG_<component>_FOUND`: Libraries for the component.
  * `FFMPEG_<component>_INCLUDE_DIRS`: Include directories for
    the component.
  * `FFMPEG_<component>_LIBRARIES`: Libraries for the component.
  * `FFMPEG::<component>`: A target to use with `target_link_libraries`.

Note that only components requested with `COMPONENTS` or `OPTIONAL_COMPONENTS`
are guaranteed to set these variables or provide targets.
#]==]

find_package(PkgConfig)

function(join OUTPUT GLUE)
  set(_TMP_RESULT "")
  set(_GLUE "") # effective glue is empty at the beginning
  foreach(arg ${ARGN})
    set(_TMP_RESULT "${_TMP_RESULT}${_GLUE}${arg}")
    set(_GLUE "${GLUE}")
  endforeach()
  set(${OUTPUT} "${_TMP_RESULT}" PARENT_SCOPE)
endfunction()

function (_ffmpeg_find component headername)
  if(TARGET "FFMPEG::${component}")
    # already found before
    return()
  endif()

  # Try pkg-config first
  if(PKG_CONFIG_FOUND)
    if(CMAKE_VERSION VERSION_LESS "3.6")
      pkg_check_modules(FFMPEG_${component} lib${component})
    else()
      pkg_check_modules(FFMPEG_${component} lib${component} IMPORTED_TARGET)
    endif()
    if(FFMPEG_${component}_FOUND)
      if((TARGET PkgConfig::FFMPEG_${component}) AND (NOT CMAKE_VERSION VERSION_LESS "3.11.0"))
        if(APPLE)
          join(FFMPEG_LDFLAGS_STRING " " ${FFMPEG_${component}_LDFLAGS})
          string(REGEX REPLACE "-Wl,-framework,([^ ]+)" "-framework \\1" FFMPEG_LDFLAGS_STRING_CLEAN ${FFMPEG_LDFLAGS_STRING})
          string(REGEX MATCHALL "-framework [^ ]+" FFMPEG_FRAMEWORKS ${FFMPEG_LDFLAGS_STRING_CLEAN})
          list(APPEND FFMPEG_${component}_LIBRARIES ${FFMPEG_FRAMEWORKS})
          set_target_properties(PkgConfig::FFMPEG_${component} PROPERTIES
                  INTERFACE_LINK_DIRECTORIES "${FFMPEG_${component}_LIBRARY_DIRS}"
                  INTERFACE_LINK_LIBRARIES "${FFMPEG_${component}_LIBRARIES}"
                  INTERFACE_LINK_OPTIONS "")
          message("set libs to \"${FFMPEG_${component}_LIBRARIES}\"")
          message("set lib dirs to \"${FFMPEG_${component}_LIBRARY_DIRS}\"")
          message("set lib otps not to \"${FFMPEG_${component}_LDFLAGS}\"")
        endif()
        set_target_properties(PkgConfig::FFMPEG_${component} PROPERTIES IMPORTED_GLOBAL TRUE)
        add_library(FFMPEG::${component} ALIAS PkgConfig::FFMPEG_${component})
      else()
        add_library("FFMPEG::${component}" INTERFACE IMPORTED)
        if(CMAKE_VERSION VERSION_LESS "3.6")
          link_directories("${FFMPEG_${component}_LIBRARY_DIRS}")
        endif()
        set_target_properties("FFMPEG::${component}" PROPERTIES
          INTERFACE_LINK_DIRECTORIES "${FFMPEG_${component}_LIBRARY_DIRS}"
          INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${component}_INCLUDE_DIRS}"
          INTERFACE_LINK_LIBRARIES "${FFMPEG_${component}_LIBRARIES}")
      endif()
      return()
    endif()
  endif()

  find_path("FFMPEG_${component}_INCLUDE_DIR"
    NAMES
      "lib${component}/${headername}"
    PATHS
      "${FFMPEG_ROOT}/include"
      ~/Library/Frameworks
      /Library/Frameworks
      /usr/local/include
      /usr/include
      /sw/include # Fink
      /opt/local/include # DarwinPorts
      /opt/csw/include # Blastwave
      /opt/include
      /usr/freeware/include
    PATH_SUFFIXES
      ffmpeg
    DOC "FFMPEG's ${component} include directory")
  mark_as_advanced("FFMPEG_${component}_INCLUDE_DIR")

  # On Windows, static FFMPEG is sometimes built as `lib<name>.a`.
  if (WIN32)
    list(APPEND CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "" "lib")
  endif ()

  find_library("FFMPEG_${component}_LIBRARY"
    NAMES
      "${component}"
    PATHS
      "${FFMPEG_ROOT}/lib"
      ~/Library/Frameworks
      /Library/Frameworks
      /usr/local/lib
      /usr/local/lib64
      /usr/lib
      /usr/lib64
      /sw/lib
      /opt/local/lib
      /opt/csw/lib
      /opt/lib
      /usr/freeware/lib64
      "${FFMPEG_ROOT}/bin"
    DOC "FFMPEG's ${component} library")
  mark_as_advanced("FFMPEG_${component}_LIBRARY")

  if (FFMPEG_${component}_LIBRARY AND FFMPEG_${component}_INCLUDE_DIR)
    set(_deps_found TRUE)
    set(_deps_link)
    foreach (_ffmpeg_dep IN LISTS ARGN)
      if (TARGET "FFMPEG::${_ffmpeg_dep}")
        list(APPEND _deps_link "FFMPEG::${_ffmpeg_dep}")
      else ()
        set(_deps_found FALSE)
      endif ()
    endforeach ()
    if (_deps_found)
      add_library("FFMPEG::${component}" UNKNOWN IMPORTED)
      set_target_properties("FFMPEG::${component}" PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_${component}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${component}_INCLUDE_DIR}"
        IMPORTED_LINK_INTERFACE_LIBRARIES "${_deps_link}")
      set("FFMPEG_${component}_FOUND" 1
        PARENT_SCOPE)

      set(version_header_path "${FFMPEG_${component}_INCLUDE_DIR}/lib${component}/version.h")
      if (EXISTS "${version_header_path}")
        string(TOUPPER "${component}" component_upper)
        file(STRINGS "${version_header_path}" version
          REGEX "#define  *LIB${component_upper}_VERSION_(MAJOR|MINOR|MICRO) ")
        string(REGEX REPLACE ".*_MAJOR *\([0-9]*\).*" "\\1" major "${version}")
        string(REGEX REPLACE ".*_MINOR *\([0-9]*\).*" "\\1" minor "${version}")
        string(REGEX REPLACE ".*_MICRO *\([0-9]*\).*" "\\1" micro "${version}")
        if (NOT major STREQUAL "" AND
            NOT minor STREQUAL "" AND
            NOT micro STREQUAL "")
          set("FFMPEG_${component}_VERSION" "${major}.${minor}.${micro}"
            PARENT_SCOPE)
        endif ()
      endif ()
    else ()
      set("FFMPEG_${component}_FOUND" 0
        PARENT_SCOPE)
      set(what)
      if (NOT FFMPEG_${component}_LIBRARY)
        set(what "library")
      endif ()
      if (NOT FFMPEG_${component}_INCLUDE_DIR)
        if (what)
          string(APPEND what " or headers")
        else ()
          set(what "headers")
        endif ()
      endif ()
      set("FFMPEG_${component}_NOT_FOUND_MESSAGE"
        "Could not find the ${what} for ${component}."
        PARENT_SCOPE)
    endif ()
  endif ()
endfunction ()

unset(_pkg_config_extra_arg)

_ffmpeg_find(avutil     avutil.h)
#_ffmpeg_find(avresample avresample.h
#  avutil)
_ffmpeg_find(swresample swresample.h
  avutil)
_ffmpeg_find(swscale    swscale.h
  avutil)
_ffmpeg_find(avcodec    avcodec.h
  avutil)
_ffmpeg_find(avformat   avformat.h
 avcodec avutil)
#_ffmpeg_find(avfilter   avfilter.h
#  avutil)
#_ffmpeg_find(avdevice   avdevice.h
#  avformat avutil)

if (TARGET FFMPEG::avutil)
  if(TARGET PkgConfig::FFMPEG_avutil)
    set(FFMPEG_VERSION ${FFMPEG_avutil_VERSION})
  else()
    set(_ffmpeg_version_header_path "${FFMPEG_avutil_INCLUDE_DIR}/libavutil/ffversion.h")
    if (EXISTS "${_ffmpeg_version_header_path}")
      file(STRINGS "${_ffmpeg_version_header_path}" _ffmpeg_version
        REGEX "FFMPEG_VERSION")
      string(REGEX REPLACE ".*\"n?\(.*\)\"" "\\1" FFMPEG_VERSION "${_ffmpeg_version}")
      unset(_ffmpeg_version)
    else ()
      set(FFMPEG_VERSION FFMPEG_VERSION-NOTFOUND)
    endif ()
    unset(_ffmpeg_version_header_path)
  endif()
endif ()

set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARIES)
set(_ffmpeg_required_vars)
foreach (_ffmpeg_component IN LISTS FFMPEG_FIND_COMPONENTS)
  if (TARGET "FFMPEG::${_ffmpeg_component}")
    if(TARGET PkgConfig::FFMPEG_${_ffmpeg_component})
      list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${_ffmpeg_component}_INCLUDE_DIRS})
      list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${_ffmpeg_component}_LIBRARIES})
      list(APPEND _ffmpeg_required_vars
              "FFMPEG_${_ffmpeg_component}_LIBRARIES")
    else()
      if(NOT FFMPEG_${_ffmpeg_component}_INCLUDE_DIRS)
        set(FFMPEG_${_ffmpeg_component}_INCLUDE_DIRS
          "${FFMPEG_${_ffmpeg_component}_INCLUDE_DIR}")
      endif()
      if(NOT FFMPEG_${_ffmpeg_component}_LIBRARIES)
        set(FFMPEG_${_ffmpeg_component}_LIBRARIES
          "${FFMPEG_${_ffmpeg_component}_LIBRARY}")
      endif()
      list(APPEND FFMPEG_INCLUDE_DIRS
        "${FFMPEG_${_ffmpeg_component}_INCLUDE_DIRS}")
      list(APPEND FFMPEG_LIBRARIES
        "${FFMPEG_${_ffmpeg_component}_LIBRARIES}")
      if (FFMEG_FIND_REQUIRED_${_ffmpeg_component})
        list(APPEND _ffmpeg_required_vars
          "FFMPEG_${_ffmpeg_component}_INCLUDE_DIRS"
          "FFMPEG_${_ffmpeg_component}_LIBRARIES")
      endif()
    endif ()
  endif ()
endforeach ()
unset(_ffmpeg_component)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG
  REQUIRED_VARS FFMPEG_LIBRARIES
  VERSION_VAR FFMPEG_VERSION
  HANDLE_COMPONENTS)
unset(_ffmpeg_required_vars)
