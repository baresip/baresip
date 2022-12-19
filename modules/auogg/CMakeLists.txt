project(auogg)

set(SRCS auogg.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-strict-prototypes
)


