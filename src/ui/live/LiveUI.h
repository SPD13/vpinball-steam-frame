// license:GPLv3+

#pragma once

#include "core/def.h"
#include "input/InputManager.h"
#include "renderer/Renderer.h"

#include "imgui/imgui.h"
#include "imgui_markdown/imgui_markdown.h"

#include "PerfUI.h"
#include "ingameui/InGameUI.h"
#include "EditorUI.h"
#include "NotificationOverlay.h"
#include "PlumbOverlay.h"
#include "BallControl.h"
#include "DebuggerUI.h"

#include <deque>

class LiveUI final : public UserMessageSink
{
public:
   LiveUI(RenderDevice* const rd);
   ~LiveUI();

   // UserMessageSink interface: non fatal messages are routed to the in-game notification
   // overlay, while fatal errors and confirmations use a (blocking) SDL message box.
   void Notify(MsgSeverity severity, const string &title, const string &message) override;
   bool Confirm(const string &title, const string &message, bool fallback) override;

   void Render3D(); // Called to contribute to 3D Scene
   void RenderUI(); // Called to render UI overlay

   bool IsOpened() const { return IsEditorUIOpened() || IsInGameUIOpened(); }
   void HideUI();

   void OpenEditorUI() { m_editorUI.Open(); }
   bool IsEditorUIOpened() const { return m_editorUI.IsOpened(); }
   bool IsEditorViewMode() const { return m_editorUI.IsOpened() && !m_editorUI.IsPreview(); }
   bool IsEditorBackdropViewMode() const { return m_editorUI.IsOpened() && m_editorUI.IsBackdropEditMode(); }

   void OpenInGameUI(const string& page = "homepage"s);
   bool IsInGameUIOpened() const { return m_inGameUI.IsOpened(); }

   void ToggleFPS() { m_perfUI.NextPerfMode(); }
   bool IsShowingFPSDetails() const { return m_perfUI.GetPerfMode() != PerfUI::PerfMode::PM_DISABLED; }
   
   void ShowTouchOverlay(bool show) { m_showTouchOverlay = show; }

   // Loading screen shown while switching tables (see Player::RenderLoadingFrame): a message with a spinner, drawn instead of the rest of the UI
   // while the text is not empty
   void SetLoadingText(const string& text) { m_loadingText = text; }
   bool IsLoadingScreenShown() const { return !m_loadingText.empty(); }

   unsigned int PushNotification(const string &message, const int lengthMs, const unsigned int reuseId = 0) { return m_notificationOverlay.PushNotification(message, lengthMs, reuseId); }

   // Messages that the player must acknowledge, shown one at a time in the in-game UI (which pauses the game). Lines starting with '!' are
   // shown as errors. ShowMessage can be called from any thread, ShowPendingMessage is called by the game loop.
   void ShowMessage(const string& title, const string& text);
   void ShowPendingMessage();
   const string& GetMessageTitle() const { return m_messageTitle; }
   const string& GetMessageText() const { return m_messageText; }

   // Ball Control
   BallControl m_ballControl;

   // Debugger (play/pause/step, ball control, script console)
   DebuggerUI m_debuggerUI;
   bool IsDebuggerOpened() const { return m_debuggerUI.IsOpened(); }

   // Editor UI
   VPX::EditorUI::EditorUI m_editorUI;

   // In Game UI
   VPX::InGameUI::InGameUI m_inGameUI;

   // Profiler display data
   PerfUI m_perfUI;

   // UI context (to be moved outside of LiveUI API)
   void SetMarkdownStartId(const unsigned int startId) { markdown_start_id = startId; }
   const ImGui::MarkdownConfig &GetMarkdownConfig() const { return markdown_config; }
   ImFont *GetOverlayFont() const { return m_overlayFont; }
   float GetDPI() const { return m_uiScale; }
   int GetUIOrientation() const { return m_rotate; }
   static ImGuiKey GetImGuiKeyFromSDLScancode(const SDL_Scancode sdlk);
   static void CenteredText(const string &text);

   void HandleSDLEvent(SDL_Event &e) const;

private:
   string m_loadingText;
   ImDrawList* RenderLoadingScreen(); // The draw list of the message when it goes into the loading layer of the VR device, to be left out of the UI

   std::mutex m_messageMutex;
   std::deque<std::pair<string, string>> m_pendingMessages; // Title and text
   string m_messageTitle, m_messageText; // Displayed by the 'misc/message' page

   void SetupImGuiStyle(const bool isEditor) const;
   
   void NewFrame();
   void AddMousePosEvent(bool isTouch, float x, float y) const;
   void UpdateScale();
   void UpdatePlayfieldWindowFrame();
   bool m_playfieldWindowFramed = false;

   vector<std::shared_ptr<MeshBuffer>> m_meshBuffers;
   #if defined(VPX_STEAM_FRAME)
   std::shared_ptr<MeshBuffer> m_perfPanelMeshBuffer; // Steam Frame Edition: the performance panel's (src/steamframe/PerfPanel.cpp), released with the player as the others
   std::shared_ptr<MeshBuffer> m_loadingLayerMeshBuffer; // Steam Frame Edition: the loading message's in VR (src/steamframe/XRBridge.cpp)
   int m_loadingLayerWindowSize[2] = { 0, 0 }; // Pixels of the loading layer's image covered by the message
   #endif
   std::shared_ptr<MeshBuffer> m_vrPointerRayMesh; // Ray from the VR controller to the pointed position on the UI panel

   // Touch UI overlay
   void UpdateTouchUI();
   bool m_showTouchOverlay;

   // Emulated plumb overlay
   PlumbOverlay m_plumbOverlay;

   // Notifications
   NotificationOverlay m_notificationOverlay;

   // MarkDown support
   ImGuiID markdown_start_id;
   static ImGui::MarkdownConfig markdown_config;
   static void MarkdownFormatCallback(const ImGui::MarkdownFormatInfo &markdownFormatInfo, bool start);
   static void MarkdownLinkCallback(ImGui::MarkdownLinkCallbackData data);
   static ImGui::MarkdownImageData MarkdownImageCallback(ImGui::MarkdownLinkCallbackData data);

   // UI Context
   Player   *m_player;
   InputManager *m_pininput;
   std::unique_ptr<Renderer>& m_renderer;

   // Rendering
   RenderDevice* const m_rd;
   int m_rotate = 0;
   float m_uiScale = 0.f;

   // VR controller used as a pointer (Steam Frame Edition, src/steamframe/vrui/Pointer.cpp)
   void UpdateVRPointer();
   void RenderVRPointerDot();
   void RenderVRPointerRay(bool onVRPanel, int depthSort);
   bool m_vrPointerVisible = false;
   bool m_vrPointerPressed = false;
   ImVec2 m_vrPointerPos;
   bool m_vrPointerClickSwallowed = false; // The trigger press switched from button to pointer navigation: it does not click
   bool m_vrPointerScrollBlocked = false; // The thumbstick was used for button navigation and is not back to its center yet
   bool m_vrInGameUIWasOpened = false; // To place the VR UI panel in front of the player each time the in-game UI opens
   ImFont *m_baseFont = nullptr;
   ImFont *m_overlayBoldFont = nullptr;
   ImFont *m_overlayFont = nullptr;
};

namespace plog
{
Record &operator<<(Record &record, const ImVec2 &pt);
}
