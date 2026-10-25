# Steam Frame Edition: what the fork adds to the build of the desktop standalone application (macOS, Linux, windows-mingw).
# OpenXR (ENABLE_XR on Linux and windows-mingw) is upstream's.
# Included by the root CMakeLists.txt after make/CMakeLists_app.txt, so that upstream's build files only differ from ours by that include.
# VPX_STEAM_FRAME guards the code of the fork in upstream files (see steamframe/README.md).

if(NOT (PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "windows-mingw"))
   return()
endif()

target_sources(vpinball PRIVATE
   # Table library, lobby and launcher mode, scores and leaderboards
   src/steamframe/CabinetDisplays.h
   src/steamframe/CabinetDisplays.cpp
   src/steamframe/Launcher.h
   src/steamframe/Launcher.cpp
   src/steamframe/PlayerTableImage.cpp
   src/steamframe/PupPackOptions.h
   src/steamframe/PupPackOptions.cpp
   src/steamframe/PupPacks.h
   src/steamframe/PupPacks.cpp
   src/steamframe/RomAliases.h
   src/steamframe/RomAliases.cpp
   src/steamframe/ScoresPage.h
   src/steamframe/ScoresPage.cpp
   src/steamframe/ScoreStore.h
   src/steamframe/ScoreStore.cpp
   src/steamframe/ScoreTracker.h
   src/steamframe/ScoreTracker.cpp
   src/steamframe/TableLibrary.h
   src/steamframe/TableLibrary.cpp
   src/steamframe/TablePickerPage.h
   src/steamframe/TablePickerPage.cpp
   src/steamframe/WebServerLibrary.cpp

   # Web server to manage the tables from a browser (upstream only builds it for the mobile library)
   lib/src/WebServer.h
   lib/src/WebServer.cpp
   lib/src/ZipUtils.h
   lib/src/ZipUtils.cpp
   third-party/include/mongoose/mongoose.c
   third-party/include/mongoose/mongoose.h
)

target_compile_definitions(vpinball PRIVATE
   VPX_STEAM_FRAME
   VPX_TABLE_WEBSERVER
)

# The fork's code in the PinUP Player plugin is guarded by VPX_STEAM_FRAME too. The plugins are defined after this file
# (make/CMakeLists_plugins.txt), hence a call deferred to the end of the root folder
function(steamframe_plugin_definitions)
   if(TARGET PUPPlugin)
      target_compile_definitions(PUPPlugin PRIVATE VPX_STEAM_FRAME)
   endif()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL steamframe_plugin_definitions)

# VR UI: the new VR menu, and the VR panel, pointer and other parts of the classic one
include(${CMAKE_CURRENT_LIST_DIR}/vrui.cmake)

# VPX_STEAM_FRAME_HEADSET: the build that runs on the Steam Frame itself (Linux ARM64 with OpenXR), a standalone headset without a screen
# for a desktop window (the desktop mirror is forced off, see docs/Performance on the Steam Frame.md)
if(PLATFORM STREQUAL "linux" AND ARCH STREQUAL "aarch64" AND ENABLE_XR)
   target_compile_definitions(vpinball PRIVATE VPX_STEAM_FRAME_HEADSET)
endif()

# RAR and 7z archives, when steamframe/deps/libarchive.sh built libarchive (static, 7z needs liblzma), linked by full path so that
# a system one (or MSYS2's) is never used
if(PLATFORM STREQUAL "windows-mingw")
   set(STEAMFRAME_BUILD_LIBS_DIR "${CMAKE_SOURCE_DIR}/third-party/build-libs/windows-mingw-x64")
else()
   set(STEAMFRAME_BUILD_LIBS_DIR "${CMAKE_SOURCE_DIR}/third-party/build-libs/${PLATFORM}-${ARCH}")
endif()
set(STEAMFRAME_ARCHIVE_LIBS)
if(EXISTS "${STEAMFRAME_BUILD_LIBS_DIR}/libarchive.a")
   target_compile_definitions(vpinball PRIVATE VPX_ARCHIVE_SUPPORT LIBARCHIVE_STATIC)
   set(STEAMFRAME_ARCHIVE_LIBS "${STEAMFRAME_BUILD_LIBS_DIR}/libarchive.a" "${STEAMFRAME_BUILD_LIBS_DIR}/liblzma.a" z)
   if(PLATFORM STREQUAL "windows-mingw")
      list(APPEND STEAMFRAME_ARCHIVE_LIBS bcrypt) # Random names of the temporary files of libarchive
   endif()
endif()

# Each platform section of make/CMakeLists_app.txt uses one signature of target_link_libraries, which can't be mixed
if(PLATFORM STREQUAL "windows-mingw")
   target_link_libraries(vpinball
      zip64 # Web server
      ws2_32
      iphlpapi
      ${STEAMFRAME_ARCHIVE_LIBS}
   )
   if(POST_BUILD_COPY_EXT_LIBS)
      add_custom_command(TARGET vpinball POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy "${RUNTIME_LIBS_DIR}/libzip64.dll" "$<TARGET_FILE_DIR:vpinball>"
         COMMAND "${CMAKE_COMMAND}" -E copy "${RUNTIME_LIBS_DIR}/zlib1.dll" "$<TARGET_FILE_DIR:vpinball>"
      )
   endif()
elseif(PLATFORM STREQUAL "macos")
   target_link_libraries(vpinball PUBLIC zip ${STEAMFRAME_ARCHIVE_LIBS})
elseif(PLATFORM STREQUAL "linux")
   target_link_libraries(vpinball PUBLIC zip ${STEAMFRAME_ARCHIVE_LIBS})
   if(POST_BUILD_COPY_EXT_LIBS)
      add_custom_command(TARGET vpinball POST_BUILD
         COMMAND cp -a "${RUNTIME_LIBS_DIR}/libzip.so" "$<TARGET_FILE_DIR:vpinball>"
         COMMAND cp -a "${RUNTIME_LIBS_DIR}/libzip.so.*" "$<TARGET_FILE_DIR:vpinball>"
      )
   endif()
endif()
