project(snapshot)

set(SRCS png_vf.c png_vf.h snapshot.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wno-clobbered
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PNG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PNG_LIBRARIES})
