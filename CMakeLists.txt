cmake_minimum_required(VERSION 3.22)
project(hamster)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchSparse)
set(BUILD_SHARED_LIBS ON)

set(HAMSTER_ASIO_GIT_REPOSITORY "https://github.com/chriskohlhoff/asio.git"
    CACHE STRING "Asio Git repository")
set(HAMSTER_ASIO_GIT_REVISION "12b52a54a25d1861b037bc5d9810f92a7151d920"
    CACHE STRING "Asio Git revision (1.38.2)")
FetchSparse(asio
    GIT_REPOSITORY ${HAMSTER_ASIO_GIT_REPOSITORY}
    GIT_TAG ${HAMSTER_ASIO_GIT_REVISION}
    SPARSE_EXCLUDE
        .github
        src)
FetchContent_GetProperties(asio SOURCE_DIR HAMSTER_ASIO_SOURCE_DIR)
add_library(asio INTERFACE)
add_library(asio::asio ALIAS asio)
target_include_directories(asio SYSTEM INTERFACE
    "${HAMSTER_ASIO_SOURCE_DIR}/include")
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

set(HAMSTER_BROTLI_GIT_REPOSITORY "https://github.com/google/brotli.git"
    CACHE STRING "Brotli Git repository")
set(HAMSTER_BROTLI_GIT_REVISION "ed738e842d2fbdf2d6459e39267a633c4a9b2f5d"
    CACHE STRING "Brotli Git revision (1.1.0)")
FetchSparse(brotli
    GIT_REPOSITORY ${HAMSTER_BROTLI_GIT_REPOSITORY}
    GIT_TAG ${HAMSTER_BROTLI_GIT_REVISION}
    EXCLUDE_FROM_ALL
    CACHE_OPTIONS
        BUILD_SHARED_LIBS:BOOL=ON
        BROTLI_BUNDLED_MODE:BOOL=ON
        BROTLI_DISABLE_TESTS:BOOL=ON
    SPARSE_EXCLUDE
        go
        java
        python
        research
        tests)
FetchContent_GetProperties(brotli SOURCE_DIR HAMSTER_BROTLI_SOURCE_DIR)

set(HAMSTER_ZLIB_GIT_REPOSITORY "https://github.com/madler/zlib.git"
    CACHE STRING "zlib Git repository")
set(HAMSTER_ZLIB_GIT_REVISION "da607da739fa6047df13e66a2af6b8bec7c2a498"
    CACHE STRING "zlib Git revision (1.3.2)")
FetchSparse(zlib
    GIT_REPOSITORY ${HAMSTER_ZLIB_GIT_REPOSITORY}
    GIT_TAG ${HAMSTER_ZLIB_GIT_REVISION}
    EXCLUDE_FROM_ALL
    CACHE_OPTIONS
        ZLIB_BUILD_SHARED:BOOL=ON
        ZLIB_BUILD_STATIC:BOOL=OFF
        ZLIB_BUILD_TESTING:BOOL=OFF
        ZLIB_INSTALL:BOOL=OFF
    SPARSE_EXCLUDE
        doc
        examples)
FetchContent_GetProperties(zlib
    SOURCE_DIR HAMSTER_ZLIB_SOURCE_DIR
    BINARY_DIR HAMSTER_ZLIB_BINARY_DIR)

set(HAMSTER_ZSTD_GIT_REPOSITORY "https://github.com/facebook/zstd.git"
    CACHE STRING "zstd Git repository")
set(HAMSTER_ZSTD_GIT_REVISION "f8745da6ff1ad1e7bab384bd1f9d742439278e99"
    CACHE STRING "zstd Git revision (1.5.7)")
FetchSparse(zstd
    GIT_REPOSITORY ${HAMSTER_ZSTD_GIT_REPOSITORY}
    GIT_TAG ${HAMSTER_ZSTD_GIT_REVISION}
    SOURCE_SUBDIR build/cmake
    EXCLUDE_FROM_ALL
    CACHE_OPTIONS
        ZSTD_BUILD_PROGRAMS:BOOL=OFF
        ZSTD_BUILD_TESTS:BOOL=OFF
        ZSTD_BUILD_CONTRIB:BOOL=OFF
        ZSTD_BUILD_SHARED:BOOL=ON
        ZSTD_BUILD_STATIC:BOOL=OFF
        ZSTD_LEGACY_SUPPORT:STRING=OFF
    SPARSE_EXCLUDE
        build/VS2008
        build/VS_scripts
        build/meson
        build/single_file_libs
        contrib
        doc
        examples
        lib/deprecated
        lib/legacy
        programs
        tests)
FetchContent_GetProperties(zstd SOURCE_DIR HAMSTER_ZSTD_SOURCE_DIR)

set(HAMSTER_GUMBO_GIT_REPOSITORY "https://github.com/google/gumbo-parser.git"
    CACHE STRING "Gumbo parser Git repository")
set(HAMSTER_GUMBO_GIT_REVISION "3973c58d759574f2899528d2b3379e17d66dbcad"
    CACHE STRING "Gumbo parser Git revision (0.10.1)")
FetchSparse(gumbo
    GIT_REPOSITORY ${HAMSTER_GUMBO_GIT_REPOSITORY}
    GIT_TAG ${HAMSTER_GUMBO_GIT_REVISION}
    SPARSE_EXCLUDE
        benchmarks
        examples
        fuzz
        python
        tests)
FetchContent_GetProperties(gumbo SOURCE_DIR HAMSTER_GUMBO_SOURCE_DIR)
set(HAMSTER_GUMBO_SOURCES
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/attribute.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/char_ref.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/error.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/parser.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/string_buffer.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/string_piece.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/tag.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/tokenizer.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/utf8.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/util.c
    ${HAMSTER_GUMBO_SOURCE_DIR}/src/vector.c)
add_library(gumbo STATIC ${HAMSTER_GUMBO_SOURCES})
target_include_directories(gumbo PUBLIC "${HAMSTER_GUMBO_SOURCE_DIR}/src")
if(WIN32)
    target_include_directories(gumbo PRIVATE
        "${HAMSTER_GUMBO_SOURCE_DIR}/visualc/include")
endif()

set(HAMSTER_SQLITE_URL
    "https://www.sqlite.org/2020/sqlite-amalgamation-3310000.zip"
    CACHE STRING "SQLite amalgamation archive")
FetchSparse(sqlite
    URL ${HAMSTER_SQLITE_URL})
FetchContent_GetProperties(sqlite SOURCE_DIR HAMSTER_SQLITE_SOURCE_DIR)
add_library(sqlite3 SHARED "${HAMSTER_SQLITE_SOURCE_DIR}/sqlite3.c")
add_library(sqlite3::sqlite3 ALIAS sqlite3)
target_include_directories(sqlite3 PUBLIC
    "$<BUILD_INTERFACE:${HAMSTER_SQLITE_SOURCE_DIR}>")
target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5)
if(WIN32)
    set_target_properties(sqlite3 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(NOT WIN32)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
find_package(OpenSSL REQUIRED)
if(WIN32)
    get_filename_component(OPENSSL_INSTALL_ROOT "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
    file(GLOB OPENSSL_RUNTIME_DLLS
        "${OPENSSL_INSTALL_ROOT}/bin/libssl-*.dll"
        "${OPENSSL_INSTALL_ROOT}/bin/libcrypto-*.dll")
endif()

set(HAMSTER_CURL_IMPERSONATE_VERSION "2.2.3" CACHE STRING
    "Pinned curl-impersonate release")
set(HAMSTER_CURL_IMPERSONATE_GITHUB_REPOSITORY
    "lexiforest/curl-impersonate"
    CACHE STRING "curl-impersonate GitHub repository")
include(GetCurlImpersonate)
GetCurlImpersonate(
    VERSION ${HAMSTER_CURL_IMPERSONATE_VERSION}
    GITHUB_REPOSITORY ${HAMSTER_CURL_IMPERSONATE_GITHUB_REPOSITORY})

find_package(Git QUIET)
if(NOT VERSION AND GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT VERSION MATCHES "^[0-9]+[.][0-9]+")
    set(VERSION "0.0")
endif()
string(REGEX MATCH "^([0-9]+)[.]([0-9]+)" HAMSTER_VERSION "${VERSION}")
set(HAMSTER_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(HAMSTER_VERSION_MINOR "${CMAKE_MATCH_2}")

configure_file("src/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/src/_version.h")

set(HAMSTER_RESOURCE_OBSERVER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/recording/resource_observer.js")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${HAMSTER_RESOURCE_OBSERVER_SOURCE}")
file(READ "${HAMSTER_RESOURCE_OBSERVER_SOURCE}" HAMSTER_RESOURCE_OBSERVER_SCRIPT)
string(FIND "${HAMSTER_RESOURCE_OBSERVER_SCRIPT}" ")HAMSTER_JS\""
    HAMSTER_RESOURCE_OBSERVER_DELIMITER_POSITION)
if(NOT HAMSTER_RESOURCE_OBSERVER_DELIMITER_POSITION EQUAL -1)
    message(FATAL_ERROR
        "resource_observer.js contains the reserved raw-string delimiter )HAMSTER_JS\"")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "cmake/ResourceObserverScript.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ResourceObserverScript.inc"
    @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    src/library/Archive.cpp
    src/library/HeaderStore.cpp
    src/library/Indexing.cpp
    src/library/Library.cpp
    src/library/LossyCompressor.cpp
    src/catalog/Catalog.cpp
    src/catalog/sqlite.cpp
    src/common.cpp
    src/decode_content.cpp
    src/main.cpp
    src/native_host.cpp
    src/platform.cpp
    src/proxy/CertificateAuthority.cpp
    src/proxy/Client.cpp
    src/proxy/ControlApi.cpp
    src/proxy/ProxyServer.cpp
    src/proxy/ProxyRequest.cpp
    src/proxy/run_proxy.cpp
    src/recording/CacheInfo.cpp
    src/recording/HostList.cpp
    src/recording/RecordingCoordinator.cpp
    src/recording/RecordingSession.cpp
    src/recording/ResourceGraph.cpp
    src/recording/ResponseJournal.cpp
    src/recording/SessionController.cpp
    src/recording/SessionController.cpp
    src/Settings.cpp
    src/test.cpp
)

if(MSVC)
    add_compile_options(/GR- /W1)
    set(MSVC_SHARED_RUNTIME OFF)
else()
    set_source_files_properties(${SOURCES}
        PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Wsign-conversion -Wconversion")
endif()

list(APPEND SOURCES
    libs/entities/entities.c
    libs/minizip/ioapi.c
    libs/minizip/unzip.c
    libs/minizip/zip.c
    libs/siphash/siphash.c
    libs/stb/stb_impl.c
    libs/utf8/utf8.cpp
)

set(INCLUDES
    ${HAMSTER_SQLITE_SOURCE_DIR}
    ${HAMSTER_ZLIB_SOURCE_DIR}
    ${HAMSTER_ZLIB_BINARY_DIR}
)

file(GLOB_RECURSE HEADERS include *.h *.hpp *.inc)
add_compile_definitions(NOCRYPT UNICODE)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0501)
    link_libraries(ws2_32 wsock32 crypt32)
    list(APPEND SOURCES libs/minizip/iowin32.c)
elseif(APPLE)
    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT})
    set(OPENSSL_USE_STATIC_LIBS TRUE)
else()
    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT})
    if(NOT APPLE)
        link_libraries(stdc++fs)
    endif()
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()

add_compile_definitions(HAVE_OPENSSL)
include_directories(${OPENSSL_INCLUDE_DIR})

include_directories(.)
include_directories(src)
include_directories(SYSTEM ${INCLUDES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})
add_executable(hamster ${SOURCES} ${HEADERS})
target_include_directories(hamster PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(hamster PRIVATE
    asio::asio
    brotlidec
    gumbo
    libzstd_shared
    sqlite3::sqlite3
    zlib
    curl_impersonate
    OpenSSL::SSL
    OpenSSL::Crypto
    ${CMAKE_DL_LIBS})

find_package(Iconv)
option(USE_ICONV "Use iconv for charset conversion" ${Iconv_FOUND})
if(USE_ICONV)
    add_compile_definitions(USE_ICONV)
    target_link_libraries(hamster PRIVATE ${Iconv_LIBRARY})
endif()

foreach(HAMSTER_CURL_IMPERSONATE_RUNTIME IN LISTS CURL_IMPERSONATE_RUNTIME_FILES)
    add_custom_command(TARGET hamster POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HAMSTER_CURL_IMPERSONATE_RUNTIME}" $<TARGET_FILE_DIR:hamster>)
endforeach()

if(WIN32)
    foreach(HAMSTER_SHARED_DEPENDENCY
            brotlicommon brotlidec libzstd_shared sqlite3 zlib)
        add_custom_command(TARGET hamster POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${HAMSTER_SHARED_DEPENDENCY}>
                $<TARGET_FILE_DIR:hamster>)
    endforeach()

    if(OPENSSL_RUNTIME_DLLS)
        add_custom_command(TARGET hamster POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENSSL_RUNTIME_DLLS} $<TARGET_FILE_DIR:hamster>)
    endif()
endif()

if(WIN32 AND NOT MSVC)
    target_link_options(hamster BEFORE PUBLIC -municode)
endif()

install(TARGETS hamster DESTINATION .)
install(TARGETS brotlicommon brotlidec libzstd_shared sqlite3 zlib
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .)
install(FILES ${CURL_IMPERSONATE_RUNTIME_FILES} DESTINATION .)
if(CURL_IMPERSONATE_LICENSES)
    install(FILES ${CURL_IMPERSONATE_LICENSES}
        DESTINATION licenses/curl-impersonate)
endif()
if(WIN32 AND OPENSSL_RUNTIME_DLLS)
    install(FILES ${OPENSSL_RUNTIME_DLLS} DESTINATION .)
endif()
install(FILES deploy/hamster-mozilla.json deploy/hamster-chromium.json DESTINATION .)
install(FILES webext/icons/icon.ico DESTINATION .)

set(CPACK_PACKAGE_NAME "Hamster")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hamster recording proxy")
set(CPACK_PACKAGE_VENDOR "Albert Kalchmair")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Hamster")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/houmain/hamster")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/webext/icons/icon.ico")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_VERBATIM_VARIABLES TRUE)
set(CPACK_PACKAGE_VERSION "${HAMSTER_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${HAMSTER_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${HAMSTER_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH 0)
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "Hamster-${HAMSTER_VERSION}-windows-x64")
elseif(APPLE)
    set(CPACK_PACKAGE_FILE_NAME "Hamster-${HAMSTER_VERSION}-macos-x64")
else()
    set(CPACK_PACKAGE_FILE_NAME "Hamster-${HAMSTER_VERSION}-linux-x64")
endif()
if(WIN32)
    # CPack's stock NSIS template defaults to administrator privileges. The
    # later directive overrides that default in the generated NSIS script.
    set(CPACK_NSIS_DEFINES "RequestExecutionLevel user")
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "WriteRegStr HKCU \"Software\\Mozilla\\NativeMessagingHosts\\hamster\" \"\" \"$INSTDIR\\hamster-mozilla.json\"
         WriteRegStr HKCU \"Software\\Google\\Chrome\\NativeMessagingHosts\\hamster\" \"\" \"$INSTDIR\\hamster-chromium.json\"
         WriteRegStr HKCU \"Software\\Microsoft\\Edge\\NativeMessagingHosts\\hamster\" \"\" \"$INSTDIR\\hamster-chromium.json\"")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKCU \"Software\\Mozilla\\NativeMessagingHosts\\hamster\"
         DeleteRegKey HKCU \"Software\\Google\\Chrome\\NativeMessagingHosts\\hamster\"
         DeleteRegKey HKCU \"Software\\Microsoft\\Edge\\NativeMessagingHosts\\hamster\"")
endif()
include(CPack)
