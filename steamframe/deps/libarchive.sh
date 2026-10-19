# Steam Frame Edition: liblzma (xz) and libarchive, to import tables from RAR and 7z archives (static, and only what is needed to read them).
# Sourced by platforms/<platform>/external.sh from external/<platform>/<build type>, with the platform folder name as argument:
#    source ../../../steamframe/deps/libarchive.sh linux-aarch64
# The static libraries go to third-party/build-libs/<platform>, where steamframe/steamframe.cmake links them by full path (never a system one).

LIBLZMA_SHA=d3e650e63c110e830fd5391e7f8b45df0b91d3da # xz v5.8.4, for the 7z archives
LIBARCHIVE_SHA=27cbc7827172698143e440801fc0ba39ccb4f1f5 # v3.8.9

STEAMFRAME_PLATFORM="$1"
echo "  LIBLZMA_SHA: ${LIBLZMA_SHA}"
echo "  LIBARCHIVE_SHA: ${LIBARCHIVE_SHA}"

LIBARCHIVE_EXPECTED_SHA="${LIBLZMA_SHA}-${LIBARCHIVE_SHA}"
LIBARCHIVE_FOUND_SHA="$([ -f libarchive/cache.txt ] && cat libarchive/cache.txt || echo "")"

if [ "${LIBARCHIVE_EXPECTED_SHA}" != "${LIBARCHIVE_FOUND_SHA}" ]; then
   echo "Building libarchive. Expected: ${LIBARCHIVE_EXPECTED_SHA}, Found: ${LIBARCHIVE_FOUND_SHA}"

   rm -rf libarchive
   mkdir libarchive
   cd libarchive

   curl -sL https://github.com/tukaani-project/xz/archive/${LIBLZMA_SHA}.tar.gz -o xz-${LIBLZMA_SHA}.tar.gz
   tar xzf xz-${LIBLZMA_SHA}.tar.gz
   mv xz-${LIBLZMA_SHA} xz
   cmake -S xz -B xz/build \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DXZ_NLS=OFF \
      -DXZ_DOC=OFF \
      -DXZ_TOOL_XZ=OFF \
      -DXZ_TOOL_XZDEC=OFF \
      -DXZ_TOOL_LZMADEC=OFF \
      -DXZ_TOOL_LZMAINFO=OFF \
      -DXZ_TOOL_SCRIPTS=OFF \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
   cmake --build xz/build --target liblzma -- -j${NUM_PROCS}

   curl -sL https://github.com/libarchive/libarchive/archive/${LIBARCHIVE_SHA}.tar.gz -o libarchive-${LIBARCHIVE_SHA}.tar.gz
   tar xzf libarchive-${LIBARCHIVE_SHA}.tar.gz
   mv libarchive-${LIBARCHIVE_SHA} libarchive
   cmake -S libarchive -B libarchive/build \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_C_FLAGS="-DLZMA_API_STATIC" \
      -DENABLE_LZMA=ON \
      -DLIBLZMA_INCLUDE_DIR="$(pwd)/xz/src/liblzma/api" \
      -DLIBLZMA_LIBRARY="$(pwd)/xz/build/liblzma.a" \
      -DENABLE_ZLIB=ON \
      -DENABLE_BZip2=OFF \
      -DENABLE_ZSTD=OFF \
      -DENABLE_LZ4=OFF \
      -DENABLE_LZO=OFF \
      -DENABLE_LIBB2=OFF \
      -DENABLE_OPENSSL=OFF \
      -DENABLE_MBEDTLS=OFF \
      -DENABLE_NETTLE=OFF \
      -DENABLE_CNG=OFF \
      -DENABLE_LIBXML2=OFF \
      -DENABLE_EXPAT=OFF \
      -DENABLE_WIN32_XMLLITE=OFF \
      -DENABLE_PCREPOSIX=OFF \
      -DENABLE_PCRE2POSIX=OFF \
      -DENABLE_ICONV=OFF \
      -DENABLE_ACL=OFF \
      -DENABLE_XATTR=OFF \
      -DENABLE_TAR=OFF \
      -DENABLE_CPIO=OFF \
      -DENABLE_CAT=OFF \
      -DENABLE_UNZIP=OFF \
      -DENABLE_TEST=OFF \
      -DENABLE_INSTALL=OFF \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
   cmake --build libarchive/build --target archive_static -- -j${NUM_PROCS}

   echo "$LIBARCHIVE_EXPECTED_SHA" > cache.txt

   cd ..
fi

mkdir -p ../../../third-party/build-libs/${STEAMFRAME_PLATFORM}
# The MinGW build names it libarchive_static.a
if [ -f libarchive/libarchive/build/libarchive/libarchive_static.a ]; then
   cp libarchive/libarchive/build/libarchive/libarchive_static.a ../../../third-party/build-libs/${STEAMFRAME_PLATFORM}/libarchive.a
else
   cp libarchive/libarchive/build/libarchive/libarchive.a ../../../third-party/build-libs/${STEAMFRAME_PLATFORM}
fi
cp libarchive/xz/build/liblzma.a ../../../third-party/build-libs/${STEAMFRAME_PLATFORM}
cp libarchive/libarchive/libarchive/archive.h ../../../third-party/include
cp libarchive/libarchive/libarchive/archive_entry.h ../../../third-party/include
