find_path(MBEDTLSSO_INCLUDE_DIR
    NAMES mbedtls/ssl.h mbedtls/md.h mbedtls/md5.h mbedtls/error.h
          mbedtls/sha1.h mbedtls/sha256.h
    HINTS
      "${MBEDTLSSO_INCLUDE_DIRS}"
      "${MBEDTLSSO_HINTS}/include"
    PATHS /usr/local/include /usr/include
)

find_library(MBEDTLSSO_LIBRARY
  NAMES libmbedtls.so libmbedx509.so libmbedcrypto.so
  HINTS
    "${MBEDTLSSO_LIBRARY_DIRS}"
    "${MBEDTLSSO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MBEDTLSSO DEFAULT_MSG
    MBEDTLSSO_INCLUDE_DIR MBEDTLSSO_LIBRARY)

if(MBEDTLSSO_FOUND)
  set( MBEDTLSSO_INCLUDE_DIRS ${MBEDTLSSO_INCLUDE_DIR} )
  set( MBEDTLSSO_LIBRARIES ${MBEDTLSSO_LIBRARY} )
else()
  set( MBEDTLSSO_INCLUDE_DIRS )
  set( MBEDTLSSO_LIBRARIES )
endif()

mark_as_advanced(MBEDTLSSO_INCLUDE_DIRS MBEDTLSSO_LIBRARIES)
