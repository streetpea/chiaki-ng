
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

# Chiaki includes headers as <miniupnpc/miniupnpc.h> but FetchContent provides
# them without the prefix. Create a symlink bridge directory.
set(MINIUPNPC_PREFIX_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/miniupnpc-prefix-include")
file(MAKE_DIRECTORY "${MINIUPNPC_PREFIX_INCLUDE_DIR}")
if(NOT EXISTS "${MINIUPNPC_PREFIX_INCLUDE_DIR}/miniupnpc")
	file(CREATE_LINK "${miniupnpc_SOURCE_DIR}/miniupnpc/include" "${MINIUPNPC_PREFIX_INCLUDE_DIR}/miniupnpc" SYMBOLIC)
endif()
