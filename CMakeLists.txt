cmake_minimum_required(VERSION 3.12)
project(hamster)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

option(BUILD_WEBRECORDER "Build webrecorder")
if(BUILD_WEBRECORDER)
    add_subdirectory(libs/webrecorder)
endif()
configure_file("src/version.h.in" "${CMAKE_SOURCE_DIR}/src/_version.h")

find_package(Git)
if(NOT VERSION AND GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE VERSION
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+")
    message(WARNING "Could not get version from git repository (defaulting to VERSION=0.0.0).")
    set(VERSION "0.0.0")
endif()
configure_file("src/version.h.in" "${CMAKE_SOURCE_DIR}/src/_version.h")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES ${SOURCES}
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

set(INCLUDES ${INCLUDES}
    libs/webrecorder
    libs/webrecorder/libs/gumbo/src
    libs/webrecorder/libs/zlib/src
    libs/sqlite
)

if(MSVC)
    add_compile_options(/GR- /W1)
    set(MSVC_SHARED_RUNTIME OFF)
else()
    set_source_files_properties(${SOURCES}
        PROPERTIES COMPILE_FLAGS  "-Wall -Wextra -Wsign-conversion -Wconversion")
endif()

set(SOURCES ${SOURCES}
    libs/sqlite/sqlite3.c
    libs/TinyProcessLib/process.cpp
    libs/entities/entities.c
    libs/webrecorder/src/Archive.cpp
    libs/webrecorder/src/HeaderStore.cpp
    libs/webrecorder/src/common.cpp
    libs/webrecorder/libs/siphash/siphash.c
    libs/webrecorder/libs/utf8/utf8.cpp
    libs/webrecorder/libs/siphash/siphash.c
    libs/webrecorder/libs/minizip/zip.c
    libs/webrecorder/libs/minizip/unzip.c
    libs/webrecorder/libs/minizip/ioapi.c
    libs/webrecorder/libs/gumbo/src/attribute.c
    libs/webrecorder/libs/gumbo/src/char_ref.c
    libs/webrecorder/libs/gumbo/src/error.c
    libs/webrecorder/libs/gumbo/src/parser.c
    libs/webrecorder/libs/gumbo/src/string_buffer.c
    libs/webrecorder/libs/gumbo/src/string_piece.c
    libs/webrecorder/libs/gumbo/src/tag.c
    libs/webrecorder/libs/gumbo/src/tokenizer.c
    libs/webrecorder/libs/gumbo/src/utf8.c
    libs/webrecorder/libs/gumbo/src/util.c
    libs/webrecorder/libs/gumbo/src/vector.c
    libs/webrecorder/libs/zlib/src/adler32.c
    libs/webrecorder/libs/zlib/src/compress.c
    libs/webrecorder/libs/zlib/src/crc32.c
    libs/webrecorder/libs/zlib/src/deflate.c
    libs/webrecorder/libs/zlib/src/gzclose.c
    libs/webrecorder/libs/zlib/src/gzlib.c
    libs/webrecorder/libs/zlib/src/gzread.c
    libs/webrecorder/libs/zlib/src/gzwrite.c
    libs/webrecorder/libs/zlib/src/infback.c
    libs/webrecorder/libs/zlib/src/inffast.c
    libs/webrecorder/libs/zlib/src/inflate.c
    libs/webrecorder/libs/zlib/src/inftrees.c
    libs/webrecorder/libs/zlib/src/trees.c
    libs/webrecorder/libs/zlib/src/uncompr.c
    libs/webrecorder/libs/zlib/src/zutil.c
)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0501)

    set(SOURCES ${SOURCES}
        libs/TinyProcessLib/process_win.cpp
        libs/webrecorder/libs/minizip/iowin32.c
    )
    set(INCLUDES ${INCLUDES}
        libs/webrecorder/libs/gumbo/src/visualc
    )
else()
    if(NOT APPLE)
        link_libraries(stdc++fs dl)
    endif()
    set(SOURCES ${SOURCES}
        libs/TinyProcessLib/process_unix.cpp
    )
endif()

add_compile_definitions(SQLITE_ENABLE_FTS5 RAPIDJSON_HAS_STDSTRING=1 UNICODE NOCRYPT)

find_package(Threads REQUIRED)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

option(USE_ICONV "Use iconv for charset conversion" ON)
if(USE_ICONV)
    find_package(Iconv)
    if(Iconv_FOUND)
        add_compile_definitions(USE_ICONV)
        if(Iconv_LIBRARY)
            link_libraries(${Iconv_LIBRARY})
        endif()
    endif()
endif()

if(WIN32)
    set(SOURCES ${SOURCES} libs/noc/noc_file_dialog.c)
    add_definitions(-DNOC_FILE_DIALOG_WIN32)
elseif(APPLE)
    set(SOURCES ${SOURCES} libs/noc/noc_file_dialog.m)
    add_definitions(-DNOC_FILE_DIALOG_OSX)
    find_library(COCOA_LIBRARY Cocoa)
    link_libraries(${COCOA_LIBRARY})
else()
    set(SOURCES ${SOURCES} libs/noc/noc_file_dialog.c)
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

# install
if(NOT WIN32)
    install(TARGETS hamster DESTINATION "bin")
else()
    install(TARGETS hamster DESTINATION .)
    install(TARGETS ctrl_c DESTINATION .)
    install(FILES deploy/hamster-mozilla.json DESTINATION .)
    install(FILES webext/icons/icon.ico DESTINATION .)
endif()

# package
set(CPACK_PACKAGE_NAME "BookmarkHamster")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bookmark Hamster")
set(CPACK_PACKAGE_VENDOR "Albert Kalchmair")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Bookmark Hamster")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/houmain/hamster")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/webext/icons/icon.ico")
 
set(CPACK_STRIP_FILES TRUE)
string(REGEX MATCHALL "[0-9]+" VERSION_LIST "${VERSION}")
list(GET VERSION_LIST 0 CPACK_PACKAGE_VERSION_MAJOR)
list(GET VERSION_LIST 1 CPACK_PACKAGE_VERSION_MINOR)
list(GET VERSION_LIST 2 CPACK_PACKAGE_VERSION_PATCH)

include(CPack)
