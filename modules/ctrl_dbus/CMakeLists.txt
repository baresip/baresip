project(ctrl_dbus)

set(SRCS ctrl_dbus.c baresipbus.c)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0>=0.29.1)
pkg_check_modules(GIO  REQUIRED gio-unix-2.0>=0.29.1)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
  target_link_libraries(${PROJECT_NAME} PRIVATE baresip)
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_directories(${PROJECT_NAME} PRIVATE
  ${GLIB_LIBRARY_DIRS}
  ${GIO_LIBRARY_DIRS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${GLIB_INCLUDE_DIRS}
  ${GIO_INCLUDE_DIRS}
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wno-unused-parameter
  -Wno-declaration-after-statement
  -Wno-pedantic -Wno-shorten-64-to-32
)

add_dependencies(${PROJECT_NAME} baresipbus.c)

add_custom_target(baresipbus.c ALL
  gdbus-codegen
    --generate-c-code baresipbus --c-namespace DBus
    --interface-prefix com.github.
    ${CMAKE_SOURCE_DIR}/modules/${PROJECT_NAME}/com.github.Baresip.xml
  DEPENDS com.github.Baresip.xml
  BYPRODUCTS baresipbus.c baresipbus.h)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
