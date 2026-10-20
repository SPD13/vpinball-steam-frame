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
// Pointer navigation is the default (see InGameUI::Open): a button of the UI switches to button navigation (InGameUI::UseButtonNav), a trigger
// press switches back, without clicking: what was hovered may not be what the player looked at, the selected item. Moving the pointer or
// scrolling does not switch, as a held controller is never still. In button navigation the pointer hovers nothing.
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
         if (pressed && !m_vrPointerPressed && m_inGameUI.IsFlipperNav())
         {
            m_inGameUI.UsePointerNav(); // Items are then highlighted when hovered
            m_vrPointerClickSwallowed = true;
         }
         // In button navigation, the pointer (hidden) must not hover anything: ImGui gets no mouse, as if it had left the panel
         if (m_inGameUI.IsFlipperNav())
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
         else
            io.AddMousePosEvent(x * io.DisplaySize.x, y * io.DisplaySize.y);
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
         if (!m_vrPointerClickSwallowed)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, pressed);
         else if (!pressed)
            m_vrPointerClickSwallowed = false;
         m_vrPointerPressed = pressed;
      }
      m_vrPointerVisible = pointing;
      m_vrPointerPos = ImVec2(x * io.DisplaySize.x, y * io.DisplaySize.y);
   }
   else
   {
      m_vrPointerVisible = false;
      m_vrPointerClickSwallowed = false;
   }
}

// The dot and the ray are hidden in button navigation (the controller still points, ready for a trigger press to switch back)
void LiveUI::RenderVRPointerDot()
{
   if (!m_vrPointerVisible || m_inGameUI.IsFlipperNav())
      return;
   ImGui::GetForegroundDrawList()->AddCircleFilled(m_vrPointerPos, 7.f * m_uiScale, m_vrPointerPressed ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 255, 255));
   ImGui::GetForegroundDrawList()->AddCircle(m_vrPointerPos, 7.f * m_uiScale, IM_COL32(0, 0, 0, 255), 0, 2.f * m_uiScale);
}

// Ray from the pointing controller to the dot, drawn with the UI shader on a quad of unit size, using the white pixel of the font atlas
void LiveUI::RenderVRPointerRay(bool onVRPanel, int depthSort)
{
   const ImGuiIO& io = ImGui::GetIO();
   if (Matrix3D rayToClip[2]; onVRPanel && m_vrPointerVisible && !m_inGameUI.IsFlipperNav() && io.Fonts->TexRef.GetTexID() && m_player->m_vrDevice->GetUIPointerRayTransforms(rayToClip))
   {
      // A ribbon of segments along the ray, in two strips across it, as the pointer of SteamVR: most opaque at the controller and fading
      // towards the tip, with soft edges, narrowing towards the tip
      constexpr unsigned int segments = 8, rows = 3, nVertices = (segments + 1) * rows, nIndices = segments * (rows - 1) * 6;
      if (m_vrPointerRayMesh == nullptr)
      {
         auto ib = std::make_shared<IndexBuffer>(m_rd, nIndices, true, IndexBuffer::Format::FMT_INDEX32);
         auto vb = std::make_shared<VertexBuffer>(m_rd, nVertices, nullptr, true);
         m_vrPointerRayMesh = std::make_shared<MeshBuffer>("VRPointerRay"s, vb, ib, false);
         uint32_t* indices;
         m_vrPointerRayMesh->m_ib->Lock(indices);
         for (unsigned int c = 0; c < segments; c++)
            for (unsigned int r = 0; r < rows - 1; r++)
            {
               const uint32_t v = c * rows + r;
               const uint32_t quad[] = { v, v + rows, v + 1, v + 1, v + rows, v + rows + 1 };
               memcpy(indices, quad, sizeof(quad));
               indices += 6;
            }
         m_vrPointerRayMesh->m_ib->Unlock();
      }
      // The white pixel moves when the atlas is rebuilt, so the vertices are updated each frame
      Vertex3D_NoTex2* vertices;
      m_vrPointerRayMesh->m_vb->Lock(vertices);
      for (unsigned int c = 0; c <= segments; c++)
      {
         const float t = static_cast<float>(c) / static_cast<float>(segments); // 0 at the controller, 1 at the pointed position
         const float alpha = 0.05f + 0.6f * (1.f - t) * (1.f - t);
         const float width = 1.f - 0.6f * t;
         for (unsigned int r = 0; r < rows; r++, vertices++)
         {
            const float across = static_cast<float>(r) / static_cast<float>(rows - 1) - 0.5f; // -0.5 to 0.5
            vertices->x = t;
            vertices->y = 0.5f + across * width;
            vertices->z = across == 0.f ? alpha : 0.f; // alpha
            vertices->nx = vertices->ny = vertices->nz = 1.f; // white
            vertices->tu = io.Fonts->TexUvWhitePixel.x;
            vertices->tv = io.Fonts->TexUvWhitePixel.y;
         }
      }
      m_vrPointerRayMesh->m_vb->Unlock();
      m_rd->m_uiShader->SetMatrix(ShaderUniform::matWorldView, &rayToClip[0], 2);
      m_rd->m_uiShader->SetVector(ShaderUniform::clip_plane, -1.f, -1.f, 2.f, 2.f); // No clipping
      m_rd->m_uiShader->SetTexture(ShaderUniform::tex_base_color, io.Fonts->TexRef.GetTexID());
      m_rd->DrawMesh(m_rd->m_uiShader, true, Vertex3Ds(0.f, 0.f, 0.f), static_cast<float>(depthSort), m_vrPointerRayMesh, RenderDevice::TRIANGLELIST, 0, nIndices);
   }
}

#endif
