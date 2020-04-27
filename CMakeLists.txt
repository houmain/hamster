cmake_minimum_required(VERSION 3.0.0)
project(hamster
    VERSION 0.8
)

option(BUILD_WEBRECORDER "Build webrecorder")
if(BUILD_WEBRECORDER)
    add_subdirectory(libs/webrecorder)
endif()

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    src/main.cpp
    src/common.cpp
    src/Webrecorder.cpp
    src/Json.cpp
    src/Logic.cpp
    src/Database.cpp
    src/Indexing.cpp
    src/Settings.cpp
    src/sqlite.cpp
    src/platform.cpp
)
file(GLOB_RECURSE HEADERS include *.h)

set(INCLUDES libs/webrecorder)

if(NOT MSVC)
    set_source_files_properties(${SOURCES}
       PROPERTIES COMPILE_FLAGS  "-Wall -Wextra -Wsign-conversion -Wconversion")
else()
    add_compile_options(/GR- /W1)
    set(MSVC_SHARED_RUNTIME OFF)
endif()

set(SOURCES ${SOURCES}
    libs/TinyProcessLib/process.cpp
    libs/entities/entities.c
    libs/webrecorder/src/Archive.cpp
    libs/webrecorder/src/HeaderStore.cpp
    libs/webrecorder/src/LossyCompressor.cpp
    libs/webrecorder/src/common.cpp
    libs/webrecorder/libs/siphash/siphash.c
    libs/webrecorder/libs/utf8/utf8.cpp
    libs/webrecorder/libs/siphash/siphash.c
    libs/webrecorder/libs/minizip/zip.c
    libs/webrecorder/libs/minizip/unzip.c
    libs/webrecorder/libs/minizip/ioapi.c
)

if(NOT WIN32)
    set(SOURCES ${SOURCES}
        libs/TinyProcessLib/process_unix.cpp
    )
    link_libraries(stdc++fs sqlite3)
else()
    add_compile_definitions(_WIN32_WINNT=0x0501 SQLITE_ENABLE_FTS5)

    set(SOURCES ${SOURCES}
        libs/TinyProcessLib/process_win.cpp
        libs/webrecorder/libs/minizip/iowin32.c
        libs/sqlite/sqlite3.c
    )
    set(INCLUDES ${INCLUDES}
        libs/sqlite
    )
endif()

add_compile_definitions(RAPIDJSON_HAS_STDSTRING=1 UNICODE NOCRYPT)

link_libraries(gumbo z)

find_package(Threads REQUIRED)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
    add_definitions(-DNOC_FILE_DIALOG_WIN32)
else()
    find_package(PkgConfig REQUIRED)
    PKG_CHECK_MODULES(GTK3 REQUIRED gtk+-3.0)
    include_directories(${GTK3_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS})
    add_definitions(${GTK3_CFLAGS_OTHER} -DNOC_FILE_DIALOG_GTK)
    link_libraries(${GTK3_LIBRARIES})
endif()

include_directories(.)
include_directories(SYSTEM ${INCLUDES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

add_executable(hamster ${SOURCES} ${HEADERS})

if(WIN32 AND NOT MSVC)
    target_link_options(hamster PRIVATE -municode)
endif()

if(WIN32)
    add_executable(ctrl_c WIN32 src/ctrl_c.cpp)
endif()

if(NOT WIN32)
    install(TARGETS hamster DESTINATION "bin")
else()
    install(TARGETS hamster DESTINATION .)
    install(TARGETS ctrl_c DESTINATION .)
    install(FILES hamster.json DESTINATION .)
endif()
