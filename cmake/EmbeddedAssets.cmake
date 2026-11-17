if(TTPLAYER_RESOURCE_ROOT STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/../src/main/resources" OR
   TTPLAYER_RESOURCE_ROOT STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/../ttplayer/src/main/resources")
    set(TTPLAYER_RESOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/main/resources" CACHE PATH
        "Directory containing TTPlayer skins, icons and translations" FORCE)
endif()
set(TTPLAYER_RESOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/main/resources" CACHE PATH
    "Directory containing TTPlayer skins, icons and translations")
if(NOT IS_DIRECTORY "${TTPLAYER_RESOURCE_ROOT}/skin" OR
   NOT EXISTS "${TTPLAYER_RESOURCE_ROOT}/messages.properties")
    message(FATAL_ERROR "Missing TTPlayer resources at '${TTPLAYER_RESOURCE_ROOT}'. Set -DTTPLAYER_RESOURCE_ROOT=<path-to-src/main/resources>.")
endif()
set(TTPLAYER_ASSET_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAssetsData.cpp")
if(TTPLAYER_EMBED_ASSETS)
    add_executable(ttplayer_asset_packer EXCLUDE_FROM_ALL tools/PackAssets.cpp)
    if(APPLE AND CMAKE_HOST_APPLE)
        target_sources(ttplayer_asset_packer PRIVATE thirdparty/miniz.c thirdparty/miniz_tdef.c
            thirdparty/miniz_tinfl.c thirdparty/miniz_zip.c)
        target_include_directories(ttplayer_asset_packer PRIVATE thirdparty)
        set_target_properties(ttplayer_asset_packer PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    else()
        target_link_libraries(ttplayer_asset_packer PRIVATE ttplayer_data_deps)
    endif()
    if(MINGW)
        target_link_options(ttplayer_asset_packer PRIVATE -static -static-libgcc -static-libstdc++ -municode)
    endif()
    file(GLOB_RECURSE TTPLAYER_ASSET_INPUTS CONFIGURE_DEPENDS
        "${TTPLAYER_RESOURCE_ROOT}/skin/*" "${TTPLAYER_RESOURCE_ROOT}/ico/*")
    file(GLOB TTPLAYER_TRANSLATION_INPUTS CONFIGURE_DEPENDS "${TTPLAYER_RESOURCE_ROOT}/messages*.properties")
    list(APPEND TTPLAYER_ASSET_INPUTS ${TTPLAYER_TRANSLATION_INPUTS})
    file(GLOB TTPLAYER_LICENSE_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sdl3-src/LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sdl_ttf-src/LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/curl-src/COPYING"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg-src/COPYING.LGPLv2.1"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg-src/LICENSE.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sdl_ttf-src/external/freetype/docs/FTL.TXT"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sdl_ttf-src/external/freetype/docs/GPLv2.TXT"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cJSON.h" "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniz.c")
    add_custom_command(OUTPUT "${TTPLAYER_ASSET_SOURCE}"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/assets.zip"
        COMMAND ${CMAKE_COMMAND}
            "-DRESOURCE_ROOT=${TTPLAYER_RESOURCE_ROOT}"
            "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/generated"
            "-DTHIRDPARTY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
            "-DPACKER=$<TARGET_FILE:ttplayer_asset_packer>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedAssets.cmake"
        DEPENDS ttplayer_asset_packer ${TTPLAYER_ASSET_INPUTS} ${TTPLAYER_LICENSE_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedAssets.cmake"
        VERBATIM)
else()
    file(GENERATE OUTPUT "${TTPLAYER_ASSET_SOURCE}" CONTENT [=[
#include <app/EmbeddedAssets.hpp>
namespace ttplayer::app::embedded {
std::span<const unsigned char> archive() { return {}; }
std::string_view identifier() { return {}; }
}
]=])
endif()
add_library(ttplayer_embedded_assets STATIC src/app/EmbeddedAssets.cpp "${TTPLAYER_ASSET_SOURCE}")
target_include_directories(ttplayer_embedded_assets PUBLIC src)
target_link_libraries(ttplayer_embedded_assets PUBLIC ttplayer_data_deps)
if(MSVC)
    target_compile_options(ttplayer_embedded_assets PRIVATE /utf-8)
endif()
