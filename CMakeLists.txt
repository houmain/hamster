cmake_minimum_required(VERSION 3.0.0)
project(pagesowned)

option(ENABLE_WEBRECORDER "Build webrecorder")
if(ENABLE_WEBRECORDER)
    include (ExternalProject)

    ExternalProject_Add(webrecorder
        GIT_REPOSITORY https://github.com/houmaster/webrecorder
        CMAKE_ARGS ${CMAKE_ARGS}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
    )
endif()

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    src/main.cpp
    src/common.cpp
    src/Webrecorder.cpp
    src/Json.cpp
    libs/utf8/utf8.cpp
    libs/TinyProcessLib/process.cpp
)
file(GLOB_RECURSE HEADERS include *.h)

if(NOT MSVC)
    set(SOURCES ${SOURCES} libs/TinyProcessLib/process_unix.cpp)
    add_compile_options(-Wall -Wextra -Wsign-conversion)
    link_libraries(stdc++fs)
else()
    set(SOURCES ${SOURCES} libs/TinyProcessLib/process_win.cpp)
    add_compile_options(/GR- /W1)
endif()

add_compile_definitions(RAPIDJSON_HAS_STDSTRING=1 UNICODE)

find_package(Threads REQUIRED)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

include_directories(. ${INCLUDES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})
add_executable(pagesowned ${SOURCES} ${HEADERS})

if(WIN32)
    add_executable(ctrl_c WIN32 src/ctrl_c.cpp)
endif()

if(NOT WIN32)
    install(TARGETS pagesowned DESTINATION "bin")
else()
    install(TARGETS pagesowned DESTINATION .)
    install(TARGETS ctrl_c DESTINATION .)
endif()
