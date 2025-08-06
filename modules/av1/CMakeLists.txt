project(av1)

# Try Conan first, then fallback to manual find
if(BARESIP_USE_CONAN)
    find_package(libaom-av1 QUIET)
endif()

if(NOT TARGET libaom-av1::libaom-av1)
    find_package(AOM)
    if(NOT AOM_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS av1.c encode.c decode.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Link dependencies based on whether we're using Conan or not
if(BARESIP_USE_CONAN AND TARGET libaom-av1::libaom-av1)
    target_link_libraries(${PROJECT_NAME} PRIVATE libaom-av1::libaom-av1)
    # Add to global list for main baresip library linking
    set_property(GLOBAL APPEND PROPERTY BARESIP_LINK_LIBRARIES libaom-av1::libaom-av1)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${AOM_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${AOM_LIBRARY})
endif()
