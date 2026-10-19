// license:GPLv3+

// Steam Frame Edition: the images of the tables of the table library, captured by the player (members of Player, declared in core/player.h)

#include "core/stdafx.h"
#include "core/player.h"

#include "core/VPApp.h"
#include "parts/PartGroup.h"
#include "parts/pintable.h"
#include "parts/primitive.h"
#include "renderer/Renderer.h"
#include "renderer/VRDevice.h"
#include "steamframe/Launcher.h"
#include "steamframe/TableLibrary.h"
#include "ui/live/LiveUI.h"

#ifdef ENABLE_BGFX


// Tables of the table library which have no image get a screenshot of the playfield when closed (the mobile launchers do the same)
void Player::CaptureTableImageBeforeClosing()
{
   if (m_tableImageCaptureStarted)
   {
      if (m_tableImageCaptureDone)
         CloseAfterLoadingScreen();
      return;
   }

   std::filesystem::path imagePath = m_ptable->m_filename;
   imagePath.replace_extension(".jpg");
   std::filesystem::path pngPath = m_ptable->m_filename;
   pngPath.replace_extension(".png");
   if (!CanReplaceTableImage() || FileExists(imagePath) || FileExists(pngPath))
   {
      m_tableImageCaptureStarted = true;
      m_tableImageCaptureDone = true;
      CloseAfterLoadingScreen();
      return;
   }

   // The in-game menu slides out when closed: wait for it to be gone, so that it is not captured
   if (!m_tableImageMenuHidden)
   {
      m_liveUI->HideUI();
      m_tableImageMenuHidden = true;
   }
   if (!IsInGameUIClosed())
      return;

   m_tableImageCaptureStarted = true;
   CaptureTableImage(imagePath,
      [this, imagePath](bool success)
      {
         if (success)
            PLOGI << "Table image saved: " << imagePath;
         else
            PLOGE << "Failed to save table image: " << imagePath;
         m_tableImageCaptureDone = true;
      });
}

// When another table or the lobby follows, its loading screen is shown before closing (after the image capture, which must not show it): it is then what
// the display shows while this table closes and the next one starts loading
void Player::CloseAfterLoadingScreen()
{
   if (m_closingLoadingFrame == 0)
   {
      if (const string text = g_launcher->GetNextLoadingText(m_isLobby); !text.empty())
      {
         m_liveUI->SetLoadingText(text);
         #ifdef ENABLE_XR
         if (m_vrDevice)
            m_vrDevice->RecenterUIPanel(); // In front of the player, who may have turned since the menu was opened
         #endif
         m_closingLoadingFrame = m_overall_frames + 1;
         m_closingLoadingTime = std::chrono::steady_clock::now();
         return;
      }
   }
   // Frames are prepared ahead of their display: a few more let the first one with the loading screen be displayed (a time limit in case no frame
   // is requested, for example in VR while the headset is not worn)
   else if (m_overall_frames < m_closingLoadingFrame + 3 && std::chrono::steady_clock::now() - m_closingLoadingTime < std::chrono::milliseconds(500))
      return;
   SetCloseState(CS_CLOSE_APP);
}

bool Player::IsInGameUIClosed() const
{
   const VPX::InGameUI::InGameUIPage* const page = m_liveUI->m_inGameUI.GetActivePage();
   return page == nullptr || page->IsClosed();
}

#ifdef ENABLE_XR
// The parts showing the backglass of a table, as visible now: named after it (like 'VR_Backbox_Backglass'), or primitives showing an image named after
// it. Their bounding vertices, each with the space reference of its part, to frame the backglass images on them.
static vector<std::pair<int, vec3>> GetBackglassBounds(PinTable* table)
{
   vector<std::pair<int, vec3>> result;
   vector<Vertex3Ds> bounds;
   for (IEditable* part : table->GetParts())
   {
      if (part->m_desktopBackdrop) // Not in the 3D scene
         continue;
      const Primitive* const prim = part->GetItemType() == ItemTypeEnum::eItemPrimitive ? static_cast<const Primitive*>(part) : nullptr;
      if (lowerCase(part->GetName()).find("backglass") == string::npos && (prim == nullptr || lowerCase(prim->m_d.m_szImage).find("backglass") == string::npos))
         continue;
      bounds.clear();
      part->GetBoundingVertices(bounds, nullptr); // Nothing for hidden parts
      const int space = static_cast<int>(part->GetPartGroup() ? part->GetPartGroup()->GetReferenceSpace() : PartGroupData::SpaceReference::SR_PLAYFIELD);
      for (const Vertex3Ds& v : bounds)
         result.emplace_back(space, vec3(v.x, v.y, v.z));
   }
   return result;
}
#endif

// In VR, the view follows the head of the player, which may not face the table: the image is then framed on the table as the settings choose
// (its backglass by default, its playfield seen from above, or its whole cabinet)
void Player::CaptureTableImage(const std::filesystem::path& imagePath, const std::function<void(bool)>& onCaptured)
{
   SetTableImageLighting(true);
   #ifdef ENABLE_XR
   if (m_vrDevice)
   {
      constexpr VRDevice::TableCaptureView views[] = { VRDevice::TableCaptureView::Backglass, VRDevice::TableCaptureView::Table, VRDevice::TableCaptureView::Cabinet };
      const VRDevice::TableCaptureView view = views[clamp(g_settingsService.GetAppSettings().GetStandalone_TableImageFocus(), 0, 2)];
      m_vrDevice->SetTableCaptureView(view, view == VRDevice::TableCaptureView::Backglass ? GetBackglassBounds(m_ptable) : vector<std::pair<int, vec3>>());
   }
   #endif
   m_renderer->m_renderDevice->CaptureScreenshot({ m_playfieldWnd }, { imagePath },
      [this, onCaptured](bool success)
      {
         #ifdef ENABLE_XR
         if (m_vrDevice)
            m_vrDevice->SetTableCaptureView(VRDevice::TableCaptureView::None);
         #endif
         m_tableImageLightingDone = true; // The lighting is set back by the game loop
         onCaptured(success);
      },
      3); // The views are moved from the next frame on
}

// Dark rooms, a night light level or the time of day (automatic light level) would give a dark image: the capture frames are rendered with the
// light level at 100 %, as the 'Day/Night' option of the table would, without saving it
void Player::SetTableImageLighting(bool capture)
{
   if (capture == m_tableImageLighting)
      return;
   m_tableImageLighting = capture;
   Renderer::SceneLighting& lighting = m_renderer->m_sceneLighting;
   if (capture)
   {
      m_tableImageLightMode = static_cast<int>(lighting.GetMode());
      m_tableImageLightLevel = lighting.GetUserLightLevel();
      lighting.SetUserLightLevel(1.f);
      lighting.SetMode(Renderer::SceneLighting::Mode::User);
   }
   else
   {
      lighting.SetUserLightLevel(m_tableImageLightLevel);
      lighting.SetMode(static_cast<Renderer::SceneLighting::Mode>(m_tableImageLightMode));
   }
   // The static parts are prerendered with the lighting: they are rendered with the others while the capture lighting is used
   m_renderer->DisableStaticPrePass(capture);
   m_renderer->MarkShaderDirty();
}

bool Player::CanReplaceTableImage() const
{
   // Only the tables of the table library have an image (not the lobby, even when loaded from a table of the library, nor tables played from elsewhere)
   if (m_isLobby)
      return false;
   const std::filesystem::path relativePath = m_ptable->m_filename.lexically_normal().lexically_relative(g_launcher->GetTableLibrary().GetTablesPath().lexically_normal());
   return !relativePath.empty() && *relativePath.begin() != "..";
}

void Player::ReplaceTableImage()
{
   if (!CanReplaceTableImage())
      return;
   // The table picker uses <table>.png first, then <table>.jpg: replace the one in use (the picker notices the new modification date)
   std::filesystem::path imagePath = m_ptable->m_filename;
   imagePath.replace_extension(".png");
   if (!FileExists(imagePath))
      imagePath.replace_extension(".jpg");
   m_liveUI->HideUI();
   m_tableImageReplacePath = imagePath; // Captured by the game loop once the in-game menu is gone
}

void Player::UpdateTableImageReplacement()
{
   if (m_tableImageLightingDone.exchange(false))
      SetTableImageLighting(false);
   if (!m_tableImageReplacePath.empty() && IsInGameUIClosed())
   {
      const std::filesystem::path imagePath = std::exchange(m_tableImageReplacePath, std::filesystem::path());
      CaptureTableImage(imagePath,
         [this, imagePath](bool success)
         {
            if (success)
               PLOGI << "Table image replaced: " << imagePath;
            else
               PLOGE << "Failed to replace table image: " << imagePath;
            m_tableImageReplaced = success ? 1 : 2; // Called on the render thread: the notification is pushed by the game loop
         });
   }
   if (const int replaced = m_tableImageReplaced.exchange(0); replaced != 0)
      m_liveUI->PushNotification(replaced == 1 ? "Table image replaced"s : "Failed to replace the table image"s, 3000);
}
#endif
