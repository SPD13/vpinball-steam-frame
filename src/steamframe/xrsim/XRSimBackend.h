// license:GPLv3+

#pragma once

// Graphics backend of the simulated OpenXR runtime (see XRSim.h): bgfx creates its Metal device and the swapchain images itself, as
// sampleable render targets, which the desktop display shows (the simulated runtime has no compositor).

#include "renderer/XRGraphicBackend.h"

#include <map>

extern "C" void* MTLCreateSystemDefaultDevice();

class XRSimBackend final : public XRGraphicBackend
{
public:
   XRSimBackend() : m_device(MTLCreateSystemDefaultDevice()) { }

   void* GetGraphicContext() const override { return m_device; }
   bgfx::RendererType::Enum GetRendererType() const override { return bgfx::RendererType::Metal; }
   void* GetGraphicsBinding() override { return nullptr; }

   XrSwapchainImageBaseHeader* AllocateSwapchainImageData(XrSwapchain swapchain, VRDevice::SwapchainType type, uint32_t count) override
   {
      m_images[swapchain].resize(count, { XR_TYPE_UNKNOWN, nullptr });
      return m_images[swapchain].data();
   }
   void FreeSwapchainImageData(XrSwapchain swapchain) override { m_images.erase(swapchain); }
   void* GetSwapchainImage(XrSwapchain swapchain, uint32_t index) override { return nullptr; }

   void CreateImageViews(VRDevice::SwapchainInfo& swapchain) override
   {
      uint64_t flags = BGFX_TEXTURE_RT;
      switch (swapchain.backendFormat)
      {
      case 1: swapchain.format = bgfx::TextureFormat::RGBA8; flags |= BGFX_TEXTURE_SRGB; break;
      case 2: swapchain.format = bgfx::TextureFormat::RGBA8; break;
      case 3: swapchain.format = bgfx::TextureFormat::D32F; flags |= BGFX_TEXTURE_BLIT_DST; break;
      default: PLOGE << "XR simulator: unsupported swapchain format " << swapchain.backendFormat; return;
      }
      for (size_t i = 0; i < m_images[swapchain.swapchain].size(); ++i)
         swapchain.imageViews.push_back(bgfx::createTexture2D(static_cast<uint16_t>(swapchain.width), static_cast<uint16_t>(swapchain.height), false,
            static_cast<uint16_t>(swapchain.arraySize), swapchain.format, flags, nullptr));
   }

   const std::vector<int64_t> GetSupportedColorSwapchainFormats() override { return { 1, 2 }; }
   const std::vector<int64_t> GetSupportedDepthSwapchainFormats() override { return { 3 }; }

private:
   void* m_device;
   std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader>> m_images;
};
