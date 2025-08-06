#
# Conan Integration Helper for Baresip
#
# This file provides helper functions to integrate Conan dependencies
# with the existing module system while maintaining backward compatibility
#

# Check if we're using Conan
if(DEFINED BARESIP_USE_CONAN AND BARESIP_USE_CONAN)
    message(STATUS "Using Conan for dependency management")
    
    # Include Conan-generated files
    include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake OPTIONAL)
    
    # Helper function to find Conan packages with fallback to system packages
    function(find_conan_package_with_fallback PACKAGE_NAME)
        set(options QUIET REQUIRED)
        set(oneValueArgs TARGET_NAME FALLBACK_MODULE)
        set(multiValueArgs COMPONENTS)
        cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
        
        # Try to find the Conan package first
        if(ARGS_COMPONENTS)
            find_package(${PACKAGE_NAME} ${ARGS_COMPONENTS} QUIET)
        else()
            find_package(${PACKAGE_NAME} QUIET)
        endif()
        
        # If Conan package not found, fallback to system package
        if(NOT ${PACKAGE_NAME}_FOUND)
            if(ARGS_FALLBACK_MODULE)
                message(STATUS "Conan package ${PACKAGE_NAME} not found, trying system package with ${ARGS_FALLBACK_MODULE}")
                include(${ARGS_FALLBACK_MODULE})
            else()
                message(STATUS "Conan package ${PACKAGE_NAME} not found, trying system package")
                if(ARGS_COMPONENTS)
                    find_package(${PACKAGE_NAME} ${ARGS_COMPONENTS} ${ARGS_QUIET} ${ARGS_REQUIRED})
                else()
                    find_package(${PACKAGE_NAME} ${ARGS_QUIET} ${ARGS_REQUIRED})
                endif()
            endif()
        else()
            message(STATUS "Found Conan package: ${PACKAGE_NAME}")
        endif()
        
        # Set the found variable in parent scope
        set(${PACKAGE_NAME}_FOUND ${${PACKAGE_NAME}_FOUND} PARENT_SCOPE)
    endfunction()
    
    # Helper function to link Conan targets
    function(link_conan_target MODULE_TARGET CONAN_TARGET)
        if(TARGET ${CONAN_TARGET})
            target_link_libraries(${MODULE_TARGET} PRIVATE ${CONAN_TARGET})
        endif()
    endfunction()
    
    # Helper function to add module dependencies via Conan
    function(add_conan_module_deps MODULE_NAME)
        # Audio codecs
        if(CONAN_OPUS_ENABLED AND MODULE_NAME MATCHES "opus")
            find_conan_package_with_fallback(opus FALLBACK_MODULE FindOPUS)
            if(opus_FOUND)
                link_conan_target(${MODULE_NAME} opus::opus)
            endif()
        endif()
        
        # Video codecs
        if(CONAN_VPX_ENABLED AND MODULE_NAME MATCHES "vp[89]")
            find_conan_package_with_fallback(libvpx FALLBACK_MODULE FindVPX)
            if(libvpx_FOUND)
                link_conan_target(${MODULE_NAME} libvpx::libvpx)
            endif()
        endif()
        
        if(CONAN_AV1_ENABLED AND MODULE_NAME MATCHES "av1")
            find_conan_package_with_fallback(libaom FALLBACK_MODULE FindAOM)
            if(libaom_FOUND)
                link_conan_target(${MODULE_NAME} libaom::libaom)
            endif()
        endif()
        
        if(CONAN_FFMPEG_ENABLED AND MODULE_NAME MATCHES "av(codec|filter|format)")
            find_conan_package_with_fallback(ffmpeg FALLBACK_MODULE FindFFMPEG)
            if(ffmpeg_FOUND)
                if(MODULE_NAME MATCHES "avcodec")
                    link_conan_target(${MODULE_NAME} ffmpeg::avcodec)
                elseif(MODULE_NAME MATCHES "avfilter")
                    link_conan_target(${MODULE_NAME} ffmpeg::avfilter)
                elseif(MODULE_NAME MATCHES "avformat")
                    link_conan_target(${MODULE_NAME} ffmpeg::avformat)
                endif()
            endif()
        endif()
        
        # Audio systems
        if(CONAN_PORTAUDIO_ENABLED AND MODULE_NAME MATCHES "portaudio")
            find_conan_package_with_fallback(portaudio FALLBACK_MODULE FindPORTAUDIO)
            if(portaudio_FOUND)
                link_conan_target(${MODULE_NAME} portaudio::portaudio)
            endif()
        endif()
        
        if(CONAN_PULSEAUDIO_ENABLED AND MODULE_NAME MATCHES "pulse")
            find_conan_package_with_fallback(pulseaudio FALLBACK_MODULE FindPULSE)
            if(pulseaudio_FOUND)
                link_conan_target(${MODULE_NAME} pulseaudio::pulseaudio)
            endif()
        endif()
        
        # Other dependencies
        if(CONAN_SDL_ENABLED AND MODULE_NAME MATCHES "sdl")
            find_conan_package_with_fallback(sdl FALLBACK_MODULE FindSDL)
            if(sdl_FOUND)
                link_conan_target(${MODULE_NAME} sdl::sdl)
            endif()
        endif()
        
        if(CONAN_PNG_ENABLED AND MODULE_NAME MATCHES "snapshot")
            find_conan_package_with_fallback(libpng FALLBACK_MODULE FindPNG)
            if(libpng_FOUND)
                link_conan_target(${MODULE_NAME} libpng::libpng)
            endif()
        endif()
        
        if(CONAN_SNDFILE_ENABLED AND MODULE_NAME MATCHES "sndfile")
            find_conan_package_with_fallback(libsndfile FALLBACK_MODULE FindSNDFILE)
            if(libsndfile_FOUND)
                link_conan_target(${MODULE_NAME} libsndfile::libsndfile)
            endif()
        endif()
        
        if(CONAN_MOSQUITTO_ENABLED AND MODULE_NAME MATCHES "mqtt")
            find_conan_package_with_fallback(libmosquitto FALLBACK_MODULE FindMOSQUITTO)
            if(libmosquitto_FOUND)
                link_conan_target(${MODULE_NAME} libmosquitto::libmosquitto)
            endif()
        endif()
    endfunction()
    
else()
    message(STATUS "Using traditional dependency management")
    
    # Fallback function that does nothing when Conan is not used
    function(add_conan_module_deps MODULE_NAME)
        # No-op when not using Conan
    endfunction()
endif()