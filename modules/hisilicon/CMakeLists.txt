project(hisilicon)

set(SRCS hisi.c hisi_src.c hisi_play.c errors.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

set(SDK_DIR /home/dima/projects/cameras/sdk/Hi3516EV200_SDK_V1.0.1.2/mpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${SDK_DIR}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDK_DIR}/lib/libmpi.so)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDK_DIR}/lib/libVoiceEngine.so)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDK_DIR}/lib/libsecurec.so)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDK_DIR}/lib/libupvqe.so)
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDK_DIR}/lib/libdnvqe.so)
