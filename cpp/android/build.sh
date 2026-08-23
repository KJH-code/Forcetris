#!/bin/bash
# Build the Forcetris APK without Gradle: SDL2 and the game cross-compiled
# with the NDK's CMake toolchain, the Java glue compiled against
# android.jar, dexed with d8, packaged with aapt, aligned and signed.
#
# Required environment:
#   ANDROID_SDK   - with platforms/android-34/android.jar
#   ANDROID_NDK   - an r26+ NDK
#   SDL2_SRC      - an SDL2 2.30.x source tree
#   R8_JAR        - r8.jar (for its D8 dexer)
# Tools on PATH: cmake, javac (17+), java, aapt, zipalign, apksigner, zip.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${BUILD_DIR:-$ROOT/cpp/build-android}"
ABI="${ANDROID_ABI:-arm64-v8a}"
API="${ANDROID_API:-26}"
JAR="$ANDROID_SDK/platforms/android-34/android.jar"
TOOLCHAIN="$ANDROID_NDK/build/cmake/android.toolchain.cmake"

mkdir -p "$OUT"

# --- 1. SDL2 for Android. -------------------------------------------------
if [ ! -f "$OUT/sdl2/lib/libSDL2.so" ]; then
	cmake -S "$SDL2_SRC" -B "$OUT/sdl2-build" \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
		-DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
		-DCMAKE_BUILD_TYPE=Release \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
		-DCMAKE_INSTALL_PREFIX="$OUT/sdl2" > "$OUT/sdl2-configure.log"
	cmake --build "$OUT/sdl2-build" -j > "$OUT/sdl2-build.log"
	cmake --install "$OUT/sdl2-build" >> "$OUT/sdl2-build.log"
fi

# --- 2. The game as libmain.so. -------------------------------------------
cmake -S "$ROOT/cpp" -B "$OUT/game-build" \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
	-DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
	-DCMAKE_BUILD_TYPE=Release \
	-DSDL2_DIR="$OUT/sdl2/lib/cmake/SDL2" > "$OUT/game-configure.log"
cmake --build "$OUT/game-build" -j --target main > "$OUT/game-build.log"

# --- 3. The Java glue. ----------------------------------------------------
rm -rf "$OUT/classes" "$OUT/dex" "$OUT/stage"
mkdir -p "$OUT/classes" "$OUT/dex"
javac --release 8 -classpath "$JAR" -d "$OUT/classes" \
	"$SDL2_SRC"/android-project/app/src/main/java/org/libsdl/app/*.java \
	"$HERE"/src/net/kjh/forcetris/*.java 2> "$OUT/javac.log" || {
	cat "$OUT/javac.log"; exit 1; }
java -cp "$R8_JAR" com.android.tools.r8.D8 --release --lib "$JAR" \
	--output "$OUT/dex" $(find "$OUT/classes" -name '*.class')

# --- 4. Stage the APK contents. -------------------------------------------
STAGE="$OUT/stage"
mkdir -p "$STAGE/lib/$ABI" "$STAGE/assets"
cp "$OUT/dex/classes.dex" "$STAGE/"
cp "$OUT/game-build/libmain.so" "$STAGE/lib/$ABI/"
cp "$OUT/sdl2/lib/libSDL2.so" "$STAGE/lib/$ABI/"
"$ANDROID_NDK"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip \
	"$STAGE/lib/$ABI/libmain.so" "$STAGE/lib/$ABI/libSDL2.so"
cp "$ANDROID_NDK"/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so \
	"$STAGE/lib/$ABI/"
cp -r "$ROOT/sound" "$ROOT/music" "$STAGE/assets/"
(cd "$STAGE/assets" && find sound music -type f | sort > assets.txt)

# --- 5. Package, align, sign. ---------------------------------------------
aapt package -f -M "$HERE/AndroidManifest.xml" -I "$JAR" \
	-F "$OUT/forcetris-unaligned.apk"
(cd "$STAGE" && zip -qr "$OUT/forcetris-unaligned.apk" \
	classes.dex lib assets)
zipalign -f 4 "$OUT/forcetris-unaligned.apk" "$OUT/forcetris-unsigned.apk"
if [ ! -f "$OUT/debug.keystore" ]; then
	keytool -genkeypair -keystore "$OUT/debug.keystore" -alias forcetris \
		-storepass forcetris -keypass forcetris -keyalg RSA -keysize 2048 \
		-validity 10000 -dname "CN=Forcetris" > /dev/null 2>&1
fi
apksigner sign --ks "$OUT/debug.keystore" --ks-pass pass:forcetris \
	--key-pass pass:forcetris --out "$OUT/Forcetris.apk" \
	"$OUT/forcetris-unsigned.apk"
apksigner verify "$OUT/Forcetris.apk"
echo "built $OUT/Forcetris.apk"
