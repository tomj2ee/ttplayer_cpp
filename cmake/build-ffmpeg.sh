#!/usr/bin/env bash
set -euo pipefail

operation="$1"
source_dir="$2"
prefix="$3"
platform="${4:-native}"
architecture="${5:-}"
compiler="${6:-cc}"
cxx_compiler="${7:-c++}"
make_command=make
if [ "$operation" = configure ]; then
    sdk="${8:-}"
    deployment="${9:-12.0}"
else
    make_command="${8:-make}"
fi
if [ "${OS:-}" = Windows_NT ]; then
    export PATH="/usr/bin:$PATH"
    compiler="$(cygpath -u "$compiler")"
    cxx_compiler="$(cygpath -u "$cxx_compiler")"
    export PATH="${compiler%/*}:$PATH"
fi
if command -v cygpath >/dev/null 2>&1; then
    source_dir="$(cygpath -u "$source_dir")"
    prefix="$(cygpath -u "$prefix")"
    make_command="$(cygpath -u "$make_command")"
fi

case "$operation" in
    configure) ;;
    build) exec "$make_command" -j4 ;;
    install) exec "$make_command" install ;;
    *) exit 2 ;;
esac

arguments=(
    "--prefix=$prefix"
    --disable-programs
    --disable-doc
    --disable-debug
    --disable-shared
    --enable-static
    --disable-autodetect
    --disable-x86asm
    --enable-network
    --disable-avdevice
    --disable-avfilter
    --disable-swscale
    --disable-encoders
    --disable-muxers
    --disable-hwaccels
    --disable-devices
    --disable-everything
    --enable-avformat
    --enable-avcodec
    --enable-swresample
    --enable-protocol=file,http,https,tcp,tls
    --enable-demuxer=mp3,flac,wav,ogg,aac,mov,ape,wv,aiff,asf
    --enable-decoder=mp3,mp3float,flac,vorbis,aac,alac,ape,wavpack,opus,wmav1,wmav2,wmapro,wmalossless,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f64le,pcm_u8
    --enable-parser=mpegaudio,flac,vorbis,aac,opus
)
if [ "${OS:-}" = Windows_NT ]; then
    arguments+=(--target-os=mingw32 --arch=x86_64 "--cc=$compiler" "--cxx=$cxx_compiler" --enable-schannel)
elif [ "$platform" = darwin ]; then
    case "$architecture" in
        arm64|aarch64) ffmpeg_arch=aarch64; architecture=arm64 ;;
        x86_64) ffmpeg_arch=x86_64 ;;
        *) printf 'Unsupported macOS architecture: %s\n' "$architecture" >&2; exit 2 ;;
    esac
    flags="-arch $architecture -mmacosx-version-min=${deployment:-12.0}"
    if [ -n "$sdk" ]; then flags="$flags -isysroot $sdk"; fi
    arguments+=(--target-os=darwin "--arch=$ffmpeg_arch" --enable-cross-compile
        "--cc=$compiler" "--cxx=$cxx_compiler" "--extra-cflags=$flags" "--extra-ldflags=$flags" --enable-securetransport)
fi
"$BASH" "$source_dir/configure" "${arguments[@]}"
