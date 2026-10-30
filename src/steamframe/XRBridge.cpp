// license:GPLv3+

// Steam Frame Edition: what keeps the loading message in the headset while the application goes from one table to the next, members of VRDevice.
//
// The OpenXR session is kept from one table to the next (see VRDevice::CreateSession), but its frames are submitted by the render thread of the
// table played, and for a while there is none: the previous table is freed, the next one is read from its file and started. Without frames, SteamVR
// shows its own scene (the passthrough on the Steam Frame). The bridge is a thread that submits the frames meanwhile. They hold the last image of
// the loading message, which the tables draw into a quad layer of its own, standing in the table's forward direction (see AddLoadingLayer and
// LiveUI::RenderLoadingScreen): the message stays where it is, unchanged, from the last frame of a table to the first frame of the next (only its spinner stops). The bridge does no GPU work.

#include "core/stdafx.h"

#if defined(ENABLE_XR)

#include "renderer/VRDevice.h"
#include "renderer/RenderTarget.h"
#include "renderer/XRGraphicBackend.h"
#include "steamframe/Launcher.h"

#include "bgfx/bgfx.h"

namespace
{
// Width of the UI panel in meters, laid out on the width of the eye images (see UpdateUIPanel, src/steamframe/vrui/Panel.cpp)
constexpr float kUIPanelWidth = 2.8f;
}

// As in VRDevice.cpp: errors are logged
#define OPENXR_CHECK(x, y)                                                                                                                                     \
   {                                                                                                                                                           \
      if (const XrResult res = (x); !XR_SUCCEEDED(res))                                                                                                        \
      {                                                                                                                                                        \
         PLOGE << "ERROR: OPENXR: " << int(res) << ' ' << (y);                                                                                                  \
      }                                                                                                                                                        \
   }

void VRDevice::CreateLoadingLayerSwapchain()
{
   m_loadingSwapchainInfo.backendFormat = m_backend->SelectColorSwapchainFormat(m_swapchainFormats);
   m_loadingSwapchainInfo.width = kLoadingLayerWidth;
   m_loadingSwapchainInfo.height = kLoadingLayerHeight;
   m_loadingSwapchainInfo.arraySize = 1;
   XrSwapchainCreateInfo swapchainCreateInfo { XR_TYPE_SWAPCHAIN_CREATE_INFO };
   swapchainCreateInfo.arraySize = 1;
   swapchainCreateInfo.format = m_loadingSwapchainInfo.backendFormat;
   swapchainCreateInfo.width = m_loadingSwapchainInfo.width;
   swapchainCreateInfo.height = m_loadingSwapchainInfo.height;
   swapchainCreateInfo.mipCount = 1;
   swapchainCreateInfo.faceCount = 1;
   swapchainCreateInfo.sampleCount = 1;
   swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
   if (!XR_SUCCEEDED(xrCreateSwapchain(m_session, &swapchainCreateInfo, &m_loadingSwapchainInfo.swapchain)))
   {
      PLOGE << "Loading layer: the swapchain could not be created, the loading message is drawn on the UI panel";
      m_loadingSwapchainInfo = {};
      return;
   }
   uint32_t imageCount = 0;
   OPENXR_CHECK(xrEnumerateSwapchainImages(m_loadingSwapchainInfo.swapchain, 0, &imageCount, nullptr), "Failed to enumerate loading layer Swapchain Images.");
   XrSwapchainImageBaseHeader* images = m_backend->AllocateSwapchainImageData(m_loadingSwapchainInfo.swapchain, SwapchainType::COLOR, imageCount);
   OPENXR_CHECK(xrEnumerateSwapchainImages(m_loadingSwapchainInfo.swapchain, imageCount, &imageCount, images), "Failed to enumerate loading layer Swapchain Images.");
   m_loadingHasImage = false;
}

void VRDevice::CreateLoadingLayerViews()
{
   if (m_loadingSwapchainInfo.swapchain == XR_NULL_HANDLE)
      return;
   m_backend->CreateImageViews(m_loadingSwapchainInfo);
   m_loadingRenderTargets.resize(m_loadingSwapchainInfo.imageViews.size());
   m_loadingLayerUsable = IsLoadingLayerAvailable();
   if (m_loadingSwapchainInfo.imageViews.empty())
      PLOGE << "Loading layer: the swapchain images could not be used, the loading message is drawn on the UI panel";
}

// An image acquired and not drawn yet stays acquired (see ReleaseLoadingLayerImage), for the next table
void VRDevice::DestroyLoadingLayerViews()
{
   m_loadingRenderTarget = nullptr;
   m_loadingRenderTargets.clear();
   m_loadingLayerUsable = false;
   for (const auto& imageView : m_loadingSwapchainInfo.imageViews)
      bgfx::destroy(imageView);
   m_loadingSwapchainInfo.imageViews.clear();
}

void VRDevice::DestroyLoadingLayerSwapchain()
{
   assert(m_loadingSwapchainInfo.imageViews.empty());
   if (m_loadingSwapchainInfo.swapchain != XR_NULL_HANDLE)
   {
      m_backend->FreeSwapchainImageData(m_loadingSwapchainInfo.swapchain);
      OPENXR_CHECK(xrDestroySwapchain(m_loadingSwapchainInfo.swapchain), "Failed to destroy the loading layer Swapchain");
   }
   m_loadingSwapchainInfo = {};
   m_loadingImageAcquired = false;
   m_loadingHasImage = false;
}

bool VRDevice::IsLoadingLayerAvailable() const
{
   return m_referenceSpace != XR_NULL_HANDLE && !m_loadingRenderTargets.empty();
}

// The image stays acquired until a frame draws it: the last frames of a table, prepared while it closes, may not draw the message, and an image
// released without being drawn would be the one the layer shows (it holds what an earlier frame drew, or nothing)
void VRDevice::AcquireLoadingLayerImage(RenderDevice* rd)
{
   m_loadingRenderTarget = nullptr;
   if (!m_loadingShown || !IsLoadingLayerAvailable())
      return;
   if (!m_loadingImageAcquired)
   {
      constexpr XrSwapchainImageAcquireInfo acquireInfo { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, nullptr };
      if (!XR_SUCCEEDED(xrAcquireSwapchainImage(m_loadingSwapchainInfo.swapchain, &acquireInfo, &m_loadingImageIndex)))
         return;
      XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
      waitInfo.timeout = XR_INFINITE_DURATION;
      OPENXR_CHECK(xrWaitSwapchainImage(m_loadingSwapchainInfo.swapchain, &waitInfo), "Failed to wait for Image from the loading layer Swapchain");
      m_loadingImageAcquired = true;
   }
   const uint32_t imageIndex = m_loadingImageIndex;
   RenderTarget* renderTarget = m_loadingRenderTargets[imageIndex].get();
   if (renderTarget == nullptr)
   {
      bgfx::Attachment attachment;
      attachment.init(m_loadingSwapchainInfo.imageViews[imageIndex], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
      const bgfx::FrameBufferHandle fbh = bgfx::createFrameBuffer(1, &attachment);
      bgfx::TextureHandle depthHandle = BGFX_INVALID_HANDLE;
      m_loadingRenderTargets[imageIndex] = std::make_unique<RenderTarget>(rd, SurfaceType::RT_DEFAULT, fbh, attachment.handle, m_loadingSwapchainInfo.format, depthHandle,
         bgfx::TextureFormat::Enum::Unknown, std::format("VRLoadingLayer [{}]", imageIndex), m_loadingSwapchainInfo.width, m_loadingSwapchainInfo.height, colorFormat::RGBA);
      renderTarget = m_loadingRenderTargets[imageIndex].get();
   }
   m_loadingDrawn = false;
   m_loadingRenderTarget = renderTarget;
}

void VRDevice::ReleaseLoadingLayerImage()
{
   m_loadingRenderTarget = nullptr;
   if (!m_loadingImageAcquired || !m_loadingDrawn)
      return;
   m_loadingImageAcquired = false;
   constexpr XrSwapchainImageReleaseInfo releaseInfo { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, nullptr };
   OPENXR_CHECK(xrReleaseSwapchainImage(m_loadingSwapchainInfo.swapchain, &releaseInfo), "Failed to release Image back to the loading layer Swapchain");
   m_loadingHasImage = true;
   // As big and as far as the UI panel shows the UI (see UpdateUIPanel), taken now since the next table may change them while the bridge shows the image
   const float metersPerPixel = kUIPanelWidth / static_cast<float>(m_eyeWidth);
   m_loadingLayerRect = { clamp(m_loadingDrawnWidth.load(), 1, kLoadingLayerWidth), clamp(m_loadingDrawnHeight.load(), 1, kLoadingLayerHeight) };
   m_loadingLayerSize = { static_cast<float>(m_loadingLayerRect.width) * metersPerPixel, static_cast<float>(m_loadingLayerRect.height) * metersPerPixel };
   m_loadingLayerDistance = clamp(g_settingsService.GetAppSettings().GetStandalone_VRMenuDistance(), 0.3f, 3.f);
}

// Standing in the room in the table's forward direction (the way the player faces it at the lockbar), wherever the head looks, as the UI panel when
// it does not follow the head (see UpdateUIPanel): placed where the player stands when the message appears, then kept there until it is hidden, from
// one table to the next. Over every other layer.
void VRDevice::AddLoadingLayer(std::vector<XrCompositionLayerBaseHeader*>& layers)
{
   // Once the message is hidden, its last image is not shown again (the next loading screen shows once drawn into the layer), and it is placed again
   if (!m_loadingShown)
   {
      m_loadingHasImage = false;
      m_loadingLayerPlaced = false;
   }
   if (!m_loadingHasImage || m_referenceSpace == XR_NULL_HANDLE)
      return;
   if (!m_loadingLayerPlaced)
   {
      // The scene orientation is the yaw of the table's forward direction (see UpdateUIPanel); the quad faces +Z of its pose, turned to face the player
      constexpr float belowEyes = 0.1f; // meters, as the UI panel
      const float orientation = ANGTORAD(m_orientation);
      const vec3 forward(-sinf(orientation), 0.f, -cosf(orientation));
      const vec3 center = m_uiHeadPos + forward * m_loadingLayerDistance - vec3(0.f, belowEyes, 0.f);
      m_loadingLayerPose = { { 0.f, sinf(orientation * 0.5f), 0.f, cosf(orientation * 0.5f) }, { center.x, center.y, center.z } };
      m_loadingLayerPlaced = true;
      PLOGI << std::format("Loading message placed at {:.2f},{:.2f},{:.2f} (reference space), facing the table's forward direction ({:.0f} degrees)", center.x, center.y, center.z, m_orientation);
   }
   m_loadingLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
   m_loadingLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
   m_loadingLayer.space = m_referenceSpace;
   m_loadingLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
   m_loadingLayer.subImage.swapchain = m_loadingSwapchainInfo.swapchain;
   m_loadingLayer.subImage.imageRect = { { 0, 0 }, m_loadingLayerRect };
   m_loadingLayer.subImage.imageArrayIndex = 0;
   m_loadingLayer.pose = m_loadingLayerPose;
   m_loadingLayer.size = m_loadingLayerSize;
   layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&m_loadingLayer));
}

// From the render thread, once the table is detached from the session (see ReleaseSession): the bridge submits the frames until the render thread of
// the next table takes the frame loop back (EndBridge). Not when the application quits, nor when the session can't be used anymore.
void VRDevice::StartBridge()
{
   assert(!m_bridgeThread.joinable());
   if (g_launcher == nullptr || !g_launcher->m_launcherMode || m_lost || m_runtimeExitDuringBridge)
      return;
   {
      std::lock_guard lock(m_tableReadyMutex);
      m_tableReady = false;
   }
   m_bridgeStop = false;
   m_bridgeRunning = true;
   m_bridgeThread = std::thread(&VRDevice::BridgeLoop, this);
}

void VRDevice::StopBridge()
{
   if (!m_bridgeThread.joinable())
      return;
   m_bridgeStop = true;
   m_bridgeThread.join();
   m_bridgeRunning = false;
}

// From the main thread, when the next table is about to draw its first frame (its loading screen, see Player::Player)
bool VRDevice::OnTableReadyForFrames()
{
   if (!m_bridgeRunning)
      return false;
   {
      std::lock_guard lock(m_tableReadyMutex);
      m_tableReady = true;
   }
   m_tableReadyCondition.notify_all();
   return true;
}

// From the render thread of the next table, before its first frame: the frames are submitted by the bridge until the table is about to draw its
// first one (the bridge shows the message meanwhile), at most a few seconds in case the table never says so
void VRDevice::EndBridge()
{
   if (!m_bridgeThread.joinable())
      return;
   const auto start = std::chrono::steady_clock::now();
   bool ready;
   {
      std::unique_lock lock(m_tableReadyMutex);
      ready = m_tableReadyCondition.wait_for(lock, std::chrono::seconds(5), [this]() { return m_tableReady; });
   }
   if (!ready)
      PLOGW << "XR bridge: the table did not signal its first frame, the bridge stops anyway";
   StopBridge();
   m_bridgeEndTime = std::chrono::steady_clock::now();
   PLOGI << "XR bridge stopped, the table takes the frames back (waited " << std::chrono::duration_cast<std::chrono::milliseconds>(m_bridgeEndTime - start).count() << "ms for it)";
}

void VRDevice::BridgeLoop()
{
   SetThreadName("XRBridge"s);
   // Not usec(): its origin is set again by each table (wintimer_init)
   const auto start = std::chrono::steady_clock::now();
   auto lastFrame = start;
   std::chrono::steady_clock::duration longestGap {};
   unsigned int frames = 0, shownFrames = 0;
   while (!m_bridgeStop)
   {
      if (!m_sessionRunning)
      {
         {
            std::lock_guard lock(m_queueMutex);
            BridgePollEvents();
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         continue;
      }
      XrFrameState frameState { XR_TYPE_FRAME_STATE };
      constexpr XrFrameWaitInfo frameWaitInfo { XR_TYPE_FRAME_WAIT_INFO, nullptr };
      if (!XR_SUCCEEDED(xrWaitFrame(m_session, &frameWaitInfo, &frameState)))
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         continue;
      }
      // The render thread of the next table holds the lock while BGFX starts and the table is attached to the session
      std::lock_guard lock(m_queueMutex);
      constexpr XrFrameBeginInfo frameBeginInfo { XR_TYPE_FRAME_BEGIN_INFO, nullptr };
      if (!XR_SUCCEEDED(xrBeginFrame(m_session, &frameBeginInfo)))
         continue;
      std::vector<XrCompositionLayerBaseHeader*> layers;
      if (frameState.shouldRender)
         AddLoadingLayer(layers);
      XrFrameEndInfo frameEndInfo { XR_TYPE_FRAME_END_INFO };
      frameEndInfo.displayTime = frameState.predictedDisplayTime;
      frameEndInfo.environmentBlendMode = m_environmentBlendMode;
      frameEndInfo.layerCount = static_cast<uint32_t>(layers.size());
      frameEndInfo.layers = layers.data();
      OPENXR_CHECK(xrEndFrame(m_session, &frameEndInfo), "Failed to end the XR frame of the bridge.");
      const auto now = std::chrono::steady_clock::now();
      longestGap = std::max(longestGap, now - lastFrame);
      lastFrame = now;
      frames++;
      if (!layers.empty())
         shownFrames++;
      BridgePollEvents();
   }
   const auto ms = [](std::chrono::steady_clock::duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); };
   PLOGI << "XR bridge: " << frames << " frames in " << ms(std::chrono::steady_clock::now() - start) << "ms (" << shownFrames
         << " with the loading message), longest time without a frame " << ms(longestGap) << "ms";
}

// As PollEvents, without the table: the session states, and what tells to update what the next table attaches
void VRDevice::BridgePollEvents()
{
   XrEventDataBuffer eventData { XR_TYPE_EVENT_DATA_BUFFER };
   while (xrPollEvent(m_xrInstance, &eventData) == XR_SUCCESS)
   {
      switch (eventData.type)
      {
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
         PLOGW << "OPENXR: Instance Loss Pending (between two tables)";
         m_sessionRunning = false;
         m_lost = true;
         break;
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
         m_controllerModelsDirty = true;
         break;
      case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR:
         m_visibilityMaskDirty = true;
         break;
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
      {
         const XrEventDataSessionStateChanged* const stateChanged = reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
         if (stateChanged->session != m_session)
            break;
         PLOGI << "OPENXR: State " << stateChanged->state << " (between two tables)";
         if (stateChanged->state == XR_SESSION_STATE_READY)
         {
            XrSessionBeginInfo sessionBeginInfo { XR_TYPE_SESSION_BEGIN_INFO };
            sessionBeginInfo.primaryViewConfigurationType = m_viewConfiguration;
            OPENXR_CHECK(xrBeginSession(m_session, &sessionBeginInfo), "Failed to begin Session.");
            m_sessionRunning = true; // The refresh rate is applied by the next table (see AttachTable)
         }
         else if (stateChanged->state == XR_SESSION_STATE_STOPPING)
         {
            OPENXR_CHECK(xrEndSession(m_session), "Failed to end Session.");
            m_sessionRunning = false;
         }
         else if (stateChanged->state == XR_SESSION_STATE_EXITING)
         {
            // The runtime asks the application to quit (from the dashboard of the headset): no other table, the next one closes as soon as it starts
            // (see CreateSession)
            m_sessionRunning = false;
            if (!m_exitRequested)
            {
               PLOGI << "OPENXR: The runtime asked the application to exit";
               m_runtimeExitDuringBridge = true;
               g_launcher->m_launcherMode = false;
            }
         }
         else if (stateChanged->state == XR_SESSION_STATE_LOSS_PENDING)
         {
            PLOGW << "OPENXR: Session Loss Pending (between two tables)";
            m_sessionRunning = false;
            m_lost = true;
         }
         m_sessionState = stateChanged->state;
         break;
      }
      default: break;
      }
      eventData = { XR_TYPE_EVENT_DATA_BUFFER };
   }
}

#endif
