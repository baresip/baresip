project(dtls_srtp)

set(SRCS dtls_srtp.c dtls.c srtp.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

