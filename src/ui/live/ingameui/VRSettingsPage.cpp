// license:GPLv3+

#include "core/stdafx.h"
#include "VRSettingsPage.h"

#include "audio/AudioPlayer.h"
#include "renderer/VRDevice.h"
#include "parts/flasher.h"
#include "parts/Sound.h"
#include "ui/live/LiveUI.h"
#include "utils/color.h"
#if defined(VPX_STEAM_FRAME)
#include "steamframe/MeshSimplifier.h"
#endif


namespace VPX::InGameUI
{

VRSettingsPage::VRSettingsPage()
   : InGameUIPage("Virtual Reality Settings"s, ""s, SaveMode::Both)
{
}

void VRSettingsPage::BuildPage()
{
   // Positioning and physical setup are defined for the user's rig, so these items are always saved to the application settings (never as table overrides)
   const auto addGlobalOnlyItem = [this](std::unique_ptr<InGameUIItem> item) { AddItem(std::move(item)).m_globalOnly = true; };

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "View Offset"s));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_Orientation, 1.f, "%4.1f deg"s, //
      [this]() { return m_player->m_vrDevice->GetSceneOrientation(); }, //
      [this](float, float v) { m_player->m_vrDevice->SetSceneOrientation(v); }));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_TableX, 1.f, "%4.1f cm"s, //
      [this]() { return m_player->m_vrDevice->GetSceneOffset().x; }, //
      [this](float, float v)
      {
         Vertex3Ds offset = m_player->m_vrDevice->GetSceneOffset();
         offset.x = v;
         m_player->m_vrDevice->SetSceneOffset(offset);
      }));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_TableY, 1.f, "%4.1f cm"s, //
      [this]() { return m_player->m_vrDevice->GetSceneOffset().y; }, //
      [this](float, float v)
      {
         Vertex3Ds offset = m_player->m_vrDevice->GetSceneOffset();
         offset.y = v;
         m_player->m_vrDevice->SetSceneOffset(offset);
      }));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_TableZ, 1.f, "%4.1f cm"s, //
      [this]() { return m_player->m_vrDevice->GetSceneOffset().z; }, //
      [this](float, float v)
      {
         Vertex3Ds offset = m_player->m_vrDevice->GetSceneOffset();
         offset.z = v;
         m_player->m_vrDevice->SetSceneOffset(offset);
      }));

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Cabinet Layout"s));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_LockbarWidth, 1.f, "%4.1f cm"s, //
      [this]() { return m_player->m_vrDevice->GetLockbarWidth(); }, //
      [this](float, float v) { m_player->m_vrDevice->SetLockbarWidth(v); }));

#ifdef ENABLE_BGFX
   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_LockbarHeight, 1.f, "%4.1f cm"s, //
      [this]() { return m_player->m_vrDevice->GetLockbarHeight(); }, //
      [this](float, float v) { m_player->m_vrDevice->SetLockbarHeight(v); }));
#endif

   auto& playerXItem = AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_ScreenPlayerX, 1.f, "%4.1f cm"s, //
      [this]() { return g_settingsService.GetActiveSettings().GetPlayer_ScreenPlayerX(); }, //
      [this](float, float v)
      {
         m_notifId = m_player->m_liveUI->PushNotification("This change is directly persisted and is used when centering view"s, 5000, m_notifId);
         g_settingsService.GetActiveSettings().SetPlayer_ScreenPlayerX(v, false);
      }));
   playerXItem.m_excludeFromDefault = true;
   playerXItem.m_globalOnly = true;

   auto& playerYItem = AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_ScreenPlayerY, 1.f, "%4.1f cm"s, //
      [this]() { return g_settingsService.GetActiveSettings().GetPlayer_ScreenPlayerY(); }, //
      [this](float, float v)
      {
         m_notifId = m_player->m_liveUI->PushNotification("This change is directly persisted and is used when centering view"s, 5000, m_notifId);
         g_settingsService.GetActiveSettings().SetPlayer_ScreenPlayerY(v, false);
      }));
   playerYItem.m_excludeFromDefault = true;
   playerYItem.m_globalOnly = true;

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Augmented Reality"s));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_UsePassthroughColor, //
      [this]() { return m_player->m_renderer->m_vrApplyColorKey; }, //
      [this](bool v) { m_player->m_renderer->m_vrApplyColorKey = v; }));

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Spatial Audio"s));

   // Recreate the audio player on spatial mode change, preserving the live listener setup
   const auto recreateAudioPlayer = [this](const bool spatialBackglass, const bool spatialPlayfield)
   {
      m_player->m_audioPlayer = std::make_unique<VPX::AudioPlayer>(m_player->m_audioPlayer->GetBackglassDeviceName(), //
         m_player->m_audioPlayer->GetPlayfieldDeviceName(), //
         m_player->m_audioPlayer->GetSoundMode3D());
      m_player->m_audioPlayer->SetTableDimensions(m_player->m_ptable->m_right - m_player->m_ptable->m_left, m_player->m_ptable->m_bottom - m_player->m_ptable->m_top);
      if (m_player->IsVR())
         m_player->m_audioPlayer->SetSpatialMode(spatialBackglass, spatialPlayfield);
   };

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_SpatialAudioBackglass, //
      [this]() { return m_player->m_audioPlayer->IsSpatialAudioEnabled(SoundOutTypes::SNDOUT_BACKGLASS); }, //
      [this, recreateAudioPlayer](bool v) { recreateAudioPlayer(v, m_player->m_audioPlayer->IsSpatialAudioEnabled(SoundOutTypes::SNDOUT_TABLE)); }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_SpatialAudioPlayfield, //
      [this]() { return m_player->m_audioPlayer->IsSpatialAudioEnabled(SoundOutTypes::SNDOUT_TABLE); }, //
      [this, recreateAudioPlayer](bool v) { recreateAudioPlayer(m_player->m_audioPlayer->IsSpatialAudioEnabled(SoundOutTypes::SNDOUT_BACKGLASS), v); }));

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Miscellaneous Settings"s));

   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_LockFeetToGround, //
      [this]() { return m_player->m_vrDevice->IsLockFeetToGround(); }, //
      [this](bool v) { m_player->m_vrDevice->SetLockFeetToGround(v); }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_AddBackglass, //
      [this]() { return m_player->m_implicitVRBackglass->m_d.m_isVisible; }, //
      [this](bool v) { m_player->m_implicitVRBackglass->m_d.m_isVisible = v; }));

#ifdef ENABLE_XR
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_ShowControllers, //
      [this]() { return m_player->m_vrDevice->IsShowControllers(); }, //
      [this](bool v) { m_player->m_vrDevice->SetShowControllers(v); }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_DisplayRefreshRate, //
      [this]() { return m_player->m_vrDevice->GetDisplayRefreshRateMode(); }, //
      [this](int, int v) { m_player->m_vrDevice->SetDisplayRefreshRateMode(v); }));

   // Foveated rendering, applied live through the runtime; the status line says what the runtime actually does with it
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_Foveation, //
      [this]() { return m_player->m_vrDevice->GetFoveationMode(); }, //
      [this](int, int v) { m_player->m_vrDevice->SetFoveationMode(v); }));
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_FoveationEyeTracked, //
      [this]() { return m_player->m_vrDevice->IsFoveationEyeTracked(); }, //
      [this](bool v) { m_player->m_vrDevice->SetFoveationEyeTracked(v); }));
   AddItem(std::make_unique<InGameUIItem>("Foveation status"s, "What the runtime does with the foveated rendering setting"s,
      [this](int, const InGameUIItem*)
      {
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(("Foveation status: " + m_player->m_vrDevice->GetFoveationStatus()).c_str());
      }));

   // Rendering resolution (PlayerVR/ResFactor, a factor of the headset's recommended size, unset meaning 100 %). The swapchains are created
   // with the session, so a change is used by the next table played; the status line shows the current size and the next one
   AddItem(std::make_unique<InGameUIItem>(
      VPX::Properties::FloatPropertyDef(""s, ""s, "Render resolution"s,
         "Rendering resolution of each eye, relative to the size recommended by the headset. Higher is sharper and costs more GPU time; dynamic resolution, when on, lowers it as needed.\nOnce saved, used by the next table played"s,
         false, 0.5f, 2.f, 0.05f, 1.f),
      100.f, "%4.0f %%"s, //
      [this]() { return m_player->m_vrDevice->GetResolutionFactor(); }, // Live
      [](const Settings& settings)
      {
         const float resFactor = settings.GetPlayerVR_ResFactor();
         return (resFactor <= 0.1f || resFactor > 10.f) ? 1.f : resFactor;
      }, // Stored
      [this](float, float v)
      {
         m_player->m_vrDevice->SetResolutionFactor(v);
         m_notifId = m_player->m_liveUI->PushNotification("Save the settings: the render resolution is used by the next table played"s, 3000, m_notifId);
      },
      [](Settings& settings) { settings.ResetPlayerVR_ResFactor(); }, //
      [](float v, Settings& settings, bool asTableOverride) { settings.SetPlayerVR_ResFactor(v, asTableOverride); }));
   AddItem(std::make_unique<InGameUIItem>("Render resolution status"s, "Rendering size of the current table, and of the next one when the setting changed"s,
      [this](int, const InGameUIItem*)
      {
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(("Render resolution: " + m_player->m_vrDevice->GetResolutionStatus()).c_str());
      }));

   // Dynamic resolution, applied live; the status line shows the current size and the GPU time it follows
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_DynamicResolution, //
      [this]() { return m_player->m_vrDevice->IsDynamicResolution(); }, //
      [this](bool v) { m_player->m_vrDevice->SetDynamicResolution(v); }));
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_DynamicResolutionTarget, 100.f, "%4.0f %%"s, //
      [this]() { return m_player->m_vrDevice->GetDynamicResolutionTarget(); }, //
      [this](float, float v) { m_player->m_vrDevice->SetDynamicResolutionTarget(v); }));
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_DynamicResolutionMinScale, 100.f, "%4.0f %%"s, //
      [this]() { return m_player->m_vrDevice->GetDynamicResolutionMinScale(); }, //
      [this](float, float v) { m_player->m_vrDevice->SetDynamicResolutionMinScale(v); }));
   AddItem(std::make_unique<InGameUIItem>("Dynamic resolution status"s, "Current rendering size, and the GPU time per frame against the frame budget"s,
      [this](int, const InGameUIItem*)
      {
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(("Dynamic resolution: " + m_player->m_vrDevice->GetDynamicResolutionStatus()).c_str());
      }));
#endif

#if defined(VPX_STEAM_FRAME)
   // Simplified meshes of heavy tables, switched live (see steamframe/MeshSimplifier.h)
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_SimplifyMeshes, //
      []() { return VPX::MeshSimplifier::Get().IsEnabled(); }, //
      [](bool v) { VPX::MeshSimplifier::Get().SetEnabled(v); }));
#endif

#if !defined(VPX_STEAM_FRAME_HEADSET) // The Steam Frame has no desktop display to show (forced off, see Renderer::Renderer)
   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Desktop Display"s));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_DesktopDisplay, //
      [this]() { return static_cast<int>(m_player->m_renderer->m_vrDesktop); }, //
      [this](int, int v)
      {
         m_player->m_renderer->m_vrDesktop = static_cast<VRDesktopMode>(v);
         RequestRebuild(); // Rebuild the page to update the eye display related options
      }));

   // Shrinking only applies to the desktop displays which mirror a headset eye
   if (m_player->m_renderer->m_vrDesktop >= VRDesktopMode::Left)
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayerVR_ShrinkDesktopDisplay, //
         [this]() { return m_player->m_renderer->m_vrDesktopShrink; }, //
         [this](bool v) { m_player->m_renderer->m_vrDesktopShrink = v; }));
#endif

#ifdef ENABLE_XR
   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Cabinet positioning using controllers"s));

   // TODO this property is directly persisted. It does not follow the overall UI design: App/Table/Live state => Implement live state (will also enable table override)
   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_ControllerCabYOffset, 1.f, "%4.1f cm"s, //
      [this]() { return g_settingsService.GetActiveSettings().GetPlayerVR_ControllerCabYOffset(); }, //
      [this](float, float v) { g_settingsService.GetActiveSettings().SetPlayerVR_ControllerCabYOffset(v, false); }));

   // TODO this property is directly persisted. It does not follow the overall UI design: App/Table/Live state => Implement live state (will also enable table override)
   addGlobalOnlyItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayerVR_ControllerLockbarScale, 100.f, "%4.1f %%"s, //
      [this]() { return g_settingsService.GetActiveSettings().GetPlayerVR_ControllerLockbarScale(); }, //
      [this](float, float v) { g_settingsService.GetActiveSettings().SetPlayerVR_ControllerLockbarScale(v, false); }));

   const auto& action = m_player->m_pininput.GetInputActions()[m_player->m_pininput.GetVRControllerViewCenteringActionId()];
   addGlobalOnlyItem(std::make_unique<InGameUIItem>(action->GetLabel(), "Select to add a new input binding which can be composed of multiple pressed button."s, action.get()));
#endif
}

void VRSettingsPage::ResetToDefaults()
{
#ifdef ENABLE_XR
   // Recentering the table is asynchronous so define defaults as the last acquired values (not perfect but fine enough for user feedback)
   Settings::SetPlayerVR_Orientation_Default(m_player->m_vrDevice->GetSceneOrientation());
   Settings::SetPlayerVR_TableX_Default(m_player->m_vrDevice->GetSceneOffset().x);
   Settings::SetPlayerVR_TableY_Default(m_player->m_vrDevice->GetSceneOffset().y);
   Settings::SetPlayerVR_TableZ_Default(m_player->m_vrDevice->GetSceneOffset().z);
#endif
   InGameUIPage::ResetToDefaults();
   m_player->m_vrDevice->RecenterTable();
}

}
