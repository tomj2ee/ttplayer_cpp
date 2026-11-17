set(TTPLAYER_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty" CACHE PATH "Local dependency sources")

foreach(dependency_file IN ITEMS
        "sdl3-src/CMakeLists.txt"
        "sdl3-src/cmake/sdltargets.cmake"
        "ffmpeg-src/configure")
    if(NOT EXISTS "${TTPLAYER_THIRDPARTY_DIR}/${dependency_file}")
        message(FATAL_ERROR
            "Incomplete bundled dependency sources: missing ${TTPLAYER_THIRDPARTY_DIR}/${dependency_file}\n"
            "Restore the complete bundled source tree, including its CMake helper scripts, before configuring.")
    endif()
endforeach()

if(TTPLAYER_STATIC_DEPS)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${TTPLAYER_THIRDPARTY_DIR}/sdl3-src" "${CMAKE_BINARY_DIR}/deps/sdl3" EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TTPLAYER_THIRDPARTY_DIR}/sdl_ttf-src" "${CMAKE_BINARY_DIR}/deps/sdl_ttf" EXCLUDE_FROM_ALL)
target_link_libraries(SDL3_ttf-static PUBLIC SDL3::SDL3)

if(WIN32)
    if(NOT EXISTS "${TTPLAYER_THIRDPARTY_DIR}/curl-src/CMakeLists.txt")
        message(FATAL_ERROR "Expected curl-src in ${TTPLAYER_THIRDPARTY_DIR}")
    endif()
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE STRING "" FORCE)
    set(CURL_ZSTD OFF CACHE STRING "" FORCE)
    add_subdirectory("${TTPLAYER_THIRDPARTY_DIR}/curl-src" "${CMAKE_BINARY_DIR}/deps/curl" EXCLUDE_FROM_ALL)
endif()

add_library(ttplayer_data_deps STATIC
    "${TTPLAYER_THIRDPARTY_DIR}/cJSON.c"
    "${TTPLAYER_THIRDPARTY_DIR}/miniz.c"
    "${TTPLAYER_THIRDPARTY_DIR}/miniz_tdef.c"
    "${TTPLAYER_THIRDPARTY_DIR}/miniz_tinfl.c"
    "${TTPLAYER_THIRDPARTY_DIR}/miniz_zip.c")
target_include_directories(ttplayer_data_deps PUBLIC "${TTPLAYER_THIRDPARTY_DIR}")

if(WIN32 AND NOT MINGW)
    message(FATAL_ERROR "Bundled FFmpeg currently requires MinGW. Use build.ps1 or the windows-local preset.")
endif()

include(ExternalProject)
find_program(TTPLAYER_BASH bash HINTS "C:/msys64/usr/bin" REQUIRED)
find_program(TTPLAYER_MAKE make HINTS "C:/msys64/usr/bin" REQUIRED)
set(TTPLAYER_FFMPEG_PLATFORM native)
set(TTPLAYER_FFMPEG_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(TTPLAYER_FFMPEG_SDK "")
if(APPLE)
    set(TTPLAYER_FFMPEG_PLATFORM darwin)
    if(CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
        if(NOT architecture_count EQUAL 1)
            message(FATAL_ERROR "Build arm64 and x86_64 separately; bundled FFmpeg does not produce universal archives.")
        endif()
        set(TTPLAYER_FFMPEG_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE TTPLAYER_FFMPEG_SDK OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
set(TTPLAYER_FFMPEG_PREFIX "${CMAKE_BINARY_DIR}/deps/ffmpeg-install")
file(MAKE_DIRECTORY "${TTPLAYER_FFMPEG_PREFIX}/include")
file(MAKE_DIRECTORY "${TTPLAYER_FFMPEG_PREFIX}/lib")

ExternalProject_Add(ttplayer_ffmpeg_build
    SOURCE_DIR "${TTPLAYER_THIRDPARTY_DIR}/ffmpeg-src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/deps/ffmpeg-build"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND "${TTPLAYER_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-ffmpeg.sh"
        configure "${TTPLAYER_THIRDPARTY_DIR}/ffmpeg-src" "${TTPLAYER_FFMPEG_PREFIX}"
        "${TTPLAYER_FFMPEG_PLATFORM}" "${TTPLAYER_FFMPEG_ARCH}" "${CMAKE_C_COMPILER}" "${CMAKE_CXX_COMPILER}"
        "${TTPLAYER_FFMPEG_SDK}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    BUILD_COMMAND "${TTPLAYER_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-ffmpeg.sh"
        build "${TTPLAYER_THIRDPARTY_DIR}/ffmpeg-src" "${TTPLAYER_FFMPEG_PREFIX}"
        "${TTPLAYER_FFMPEG_PLATFORM}" "${TTPLAYER_FFMPEG_ARCH}" "${CMAKE_C_COMPILER}" "${CMAKE_CXX_COMPILER}"
        "${TTPLAYER_MAKE}"
    INSTALL_COMMAND "${TTPLAYER_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-ffmpeg.sh"
        install "${TTPLAYER_THIRDPARTY_DIR}/ffmpeg-src" "${TTPLAYER_FFMPEG_PREFIX}"
        "${TTPLAYER_FFMPEG_PLATFORM}" "${TTPLAYER_FFMPEG_ARCH}" "${CMAKE_C_COMPILER}" "${CMAKE_CXX_COMPILER}"
        "${TTPLAYER_MAKE}"
    BUILD_BYPRODUCTS
        "${TTPLAYER_FFMPEG_PREFIX}/lib/libavformat.a"
        "${TTPLAYER_FFMPEG_PREFIX}/lib/libavcodec.a"
        "${TTPLAYER_FFMPEG_PREFIX}/lib/libswresample.a"
        "${TTPLAYER_FFMPEG_PREFIX}/lib/libavutil.a"
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
ExternalProject_Add_StepDependencies(ttplayer_ffmpeg_build configure
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-ffmpeg.sh")

foreach(component avformat avcodec swresample avutil)
    add_library(ttplayer_${component} STATIC IMPORTED GLOBAL)
    set_target_properties(ttplayer_${component} PROPERTIES
        IMPORTED_LOCATION "${TTPLAYER_FFMPEG_PREFIX}/lib/lib${component}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${TTPLAYER_FFMPEG_PREFIX}/include"
    )
    add_dependencies(ttplayer_${component} ttplayer_ffmpeg_build)
endforeach()
set(FFMPEG_LIBRARIES ttplayer_avformat ttplayer_avcodec ttplayer_swresample ttplayer_avutil)
if(WIN32)
    list(APPEND FFMPEG_LIBRARIES bcrypt ws2_32 secur32 crypt32 ncrypt mfuuid ole32 strmiids uuid)
elseif(APPLE)
    find_library(TTPLAYER_SECURITY Security REQUIRED)
    find_library(TTPLAYER_COREFOUNDATION CoreFoundation REQUIRED)
    list(APPEND FFMPEG_LIBRARIES ${TTPLAYER_SECURITY} ${TTPLAYER_COREFOUNDATION})
else()
    list(APPEND FFMPEG_LIBRARIES m ${CMAKE_DL_LIBS})
endif()
