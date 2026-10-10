include_guard(GLOBAL)

include(FetchContent)

function(GetCurlImpersonate)
    set(one_value_args VERSION GITHUB_REPOSITORY)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "${one_value_args}" "")

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments for GetCurlImpersonate: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required_arg IN LISTS one_value_args)
        if(NOT ARG_${required_arg})
            message(FATAL_ERROR
                "GetCurlImpersonate requires ${required_arg}")
        endif()
    endforeach()

    find_library(SYSTEM_CURL_IMPERSONATE_LIBRARY
        NAMES curl-impersonate libcurl-impersonate)
    if(WIN32)
        find_library(SYSTEM_CURL_IMPERSONATE_IMPLIB
            NAMES curl-impersonate_imp libcurl-impersonate_imp)
        if(NOT SYSTEM_CURL_IMPERSONATE_IMPLIB)
            set(SYSTEM_CURL_IMPERSONATE_IMPLIB
                "${SYSTEM_CURL_IMPERSONATE_LIBRARY}")
        endif()
        if(SYSTEM_CURL_IMPERSONATE_IMPLIB)
            get_filename_component(CURL_IMPERSONATE_LIB_DIR
                "${SYSTEM_CURL_IMPERSONATE_IMPLIB}" DIRECTORY)
        endif()
    elseif(SYSTEM_CURL_IMPERSONATE_LIBRARY)
        get_filename_component(CURL_IMPERSONATE_LIB_DIR
            "${SYSTEM_CURL_IMPERSONATE_LIBRARY}" DIRECTORY)
    endif()

    if(CURL_IMPERSONATE_LIB_DIR)
        get_filename_component(CURL_IMPERSONATE_PREFIX
            "${CURL_IMPERSONATE_LIB_DIR}" DIRECTORY)
    endif()
    if(WIN32)
        find_file(SYSTEM_CURL_IMPERSONATE_RUNTIME
            NAMES libcurl-impersonate.dll curl-impersonate.dll
            HINTS
                "${CURL_IMPERSONATE_LIB_DIR}"
                "${CURL_IMPERSONATE_PREFIX}"
            PATH_SUFFIXES bin lib)
    endif()
    find_path(SYSTEM_CURL_IMPERSONATE_INCLUDE_DIR
        NAMES curl/curl.h
        HINTS "${CURL_IMPERSONATE_PREFIX}"
        PATH_SUFFIXES include)

    set(CURL_IMPERSONATE_LICENSES)
    set(CURL_IMPERSONATE_RUNTIME_FILES)
    if(SYSTEM_CURL_IMPERSONATE_INCLUDE_DIR
       AND ((WIN32 AND SYSTEM_CURL_IMPERSONATE_IMPLIB
                    AND SYSTEM_CURL_IMPERSONATE_RUNTIME)
            OR (NOT WIN32 AND SYSTEM_CURL_IMPERSONATE_LIBRARY)))
        set(CURL_IMPERSONATE_INCLUDE_DIR
            "${SYSTEM_CURL_IMPERSONATE_INCLUDE_DIR}")
        if(WIN32)
            set(CURL_IMPERSONATE_IMPLIB
                "${SYSTEM_CURL_IMPERSONATE_IMPLIB}")
            set(CURL_IMPERSONATE_RUNTIME
                "${SYSTEM_CURL_IMPERSONATE_RUNTIME}")
        else()
            set(CURL_IMPERSONATE_RUNTIME
                "${SYSTEM_CURL_IMPERSONATE_LIBRARY}")
        endif()
        set(CURL_IMPERSONATE_RUNTIME_FILES
            "${CURL_IMPERSONATE_RUNTIME}")
        message(STATUS "Using system curl-impersonate: ${CURL_IMPERSONATE_RUNTIME}")
    else()
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CURL_IMPERSONATE_PROCESSOR)
        if(WIN32)
            set(CURL_IMPERSONATE_PLATFORM "x86_64-win32")
        elseif(APPLE)
            if(CURL_IMPERSONATE_PROCESSOR MATCHES "^(arm64|aarch64)$")
                set(CURL_IMPERSONATE_PLATFORM "arm64-macos")
            else()
                set(CURL_IMPERSONATE_PLATFORM "x86_64-macos")
            endif()
        else()
            if(CURL_IMPERSONATE_PROCESSOR MATCHES "^(arm64|aarch64)$")
                set(CURL_IMPERSONATE_PLATFORM "aarch64-linux-gnu")
            else()
                set(CURL_IMPERSONATE_PLATFORM "x86_64-linux-gnu")
            endif()
        endif()

        set(CURL_IMPERSONATE_ARCHIVE
            "libcurl-impersonate-v${ARG_VERSION}.${CURL_IMPERSONATE_PLATFORM}.tar.gz")
        FetchContent_Declare(curl_impersonate_package
            URL "https://github.com/${ARG_GITHUB_REPOSITORY}/releases/download/v${ARG_VERSION}/${CURL_IMPERSONATE_ARCHIVE}")
        FetchContent_MakeAvailable(curl_impersonate_package)

        set(CURL_IMPERSONATE_PACKAGE_DIR "${curl_impersonate_package_SOURCE_DIR}")
        set(CURL_IMPERSONATE_INCLUDE_DIR "${CURL_IMPERSONATE_PACKAGE_DIR}/include")
        message(STATUS
            "Using downloaded curl-impersonate ${ARG_VERSION} binaries (${CURL_IMPERSONATE_PLATFORM})")
        file(GLOB CURL_IMPERSONATE_LICENSES
            "${CURL_IMPERSONATE_PACKAGE_DIR}/LICENSE*")
        if(WIN32)
            set(CURL_IMPERSONATE_RUNTIME
                "${CURL_IMPERSONATE_PACKAGE_DIR}/lib/libcurl-impersonate.dll")
            set(CURL_IMPERSONATE_IMPLIB
                "${CURL_IMPERSONATE_PACKAGE_DIR}/lib/libcurl-impersonate_imp.lib")
            set(CURL_IMPERSONATE_RUNTIME_FILES
                "${CURL_IMPERSONATE_RUNTIME}")
        else()
            find_library(CURL_IMPERSONATE_RUNTIME
                NAMES curl-impersonate
                PATHS
                    "${CURL_IMPERSONATE_PACKAGE_DIR}"
                    "${CURL_IMPERSONATE_PACKAGE_DIR}/lib"
                NO_DEFAULT_PATH)
            if(APPLE)
                file(GLOB CURL_IMPERSONATE_RUNTIME_FILES
                    "${CURL_IMPERSONATE_PACKAGE_DIR}/libcurl-impersonate*.dylib"
                    "${CURL_IMPERSONATE_PACKAGE_DIR}/lib/libcurl-impersonate*.dylib")
            else()
                file(GLOB CURL_IMPERSONATE_RUNTIME_FILES
                    "${CURL_IMPERSONATE_PACKAGE_DIR}/libcurl-impersonate.so*"
                    "${CURL_IMPERSONATE_PACKAGE_DIR}/lib/libcurl-impersonate.so*")
            endif()
        endif()
    endif()

    if(NOT EXISTS "${CURL_IMPERSONATE_INCLUDE_DIR}/curl/curl.h")
        message(FATAL_ERROR "curl-impersonate headers not found")
    endif()

    add_library(curl_impersonate SHARED IMPORTED GLOBAL)
    set_target_properties(curl_impersonate PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CURL_IMPERSONATE_INCLUDE_DIR}")
    if(WIN32)
        if(NOT EXISTS "${CURL_IMPERSONATE_RUNTIME}" OR NOT EXISTS "${CURL_IMPERSONATE_IMPLIB}")
            message(FATAL_ERROR "curl-impersonate Windows libraries not found")
        endif()
        set_target_properties(curl_impersonate PROPERTIES
            IMPORTED_LOCATION "${CURL_IMPERSONATE_RUNTIME}"
            IMPORTED_IMPLIB "${CURL_IMPERSONATE_IMPLIB}")
    else()
        if(NOT EXISTS "${CURL_IMPERSONATE_RUNTIME}")
            message(FATAL_ERROR "curl-impersonate shared library not found")
        endif()
        set_target_properties(curl_impersonate PROPERTIES
            IMPORTED_LOCATION "${CURL_IMPERSONATE_RUNTIME}")
    endif()

    set(CURL_IMPERSONATE_RUNTIME "${CURL_IMPERSONATE_RUNTIME}" PARENT_SCOPE)
    set(CURL_IMPERSONATE_RUNTIME_FILES
        "${CURL_IMPERSONATE_RUNTIME_FILES}" PARENT_SCOPE)
    set(CURL_IMPERSONATE_LICENSES "${CURL_IMPERSONATE_LICENSES}" PARENT_SCOPE)
endfunction()
