
include(FetchContent)

set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(miniupnpc
	URL https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_8.tar.gz
	SOURCE_SUBDIR miniupnpc
)
FetchContent_MakeAvailable(miniupnpc)

if(CHIAKI_IS_WIIU)
	# wut no expone IPv6 (nsysnet es IPv4-only) ni trae <net/if.h>/<sys/un.h>.
	# Ver cmake/miniupnpc-wiiu-compat.h y cmake/miniupnpc-stub-include/.
	target_compile_options(libminiupnpc-static PRIVATE
		-include "${CMAKE_CURRENT_LIST_DIR}/miniupnpc-wiiu-compat.h")
	target_include_directories(libminiupnpc-static PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/miniupnpc-stub-include")
	# Su CMakeLists fuerza POSITION_INDEPENDENT_CODE=ON para GNUCC; w2r
	# (elf->rpx) no admite las reubicaciones PIC (PLTREL24/LOCAL24PC).
	set_property(TARGET libminiupnpc-static PROPERTY POSITION_INDEPENDENT_CODE OFF)
endif()

# Chiaki includes headers as <miniupnpc/miniupnpc.h> but FetchContent provides
# them without the prefix. Create a symlink bridge directory.
set(MINIUPNPC_PREFIX_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/miniupnpc-prefix-include")
file(MAKE_DIRECTORY "${MINIUPNPC_PREFIX_INCLUDE_DIR}")
if(NOT EXISTS "${MINIUPNPC_PREFIX_INCLUDE_DIR}/miniupnpc")
	file(CREATE_LINK "${miniupnpc_SOURCE_DIR}/miniupnpc/include" "${MINIUPNPC_PREFIX_INCLUDE_DIR}/miniupnpc" SYMBOLIC)
endif()
