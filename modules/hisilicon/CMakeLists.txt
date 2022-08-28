project(hisilicon)

set(SRCS hisi.c hisi_src.c hisi_play.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

find_path(VENDOR_SDK_INCLUDE_DIRS hi_common.h
    HINTS ${HISI_SDK_DIR}
    PATH_SUFFIXES mpp2/include mpp/include include source/gmp/include
    REQUIRED
)

target_include_directories(${PROJECT_NAME} PRIVATE ${VENDOR_SDK_INCLUDE_DIRS})

set(LIBS_NAMES
    mpi
    VoiceEngine
    securec
    upvqe
    dnvqe
    )

foreach(LIB ${LIBS_NAMES})
  find_library(FOUND_LIB_${LIB} ${LIB}
      PATH_SUFFIXES mpp2/lib mpp/lib lib
        source/gmp/lib source/gmp/lib_log/static
      HINTS ${HISI_SDK_DIR}
      REQUIRED
  )
  target_link_libraries(${PROJECT_NAME} PRIVATE ${FOUND_LIB_${LIB}})
endforeach(LIB)
