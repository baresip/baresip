find_path(MBEDTLS_INCLUDE_DIR
    NAMES mbedtls/ssl.h mbedtls/md.h mbedtls/md5.h mbedtls/error.h
          mbedtls/sha1.h mbedtls/sha256.h
    HINTS
      "${MBEDTLS_INCLUDE_DIRS}"
      "${MBEDTLS_HINTS}/include"
    PATHS /usr/local/include /usr/include
)

find_library(MBEDTLS_LIBRARY
  NAMES mbedtls mbedx509 mbedcrypto
  HINTS
    "${MBEDTLS_LIBRARY_DIRS}"
    "${MBEDTLS_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MBEDTLS DEFAULT_MSG
    MBEDTLS_INCLUDE_DIR MBEDTLS_LIBRARY)

if(MBEDTLS_FOUND)
  set( MBEDTLS_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR} )
  set( MBEDTLS_LIBRARIES ${MBEDTLS_LIBRARY} )
else()
  set( MBEDTLS_INCLUDE_DIRS )
  set( MBEDTLS_LIBRARIES )
endif()

mark_as_advanced(MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARIES)
