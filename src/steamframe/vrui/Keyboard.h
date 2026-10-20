// license:GPLv3+

#pragma once

#include "imgui/imgui.h"

#include <functional>

// Keyboard of the new VR UI, for VR where there is no keyboard. It edits a text of its own (not an ImGui text field, which would lose the
// focus when a key is clicked) and reports each change, so the edited value or list updates while typing. The shell draws it at the bottom
// of the menu while it is opened.
namespace VPX::VRUI::Keyboard
{

// Whether text is typed with this keyboard: in VR, or on the desktop with VPX_VRUI_KEYBOARD=1 (to test it)
bool IsUsed();

// Starts editing a text. 'owner' identifies what is edited (to know if a field is the one being edited, and to close the keyboard when it
// is no longer shown), 'onChange' gets the text after each key, 'onDone' is called when Done is pressed.
void Open(const string& owner, const string& title, const string& text, size_t maxLength, const std::function<void(const string&)>& onChange,
   const std::function<void()>& onDone = nullptr);
void Close();
bool IsOpen();
bool IsEditing(const string& owner);

// The field being edited is shown: keeps the keyboard opened (it closes at the end of a frame where its field was not shown)
void KeepOpen(const string& owner);
void EndFrame();

// Types as if the keys were clicked (screenshot self-test)
void TypeForTest(const string& text);

float GetHeight(float width);
void Render(const ImVec2& pos, float width);

}
