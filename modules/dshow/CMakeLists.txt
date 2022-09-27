project(dshow)

set(SRCS dshow.cpp)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE strmiids ole32 oleaut32)

if(NOT MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE stdc++)
endif()
