project(menu)

set(SRCS menu.c static_menu.c dynamic_menu.c tone.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

