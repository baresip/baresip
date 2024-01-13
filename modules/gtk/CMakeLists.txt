project(gtk)

find_package(GTK3)

if(NOT GTK3_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS gtk_mod.c call_window.c dial_dialog.c transfer_dialog.c uri_entry.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

add_definitions(${GTK3_CFLAGS} ${GTK3_CFLAGS_OTHER})
target_link_directories(${PROJECT_NAME} PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${GTK3_LIBRARIES})

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-strict-prototypes
    -Wno-deprecated-declarations
)
