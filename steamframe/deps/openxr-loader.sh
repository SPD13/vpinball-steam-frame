# Steam Frame Edition: the OpenXR loader for the Linux builds with ENABLE_XR (OPENXR_SHA of platforms/config.sh, as the Android build).
# Sourced by platforms/linux-*/external.sh from external/<platform>/<build type>, with the platform folder name as argument:
#    source ../../../steamframe/deps/openxr-loader.sh linux-aarch64
# Upstream builds it too since 'linux: add OpenXR support for BGFX (#3837)': drop this script once our branch is based on it.

STEAMFRAME_PLATFORM="$1"
echo "  OPENXR_SHA: ${OPENXR_SHA}"

OPENXR_EXPECTED_SHA="${OPENXR_SHA}_001"
OPENXR_FOUND_SHA="$([ -f openxr/cache.txt ] && cat openxr/cache.txt || echo "")"

if [ "${OPENXR_EXPECTED_SHA}" != "${OPENXR_FOUND_SHA}" ]; then
   echo "Building OpenXR. Expected: ${OPENXR_EXPECTED_SHA}, Found: ${OPENXR_FOUND_SHA}"

   rm -rf openxr
   mkdir openxr
   cd openxr

   curl -sL https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/${OPENXR_SHA}.tar.gz -o OpenXR-SDK-Source-${OPENXR_SHA}.tar.gz
   tar xzf OpenXR-SDK-Source-${OPENXR_SHA}.tar.gz
   mv OpenXR-SDK-Source-${OPENXR_SHA} openxr
   cd openxr
   cmake \
      -DDYNAMIC_LOADER=ON \
      -DBUILD_TESTS=OFF \
      -DBUILD_API_LAYERS=OFF \
      -DBUILD_CONFORMANCE_TESTS=OFF \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -B build
   cmake --build build --target openxr_loader -- -j${NUM_PROCS}
   cd ..

   echo "$OPENXR_EXPECTED_SHA" > cache.txt

   cd ..
fi

mkdir -p ../../../third-party/runtime-libs/${STEAMFRAME_PLATFORM}
cp -a openxr/openxr/build/src/loader/libopenxr_loader.so* ../../../third-party/runtime-libs/${STEAMFRAME_PLATFORM}
mkdir -p ../../../third-party/include/openxr
cp openxr/openxr/build/include/openxr/*.h ../../../third-party/include/openxr
