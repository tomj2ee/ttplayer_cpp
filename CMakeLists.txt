cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()
project(ttplayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TTPLAYER_BUNDLED_DEPS "Build dependencies from thirdparty without downloads" ON)
option(TTPLAYER_STATIC_DEPS "Link bundled libraries and Windows compiler runtimes statically" ${TTPLAYER_BUNDLED_DEPS})
option(TTPLAYER_EMBED_ASSETS "Embed skins, icons and translations in the application" ON)
if(TTPLAYER_STATIC_DEPS AND NOT TTPLAYER_BUNDLED_DEPS)
    message(FATAL_ERROR "Static deployment requires TTPLAYER_BUNDLED_DEPS=ON; disable TTPLAYER_STATIC_DEPS to use external packages.")
endif()
if(TTPLAYER_STATIC_DEPS AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(TTPLAYER_BUNDLED_DEPS)
    include(cmake/BundledDependencies.cmake)
else()
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    set(FFMPEG_LIBRARIES PkgConfig::FFMPEG)
    add_library(ttplayer_data_deps STATIC thirdparty/cJSON.c thirdparty/miniz.c thirdparty/miniz_tdef.c thirdparty/miniz_tinfl.c thirdparty/miniz_zip.c)
    target_include_directories(ttplayer_data_deps PUBLIC thirdparty)
endif()
find_package(Threads REQUIRED)

add_library(ttplayer_runtime_dependencies INTERFACE)
target_link_libraries(ttplayer_runtime_dependencies INTERFACE SDL3::SDL3 ttplayer_data_deps ${FFMPEG_LIBRARIES} Threads::Threads)
if(TTPLAYER_STATIC_DEPS AND MINGW)
    target_link_options(ttplayer_runtime_dependencies INTERFACE -static -static-libgcc -static-libstdc++)
endif()
if(NOT TTPLAYER_BUNDLED_DEPS)
    target_include_directories(ttplayer_runtime_dependencies INTERFACE ${FFMPEG_INCLUDE_DIRS})
endif()
if(FFMPEG_LIBRARY_DIRS)
    target_link_directories(ttplayer_runtime_dependencies INTERFACE ${FFMPEG_LIBRARY_DIRS})
endif()

include(cmake/EmbeddedAssets.cmake)

add_executable(ttplayer
    src/main.cpp
    src/media/AudioEngine.cpp
    src/media/SpectrumAnalyzer.cpp
    src/media/Equalizer.cpp
    src/app/Data.cpp
    src/app/Skin.cpp
    src/app/Store.cpp
    src/app/Network.cpp
    src/app/Application.cpp
    src/app/MediaImportService.cpp
    src/app/LyricRequestService.cpp
    src/app/FileDialogService.cpp
    src/app/LocalizationCatalog.cpp
    src/app/LyricsRepository.cpp
    src/app/ApplicationJobs.cpp
    src/app/Ui.cpp
    src/app/PopupMenu.cpp
    src/app/SystemTray.cpp
    src/app/Events.cpp
    src/app/WindowLayout.cpp
    src/app/WindowDragSession.cpp
    src/app/WindowManagement.cpp
    src/app/Views.cpp
    src/app/ClassicViews.cpp
    src/app/Menus.cpp
    src/app/Dialogs.cpp
    src/app/PlaylistActions.cpp
    src/app/Localization.cpp
    src/app/ClassicVisual.cpp
)

target_include_directories(ttplayer PRIVATE src)
target_link_libraries(ttplayer PRIVATE ttplayer_runtime_dependencies SDL3_ttf::SDL3_ttf ttplayer_embedded_assets)
if(WIN32)
    enable_language(RC)
    target_sources(ttplayer PRIVATE src/app/WindowsTray.cpp)
    set(TTPLAYER_WINDOWS_ICON "${TTPLAYER_RESOURCE_ROOT}/ico/TTPlayer.ico")
    if(NOT EXISTS "${TTPLAYER_WINDOWS_ICON}")
        message(FATAL_ERROR "Missing Windows icon '${TTPLAYER_WINDOWS_ICON}'. Set -DTTPLAYER_RESOURCE_ROOT=<path-to-src/main/resources>.")
    endif()
    configure_file(cmake/WindowsResources.rc.in "${CMAKE_CURRENT_BINARY_DIR}/WindowsResources.rc" @ONLY)
    target_sources(ttplayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/WindowsResources.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/WindowsResources.rc" PROPERTIES OBJECT_DEPENDS "${TTPLAYER_WINDOWS_ICON}")
    target_link_libraries(ttplayer_runtime_dependencies INTERFACE user32 gdi32 shell32)
    set_target_properties(ttplayer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE TTPLAYER_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    find_library(TTPLAYER_SYSTEM_CURL NAMES curl PATHS "${TTPLAYER_MACOS_SDK}/usr/lib" NO_DEFAULT_PATH REQUIRED)
    find_library(TTPLAYER_SYSTEM_ICONV NAMES iconv PATHS "${TTPLAYER_MACOS_SDK}/usr/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(ttplayer_runtime_dependencies INTERFACE ${TTPLAYER_SYSTEM_CURL} ${TTPLAYER_SYSTEM_ICONV})
elseif(NOT TARGET CURL::libcurl)
    find_package(CURL REQUIRED)
endif()
if(NOT APPLE)
    target_link_libraries(ttplayer_runtime_dependencies INTERFACE CURL::libcurl)
endif()
if(NOT WIN32 AND NOT APPLE)
    find_package(Iconv REQUIRED)
    target_link_libraries(ttplayer_runtime_dependencies INTERFACE Iconv::Iconv)
endif()
if(APPLE)
    enable_language(OBJCXX)
    target_sources(ttplayer PRIVATE src/app/PlatformMac.mm src/app/PlatformMacIcon.mm src/app/PlatformMacIntegration.mm)
    set_target_properties(ttplayer PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED TRUE)
    set_source_files_properties(src/app/PlatformMac.mm src/app/PlatformMacIcon.mm src/app/PlatformMacIntegration.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(TTPLAYER_APPKIT AppKit REQUIRED)
    find_library(TTPLAYER_CORETEXT CoreText REQUIRED)
    find_library(TTPLAYER_CARBON Carbon REQUIRED)
    target_link_libraries(ttplayer_runtime_dependencies INTERFACE ${TTPLAYER_APPKIT} ${TTPLAYER_CORETEXT} ${TTPLAYER_CARBON})
    set_target_properties(ttplayer PROPERTIES
        MACOSX_BUNDLE TRUE OUTPUT_NAME TTPlayer
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(TTPLAYER_BUNDLED_DEPS)
    add_dependencies(ttplayer ttplayer_ffmpeg_build)
endif()

function(ttplayer_copy_windows_runtime target)
    if(NOT WIN32 OR TTPLAYER_STATIC_DEPS)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${target}>)
    if(MINGW)
        get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(runtime libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${runtime}
                OUTPUT_VARIABLE runtime_path OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT EXISTS "${runtime_path}")
                set(runtime_path "${compiler_directory}/${runtime}")
            endif()
            if(EXISTS "${runtime_path}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_path}" $<TARGET_FILE_DIR:${target}>)
            endif()
        endforeach()
    endif()
endfunction()

ttplayer_copy_windows_runtime(ttplayer)

if(MSVC)
    target_compile_options(ttplayer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
    target_compile_definitions(ttplayer PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ttplayer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
endif()

set(TTPLAYER_RUNTIME_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/bin-complete" CACHE PATH "Player output directory")
set_target_properties(ttplayer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
file(GENERATE OUTPUT "${TTPLAYER_RUNTIME_DIRECTORY}/deployment.json" CONTENT
"{\"staticDependencies\":$<BOOL:${TTPLAYER_STATIC_DEPS}>,\"embeddedAssets\":$<BOOL:${TTPLAYER_EMBED_ASSETS}>,\"buildDirectory\":\"${CMAKE_CURRENT_BINARY_DIR}\"}\n")

if(APPLE)
    set(TTPLAYER_ASSET_DIRECTORY "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Resources/assets")
    if(NOT TTPLAYER_STATIC_DEPS)
    add_custom_command(TARGET ttplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_SONAME_FILE:SDL3::SDL3>" "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Frameworks/")
    endif()
    add_custom_command(TARGET ttplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TTPLAYER_RESOURCE_ROOT}/ico/TTPlayer.icns" "$<TARGET_BUNDLE_CONTENT_DIR:ttplayer>/Resources/TTPlayer.icns")
else()
    set(TTPLAYER_ASSET_DIRECTORY "$<TARGET_FILE_DIR:ttplayer>/assets")
endif()
if(NOT TTPLAYER_EMBED_ASSETS)
add_custom_command(TARGET ttplayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TTPLAYER_ASSET_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TTPLAYER_RESOURCE_ROOT}/skin"
        "${TTPLAYER_ASSET_DIRECTORY}/skin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TTPLAYER_RESOURCE_ROOT}/ico"
        "${TTPLAYER_ASSET_DIRECTORY}/ico"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TTPLAYER_ASSET_DIRECTORY}/messages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TTPLAYER_RESOURCE_ROOT}/messages_zh_CN.properties"
        "${TTPLAYER_RESOURCE_ROOT}/messages.properties"
        "${TTPLAYER_RESOURCE_ROOT}/messages_ja.properties"
        "${TTPLAYER_RESOURCE_ROOT}/messages_ko.properties"
        "${TTPLAYER_RESOURCE_ROOT}/messages_de.properties"
        "${TTPLAYER_ASSET_DIRECTORY}/messages")
endif()

option(TTPLAYER_BUILD_TESTS "Build deterministic application service tests" OFF)
option(TTPLAYER_RUN_GUI_TESTS "Register popup tests requiring an interactive desktop" OFF)
if(TTPLAYER_BUILD_TESTS)
    enable_testing()
    add_executable(ttplayer_service_tests
        tests/ServicesTests.cpp
        src/app/Data.cpp
        src/app/Network.cpp
        src/app/MediaImportService.cpp
        src/app/LyricRequestService.cpp
        src/app/LyricsRepository.cpp
        src/app/FileDialogService.cpp
        src/app/LocalizationCatalog.cpp
        src/app/WindowLayout.cpp
        src/app/WindowDragSession.cpp)
    target_include_directories(ttplayer_service_tests PRIVATE src)
    target_link_libraries(ttplayer_service_tests PRIVATE ttplayer_runtime_dependencies)
    set_target_properties(ttplayer_service_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
    if(TTPLAYER_BUNDLED_DEPS)
        add_dependencies(ttplayer_service_tests ttplayer_ffmpeg_build)
    endif()
    if(MSVC)
        target_compile_options(ttplayer_service_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ttplayer_service_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    ttplayer_copy_windows_runtime(ttplayer_service_tests)
    add_test(NAME application_services COMMAND ttplayer_service_tests "${TTPLAYER_RESOURCE_ROOT}")
    set_tests_properties(application_services PROPERTIES TIMEOUT 30)
    add_executable(ttplayer_asset_tests tests/EmbeddedAssetsTests.cpp)
    target_link_libraries(ttplayer_asset_tests PRIVATE ttplayer_embedded_assets ttplayer_runtime_dependencies)
    set_target_properties(ttplayer_asset_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
    if(MSVC)
        target_compile_options(ttplayer_asset_tests PRIVATE /utf-8)
    endif()
    ttplayer_copy_windows_runtime(ttplayer_asset_tests)
    add_test(NAME embedded_assets COMMAND ttplayer_asset_tests "${TTPLAYER_RESOURCE_ROOT}")
    set_tests_properties(embedded_assets PROPERTIES TIMEOUT 30)
    if(TTPLAYER_EMBED_ASSETS)
        add_test(NAME embedded_assets_generation COMMAND ${CMAKE_COMMAND}
            "-DGENERATOR_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedAssets.cmake"
            "-DPACKER=$<TARGET_FILE:ttplayer_asset_packer>"
            "-DTHIRDPARTY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/embedded-assets-generation-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/EmbeddedAssetsGenerationTests.cmake")
        set_tests_properties(embedded_assets_generation PROPERTIES TIMEOUT 90)
    endif()
    if(WIN32 OR APPLE)
        get_target_property(ttplayer_startup_sources ttplayer SOURCES)
        list(FILTER ttplayer_startup_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
        add_executable(ttplayer_startup_tests EXCLUDE_FROM_ALL tests/StartupTests.cpp ${ttplayer_startup_sources})
        target_include_directories(ttplayer_startup_tests PRIVATE src)
        target_link_libraries(ttplayer_startup_tests PRIVATE ttplayer_runtime_dependencies SDL3_ttf::SDL3_ttf ttplayer_embedded_assets)
        set_target_properties(ttplayer_startup_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
        if(APPLE)
            set_target_properties(ttplayer_startup_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED TRUE)
        endif()
        if(MSVC)
            target_compile_options(ttplayer_startup_tests PRIVATE /utf-8)
        endif()
        ttplayer_copy_windows_runtime(ttplayer_startup_tests)
    endif()
    add_executable(ttplayer_popup_tests tests/PopupMenuTests.cpp src/app/PopupMenu.cpp src/app/Ui.cpp src/app/ClassicVisual.cpp src/app/Skin.cpp src/app/Data.cpp)
    if(APPLE)
        target_sources(ttplayer_popup_tests PRIVATE src/app/PlatformMacIcon.mm src/app/PlatformMacIntegration.mm)
        set_target_properties(ttplayer_popup_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED TRUE)
        target_link_libraries(ttplayer_popup_tests PRIVATE ${TTPLAYER_APPKIT})
    endif()
    target_include_directories(ttplayer_popup_tests PRIVATE src)
    target_link_libraries(ttplayer_popup_tests PRIVATE ttplayer_runtime_dependencies SDL3_ttf::SDL3_ttf)
    set_target_properties(ttplayer_popup_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
    ttplayer_copy_windows_runtime(ttplayer_popup_tests)
    if(APPLE)
        add_executable(ttplayer_mac_tests tests/MacPlatformTests.mm src/app/PlatformMacIcon.mm src/app/PlatformMacIntegration.mm
            src/app/PopupMenu.cpp src/app/Ui.cpp src/app/ClassicVisual.cpp src/app/Skin.cpp src/app/Data.cpp)
        set_source_files_properties(tests/MacPlatformTests.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_target_properties(ttplayer_mac_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED TRUE
            RUNTIME_OUTPUT_DIRECTORY "${TTPLAYER_RUNTIME_DIRECTORY}")
        target_include_directories(ttplayer_mac_tests PRIVATE src)
        target_link_libraries(ttplayer_mac_tests PRIVATE ttplayer_runtime_dependencies SDL3_ttf::SDL3_ttf)
        if(TTPLAYER_RUN_GUI_TESTS)
            add_test(NAME mac_platform COMMAND ttplayer_mac_tests "${TTPLAYER_RESOURCE_ROOT}")
            set_tests_properties(mac_platform PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        endif()
    endif()
    if(TTPLAYER_RUN_GUI_TESTS)
        add_test(NAME popup_menus COMMAND ttplayer_popup_tests "${TTPLAYER_RESOURCE_ROOT}" native)
        set_tests_properties(popup_menus PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endif()
endif()
