if(NOT IS_DIRECTORY "${RESOURCE_ROOT}/skin" OR NOT OUTPUT_DIRECTORY)
    message(FATAL_ERROR "Missing resource root or output directory")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(LOCK "${OUTPUT_DIRECTORY}/embedded-assets.lock" GUARD PROCESS TIMEOUT 300)
set(archive "${OUTPUT_DIRECTORY}/assets.zip")
set(temporary_archive "${archive}.tmp")
set(source "${OUTPUT_DIRECTORY}/EmbeddedAssetsData.cpp")
set(temporary_source "${source}.tmp")
execute_process(COMMAND "${PACKER}" "${RESOURCE_ROOT}" "${THIRDPARTY_ROOT}" "${temporary_archive}" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${temporary_archive}" identifier)
string(ASCII 34 TTPLAYER_QUOTE)
file(WRITE "${temporary_source}" [=[
#include <app/EmbeddedAssets.hpp>
namespace ttplayer::app::embedded {
namespace {
const unsigned char bytes[]{
]=])
file(SIZE "${temporary_archive}" size)
string(REPEAT "[0-9a-f][0-9a-f]" 16 row_pattern)
set(offset 0)
while(offset LESS size)
    file(READ "${temporary_archive}" bytes OFFSET ${offset} LIMIT 4096 HEX)
    string(REGEX REPLACE "(${row_pattern})" "\\1\n" rows "${bytes}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," initializer "${rows}")
    file(APPEND "${temporary_source}" "${initializer}\n")
    math(EXPR offset "${offset} + 4096")
endwhile()
file(APPEND "${temporary_source}"
[=[};
}
std::span<const unsigned char> archive() { return bytes; }
std::string_view identifier() { return ]=]
${TTPLAYER_QUOTE}${identifier}${TTPLAYER_QUOTE}
[=[; }
}
]=])
file(RENAME "${temporary_archive}" "${archive}")
file(RENAME "${temporary_source}" "${source}")
