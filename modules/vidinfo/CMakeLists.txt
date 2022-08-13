project(vidinfo)

set(SRCS vidinfo.c draw.c xga_font_data.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

