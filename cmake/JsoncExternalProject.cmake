
include(FetchContent)

if(CHIAKI_IS_WIIU)
	# newlib sí tiene vasprintf/isnan/isinf, pero los checks de json-c no los
	# detectan (isnan es macro y vasprintf requiere __GNU_VISIBLE) y sus shims
	# de compatibilidad chocan con -Werror al redefinirlos.
	set(HAVE_VASPRINTF 1 CACHE INTERNAL "")
	set(HAVE_DECL_ISNAN 1 CACHE INTERNAL "")
	set(HAVE_DECL_ISINF 1 CACHE INTERNAL "")
	set(HAVE_DECL_INFINITY 1 CACHE INTERNAL "")
	set(HAVE_DECL_NAN 1 CACHE INTERNAL "")
	# w2r (elf->rpx) solo admite reubicaciones estaticas: ni PIC (PLTREL24,
	# LOCAL24PC) ni TLS (TPREL/DTPREL rompen la conversion. json-c ofrece
	# opciones oficiales para las dos cosas; el uso de __thread ademas esta
	# protegido por #if defined(HAVE___THREAD) en json_object.c.
	set(DISABLE_STATIC_FPIC ON CACHE BOOL "" FORCE)
	set(DISABLE_THREAD_LOCAL_STORAGE ON CACHE BOOL "" FORCE)
	# El resultado del check vive en el cache de la config anterior: si
	# persiste, #cmakedefine HAVE___THREAD lo re-definiria aunque la opcion
	# este activa (y SPEC___THREAD ya no se setea -> error de sintaxis en
	# json_object.c).
	set(HAVE___THREAD "" CACHE INTERNAL "" FORCE)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_APPS OFF CACHE BOOL "" FORCE)
set(DISABLE_EXTRA_LIBS ON CACHE BOOL "" FORCE)
set(DISABLE_BSON_OUTPUT ON CACHE BOOL "" FORCE)

FetchContent_Declare(json-c
	URL https://github.com/json-c/json-c/archive/refs/tags/json-c-0.17-20230812.tar.gz
)
FetchContent_MakeAvailable(json-c)

# Chiaki includes headers as <json-c/json_object.h> but FetchContent provides
# them without the json-c/ prefix. Create a symlink bridge directory.
set(JSONC_PREFIX_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/json-c-prefix-include")
file(MAKE_DIRECTORY "${JSONC_PREFIX_INCLUDE_DIR}")
if(NOT EXISTS "${JSONC_PREFIX_INCLUDE_DIR}/json-c")
	file(CREATE_LINK "${json-c_SOURCE_DIR}" "${JSONC_PREFIX_INCLUDE_DIR}/json-c" SYMBOLIC)
endif()
