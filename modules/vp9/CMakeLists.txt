project(vp9)

# Skip VP9 module when disabled via Conan
if(BARESIP_USE_CONAN AND DEFINED CONAN_VPX_ENABLED AND NOT CONAN_VPX_ENABLED)
    return()
endif()

find_package(libvpx QUIET)

if(NOT TARGET libvpx::libvpx)
    find_package(VPX)
    if(NOT VPX_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS decode.c encode.c sdp.c vp9.c vp9.h)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Always try Conan target first
if(TARGET libvpx::libvpx)
    target_link_libraries(${PROJECT_NAME} PRIVATE libvpx::libvpx)
    # Add to global list for main baresip library linking
    set_property(GLOBAL APPEND PROPERTY BARESIP_LINK_LIBRARIES libvpx::libvpx)
    # Workaround: Conan libvpx package doesn't provide include directories
    get_target_property(VPX_INTERFACE_LINK_LIBRARIES libvpx::libvpx INTERFACE_LINK_LIBRARIES)
    if(VPX_INTERFACE_LINK_LIBRARIES)
        # Extract libvpx package path and add include directory
        foreach(lib ${VPX_INTERFACE_LINK_LIBRARIES})
            if(lib MATCHES "libvpx")
                get_filename_component(VPX_LIB_DIR "${lib}" DIRECTORY)
                get_filename_component(VPX_PKG_DIR "${VPX_LIB_DIR}" DIRECTORY)
                set(VPX_INCLUDE_DIR "${VPX_PKG_DIR}/include")
                if(EXISTS "${VPX_INCLUDE_DIR}/vpx")
                    target_include_directories(${PROJECT_NAME} PRIVATE "${VPX_INCLUDE_DIR}")
                    message(STATUS "VP9: Added libvpx include directory: ${VPX_INCLUDE_DIR}")
                    break()
                endif()
            endif()
        endforeach()
    endif()
    message(STATUS "VP9: Using Conan libvpx::libvpx target")
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${VPX_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${VPX_LIBRARIES})
    message(STATUS "VP9: Using system VPX libraries")
endif()
