// license:GPLv3+

#pragma once

// Steam Frame Edition: the new VR UI, drawing the in-game UI (upstream's pages and ours) with its own look, instead of the classic one.
// It keeps upstream's model (pages and their items) and replaces the view: see docs/VR UI.md.

class Player;
class NotificationOverlay;
struct ImFont;

namespace VPX::VRUI
{

// Whether the new VR UI draws the in-game UI, in VR and on the desktop. The choice comes from the VPX_VRUI environment variable ('new' or
// 'classic', for tests), else from the Standalone.NewVRUI setting (VPinballX.ini, on by default). It is latched once per frame, and not while an item is being
// edited (slider dragged, text typed), so that a page never changes view in the middle of an interaction.
bool IsActive();

// Called by the new view each frame before it draws (also safe to call more than once per frame)
void LatchSwitch();

// Called when a player starts (see Launcher::OnPlayerCreated): the routes of upstream's home page and of the table picker give the main menu
// of the new view when it is active, and the classic pages otherwise
void OnPlayerCreated(Player* player, bool isLobby);

// In VR, draws the notifications with the size of the menu text: in the middle of the view when they appear, then staying there in the room,
// while the in-game UI is closed, and stacked above the menu window ('aboveY', see LiveUI::Update) while it is open. Upstream's look, the small
// desktop overlay font a quarter of the panel height from its top, put them above the eyes and too small to read in the headset.
void UpdateNotifications(Player* player, NotificationOverlay& overlay, ImFont* font, float aboveY);

}
