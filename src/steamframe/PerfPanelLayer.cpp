// license:GPLv3+

// Steam Frame Edition: the performance panel in the headset (see PerfPanel.h), members of VRDevice. The panel is a quad composition layer of its own,
// standing in the room left of the cabinet and facing the player at the lockbar: the player turns the head to the left to read it. It has its own
// small swapchain, of which an image is acquired only for the frames that draw new content (a sample every 0.5 s); between them, the compositor
// shows the image released last. Drawn by the compositor at the panel's own resolution, it is not affected by the dynamic resolution or the
// foveated rendering of the scene, and costs the scene nothing.

#include "core/stdafx.h"

#if defined(ENABLE_XR)

#include "renderer/VRDevice.h"
#include "renderer/RenderTarget.h"
#include "renderer/XRGraphicBackend.h"
#include "steamframe/PerfPanel.h"

#include "bgfx/bgfx.h"

namespace
{
// Native resolution of the headset, as a factor of the size the runtime recommends: the Steam Frame recommends 80 % of its panels (1728 of 2160
// pixels, its performance target), elsewhere the recommended size is taken as the native one
#if defined(VPX_STEAM_FRAME_HEADSET)
constexpr float kNativeFactor = 1.25f;
#else
constexpr float kNativeFactor = 1.f;
#endif

// Placement of the panel, in meters, from the middle of the lockbar: to the left, back towards the player (so that it is beside the head of a player
// standing at the lockbar), and up
constexpr float kPanelLeft = 0.8f;
constexpr float kPanelBack = 0.2f;
constexpr float kPanelUp = 0.55f;

constexpr float kVpuToMeters = static_cast<float>(0.0254 * 1.0625 / 50.); // As in VRDevice::RenderFrame
}

// As in VRDevice.cpp: errors are logged
#define OPENXR_CHECK(x, y)                                                                                                                                     \
   {                                                                                                                                                           \
      if (const XrResult res = (x); !XR_SUCCEEDED(res))                                                                                                        \
      {                                                                                                                                                        \
         PLOGE << "ERROR: OPENXR: " << int(res) << ' ' << (y);                                                                                                  \
      }                                                                                                                                                        \
   }

void VRDevice::ReportPerfPanelFrame(int periods, float gpuMs)
{
   VPX::PerfPanel::FrameInfo frame;
   frame.periods = periods;
   frame.gpuMs = gpuMs;
   frame.scale = GetDynamicRenderScale();
   frame.eyeWidth = static_cast<int>(m_eyeWidth);
   frame.eyeHeight = static_cast<int>(m_eyeHeight);
   if (!m_viewConfigurationViews.empty())
   {
      frame.nativeWidth = static_cast<int>(lroundf(static_cast<float>(m_viewConfigurationViews[0].recommendedImageRectWidth) * kNativeFactor));
      frame.nativeHeight = static_cast<int>(lroundf(static_cast<float>(m_viewConfigurationViews[0].recommendedImageRectHeight) * kNativeFactor));
   }
   frame.minScale = m_dynamicResolutionMinScale;
   frame.budgetMs = m_frameBudgetMs;
   frame.targetMs = m_frameBudgetMs * m_dynamicResolutionTarget;
   frame.dynamicResolution = m_dynamicResolution;
   // As GetFoveationStatus says it: without a gaze, the shading rate image uses the wide Low profile whatever the level
   frame.foveationLevel = !m_foveationExtensionSupported ? -1 : (m_foveationMode == 0 || !m_foveationApplied) ? 0 : m_foveationMode;
   frame.foveationEyeTracked = frame.foveationLevel > 0 && m_foveationEyeTracked && m_foveationEyeTrackedSystemSupported && m_foveationEyeTrackedActive;
   frame.foveationFixedLow = frame.foveationLevel > 0 && !frame.foveationEyeTracked && m_foveationShadingRate;
   VPX::PerfPanel::Get().OnFrame(frame);
}

void VRDevice::AcquirePerfPanelImage(RenderDevice* rd)
{
   m_perfPanelRenderTarget = nullptr;
   VPX::PerfPanel& panel = VPX::PerfPanel::Get();
   if (!panel.IsEnabled())
   {
      m_perfPanelHasImage = false; // Shown again once drawn after being turned on
      return;
   }

   // Created the first time the panel is shown in the session
   if (m_perfPanelSwapchainInfo.swapchain == XR_NULL_HANDLE)
   {
      uint32_t formatCount = 0;
      OPENXR_CHECK(xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr), "Failed to enumerate Swapchain Formats");
      std::vector<int64_t> formats(formatCount);
      OPENXR_CHECK(xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data()), "Failed to enumerate Swapchain Formats");
      m_perfPanelSwapchainInfo.backendFormat = m_backend->SelectColorSwapchainFormat(formats);
      m_perfPanelSwapchainInfo.width = VPX::PerfPanel::kWidth;
      m_perfPanelSwapchainInfo.height = VPX::PerfPanel::kHeight;
      m_perfPanelSwapchainInfo.arraySize = 1;
      XrSwapchainCreateInfo swapchainCreateInfo { XR_TYPE_SWAPCHAIN_CREATE_INFO };
      swapchainCreateInfo.arraySize = 1;
      swapchainCreateInfo.format = m_perfPanelSwapchainInfo.backendFormat;
      swapchainCreateInfo.width = m_perfPanelSwapchainInfo.width;
      swapchainCreateInfo.height = m_perfPanelSwapchainInfo.height;
      swapchainCreateInfo.mipCount = 1;
      swapchainCreateInfo.faceCount = 1;
      swapchainCreateInfo.sampleCount = 1;
      swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
      if (!XR_SUCCEEDED(xrCreateSwapchain(m_session, &swapchainCreateInfo, &m_perfPanelSwapchainInfo.swapchain)))
      {
         PLOGE << "Performance panel: the swapchain could not be created";
         m_perfPanelSwapchainInfo.swapchain = XR_NULL_HANDLE;
         panel.SetEnabled(false);
         return;
      }
      uint32_t imageCount = 0;
      OPENXR_CHECK(xrEnumerateSwapchainImages(m_perfPanelSwapchainInfo.swapchain, 0, &imageCount, nullptr), "Failed to enumerate performance panel Swapchain Images.");
      XrSwapchainImageBaseHeader* images = m_backend->AllocateSwapchainImageData(m_perfPanelSwapchainInfo.swapchain, SwapchainType::COLOR, imageCount);
      OPENXR_CHECK(xrEnumerateSwapchainImages(m_perfPanelSwapchainInfo.swapchain, imageCount, &imageCount, images), "Failed to enumerate performance panel Swapchain Images.");
      m_backend->CreateImageViews(m_perfPanelSwapchainInfo);
      m_perfPanelRenderTargets.resize(m_perfPanelSwapchainInfo.imageViews.size());
      if (m_perfPanelSwapchainInfo.imageViews.empty())
      {
         PLOGE << "Performance panel: the swapchain images could not be used";
         DestroyPerfPanel();
         panel.SetEnabled(false);
         return;
      }
      m_perfPanelHasImage = false;
   }

   if (!panel.IsDirty())
      return;
   uint32_t imageIndex = 0;
   constexpr XrSwapchainImageAcquireInfo acquireInfo { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, nullptr };
   if (!XR_SUCCEEDED(xrAcquireSwapchainImage(m_perfPanelSwapchainInfo.swapchain, &acquireInfo, &imageIndex)))
      return;
   XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
   waitInfo.timeout = XR_INFINITE_DURATION;
   OPENXR_CHECK(xrWaitSwapchainImage(m_perfPanelSwapchainInfo.swapchain, &waitInfo), "Failed to wait for Image from the performance panel Swapchain");
   m_perfPanelImageAcquired = true;
   RenderTarget* renderTarget = m_perfPanelRenderTargets[imageIndex].get();
   if (renderTarget == nullptr)
   {
      bgfx::Attachment attachment;
      attachment.init(m_perfPanelSwapchainInfo.imageViews[imageIndex], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
      const bgfx::FrameBufferHandle fbh = bgfx::createFrameBuffer(1, &attachment);
      bgfx::TextureHandle depthHandle = BGFX_INVALID_HANDLE;
      m_perfPanelRenderTargets[imageIndex] = std::make_unique<RenderTarget>(rd, SurfaceType::RT_DEFAULT, fbh, attachment.handle, m_perfPanelSwapchainInfo.format, depthHandle,
         bgfx::TextureFormat::Enum::Unknown, std::format("VRPerfPanel [{}]", imageIndex), m_perfPanelSwapchainInfo.width, m_perfPanelSwapchainInfo.height, colorFormat::RGBA);
      renderTarget = m_perfPanelRenderTargets[imageIndex].get();
   }
   m_perfPanelDrawn = false;
   m_perfPanelRenderTarget = renderTarget;
}

void VRDevice::ReleasePerfPanelImage()
{
   m_perfPanelRenderTarget = nullptr;
   if (!m_perfPanelImageAcquired)
      return;
   m_perfPanelImageAcquired = false;
   constexpr XrSwapchainImageReleaseInfo releaseInfo { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, nullptr };
   OPENXR_CHECK(xrReleaseSwapchainImage(m_perfPanelSwapchainInfo.swapchain, &releaseInfo), "Failed to release Image back to the performance panel Swapchain");
   // An image the logic thread did not draw into (the frame was not prepared, or did not draw the UI) has no meaningful content: the layer waits for
   // the next one, and the panel stays dirty so that the next frame draws it
   if (m_perfPanelDrawn)
   {
      m_perfPanelHasImage = true;
      VPX::PerfPanel::Get().ClearDirty();
   }
   else
      m_perfPanelHasImage = false;
}

void VRDevice::AddPerfPanelLayer(std::vector<XrCompositionLayerBaseHeader*>& layers)
{
   if (!m_perfPanelHasImage || m_perfPanelSwapchainInfo.swapchain == XR_NULL_HANDLE || !VPX::PerfPanel::Get().IsEnabled() || g_pplayer == nullptr || g_pplayer->m_ptable == nullptr)
      return;

   // Middle of the lockbar in the reference space: the bottom middle of the playfield at the height of the glass, through the playfield's transform
   // (which puts that point at the lockbar, see RenderFrame), from VPX units to meters
   const PinTable* const table = g_pplayer->m_ptable;
   Vertex3Ds lockbar((table->m_right - table->m_left) * 0.5f, table->m_bottom - table->m_top, table->m_glassBottomHeight);
   m_pfWorld.m_toWorld.MultiplyVector(lockbar);
   const vec3 lockbarPos(lockbar.x * kVpuToMeters, lockbar.y * kVpuToMeters, lockbar.z * kVpuToMeters);

   // Forward direction of the table (the way the player faces it at the lockbar, see UpdateUIPanel) and its right
   const float orientation = ANGTORAD(m_orientation);
   const vec3 forward(-sinf(orientation), 0.f, -cosf(orientation));
   const vec3 right(cosf(orientation), 0.f, -sinf(orientation));
   const vec3 center = lockbarPos - right * kPanelLeft - forward * kPanelBack + vec3(0.f, kPanelUp, 0.f);

   // The quad faces +Z of its pose: turned around the vertical axis so that it faces the table's right, towards the player
   const float yaw = atan2f(right.x, right.z);
   m_perfPanelLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
   m_perfPanelLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
   m_perfPanelLayer.space = m_referenceSpace;
   m_perfPanelLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
   m_perfPanelLayer.subImage.swapchain = m_perfPanelSwapchainInfo.swapchain;
   m_perfPanelLayer.subImage.imageRect = { { 0, 0 }, { static_cast<int32_t>(m_perfPanelSwapchainInfo.width), static_cast<int32_t>(m_perfPanelSwapchainInfo.height) } };
   m_perfPanelLayer.subImage.imageArrayIndex = 0;
   m_perfPanelLayer.pose = { { 0.f, sinf(yaw * 0.5f), 0.f, cosf(yaw * 0.5f) }, { center.x, center.y, center.z } };
   m_perfPanelLayer.size = { VPX::PerfPanel::kWidthMeters, VPX::PerfPanel::kHeightMeters };
   layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&m_perfPanelLayer));
}

void VRDevice::DestroyPerfPanel()
{
   m_perfPanelRenderTarget = nullptr;
   m_perfPanelImageAcquired = false;
   m_perfPanelHasImage = false;
   m_perfPanelRenderTargets.clear();
   for (const auto& imageView : m_perfPanelSwapchainInfo.imageViews)
      bgfx::destroy(imageView);
   if (m_perfPanelSwapchainInfo.swapchain != XR_NULL_HANDLE)
   {
      m_backend->FreeSwapchainImageData(m_perfPanelSwapchainInfo.swapchain);
      OPENXR_CHECK(xrDestroySwapchain(m_perfPanelSwapchainInfo.swapchain), "Failed to destroy the performance panel Swapchain");
   }
   m_perfPanelSwapchainInfo = {};
}

#endif
