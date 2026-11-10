// license:GPLv3+

// Steam Frame Edition: the cabinet positioning mode. The player holds the two controllers on a real edge (a desk, a shelf, the lockbar of a real
// cabinet) and the lockbar of the cabinet follows them, as the controller view centering of upstream does (VRDevice::RenderFrame), with three
// differences:
// - the size of the cabinet may stay as it is (Standalone/PlacementFixedSize, on by default): the place, height and orientation of the lockbar follow
//   the controllers, not its width (which upstream measures when the lockbar width is custom, PlayerVR/OverrideLockbarWidth);
// - the passthrough is shown while placing (the real edge must be seen), then back to the setting (src/steamframe/Passthrough.h);
// - the placement is fixed with the button that started it only while the lockbar is inside the play area of the headset (the stage bounds of
//   OpenXR): the bar between the controllers is green inside, orange closer than 30 cm to the edge, red outside, which is refused.
// Started from the menu (VR & Room > Cabinet positioning) or with the 'Align VR view using controllers' action (left thumbstick click), fixed with
// that action again, cancelled by opening the menu or closing the table. The placement fixed is saved at once, for all the tables.
// VRDevice and LiveUI members, defined here to keep the upstream files close to upstream's.

#include "core/stdafx.h"
#include "renderer/VRDevice.h"

#if defined(ENABLE_XR) && defined(VPX_STEAM_FRAME)

#include "ui/live/LiveUI.h"
#include "renderer/IndexBuffer.h"
#include "renderer/MeshBuffer.h"
#include "renderer/Renderer.h"
#include "renderer/VertexBuffer.h"
#include "steamframe/Passthrough.h"

#include "imgui/imgui.h"

static constexpr float kNearEdge = 0.3f; // Meters from the edge of the play area under which the lockbar is too close (orange)
static constexpr auto kBeforePress = std::chrono::milliseconds(250); // Pressing the button moves the controller: the placement fixed is from before
static constexpr auto kHistory = std::chrono::seconds(1);

VRDevice::Placement VRDevice::GetCurrentPlacement() const
{
   return { m_roomYaw, m_roomOffset, m_customLockbarWidth, m_customLockbarHeight, PlacementZone::Unknown, 0.f, std::chrono::steady_clock::now() };
}

void VRDevice::StartCabinetPlacement()
{
   if (m_placing)
      return;
   m_placementFixedSize = g_settingsService.GetAppSettings().GetStandalone_PlacementFixedSize();
   m_placementBefore = GetCurrentPlacement();
   {
      std::lock_guard lock(m_placementMutex);
      m_placementHistory.clear();
      m_placementToApply.reset();
   }
   // The play area, centered on the origin of the stage: known for a stage reference space only, and when the runtime has one
   m_playArea = { 0.f, 0.f };
   if (m_referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE && m_session != XR_NULL_HANDLE)
   {
      XrExtent2Df bounds { 0.f, 0.f };
      if (xrGetReferenceSpaceBoundsRect(m_session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds) == XR_SUCCESS)
         m_playArea = bounds;
   }
   m_placementZone = PlacementZone::Unknown;
   m_placementEdgeDistance = 0.f;
   m_placementTracked = false;
   VPX::Passthrough::SetForced(true);
   m_placing = true;
   m_controllerRecenterEnabled = true;
   PLOGI << std::format("Cabinet positioning mode: started ({}), play area {:.2f} x {:.2f} m{}", m_placementFixedSize ? "fixed cabinet size" : "lockbar width from the controllers",
      m_playArea.width, m_playArea.height, m_playArea.width > 0.f ? ""s : " (unknown, not checked)"s);
}

// Render thread, from the controller view centering, after it moved the cabinet
void VRDevice::UpdateCabinetPlacement(bool tracked, const vec3& left, const vec3& right)
{
   if (!m_placing)
      return;
   m_placementTracked = tracked;
   if (!tracked)
      return;
   m_placementLeft = left;
   m_placementRight = right;

   // The lockbar of the cabinet: its width, centered between the controllers, along them (on the floor of the play area)
   PlacementZone zone = PlacementZone::Unknown;
   float edgeDistance = 0.f;
   const vec3 center = (left + right) * 0.5f;
   float dx = right.x - left.x, dz = right.z - left.z;
   const float length = sqrtf(dx * dx + dz * dz);
   if (length > 0.01f && m_playArea.width > 0.f && m_playArea.height > 0.f)
   {
      dx /= length;
      dz /= length;
      const float halfWidth = m_customLockbarWidth * 0.01f * 0.5f;
      edgeDistance = FLT_MAX;
      for (const float side : { -1.f, 1.f })
      {
         const float x = center.x + dx * halfWidth * side, z = center.z + dz * halfWidth * side;
         edgeDistance = min(edgeDistance, min(m_playArea.width * 0.5f - fabsf(x), m_playArea.height * 0.5f - fabsf(z)));
      }
      zone = edgeDistance < 0.f ? PlacementZone::Outside : edgeDistance < kNearEdge ? PlacementZone::NearEdge : PlacementZone::Inside;
   }
   m_placementZone = zone;
   m_placementEdgeDistance = edgeDistance;

   Placement placement = GetCurrentPlacement();
   placement.zone = zone;
   placement.edgeDistance = edgeDistance;
   std::lock_guard lock(m_placementMutex);
   m_placementHistory.push_back(placement);
   while (m_placementHistory.size() > 1 && placement.time - m_placementHistory.front().time > kHistory)
      m_placementHistory.pop_front();
}

bool VRDevice::FinishCabinetPlacement()
{
   if (!m_placing)
      return true;
   std::optional<Placement> chosen;
   {
      std::lock_guard lock(m_placementMutex);
      const auto before = std::chrono::steady_clock::now() - kBeforePress;
      for (const Placement& placement : m_placementHistory)
         if (!chosen || placement.time <= before)
            chosen = placement;
   }
   if (!chosen)
   {
      PLOGI << "Cabinet positioning mode: not fixed, the controllers were not tracked";
      return false;
   }
   if (chosen->zone == PlacementZone::Outside)
   {
      PLOGI << std::format("Cabinet positioning mode: not fixed, the lockbar is {:.2f} m outside the play area", -chosen->edgeDistance);
      return false;
   }
   EndCabinetPlacement(*chosen, true);
   PLOGI << std::format("Cabinet positioning mode: fixed, room yaw {:.1f}, room offset {:.1f},{:.1f},{:.1f} cm, lockbar width {:.1f} cm, {:.2f} m from the edge of the play area",
      chosen->orientation, chosen->headsetPos.x, chosen->headsetPos.y, chosen->headsetPos.z, chosen->lockbarWidth, chosen->edgeDistance);
   return true;
}

void VRDevice::CancelCabinetPlacement()
{
   if (!m_placing)
      return;
   EndCabinetPlacement(m_placementBefore, false);
   PLOGI << "Cabinet positioning mode: cancelled, the cabinet is back where it was";
}

// Logic thread: the placement is written at once (the table may be closing, see Player::~Player), and again by the render thread before its next
// frame, in case a frame it was preparing moved the cabinet once more
void VRDevice::EndCabinetPlacement(const Placement& placement, bool save)
{
   m_controllerRecenterEnabled = false;
   m_roomYaw = placement.orientation;
   m_roomOffset = placement.headsetPos;
   m_customLockbarWidth = placement.lockbarWidth;
   m_customLockbarHeight = placement.lockbarHeight;
   m_worldDirty = true;
   {
      std::lock_guard lock(m_placementMutex);
      m_placementToApply = placement;
      m_placementHistory.clear();
   }
   m_placing = false;
   VPX::Passthrough::SetForced(false);
   if (save)
   {
      Settings& settings = g_settingsService.GetAppSettings();
      SaveVRSettings(settings);
      settings.Save();
   }
}

void VRDevice::ApplyCabinetPlacement()
{
   std::lock_guard lock(m_placementMutex);
   if (!m_placementToApply)
      return;
   m_roomYaw = m_placementToApply->orientation;
   m_roomOffset = m_placementToApply->headsetPos;
   m_customLockbarWidth = m_placementToApply->lockbarWidth;
   m_customLockbarHeight = m_placementToApply->lockbarHeight;
   m_worldDirty = true;
   m_placementToApply.reset();
}

// Flat ribbons turned towards the head (as the pointer ray, see GetUIPointerRayTransforms): a thin bar from one controller to the other, and the
// lockbar on it, thicker, its width centered between them
bool VRDevice::GetPlacementBarTransforms(Matrix3D (&barToClip)[2], Matrix3D (&lockbarToClip)[2]) const
{
   if (!m_placing || !m_placementTracked)
      return false;
   const vec3 left = m_placementLeft, right = m_placementRight;
   const vec3 along = right - left;
   const float length = along.Length();
   vec3 across = CrossProduct(along, m_uiHeadPos - left);
   if (length < 0.01f || across.LengthSquared() < 1e-10f)
      return false;
   across.Normalize();
   const auto ribbon = [this, &across](const vec3& from, const vec3& to, float thickness, Matrix3D (&toClip)[2])
   {
      const vec3 a = to - from, w = across * thickness, start = from - w * 0.5f;
      const Matrix3D quadToWorld(a.x, a.y, a.z, 0.f, w.x, w.y, w.z, 0.f, 0.f, 0.f, 1.f, 0.f, start.x, start.y, start.z, 1.f);
      for (int eye = 0; eye < 2; eye++)
         toClip[eye] = quadToWorld * m_uiPanelViewProj[eye];
   };
   ribbon(left, right, 0.006f, barToClip);
   const vec3 center = (left + right) * 0.5f, half = along * (min(m_customLockbarWidth * 0.01f, 3.f) * 0.5f / length);
   ribbon(center - half, center + half, 0.025f, lockbarToClip);
   return true;
}

// LiveUI: the message, in the middle of the UI panel, which stands in the room as the loading message while placing, in the table's forward direction
// of when it started (see LiveUI::RenderUI and VRDevice::UpdateUIPanel)
void LiveUI::RenderCabinetPlacementMessage()
{
   VRDevice* const vr = m_player->m_vrDevice;
   if (vr == nullptr || !vr->IsPlacingCabinet() || m_inGameUI.IsOpened())
      return;
   const InputAction* const action = m_player->m_pininput.GetInputActions()[m_player->m_pininput.GetVRControllerViewCenteringActionId()].get();
   string button = action->GetMappingLabel();
   button = button.empty() ? "the 'Align VR view using controllers' button (Controls)"s : "'" + button + '\'';

   string status;
   ImU32 color;
   switch (vr->IsPlacementTracked() ? vr->GetPlacementZone() : VRDevice::PlacementZone::Unknown)
   {
   case VRDevice::PlacementZone::Inside: status = "Inside the play area"s; color = IM_COL32(74, 222, 128, 255); break;
   case VRDevice::PlacementZone::NearEdge:
      status = std::format("Close to the edge of the play area ({:.0f} cm)", vr->GetPlacementEdgeDistance() * 100.f);
      color = IM_COL32(251, 146, 60, 255);
      break;
   case VRDevice::PlacementZone::Outside:
      status = std::format("Outside the play area: move the lockbar {:.0f} cm inside to fix it", -vr->GetPlacementEdgeDistance() * 100.f);
      color = IM_COL32(248, 113, 113, 255);
      break;
   default:
      status = vr->IsPlacementTracked() ? "Play area unknown: not checked"s : "Hold both controllers where they can be seen by the headset"s;
      color = IM_COL32(200, 200, 200, 255);
      break;
   }

   const ImGuiIO& io = ImGui::GetIO();
   ImGui::PushFont(m_baseFont, m_baseFont->LegacySize * 1.5f);
   ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
   ImGui::SetNextWindowBgAlpha(0.85f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.2f, ImGui::GetFontSize() * 0.9f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetFontSize() * 0.5f);
   ImGui::Begin("CabinetPlacement", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing
         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
   ImGui::TextUnformatted("Move the lockbar to the desired position and press a button");
   ImGui::PushStyleColor(ImGuiCol_Text, color);
   ImGui::TextUnformatted(status.c_str());
   ImGui::PopStyleColor();
   ImGui::PushFont(m_baseFont, m_baseFont->LegacySize);
   ImGui::TextDisabled("%s", ("Hold the controllers on the edge, press " + button + " to fix the cabinet there. Open the menu to cancel.").c_str());
   ImGui::PopFont();
   ImGui::End();
   ImGui::PopStyleVar(2);
   ImGui::PopFont();
}

// LiveUI: the bar and the lockbar, drawn with the UI shader as the pointer ray (see RenderVRPointerRay), colored as the play area check
void LiveUI::RenderCabinetPlacementBar(int depthSort)
{
   VRDevice* const vr = m_player->m_vrDevice;
   const ImGuiIO& io = ImGui::GetIO();
   Matrix3D barToClip[2], lockbarToClip[2];
   if (vr == nullptr || m_rd->m_nEyes != 2 || !io.Fonts->TexRef.GetTexID() || !vr->GetPlacementBarTransforms(barToClip, lockbarToClip))
      return;
   // A quad across, in 3 rows: opaque in the middle, transparent at the edges (soft edges)
   constexpr unsigned int rows = 3, nVertices = 2 * rows, nIndices = (rows - 1) * 6;
   // One mesh per ribbon: the frame is recorded before it is drawn, so a vertex buffer holds the vertices of one draw
   for (std::shared_ptr<MeshBuffer>& mesh : m_placementBarMeshes)
   {
      if (mesh != nullptr)
         continue;
      auto ib = std::make_shared<IndexBuffer>(m_rd, nIndices, true, IndexBuffer::Format::FMT_INDEX32);
      auto vb = std::make_shared<VertexBuffer>(m_rd, nVertices, nullptr, true, VertexFormat::VF_IMGUI);
      mesh = std::make_shared<MeshBuffer>("CabinetPlacementBar"s, vb, ib, false);
      uint32_t* indices;
      mesh->m_ib->Lock(indices);
      for (unsigned int r = 0; r < rows - 1; r++)
      {
         const uint32_t quad[] = { r, r + rows, r + 1, r + 1, r + rows, r + rows + 1 };
         memcpy(indices, quad, sizeof(quad));
         indices += 6;
      }
      mesh->m_ib->Unlock();
   }
   float red = 0.8f, green = 0.8f, blue = 0.8f;
   switch (vr->GetPlacementZone())
   {
   case VRDevice::PlacementZone::Inside: red = 0.29f; green = 0.87f; blue = 0.5f; break;
   case VRDevice::PlacementZone::NearEdge: red = 0.98f; green = 0.57f; blue = 0.24f; break;
   case VRDevice::PlacementZone::Outside: red = 0.97f; green = 0.44f; blue = 0.44f; break;
   default: break;
   }
   const auto draw = [&](const std::shared_ptr<MeshBuffer>& mesh, const Matrix3D (&toClip)[2], float alpha, float r, float g, float b)
   {
      // The white pixel moves when the atlas is rebuilt, so the vertices are updated each frame
      VertexUI* vertices;
      mesh->m_vb->Lock(vertices);
      for (unsigned int c = 0; c < 2; c++)
         for (unsigned int row = 0; row < rows; row++, vertices++)
         {
            const float across = static_cast<float>(row) / static_cast<float>(rows - 1) - 0.5f;
            vertices->x = static_cast<float>(c);
            vertices->y = 0.5f + across;
            const auto byte = [](float v) { return static_cast<int>(clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            vertices->color = IM_COL32(byte(r), byte(g), byte(b), across == 0.f ? byte(alpha) : 0); // Opaque on the middle line only
            vertices->tu = io.Fonts->TexUvWhitePixel.x;
            vertices->tv = io.Fonts->TexUvWhitePixel.y;
         }
      mesh->m_vb->Unlock();
      m_rd->m_uiShader->SetMatrix(ShaderUniform::matWorldView, &toClip[0], 2);
      m_rd->m_uiShader->SetVector(ShaderUniform::clip_plane, -1.f, -1.f, 2.f, 2.f); // No clipping
      m_rd->m_uiShader->SetTexture(ShaderUniform::tex_base_color, io.Fonts->TexRef.GetTexID());
      m_rd->DrawMesh(m_rd->m_uiShader, true, Vertex3Ds(0.f, 0.f, 0.f), static_cast<float>(depthSort), mesh, RenderDevice::TRIANGLELIST, 0, nIndices);
   };
   draw(m_placementBarMeshes[0], barToClip, 0.6f, 1.f, 1.f, 1.f);
   draw(m_placementBarMeshes[1], lockbarToClip, 0.95f, red, green, blue);
}

#endif
