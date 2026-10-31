// license:GPLv3+

#include "core/stdafx.h"
#include "SceneBufferFormat.h"

#include "renderer/Renderer.h"
#include "renderer/RenderProbe.h"

#include <atomic>

namespace VPX::SceneBufferFormat
{

static std::atomic<bool> s_requested = false; // Set by the menu while the frames are prepared
static bool s_compact = false; // Format of the current targets, only changed at the start of a frame

void Load(const Settings& settings)
{
   s_requested = settings.GetPlayer_CompactSceneBuffer();
   s_compact = s_requested;
   PLOGI << "Scene buffers: " << GetStatus();
}

bool IsCompactRequested() { return s_requested; }

void SetCompactRequested(bool compact) { s_requested = compact; }

bool TakeChange()
{
   if (s_compact == s_requested)
      return false;
   s_compact = s_requested;
   PLOGI << "Scene buffers made again in " << (s_compact ? "RG11B10F" : "RGBA16F");
   return true;
}

#if defined(ENABLE_BGFX)
static bool IsSupported(int nMSAASamples)
{
   const uint16_t caps = bgfx::getCaps()->formats[bgfx::TextureFormat::RG11B10F];
   return (caps & (nMSAASamples > 1 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA : BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)) != 0;
}

bgfx::TextureFormat::Enum GetRGB16FFormat(int nMSAASamples)
{
   return s_compact && IsSupported(nMSAASamples) ? bgfx::TextureFormat::RG11B10F : bgfx::TextureFormat::RGBA16F;
}
#endif

string GetStatus()
{
   #if defined(ENABLE_BGFX)
   if (!IsSupported(1))
      return "Not supported by the GPU: RGBA16F (8 bytes per pixel)"s;
   #endif
   if (s_compact != s_requested)
      return "Changing..."s;
   return s_compact ? "RG11B10F (4 bytes per pixel)"s : "RGBA16F (8 bytes per pixel)"s;
}

}

// The targets of each view (RenderView::ResizeRenderTargets), made again at the same size in the format just chosen
void Renderer::ApplySceneBufferFormat()
{
   if (!VPX::SceneBufferFormat::TakeChange())
      return;

   for (RenderView* const view : m_renderViews)
   {
      if (view->m_pOffscreenBackBufferTexture1 == nullptr)
         continue; // Not rendered yet: its targets are made in the new format on first use

      // Frames already submitted may still reference the previous targets: release them at the end of the frame being prepared
      vector<RenderTarget*> released { view->m_pOffscreenBackBufferTexture1, view->m_pOffscreenBackBufferTexture2, view->m_pOffscreenMSAABackBufferTexture,
         view->m_pBloomBufferTexture, view->m_pBloomTmpBufferTexture, view->m_pPostProcessRenderTarget1, view->m_pPostProcessRenderTarget2,
         view->m_pPostProcessRenderTarget3, view->m_pReflectionBufferTexture, view->m_pMotionBlurBufferTexture, view->m_pAORenderTarget1, view->m_pAORenderTarget2,
         view->m_staticPrepassRT };
      std::erase(released, nullptr);
      m_renderDevice->AddEndOfFrameCmd([released]() { for (RenderTarget* rt : released) delete rt; });

      const RenderTarget* const previous = view->GetBackBufferTexture();
      const SurfaceType rtType = previous->m_type;
      const colorFormat renderFormat = previous->GetColorFormat();
      const int width = previous->GetWidth(), height = previous->GetHeight();
      if (view->m_pOffscreenMSAABackBufferTexture)
         view->m_pOffscreenMSAABackBufferTexture = new RenderTarget(m_renderDevice, rtType, "MSAABackBuffer"s, width, height, renderFormat, true,
            view->m_pOffscreenMSAABackBufferTexture->GetMSAASamples(), "Fatal Error: unable to create MSAA render buffer!");
      view->m_pOffscreenBackBufferTexture1 = new RenderTarget(m_renderDevice, rtType, "BackBuffer1"s, width, height, renderFormat, true, previous->GetMSAASamples(),
         "Fatal Error: unable to create offscreen back buffer");
      if (view->m_pOffscreenMSAABackBufferTexture)
         view->m_pOffscreenMSAABackBufferTexture->m_dynamicResolution = true;
      view->m_pOffscreenBackBufferTexture1->m_dynamicResolution = true;
      view->m_pOffscreenBackBufferTexture2 = view->m_pOffscreenBackBufferTexture1->Duplicate("BackBuffer2"s, false);
      const RenderTarget* const previousBloom = view->m_pBloomBufferTexture;
      view->m_pBloomBufferTexture = new RenderTarget(m_renderDevice, rtType, "BloomBuffer1"s, previousBloom->GetWidth(), previousBloom->GetHeight(), renderFormat, false, 1,
         "Fatal Error: unable to create bloom buffer!");
      view->m_pBloomBufferTexture->m_dynamicResolution = true;
      view->m_pBloomTmpBufferTexture = view->m_pBloomBufferTexture->Duplicate("BloomBuffer2"s);

      // Lazily made again when next used
      view->m_pPostProcessRenderTarget1 = nullptr;
      view->m_pPostProcessRenderTarget2 = nullptr;
      view->m_pPostProcessRenderTarget3 = nullptr;
      view->m_pReflectionBufferTexture = nullptr;
      view->m_pMotionBlurBufferTexture = nullptr;
      view->m_pAORenderTarget1 = nullptr;
      view->m_pAORenderTarget2 = nullptr;
      view->m_staticPrepassRT = nullptr;
      view->m_isStaticPrepassDirty = true;
      for (RenderProbe* probe : m_table->m_vrenderprobe)
         probe->ReleaseRenderTargets(view);
   }
}
