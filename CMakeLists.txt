cmake_minimum_required(VERSION 3.0.0)
project(pagesowned)

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
    install(FILES pagesowned.json DESTINATION .)

    set(CPACK_GENERATOR WIX)
    set(CPACK_WIX_UPGRADE_GUID "FE3DD160-C524-49F0-B740-E84A37C13E26")
    set(CPACK_PACKAGE_NAME "Pages Owned")
    set(CPACK_PACKAGE_VENDOR "Albert Kalchmair")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Pages Owned")
    configure_file(LICENSE license.txt COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/license.txt")
    set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wix_patch.xml")
    include(CPack)
endif()
