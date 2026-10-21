// license:GPLv3+

// Steam Frame Edition: a VR controller is used as a pointer on the in-game UI panel (see Panel.cpp), as a mouse for ImGui.
// LiveUI members, defined here to keep the upstream file close to upstream's.

#include "core/stdafx.h"
#include "ui/live/LiveUI.h"

#if defined(ENABLE_XR)

#include "renderer/IndexBuffer.h"
#include "renderer/MeshBuffer.h"
#include "renderer/Renderer.h"
#include "renderer/VertexBuffer.h"
#include "renderer/VRDevice.h"

#include "imgui/imgui.h"

// In VR, a controller is used as a pointer when the in-game UI is opened: move the mouse where it points to, its trigger being the left button.
// Navigation switches from buttons to pointer on a trigger press, or when the pointer clearly moves (a hand is never still enough to switch on any
// move: pointing a little further than the jitter of a held controller, from where it was when button navigation started, tells a deliberate aim).
// The thumbsticks being buttons too, scrolling with them does not switch.
void LiveUI::UpdateVRPointer()
{
   ImGuiIO& io = ImGui::GetIO();
   if (m_player->m_vrDevice && m_inGameUI.IsOpened())
   {
      float x, y, scroll;
      bool pressed;
      const bool pointing = m_player->m_vrDevice->GetUIPointer(x, y, pressed, scroll);
      if (pointing)
      {
         io.AddMousePosEvent(x * io.DisplaySize.x, y * io.DisplaySize.y);
         if (pressed && !m_vrPointerPressed)
            m_inGameUI.UsePointerNav();
         if (!m_inGameUI.IsFlipperNav())
            m_vrPointerAnchorValid = false;
         else if (!m_vrPointerAnchorValid)
         {
            m_vrPointerAnchor = ImVec2(x, y);
            m_vrPointerAnchorValid = true;
         }
         else if (constexpr float moveThreshold = 0.03f; fabsf(x - m_vrPointerAnchor.x) > moveThreshold || fabsf(y - m_vrPointerAnchor.y) > moveThreshold)
            m_inGameUI.UsePointerNav(); // Items are then highlighted when hovered
         // The thumbstick of the pointing controller scrolls what it points at, as a mouse wheel (up to 12 notches per second), in pointer
         // navigation only: the thumbsticks also navigate with buttons (default mapping: left stick up/down selects, right stick left/right
         // adjusts, see XRInputHandler), and button navigation keeps scrolling back to the selected item anyway. After a UI navigation action,
         // the stick does not scroll until it gets back to its center, so that pushing or releasing it does not scroll on its way.
         constexpr float deadZone = 0.2f;
         const InputManager& input = m_player->m_pininput;
         if (input.IsPressed(input.GetUIUpActionId()) || input.IsPressed(input.GetUIDownActionId()) || input.IsPressed(input.GetUILeftActionId())
            || input.IsPressed(input.GetUIRightActionId()))
            m_vrPointerScrollBlocked = true;
         else if (fabsf(scroll) <= deadZone)
            m_vrPointerScrollBlocked = false;
         if (fabsf(scroll) > deadZone && !m_vrPointerScrollBlocked && !m_inGameUI.IsFlipperNav())
            io.AddMouseWheelEvent(0.f, (scroll > 0.f ? 1.f : -1.f) * (fabsf(scroll) - deadZone) / (1.f - deadZone) * 12.f * io.DeltaTime);
      }
      if (pressed != m_vrPointerPressed && (pointing || !pressed))
      {
         io.AddMouseButtonEvent(ImGuiMouseButton_Left, pressed);
         m_vrPointerPressed = pressed;
      }
      m_vrPointerVisible = pointing;
      m_vrPointerPos = ImVec2(x * io.DisplaySize.x, y * io.DisplaySize.y);
      if (!pointing)
         m_vrPointerAnchorValid = false;
   }
   else
   {
      m_vrPointerVisible = false;
      m_vrPointerAnchorValid = false;
   }
}

void LiveUI::RenderVRPointerDot()
{
   if (!m_vrPointerVisible)
      return;
   ImGui::GetForegroundDrawList()->AddCircleFilled(m_vrPointerPos, 7.f * m_uiScale, m_vrPointerPressed ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 255, 255));
   ImGui::GetForegroundDrawList()->AddCircle(m_vrPointerPos, 7.f * m_uiScale, IM_COL32(0, 0, 0, 255), 0, 2.f * m_uiScale);
}

// Ray from the pointing controller to the dot, drawn with the UI shader on a quad of unit size, using the white pixel of the font atlas
void LiveUI::RenderVRPointerRay(bool onVRPanel, int depthSort)
{
   const ImGuiIO& io = ImGui::GetIO();
   if (Matrix3D rayToClip[2]; onVRPanel && m_vrPointerVisible && io.Fonts->TexRef.GetTexID() && m_player->m_vrDevice->GetUIPointerRayTransforms(rayToClip))
   {
      if (m_vrPointerRayMesh == nullptr)
      {
         auto ib = std::make_shared<IndexBuffer>(m_rd, 6, true, IndexBuffer::Format::FMT_INDEX32);
         auto vb = std::make_shared<VertexBuffer>(m_rd, 4, nullptr, true);
         m_vrPointerRayMesh = std::make_shared<MeshBuffer>("VRPointerRay"s, vb, ib, false);
         uint32_t* indices;
         m_vrPointerRayMesh->m_ib->Lock(indices);
         constexpr uint32_t quad[] = { 0, 1, 2, 2, 1, 3 };
         memcpy(indices, quad, sizeof(quad));
         m_vrPointerRayMesh->m_ib->Unlock();
      }
      // The white pixel moves when the atlas is rebuilt, so the vertices are updated each frame. The ray fades in from the controller.
      Vertex3D_NoTex2* vertices;
      m_vrPointerRayMesh->m_vb->Lock(vertices);
      for (unsigned int i = 0; i < 4; i++)
      {
         vertices[i].x = static_cast<float>(i & 1);
         vertices[i].y = static_cast<float>(i >> 1);
         vertices[i].z = (i & 1) ? 0.8f : 0.1f; // alpha
         vertices[i].nx = vertices[i].ny = vertices[i].nz = 1.f; // white
         vertices[i].tu = io.Fonts->TexUvWhitePixel.x;
         vertices[i].tv = io.Fonts->TexUvWhitePixel.y;
      }
      m_vrPointerRayMesh->m_vb->Unlock();
      m_rd->m_uiShader->SetMatrix(ShaderUniform::matWorldView, &rayToClip[0], 2);
      m_rd->m_uiShader->SetVector(ShaderUniform::clip_plane, -1.f, -1.f, 2.f, 2.f); // No clipping
      m_rd->m_uiShader->SetTexture(ShaderUniform::tex_base_color, io.Fonts->TexRef.GetTexID());
      m_rd->DrawMesh(m_rd->m_uiShader, true, Vertex3Ds(0.f, 0.f, 0.f), static_cast<float>(depthSort), m_vrPointerRayMesh, RenderDevice::TRIANGLELIST, 0, 6);
   }
}

#endif
