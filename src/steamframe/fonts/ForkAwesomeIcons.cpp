// license:GPLv3+

#include "core/stdafx.h"
#include "ForkAwesomeIcons.h"

#include "ForkAwesome.h"
#include "IconsForkAwesome.h"

#include "imgui/imgui.h"

namespace VPX
{

void AddForkAwesomeIcons(float uiScale)
{
   // As LiveUI loaded them before upstream's move to Lucide: 13 px glyphs, merged into the previous font (their range, U+F000-F372, is apart from Lucide's)
   ImFontConfig config;
   config.MergeMode = true;
   config.PixelSnapH = true;
   config.GlyphMinAdvanceX = 13.0f * uiScale;
   static constexpr ImWchar ranges[] = { ICON_MIN_FK, ICON_MAX_16_FK, 0 };
   ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(fork_awesome_compressed_data, fork_awesome_compressed_size, 13.0f * uiScale, &config, ranges);
}

}
