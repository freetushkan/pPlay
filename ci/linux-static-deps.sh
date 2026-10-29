#!/usr/bin/env bash

set -euo pipefail

PREFIX="$(realpath -m "${1:-/opt/pplay-static}")"
CI_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK_DIR:-$PREFIX-build}"
JOBS="$(nproc)"
ARCH_LIB="/usr/lib/$(gcc -print-multiarch)"

mkdir -p "$PREFIX/lib/pkgconfig" "$WORK"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export PKG_CONFIG="pkg-config --static"
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC"
export LDFLAGS="-L$PREFIX/lib"
export CPPFLAGS="-I$PREFIX/include"

MESON_ARGS=(--prefix="$PREFIX" --libdir=lib --buildtype=release
            -Ddefault_library=static -Db_staticpic=true --wrap-mode=nodownload)

meson_setup() {
    local dir=$1
    shift
    if [ -d "$dir/meson-private" ]; then
        meson setup --reconfigure "$dir" "${MESON_ARGS[@]}" "$@"
    else
        meson setup "$dir" "${MESON_ARGS[@]}" "$@"
    fi
}
CMAKE=(cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
       -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PREFIX"
       -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

fetch() {
    local name=$1 url=$2 tag=$3
    if [ ! -d "$WORK/$name" ]; then
        git clone -q --depth 1 --branch "$tag" --recurse-submodules --shallow-submodules "$url" "$WORK/$name"
    fi
    cd "$WORK/$name"
}

step() { echo "::group::$1"; }
done_step() { echo "::endgroup::"; }

step "system static libraries"
for lib in z bz2 lzma png16 expat fontconfig freetype brotlidec brotlicommon uuid ssl crypto; do
    if [ -f "$ARCH_LIB/lib$lib.a" ]; then
        cp -v "$ARCH_LIB/lib$lib.a" "$PREFIX/lib/"
    else
        echo "warning: no $ARCH_LIB/lib$lib.a"
    fi
done
cp -v "$(gcc -print-file-name=libatomic.a)" "$PREFIX/lib/"
done_step

step "mbedtls"
fetch mbedtls https://github.com/Mbed-TLS/mbedtls.git v2.28.9
"${CMAKE[@]}" -B build -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
cmake --build build -j"$JOBS" && cmake --install build
done_step

step "curl"
fetch curl https://github.com/curl/curl.git curl-8_11_1
"${CMAKE[@]}" -B build -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
    -DENABLE_CURL_MANUAL=OFF -DCURL_USE_OPENSSL=ON -DCURL_DISABLE_LDAP=ON -DCURL_USE_LIBPSL=OFF \
    -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_ZSTD=OFF -DCURL_BROTLI=OFF -DCURL_USE_LIBSSH2=OFF \
    -DCURL_USE_GSSAPI=OFF -DCURL_CA_FALLBACK=ON
cmake --build build -j"$JOBS" && cmake --install build
done_step

step "SDL2"
fetch SDL https://github.com/libsdl-org/SDL.git release-2.30.11
"${CMAKE[@]}" -B build -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_STATIC_PIC=ON
cmake --build build -j"$JOBS" && cmake --install build
done_step

step "fribidi"
fetch fribidi https://github.com/fribidi/fribidi.git v1.0.16
meson_setup build -Ddocs=false -Dbin=false -Dtests=false
ninja -C build install
done_step

step "harfbuzz"
fetch harfbuzz https://github.com/harfbuzz/harfbuzz.git 10.1.0
meson_setup build -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dchafa=disabled \
    -Dfreetype=enabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled
ninja -C build install
done_step

step "libass"
fetch libass https://github.com/libass/libass.git 0.17.3
meson_setup build -Dfontconfig=enabled -Dlibunibreak=disabled -Drequire-system-font-provider=false \
    -Dtest=false -Dprofile=false
ninja -C build install
done_step

step "dav1d"
fetch dav1d https://github.com/videolan/dav1d.git 1.5.0
meson_setup build -Denable_tools=false -Denable_tests=false -Denable_examples=false
ninja -C build install
done_step

step "FFmpeg"
fetch ffmpeg https://github.com/FFmpeg/FFmpeg.git n7.1.1
./configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --pkg-config-flags=--static \
    --enable-static --disable-shared --enable-pic --disable-programs --disable-doc --disable-debug \
    --enable-gpl --enable-version3 --disable-autodetect \
    --enable-openssl --enable-libdav1d --enable-zlib --enable-bzlib --enable-lzma --enable-iconv \
    --extra-cflags="-I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib"
make -j"$JOBS" && make install
done_step

step "libplacebo"
fetch libplacebo https://github.com/haasn/libplacebo.git v7.349.0
meson_setup build -Dvulkan=disabled -Dopengl=enabled -Dglslang=disabled -Dshaderc=disabled \
    -Dlcms=disabled -Dlibdovi=disabled -Dxxhash=disabled -Dunwind=disabled -Ddemos=false -Dtests=false
ninja -C build install
done_step

step "mpv"
fetch mpv https://github.com/mpv-player/mpv.git v0.39.0
if ! patch -Rp1 -s -f --dry-run -i "$CI_DIR/mpv-mkv-noindex-seek.patch" >/dev/null 2>&1; then
    patch -Np1 -i "$CI_DIR/mpv-mkv-noindex-seek.patch"
fi
meson_setup build -Dlibmpv=true -Dcplayer=false -Dtests=false -Dbuild-date=false \
    -Dauto_features=disabled -Dgl=enabled -Dplain-gl=enabled -Diconv=enabled -Dzlib=enabled \
    -Dsdl2=enabled -Dsdl2-audio=enabled
ninja -C build install
done_step

echo "pPlay static dependencies installed in $PREFIX"
