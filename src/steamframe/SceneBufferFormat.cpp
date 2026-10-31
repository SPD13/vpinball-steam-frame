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

// The targets of ResizeRenderTargets (and of the desktop display), made again at the same size in the format just chosen
void Renderer::ApplySceneBufferFormat()
{
   if (!VPX::SceneBufferFormat::TakeChange())
      return;

   // Frames already submitted may still reference the previous targets: release them at the end of the frame being prepared
   vector<RenderTarget*> released { m_pOffscreenBackBufferTexture1, m_pOffscreenBackBufferTexture2, m_pOffscreenMSAABackBufferTexture, m_pBloomBufferTexture, m_pBloomTmpBufferTexture,
      m_pPostProcessRenderTarget1, m_pPostProcessRenderTarget2, m_pPostProcessRenderTarget3, m_pReflectionBufferTexture, m_pMotionBlurBufferTexture, m_pAORenderTarget1, m_pAORenderTarget2,
      m_staticPrepassRT, m_desktopMSAABackBuffer, m_desktopBackBuffer1, m_desktopBackBuffer2, m_desktopPostProcess1, m_desktopPostProcess2, m_desktopPostProcess3, m_desktopBloom,
      m_desktopBloomTmp, m_desktopReflection, m_desktopMotionBlur, m_desktopAO1, m_desktopAO2 };
   std::erase(released, nullptr);
   m_renderDevice->AddEndOfFrameCmd([released]() { for (RenderTarget* rt : released) delete rt; });

   const RenderTarget* const previous = GetBackBufferTexture();
   const SurfaceType rtType = previous->m_type;
   const colorFormat renderFormat = previous->GetColorFormat();
   const int width = previous->GetWidth(), height = previous->GetHeight();
   if (m_pOffscreenMSAABackBufferTexture)
      m_pOffscreenMSAABackBufferTexture = new RenderTarget(m_renderDevice, rtType, "MSAABackBuffer"s, width, height, renderFormat, true,
         m_pOffscreenMSAABackBufferTexture->GetMSAASamples(), "Fatal Error: unable to create MSAA render buffer!");
   m_pOffscreenBackBufferTexture1 = new RenderTarget(m_renderDevice, rtType, "BackBuffer1"s, width, height, renderFormat, true, previous->GetMSAASamples(),
      "Fatal Error: unable to create offscreen back buffer");
   if (m_pOffscreenMSAABackBufferTexture)
      m_pOffscreenMSAABackBufferTexture->m_dynamicResolution = true;
   m_pOffscreenBackBufferTexture1->m_dynamicResolution = true;
   m_pOffscreenBackBufferTexture2 = m_pOffscreenBackBufferTexture1->Duplicate("BackBuffer2"s, false);
   const RenderTarget* const previousBloom = m_pBloomBufferTexture;
   m_pBloomBufferTexture = new RenderTarget(m_renderDevice, rtType, "BloomBuffer1"s, previousBloom->GetWidth(), previousBloom->GetHeight(), renderFormat, false, 1,
      "Fatal Error: unable to create bloom buffer!");
   m_pBloomBufferTexture->m_dynamicResolution = true;
   m_pBloomTmpBufferTexture = m_pBloomBufferTexture->Duplicate("BloomBuffer2"s);

   // Lazily made again when next used
   m_pPostProcessRenderTarget1 = nullptr;
   m_pPostProcessRenderTarget2 = nullptr;
   m_pPostProcessRenderTarget3 = nullptr;
   m_pReflectionBufferTexture = nullptr;
   m_pMotionBlurBufferTexture = nullptr;
   m_pAORenderTarget1 = nullptr;
   m_pAORenderTarget2 = nullptr;
   m_staticPrepassRT = nullptr;
   m_isStaticPrepassDirty = true;
   m_desktopMSAABackBuffer = m_desktopBackBuffer1 = m_desktopBackBuffer2 = nullptr;
   m_desktopPostProcess1 = m_desktopPostProcess2 = m_desktopPostProcess3 = nullptr;
   m_desktopBloom = m_desktopBloomTmp = m_desktopReflection = m_desktopMotionBlur = m_desktopAO1 = m_desktopAO2 = nullptr;
   for (RenderProbe* probe : m_table->m_vrenderprobe)
      probe->ReleaseRenderTargets();
}
