project(sdl)

# Use Conan's SDL package when available
if(BARESIP_USE_CONAN)
    find_package(SDL2 QUIET)  # Conan provides SDL2 (uppercase)
endif()

if(NOT TARGET SDL2::SDL2)
    find_package(SDL)  # Fallback to custom Findsdl.cmake
    if(NOT SDL_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS sdl.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Link dependencies based on whether we're using Conan or not
if(BARESIP_USE_CONAN AND TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    # Add to global list for main baresip library linking
    set_property(GLOBAL APPEND PROPERTY BARESIP_LINK_LIBRARIES SDL2::SDL2)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL_LIBRARIES})
endif()
