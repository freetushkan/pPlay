#!/bin/bash -e
#
# pPlay for Android (arm64-v8a, armeabi-v7a, Android 6+): the native dependencies from
# mpv-android's build scripts (ffmpeg with MediaCodec, mpv, libass...), SDL2 and libpng added
# to their prefixes, libpplay.so per ABI, then the APK by Gradle (android/).
#
# usage: ci/android-build.sh <mpv-android> <SDL2> <libpng> <glm> (checkouts)
# out: pPlay-Android.apk, symbols/<abi>/*.so (unstripped)
#

ROOT="$(pwd)"
MA="$(realpath "$1")"
SDL="$(realpath "$2")"
PNG="$(realpath "$3")"
GLM="$(realpath "$4")"

# libcross2d's Android device: a Device (sdl2_renderer.cpp asks its display size)
sed -i 's/class ANDROIDDevice {/class ANDROIDDevice : public Device {/' \
	"$ROOT/libcross2d/include/cross2d/platforms/android/android_device.h"
# its window opaque (no alpha): a translucent drawing left the window alpha under 1, the
# system then blended it with black (fades, shades darker on some devices)
sed -i 's/SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);/SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);/' \
	"$ROOT/libcross2d/source/platforms/sdl2/sdl2_renderer.cpp"
# its file sizes: 64 bits (size_t is 32 bits on armv7, files over 4 GB)
sed -i 's/#include <string>/#include <cstdint>\n#include <string>/; s/const size_t &size = 0/uint64_t size = 0/; s/^\( *\)size_t size = 0;/\1uint64_t size = 0;/' \
	"$ROOT/libcross2d/include/cross2d/skeleton/io.h"
sed -i 's/file.size = (size_t) st.st_size/file.size = (uint64_t) st.st_size/' \
	"$ROOT/libcross2d/source/platforms/posix/posix_io.cpp"
# mpv's revision: the one of mpv-android's date (it builds master)
MPV_BEFORE="${MPV_BEFORE:-$(git -C "$MA" log -1 --format=%cI)}"

cd "$MA/buildscripts"
. ./include/depinfo.sh

# mbedtls thread safe (PSA, curl's entropy): TLS in pPlay's threads and mpv's at once
# (THREADING set: its programs not linked with -lpthread, bionic has it)
if ! grep -q MBEDTLS_THREADING_C scripts/mbedtls.sh; then
	sed -i 's/^make -j\$cores no_test/.\/scripts\/config.py set MBEDTLS_THREADING_C\n.\/scripts\/config.py set MBEDTLS_THREADING_PTHREAD\nexport THREADING=bionic\n&/' \
		scripts/mbedtls.sh
fi
grep -n "THREADING\|no_test" scripts/mbedtls.sh

# ffmpeg's MediaCodec decoder: a codec taking no input and giving no output (a Realtek one on
# some streams) looped on forever, mpv stuck with it; an error after 3 s instead (mpv: the
# software decoder after 3 errors)
mediacodecStuck() {
	if ! grep -q "Decoder stuck" "$1"; then
		perl -0pi -e 's|#include "libavutil/mem.h"\n|#include "libavutil/mem.h"\n#include "libavutil/time.h"\n|;
			s|(static int mediacodec_receive_frame\(AVCodecContext \*avctx, AVFrame \*frame\)\n\{\n(?:    .*\n)*?    ssize_t index;\n)|$1    int64_t stuck_since = 0;\n|;
			s|(                    ret == AVERROR\(EAGAIN\)\)\n)                    continue;\n|$1                \{\n                    if (!stuck_since)\n                        stuck_since = av_gettime_relative();\n                    if (av_gettime_relative() - stuck_since < 3000000)\n                        continue;\n                    av_log(avctx, AV_LOG_ERROR, "Decoder stuck (no input space, no output)\\n");\n                    return AVERROR_EXTERNAL;\n                \}\n|' "$1"
	fi
	[ "$(grep -c "Decoder stuck\|stuck_since = 0" "$1")" -eq 2 ]
}

# the SDK of the runner, mpv-android's NDK
mkdir -p sdk
if [[ -n "$ANDROID_HOME" && ! -e sdk/android-sdk-linux ]]; then
	ln -s "$ANDROID_HOME" sdk/android-sdk-linux
fi
IN_CI=1 WGET="wget -q" ./include/download-sdk.sh
NDK="$MA/buildscripts/sdk/android-ndk-$v_ndk"
NDK_TC="$NDK/build/cmake/android.toolchain.cmake"
NDK_BIN="$(echo "$NDK"/toolchains/llvm/prebuilt/*/bin)"
NDK_SYSROOT="$(echo "$NDK"/toolchains/llvm/prebuilt/*/sysroot)"

# <mpv-android arch>:<abi>:<triple>:<libcross2d toolchain name>
ARCHS="arm64:arm64-v8a:aarch64-linux-android:aarch64 armv7l:armeabi-v7a:arm-linux-androideabi:armv7a"

fetched=0
for entry in $ARCHS; do
	IFS=: read -r arch abi triple tc <<<"$entry"
	prefix="$MA/buildscripts/prefix/$arch"

	# ffmpeg, mpv and theirs (the cached prefix)
	if [ ! -f "$prefix/lib/libmpv.so" ]; then
		if [ $fetched -eq 0 ]; then
			IN_CI=1 WGET="wget -q" ./include/download-deps.sh
			git -C deps/mpv checkout -q "$(git -C deps/mpv rev-list -1 --before="$MPV_BEFORE" HEAD)"
			git -C deps/mpv log -1 --oneline
			mediacodecStuck deps/ffmpeg/libavcodec/mediacodecdec.c
			fetched=1
		fi
		./buildall.sh --arch "$arch" mpv
	fi

	common=(-DCMAKE_TOOLCHAIN_FILE="$NDK_TC" -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-23
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local)

	# SDL2 (libcross2d), shared: its Java part calls it
	if [ ! -f "$prefix/lib/libSDL2.so" ]; then
		rm -rf "/tmp/sdl-$arch"
		cmake -S "$SDL" -B "/tmp/sdl-$arch" "${common[@]}" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
		cmake --build "/tmp/sdl-$arch" -j"$(nproc)"
		DESTDIR="$prefix" cmake --install "/tmp/sdl-$arch"
	fi

	# the NDK's zlib, for pkg-config
	if [ ! -f "$prefix/lib/pkgconfig/zlib.pc" ]; then
		printf '%s\n' "Name: zlib" "Description: zlib (NDK)" "Version: 1.2.13" "Libs: -lz" "Cflags:" \
			>"$prefix/lib/pkgconfig/zlib.pc"
	fi

	# libpng (libcross2d), static
	if [ ! -f "$prefix/lib/libpng.a" ]; then
		rm -rf "/tmp/png-$arch"
		cmake -S "$PNG" -B "/tmp/png-$arch" "${common[@]}" -DPNG_SHARED=OFF -DPNG_STATIC=ON \
			-DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_HARDWARE_OPTIMIZATIONS=OFF
		cmake --build "/tmp/png-$arch" -j"$(nproc)"
		DESTDIR="$prefix" cmake --install "/tmp/png-$arch"
	fi

	# glm (libcross2d's renderer), headers only
	if [ ! -d "$prefix/include/glm" ]; then
		cp -r "$GLM/glm" "$prefix/include/"
	fi

	# libcross2d asks for /opt/pacbrew/android/cmake/android-<tc>.cmake: the NDK one there
	sudo mkdir -p /opt/pacbrew/android/cmake
	sudo tee "/opt/pacbrew/android/cmake/android-$tc.cmake" >/dev/null <<TOOLCHAIN
set(ANDROID_ABI $abi)
set(ANDROID_PLATFORM android-23)
set(ANDROID_STL c++_shared)
include($NDK_TC)
list(APPEND CMAKE_FIND_ROOT_PATH $prefix)
TOOLCHAIN

	# libpplay.so
	build="$ROOT/build-android-$abi"
	rm -rf "$build"
	PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$prefix" \
		cmake -S "$ROOT" -B "$build" -G "Unix Makefiles" \
		-DCMAKE_TOOLCHAIN_FILE="/opt/pacbrew/android/cmake/android-$tc.cmake" \
		-DPLATFORM_ANDROID=ON -DANDROID_ABI="$abi" \
		-DOPTION_BUILTIN_LIBCONFIG=ON -DOPTION_SMB2=OFF \
		-DANDROID_ASSETS_PATH="$ROOT/android/app/src/main/assets" \
		-DCMAKE_C_FLAGS="-isystem $prefix/include" -DCMAKE_CXX_FLAGS="-isystem $prefix/include" \
		-DCMAKE_BUILD_TYPE=Release
	PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$prefix" \
		make -C "$build" pplay -j"$(nproc)"

	# the libraries of the APK stripped (kept whole in symbols/ for the crash logs)
	jni="$ROOT/android/app/src/main/jniLibs/$abi"
	rm -rf "$jni" && mkdir -p "$jni" "$ROOT/symbols/$abi"
	for lib in "$build/libpplay.so" "$prefix"/lib/*.so; do
		name="$(basename "$lib")"
		cp -L "$lib" "$ROOT/symbols/$abi/$name"
		"$NDK_BIN/llvm-strip" --strip-unneeded -o "$jni/$name" "$ROOT/symbols/$abi/$name"
	done
	cp "$NDK_SYSROOT/usr/lib/$triple/libc++_shared.so" "$jni/"

	# all the needed libraries there, or the system ones
	system=" libc.so libm.so libdl.so liblog.so libandroid.so libGLESv1_CM.so libGLESv2.so libGLESv3.so libEGL.so "
	system+="libOpenSLES.so libmediandk.so libz.so libjnigraphics.so libaaudio.so libvulkan.so libcamera2ndk.so "
	missing=0
	for lib in "$jni"/*.so; do
		for needed in $("$NDK_BIN/llvm-readelf" -d "$lib" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
			if [[ ! -f "$jni/$needed" && "$system" != *" $needed "* ]]; then
				echo "Error: $(basename "$lib") needs $needed (not packaged)" >&2
				missing=1
			fi
		done
	done
	[ $missing -eq 0 ]
	ls -l "$jni"
done

# SDL's Java part, its version
cp -r "$SDL/android-project/app/src/main/java/org" "$ROOT/android/app/src/main/java/"

# the APK (release: signed by ANDROID_KEYSTORE if set, else the debug key)
cd "$ROOT"
VERSION_MAJOR=$(sed -n 's/^set(VERSION_MAJOR "\(.*\)")/\1/p' CMakeLists.txt)
VERSION_MINOR=$(sed -n 's/^set(VERSION_MINOR "\(.*\)")/\1/p' CMakeLists.txt)
export PPLAY_VERSION_CODE=$((VERSION_MAJOR * 100 + VERSION_MINOR))
export PPLAY_VERSION_NAME="$VERSION_MAJOR.$VERSION_MINOR${GITHUB_SHA:+-${GITHUB_SHA:0:8}}"
(cd android && ./gradlew --no-daemon assembleRelease)
cp android/app/build/outputs/apk/release/app-release.apk pPlay-Android.apk
ls -l pPlay-Android.apk
