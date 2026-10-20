// license:GPLv3+

#pragma once

namespace VPX
{

// Merges the Fork Awesome icons (ICON_FK_*, steamframe/fonts/IconsForkAwesome.h) into the font just added to ImGui's atlas, the base font of LiveUI.
// Upstream's LiveUI moved to a subset of the Lucide icons (2026-11-09), while the menus, the table picker and the scores of the Steam Frame Edition
// use Fork Awesome icons that the subset does not have. Fork Awesome is under the SIL Open Font License 1.1 (https://forkaweso.me).
void AddForkAwesomeIcons(float uiScale);

}
