project(auogg)

find_package(OGG)
find_package(SPEEX)

if(NOT OGG_FOUND OR NOT SPEEX_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS auogg.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-strict-prototypes
)

target_include_directories(${PROJECT_NAME} PRIVATE ${OGG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OGG_LIBRARIES})
