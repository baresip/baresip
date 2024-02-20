project(comvideo)

find_package(CAMERAD)

if(NOT CAMERAD_FOUND)
  return()
endif()

pkg_search_module(GST REQUIRED gstreamer-1.0>=1.16)
link_directories(${GST_LIBDIR})

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS comvideo.c decode_h264.c encode_h264.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${GIO_INCLUDE_DIRS}
  ${GST_INCLUDE_DIRS}
  ${CAMERAD_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GIO_LIBRARIES}
    ${GST_LIBRARIES}
    ${CAMERAD_LIBRARIES}
    )

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-strict-prototypes)
