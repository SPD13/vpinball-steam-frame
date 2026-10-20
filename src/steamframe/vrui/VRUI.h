// license:GPLv3+

#pragma once

// Steam Frame Edition: the new VR UI, drawing the in-game UI (upstream's pages and ours) with its own look, instead of the classic one.
// It keeps upstream's model (pages and their items) and replaces the view: see docs/VR UI.md.

namespace VPX::VRUI
{

// Whether the new VR UI draws the in-game UI. The choice comes from the VPX_VRUI environment variable ('new' or 'classic', in VR and on the
// desktop, for tests), else from the Standalone.NewVRUI setting (in VR only). It is latched once per frame, and not while an item is being
// edited (slider dragged, text typed), so that a page never changes view in the middle of an interaction.
bool IsActive();

// Called by the new view each frame before it draws (also safe to call more than once per frame)
void LatchSwitch();

}
