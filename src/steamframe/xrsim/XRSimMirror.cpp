// license:GPLv3+

// Desktop display of the simulated headset (see XRSim.h): the eye chosen by the VR desktop display setting (left by default), drawn
// once the UI panel and its pointer are in it. Renderer::RenderPostProcess mirrors the eyes before the UI, which is fine for a headset
// (the desktop display is a preview) but not for the simulator, where the desktop display is all there is to see.

#include "core/stdafx.h"
#include "renderer/Renderer.h"

void Renderer::RenderXRSimMirror(RenderTarget* eyes)
{
   if (m_renderDevice->m_outputWnd.size() < 2 || m_renderDevice->m_outputWnd[1]->GetBackBuffer() == nullptr || NeedsDesktopSceneRender())
      return;
   RenderTarget* const desktopRT = m_renderDevice->m_outputWnd[1]->GetBackBuffer();
   m_renderDevice->SetRenderTarget("XR simulator display"s, desktopRT, false, true);
   m_renderDevice->AddRenderTargetDependency(eyes);
   m_renderDevice->ResetRenderState();
   m_renderDevice->SetRenderState(RenderState::ALPHABLENDENABLE, RenderState::RS_FALSE);
   m_renderDevice->SetRenderState(RenderState::CULLMODE, RenderState::CULL_NONE);
   m_renderDevice->SetRenderState(RenderState::ZWRITEENABLE, RenderState::RS_FALSE);
   m_renderDevice->SetRenderState(RenderState::ZENABLE, RenderState::RS_FALSE);

   // Fill the display, cropping the eye to its aspect ratio
   const float w = static_cast<float>(eyes->GetWidth()), h = static_cast<float>(eyes->GetHeight());
   const float ar = w / h, desktopAr = static_cast<float>(desktopRT->GetWidth()) / static_cast<float>(desktopRT->GetHeight());
   float x = 0.f, y = 0.f, fw = w, fh = h;
   if (ar > desktopAr)
   {
      fw = h * desktopAr;
      x = (w - fw) * 0.5f;
   }
   else
   {
      fh = w / desktopAr;
      y = (h - fh) * 0.5f;
   }
   const Vertex3D_TexelOnly verts[4] = {
      { -1.0f, 1.0f, 0.0f, x / w, y / h },
      { 1.0f, 1.0f, 0.0f, (x + fw) / w, y / h },
      { -1.0f, -1.0f, 0.0f, x / w, (y + fh) / h },
      { 1.0f, -1.0f, 0.0f, (x + fw) / w, (y + fh) / h },
   };
   m_renderDevice->m_FBShader->SetTechnique(ShaderTechnique::fb_mirror);
   m_renderDevice->m_FBShader->SetVector(ShaderUniform::w_h_height, 1.f, 1.f, 1.f, 1.f);
   m_renderDevice->m_FBShader->SetTexture(ShaderUniform::tex_fb_unfiltered, eyes->GetColorSampler(), SamplerFilter::SF_BILINEAR);
   m_renderDevice->m_FBShader->SetInt(ShaderUniform::layer, m_vrDesktop == VRDesktopMode::Right ? 1 : 0);
   m_renderDevice->DrawTexturedQuad(m_renderDevice->m_FBShader, verts);
}
