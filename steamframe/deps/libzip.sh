# Steam Frame Edition: libzip, for the web server of the desktop builds (zip uploads, lib/src/ZipUtils.cpp). Upstream only builds it
# for Android now, where its web server lives in the mobile library.
# Sourced by platforms/<platform>/external.sh from external/<platform>/<build type>, with the platform folder name as argument:
#    source ../../../steamframe/deps/libzip.sh linux-aarch64
# Builds the shared library and copies it to third-party/runtime-libs/<platform> (import library to build-libs on windows-mingw),
# its headers to third-party/include.

STEAMFRAME_PLATFORM="$1"
LIBZIP_SHA="${LIBZIP_SHA:-6f8a0cdd24a0dc6cce9dac4a7679da784ab124ea}" # Upstream's, from platforms/config.sh while it keeps it for Android
echo "  LIBZIP_SHA: ${LIBZIP_SHA}"

LIBZIP_EXPECTED_SHA="${LIBZIP_SHA}"
LIBZIP_CMAKE_ARGS=()
case "$STEAMFRAME_PLATFORM" in
   macos-arm64) LIBZIP_CMAKE_ARGS=(-DCMAKE_OSX_ARCHITECTURES=arm64) ;;
   macos-x64) LIBZIP_CMAKE_ARGS=(-DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0) ;;
   windows-mingw-x64) LIBZIP_EXPECTED_SHA="${LIBZIP_SHA}-002" ;;
esac
LIBZIP_FOUND_SHA="$([ -f libzip/cache.txt ] && cat libzip/cache.txt || echo "")"

if [ "${LIBZIP_EXPECTED_SHA}" != "${LIBZIP_FOUND_SHA}" ]; then
   echo "Building libzip. Expected: ${LIBZIP_EXPECTED_SHA}, Found: ${LIBZIP_FOUND_SHA}"

   rm -rf libzip
   mkdir libzip
   cd libzip

   curl -sL https://github.com/nih-at/libzip/archive/${LIBZIP_SHA}.tar.gz -o libzip-${LIBZIP_SHA}.tar.gz
   tar xzf libzip-${LIBZIP_SHA}.tar.gz
   mv libzip-${LIBZIP_SHA} libzip
   cd libzip
   if [ "$STEAMFRAME_PLATFORM" = windows-mingw-x64 ]; then
      sed -i.bak 's/\(set_target_properties(zip PROPERTIES\)/\1 OUTPUT_NAME "zip64"/' lib/CMakeLists.txt
   fi
   # Deflate only, otherwise it links against zstd, bzip2 and lzma libraries of the build machine that are not shipped
   cmake \
      -DBUILD_SHARED_LIBS=ON \
      -DENABLE_ZSTD=OFF \
      -DENABLE_BZIP2=OFF \
      -DENABLE_LZMA=OFF \
      -DBUILD_TOOLS=OFF \
      -DBUILD_REGRESS=OFF \
      -DBUILD_OSSFUZZ=OFF \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_DOC=OFF \
      "${LIBZIP_CMAKE_ARGS[@]}" \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -B build
   cmake --build build -- -j${NUM_PROCS}
   cd ..

   echo "$LIBZIP_EXPECTED_SHA" > cache.txt

   cd ..
fi

case "$STEAMFRAME_PLATFORM" in
   linux-*) cp -a libzip/libzip/build/lib/libzip.{so,so.*} ../../../third-party/runtime-libs/${STEAMFRAME_PLATFORM} ;;
   macos-*) cp -a libzip/libzip/build/lib/libzip.{dylib,*.dylib} ../../../third-party/runtime-libs/${STEAMFRAME_PLATFORM} ;;
   windows-mingw-x64)
      cp libzip/libzip/build/lib/libzip64.dll ../../../third-party/runtime-libs/windows-mingw-x64
      cp libzip/libzip/build/lib/libzip64.dll.a ../../../third-party/build-libs/windows-mingw-x64
      ;;
esac
cp libzip/libzip/build/zipconf.h ../../../third-party/include
cp libzip/libzip/lib/zip.h ../../../third-party/include
