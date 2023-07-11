find_library(MBEDTLSSO_LIBRARY
  NAMES libmbedtls.so libmbedx509.so libmbedcrypto.so
  HINTS
    "${MBEDTLSSO_LIBRARY_DIRS}"
    "${MBEDTLSSO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MBEDTLSSO DEFAULT_MSG
    MBEDTLSSO_LIBRARY)

if(MBEDTLSSO_FOUND)
  set( MBEDTLSSO_LIBRARIES ${MBEDTLSSO_LIBRARY} )
else()
  set( MBEDTLSSO_LIBRARIES )
endif()

mark_as_advanced(MBEDTLSSO_LIBRARIES)
