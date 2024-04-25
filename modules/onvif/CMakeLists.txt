project(onvif)

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS soap.c soap_str.c fault.c fakevideo.c rtspd.c onvif.c device.c scopes.c wsd.c ptz.c event.c deviceio.c filter.c pl.c onvif_auth.c media.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

