# Steam Frame Edition: the VR UI (see docs/VR UI.md). Included by steamframe/steamframe.cmake.
# The new VR menu and the parts of the classic one that the fork moved out of upstream files (VR panel, pointer, tiles).

set(VRUI_SOURCES
   src/steamframe/vrui/VRUI.h
   src/steamframe/vrui/VRUI.cpp
   src/steamframe/vrui/ComposedPage.h
   src/steamframe/vrui/ComposedPage.cpp
   src/steamframe/vrui/Keyboard.h
   src/steamframe/vrui/Keyboard.cpp
   src/steamframe/vrui/MainMenuPage.h
   src/steamframe/vrui/MainMenuPage.cpp
   src/steamframe/vrui/PageAdapter.cpp
   src/steamframe/vrui/PageView.h
   src/steamframe/vrui/Screens.h
   src/steamframe/vrui/Screens.cpp
   src/steamframe/vrui/SelfTest.h
   src/steamframe/vrui/SelfTest.cpp
   src/steamframe/vrui/Shell.h
   src/steamframe/vrui/Shell.cpp
   src/steamframe/vrui/Theme.h
   src/steamframe/vrui/Theme.cpp
   src/steamframe/vrui/Tripwires.cpp
   src/steamframe/vrui/Widgets.h
   src/steamframe/vrui/Widgets.cpp

   # Moved out of upstream files, used by the classic UI too
   src/steamframe/vrui/LegacyTiles.cpp
   src/steamframe/vrui/Panel.cpp
   src/steamframe/vrui/Pointer.cpp
)

target_sources(vpinball PRIVATE ${VRUI_SOURCES})

# A new upstream item or property type must break the build of the view, not be silently skipped (see Tripwires.cpp)
if(NOT MSVC)
   set_source_files_properties(${VRUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-Werror=switch")
endif()
