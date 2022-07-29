project(dshow)

set(SRCS dshow.cpp)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
  baresip strmiids ole32 oleaut32 stdc++)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
