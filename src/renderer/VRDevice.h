// license:GPLv3+

#pragma once

#if defined(ENABLE_XR)
   #if defined(__STANDALONE__) && !defined(_MSC_VER) // wine's headers (non MSVC standalone builds) define _WIN64 on any 64 bit target
   #pragma push_macro("_WIN64")
   #undef _WIN64
   #endif
   #include "bx/platform.h"
   #if defined(__STANDALONE__) && !defined(_MSC_VER)
   #pragma pop_macro("_WIN64")
   #endif

   #if BX_PLATFORM_WINDOWS && defined(__STANDALONE__) && !defined(_MSC_VER)
      // Standalone Windows build (MinGW): Vulkan only, to render like the standalone builds for headsets
      #define XR_USE_PLATFORM_WIN32
      #define XR_USE_GRAPHICS_API_VULKAN
      #define VK_USE_PLATFORM_WIN32_KHR
   #elif BX_PLATFORM_WINDOWS
      #define XR_USE_PLATFORM_WIN32
      #define XR_USE_GRAPHICS_API_VULKAN
      #define XR_USE_GRAPHICS_API_OPENGL
      #define XR_USE_GRAPHICS_API_OPENGL_ES
      #define XR_USE_GRAPHICS_API_D3D11
      #define XR_USE_GRAPHICS_API_D3D12
      #define VK_USE_PLATFORM_WIN32_KHR
   #elif BX_PLATFORM_ANDROID
      #define XR_USE_TIMESPEC
      #define XR_USE_PLATFORM_ANDROID
      #define XR_USE_GRAPHICS_API_VULKAN
      //#define XR_USE_GRAPHICS_API_OPENGL_ES
   #elif BX_PLATFORM_LINUX
      #define XR_USE_TIMESPEC
      #define XR_USE_GRAPHICS_API_VULKAN
   #endif


   // OpenXR Dependencies

   #ifdef XR_USE_PLATFORM_ANDROID
   #include <android/native_window.h>
   #include <android/window.h>
   #include <android/native_window_jni.h>
   #endif  // XR_USE_PLATFORM_ANDROID

   #ifdef XR_USE_PLATFORM_WIN32

   #include <winapifamily.h>
   #if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM))
   // Enable desktop partition APIs, such as RegOpenKeyEx, LoadLibraryEx, PathFileExists etc.
   #undef WINAPI_PARTITION_DESKTOP
   #define WINAPI_PARTITION_DESKTOP 1
   #endif

   #ifndef NOMINMAX
   #define NOMINMAX
   #endif  // !NOMINMAX

   #ifndef WIN32_LEAN_AND_MEAN
   #define WIN32_LEAN_AND_MEAN
   #endif  // !WIN32_LEAN_AND_MEAN

   #include <windows.h>
   #include <unknwn.h>

   #endif  // XR_USE_PLATFORM_WIN32

   #ifdef XR_USE_GRAPHICS_API_D3D11
   #include <d3d11.h>
   #endif  // XR_USE_GRAPHICS_API_D3D11

   #ifdef XR_USE_GRAPHICS_API_D3D12
   #include <d3d12.h>
   #endif  // XR_USE_GRAPHICS_API_D3D12

   #ifdef XR_USE_PLATFORM_XLIB
   #include <X11/Xlib.h>
   #include <X11/Xutil.h>
   #endif  // XR_USE_PLATFORM_XLIB

   #ifdef XR_USE_PLATFORM_XCB
   #include <xcb/xcb.h>
   #endif  // XR_USE_PLATFORM_XCB

   #ifdef XR_USE_GRAPHICS_API_OPENGL
   #if defined(XR_USE_PLATFORM_XLIB) || defined(XR_USE_PLATFORM_XCB)
   #include <GL/glx.h>
   #endif  // (XR_USE_PLATFORM_XLIB || XR_USE_PLATFORM_XCB)
   #ifdef XR_USE_PLATFORM_XCB
   #include <xcb/glx.h>
   #endif  // XR_USE_PLATFORM_XCB
   #ifdef XR_USE_PLATFORM_MACOS
   #include <OpenCL/cl_gl_ext.h>
   #endif  // XR_USE_PLATFORM_MACOS
   #endif  // XR_USE_GRAPHICS_API_OPENGL

   #ifdef XR_USE_GRAPHICS_API_OPENGL_ES
   #include <SDL3/SDL_egl.h>
   #endif  // XR_USE_GRAPHICS_API_OPENGL_ES

   #ifdef XR_USE_GRAPHICS_API_VULKAN
   #include <vulkan/vulkan.h>
   #ifdef XR_USE_PLATFORM_ANDROID
   #include <vulkan/vulkan_android.h>
   #endif  // XR_USE_PLATFORM_ANDROID
   #endif  // XR_USE_GRAPHICS_API_VULKAN

   #ifdef XR_USE_PLATFORM_WAYLAND
   #include "wayland-client.h"
   #endif  // XR_USE_PLATFORM_WAYLAND

   #ifdef XR_USE_PLATFORM_EGL
   #include <EGL/egl.h>
   #endif  // XR_USE_PLATFORM_EGL

   #if defined(XR_USE_PLATFORM_XLIB) || defined(XR_USE_PLATFORM_XCB)
   #ifdef Success
   #undef Success
   #endif  // Success

   #ifdef Always
   #undef Always
   #endif  // Always

   #ifdef None
   #undef None
   #endif  // None
   #endif // defined(XR_USE_PLATFORM_XLIB) || defined(XR_USE_PLATFORM_XCB)

   #include <openxr/openxr.h>
   #include <openxr/openxr_platform.h>

   #include "input/XRInputHandler.h"

   #if defined(VPX_STEAM_FRAME)
   #include <condition_variable>
   #include <mutex>
   #include <thread>
   #endif
#endif

#include "math/matrix.h"
#include "parts/PartGroup.h"

class MeshBuffer;

// The VR device lives as long as the OpenXR instance: when tables are played one after another, the application can keep it from one table
// to the next (see VPApp::AcquireVRDevice), so the runtime does not see the application quit and start again. The session is kept with it
// (SetKeepSession): SteamVR 2.17.10 does not survive a second session on one instance. Each table attaches to the session what depends on it
// and on BGFX, which is started again for each table on the same graphics device (CreateSession / ReleaseSession, from the render thread),
// and applies its settings (ApplyTableSettings).
class VRDevice final
{
public:
   VRDevice(const Settings& settings);
   ~VRDevice();

   // Settings that a table may override (scene placement, foveation, resolution, ...), applied each time a table is played with this device
   void ApplyTableSettings(const Settings& settings);

   unsigned int GetEyeWidth() const { return m_eyeWidth; }
   unsigned int GetEyeHeight() const { return m_eyeHeight; }

   #if defined(ENABLE_XR)
   // The UI is displayed on a panel standing in the room, placed in front of the player in the table's forward direction (not where the head looks)
   // when it is first displayed and each time RecenterUIPanel is called
   void RecenterUIPanel() { m_uiPanelPlaced = false; }
   // Same, the panel staying where it is until it is placed again, instead of the UI being head locked for a frame (menu distance changes)
   void ReplaceUIPanel() { m_uiPanelReplace = true; }
   // While the in-game UI is closed, the panel faces the head and follows it, so that what is drawn in its middle (notifications) stays in the
   // middle of the view, at the menu distance. Opening the UI recenters it (upright, in the table's forward direction)
   void SetUIPanelFollowsHead(bool follow) { m_uiPanelFollowsHead = follow; }
   // Transforms from UI pixel coordinates (0..width, 0..height from the top left of the panel) to each eye clip space. Returns false if the panel is not placed yet
   bool GetUIPanelTransforms(float width, float height, Matrix3D (&pixelToClip)[2]) const;
   // Position pointed by a controller on the UI panel, normalized to 0..1 from its top left, and vertical axis of the thumbstick of that
   // controller (-1..1, up is positive). Returns false if no controller points at it
   bool GetUIPointer(float& x, float& y, bool& pressed, float& scroll) const
   {
      x = m_uiPointerX;
      y = m_uiPointerY;
      pressed = m_uiPointerPressed;
      scroll = m_uiPointerScroll;
      return m_uiPointerValid;
   }
   // Transforms from a unit quad (x along the ray from the controller to the pointed position, y across it) to each eye clip space.
   // Returns false if no controller points at the UI panel
   bool GetUIPointerRayTransforms(Matrix3D (&quadToClip)[2]) const;
   // Unless None, both eyes frame the table instead of following the head, to capture the image of a table: its backglass, its whole playfield
   // seen from above, or its whole cabinet seen from the front and a little from the side and above
   enum class TableCaptureView { None, Backglass, Table, Cabinet };
   // backglassBounds: bounding vertices of the parts showing the backglass (see Player::CaptureTableImage), each with the space reference of its
   // part (PartGroupData::SpaceReference). Without them, the backglass is placed like on a real cabinet.
   void SetTableCaptureView(TableCaptureView view, vector<std::pair<int, vec3>> backglassBounds = {})
   {
      if (view != TableCaptureView::None)
         m_tableCaptureBounds = std::move(backglassBounds); // Only read by the render thread while a capture view is set
      m_tableCaptureView = view;
   }
   #endif
   
   float GetLockbarWidth() const { return m_lockbarWidth; }
   void SetLockbarWidth(float width) { m_lockbarWidth = width; m_worldDirty = true; }
   float GetLockbarHeight() const { return m_lockbarHeight; }
   void SetLockbarHeight(float height) { m_lockbarHeight = height; m_worldDirty = true; }
   bool IsLockFeetToGround() const { return m_lockFeetToGround; }
   void SetLockFeetToGround(bool lock)
   {
      m_lockFeetToGround = lock;
      if (lock)
         m_seatedTilt = 0.f;
      m_worldDirty = true;
   }
   float GetSeatedTilt() const { return m_seatedTilt; }
   void SetSeatedTilt(float tilt)
   {
      m_seatedTilt = tilt;
      if (tilt != 0.f)
         m_lockFeetToGround = false;
      m_worldDirty = true;
   }

   void OffsetTable(float dx, float dy, float dz);
   void RecenterTable();
   float GetSceneOrientation() const { return m_orientation; }
   const Vertex3Ds& GetSceneOffset() const { return m_tablePos; }
   void SetSceneOrientation(float orientation) { m_orientation = orientation; m_worldDirty = true; }
   void SetSceneOffset(const Vertex3Ds& pos) { m_tablePos = pos; m_worldDirty = true; }
   void SaveVRSettings(Settings& settings) const;

   void UpdateVRPosition(PartGroupData::SpaceReference spaceRef, ModelViewProj& mvp);

   float GetPredictedDisplayTimestamp() const { return m_predictedDisplayTimestamp; }

   void SetShowUILayer(bool show) { m_showUILayer = show; }

private:
   unsigned int m_eyeWidth = 1080;
   unsigned int m_eyeHeight = 1020;

   float m_scale = 1.0f;
   float m_lockbarWidth = 57.0f; // Real world width of the lockbar in cm
   float m_lockbarHeight = 85.0f; // Real world height (from ground) of the lockbar in cm
   bool m_lockFeetToGround = true;
   float m_seatedTilt = 0.0f; // Cabinet tilt toward the player for seated/reclined play, in degrees
   float m_orientation = 0.0f;
   Vertex3Ds m_tablePos;
   float m_slope = 0.0f;

   float m_predictedDisplayTimestamp = 0.f;

   bool m_worldDirty = true;
   struct Viewpoint
   {
      Matrix3D m_toWorld; // Matrix to transform from this viewpoint to world coordinates
      Matrix3D m_view[2];
   };
   Viewpoint m_pfWorld;
   Viewpoint m_cabWorld;
   Viewpoint m_feetWorld;
   Viewpoint m_roomWorld;
   Matrix3D m_roomProj[2];
   Matrix3D m_sceneProj[2];

   bool m_showUILayer = false;

#ifdef ENABLE_XR
public:
   int GetDisplayRefreshRateMode() const { return m_displayRefreshRateMode; }
   void SetDisplayRefreshRateMode(int mode);
   // Foveated rendering (see the private members below): level 0 to 3, eye-tracked when the headset allows it
   int GetFoveationMode() const { return m_foveationMode; }
   void SetFoveationMode(int mode);
   bool IsFoveationEyeTracked() const { return m_foveationEyeTracked; }
   void SetFoveationEyeTracked(bool eyeTracked);
   // One line for the settings page: "not supported", "fixed", "eye-tracked", ...
   string GetFoveationStatus() const;
   // Dynamic resolution (see the private members below): the render scale follows the GPU time of the frames reported by the runtime
   bool IsDynamicResolution() const { return m_dynamicResolution; }
   void SetDynamicResolution(bool enabled);
   float GetDynamicResolutionTarget() const { return m_dynamicResolutionTarget; }
   void SetDynamicResolutionTarget(float target) { m_dynamicResolutionTarget = clamp(target, 0.5f, 1.f); }
   float GetDynamicResolutionMinScale() const { return m_dynamicResolutionMinScale; }
   void SetDynamicResolutionMinScale(float minScale) { m_dynamicResolutionMinScale = clamp(minScale, 0.5f, 1.f); }
   // Scale the next frame renders at (1 when disabled, and while a table image is captured: the capture reads the whole swapchain image, at full sharpness)
   float GetDynamicRenderScale() const { return m_tableCaptureView != TableCaptureView::None ? 1.f : m_dynamicRenderScale.load(); }
   // One line for the settings page: "not supported", "off", or the current size, scale, GPU time and budget
   string GetDynamicResolutionStatus() const;
   // Rendering resolution, as a factor of the runtime's recommended size (Settings PlayerVR/ResFactor, 1 when not set). The swapchains are
   // created with the session, so a change is used by the next table played
   float GetResolutionFactor() const { return m_resolutionFactor; }
   void SetResolutionFactor(float factor) { m_resolutionFactor = factor; }
   // One line for the settings page: the size of the current table and, when it differs, the size the next table will use
   string GetResolutionStatus() const;
   bool IsOpenXRReady() const { return m_xrInstance != XR_NULL_HANDLE; }
   void SetupHMD();
   bool IsOpenXRHMDReady() const { return m_systemID != XR_NULL_SYSTEM_ID; }
   // Graphics backend the device was created for, and the one the settings ask for (the device must be created again if they differ)
   bgfx::RendererType::Enum GetRendererType() const { return m_rendererType; }
   static bgfx::RendererType::Enum SelectRendererType(const Settings& settings);
   // The runtime lost the session or the instance: the device can't be used anymore and must be created again
   bool IsLost() const { return m_lost; }
   void CreateSession(); // Opens the session if it was not kept from the previous table, then attaches the table to it (see AttachTable)
   void ReleaseSession(); // Detaches the table, then ends the session (letting the runtime stop it) and destroys it unless it is kept (the instance is kept)
   // Keep the session when the table is released, for the next table played with this device (see VPApp::AcquireVRDevice)
   void SetKeepSession(bool keep) { m_keepSession = keep; }
   void* GetGraphicContext() const;
   void* GetGraphicPlatformQueue() const;
   bgfx::RendererType::Enum GetGraphicContextType() const;
   void PollEvents();
   void RenderFrame(class RenderDevice* rd, const std::function<void(RenderTarget* vrRenderTarget)>& submitFrame);
   RenderTarget* GetUIRenderTarget() const { return m_uiRenderTarget; } // Acquired swapchain target for the UI composition layer (or nullptr if unsupported/not acquired yet)
   #if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: the performance panel (src/steamframe/PerfPanel.h), drawn in a quad layer standing left of the cabinet (see PerfPanelLayer.cpp).
   // Acquired swapchain target of the panel for the frame being prepared, only when the panel has new content (nullptr otherwise): the logic thread
   // draws the panel into it, then says so, else the image released is not shown
   RenderTarget* GetPerfPanelRenderTarget() const { return m_perfPanelRenderTarget; }
   void SetPerfPanelDrawn() { m_perfPanelDrawn = true; }
   // Steam Frame Edition: the loading message (see LiveUI::RenderLoadingScreen), drawn in a quad layer of its own standing in the table's forward direction
   // (src/steamframe/XRBridge.cpp), so that it stays there from one table to the next: between two tables, the bridge submits the frames with the last image of the layer.
   // Size of the layer in pixels of the UI (the UI panel is the width of the eye images), and acquired swapchain target of the layer for the frame
   // being prepared while the loading screen is shown (nullptr otherwise): the logic thread draws the message into it, then says so
   static constexpr int kLoadingLayerWidth = 2048;
   static constexpr int kLoadingLayerHeight = 256;
   RenderTarget* GetLoadingLayerRenderTarget() const { return m_loadingRenderTarget; }
   void SetLoadingLayerDrawn(int width, int height) // The part of the image covered by the message, from its top left
   {
      m_loadingDrawnWidth = width;
      m_loadingDrawnHeight = height;
      m_loadingDrawn = true;
   }
   void SetLoadingLayerShown(bool shown) { m_loadingShown = shown; } // By the logic thread, for each frame it prepares
   bool IsLoadingLayerUsable() const { return m_loadingLayerUsable; } // The message is drawn into the layer (else on the UI panel)
   // The bridge keeps the session fed with frames while no table is played, from the end of a table (ReleaseSession, when the session is kept) to
   // the first frame of the next one: the main thread says when the next table is about to draw it (OnTableReadyForFrames), and the render thread
   // of that table takes the frame loop back before its first frame (EndBridge)
   bool OnTableReadyForFrames(); // False when no bridge runs
   void EndBridge();
   // The graphics queue is shared with the runtime, which uses it in xrEndFrame: BGFX takes this lock while it starts or shuts down, as the bridge
   // may submit frames at the same time
   std::mutex& GetQueueMutex() { return m_queueMutex; }
   #endif
   void UpdateVisibilityMask(class RenderDevice* rd);
   bool UseDepthBuffer() const { return m_depthExtensionSupported; }
   bgfx::TextureFormat::Enum GetDepthFormat() const { return m_depthSwapchainInfo.format; }

   void DiscardVisibilityMask() { m_visibilityMask = nullptr; }
   std::shared_ptr<MeshBuffer> GetVisibilityMask() const { return m_visibilityMask; }

   Matrix3D* GetVisibilityMaskProjs() { return &m_nextProj[0]; }
   std::shared_ptr<MeshBuffer> GetGazeMarker() const { return m_gazeMarker; } // Debug ring around the foveation spot (VPX_FOVEATION_DEBUG), else null

   void EnableControllerViewCentering(bool enable) { m_controllerViewCentering = enable; }
   bool IsControllerViewCenteringEnabled() const { return m_controllerViewCentering; }

   // Models of the controllers the player holds, as the system shows them (XR_EXT_render_model with XR_EXT_interaction_render_model): the runtime gives
   // their glTF asset, places them and animates their parts (buttons, triggers, thumbsticks). Updated by the render thread before each frame is
   // prepared, so they are read while preparing a frame. A model that is not located (not tracked, hidden by the setting, table capture) is not drawn.
   struct ControllerModel
   {
      struct NodeState
      {
         XrPosef pose; // Relative to the parent node
         bool visible;
      };
      uint64_t id = 0; // Runtime render model id: a model with the same id keeps the same asset
      std::shared_ptr<const vector<uint8_t>> asset; // glTF binary (GLB)
      vector<string> animatableNodes; // Names of the glTF nodes moved by the runtime, in the order of nodeStates
      vector<NodeState> nodeStates;
      bool located = false;
      Matrix3D modelToReference; // glTF model space to the reference space, both in meters
   };
   const vector<ControllerModel>& GetControllerModels() const { return m_controllerModels; }
   // Reference space (meters) to the room space reference of the scene (VPU)
   const Matrix3D& GetReferenceToRoom() const { return m_referenceToRoom; }
   bool IsShowControllers() const { return m_showControllers; }
   void SetShowControllers(bool show) { m_showControllers = show; }
   // Transform from the space of a pose to the space it is given in (rotation then translation, for row vectors)
   static Matrix3D PoseToMatrix(const XrPosef& pose)
   {
      const float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
      return Matrix3D(
         1.f - 2.f * (y * y + z * z), 2.f * (x * y + z * w), 2.f * (x * z - y * w), 0.f,
         2.f * (x * y - z * w), 1.f - 2.f * (x * x + z * z), 2.f * (y * z + x * w), 0.f,
         2.f * (x * z + y * w), 2.f * (y * z - x * w), 1.f - 2.f * (x * x + y * y), 0.f,
         pose.position.x, pose.position.y, pose.position.z, 1.f);
   }

   enum class SwapchainType : uint8_t
   {
      COLOR,
      DEPTH
   };

   struct SwapchainInfo
   {
      XrSwapchain swapchain = XR_NULL_HANDLE;
      // Fragment density maps of the runtime (XR_FB_foveation_vulkan), one per swapchain image (invalid handle when the runtime gave none), see CreateFoveationTextures
      vector<bgfx::TextureHandle> foveationTextures;
      uint32_t foveationWidth = 0;
      uint32_t foveationHeight = 0;
      uint32_t width = 0;
      uint32_t height = 0;
      uint32_t arraySize = 0;
      bool isDepth = false;
      int64_t backendFormat = 0;
      bgfx::TextureFormat::Enum format;
      std::vector<bgfx::TextureHandle> imageViews;
   };

private:
   XrInstance m_xrInstance = XR_NULL_HANDLE;
   std::vector<const char*> m_activeAPILayers;
   std::vector<const char*> m_activeInstanceExtensions;
   std::vector<std::string> m_apiLayers;

   XrFormFactor m_formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
   XrSystemId m_systemID = {};
   XrSystemProperties m_systemProperties = { XR_TYPE_SYSTEM_PROPERTIES };

   XrSession m_session = {};
   XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
   bool m_sessionRunning = false;
   bool m_exitRequested = false; // We asked the runtime to stop the session (see EndSession), so its EXITING state is expected
   std::atomic<bool> m_lost = false;
   bool m_keepSession = false; // See SetKeepSession
   void OpenSession(); // The session and what lives as long as it: reference spaces, swapchains, actions
   void CloseSession(); // Ends and destroys the session (after DetachTable)
   void AttachTable(); // What depends on the table and on its BGFX: textures wrapping the swapchain images, table settings, input manager
   void DetachTable();
   void CreateSwapchains();
   void DestroySwapchains();
   void CreateSwapchainViews(); // The textures and render targets BGFX wraps the swapchain images with
   std::vector<int64_t> m_swapchainFormats; // Supported by the session, by order of preference of the runtime
   void EndSession();
   void DrainEvents();
   void UpdateEyeResolution(const Settings& settings);
   bool ComputeEyeResolution(float factor, unsigned int& width, unsigned int& height) const; // False when over the runtime's limits (then the recommended size)
   float m_resolutionFactor = 1.f;

   std::vector<XrViewConfigurationType> m_applicationViewConfigurations = { XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO };
   std::vector<XrViewConfigurationType> m_viewConfigurations;
   XrViewConfigurationType m_viewConfiguration = XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;
   std::vector<XrViewConfigurationView> m_viewConfigurationViews;

   SwapchainInfo m_colorSwapchainInfo = {};
   SwapchainInfo m_depthSwapchainInfo = {};
   SwapchainInfo m_uiSwapchainInfo = {};
   std::vector<std::unique_ptr<RenderTarget>> m_swapchainRenderTargets;
   std::vector<std::unique_ptr<RenderTarget>> m_uiRenderTargets;
   RenderTarget* m_uiRenderTarget = nullptr; // Written by the render thread before releasing the frame semaphore, read by the logic thread while recording the frame
   std::vector<XrEnvironmentBlendMode> m_applicationEnvironmentBlendModes = { XR_ENVIRONMENT_BLEND_MODE_OPAQUE, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE };
   std::vector<XrEnvironmentBlendMode> m_environmentBlendModes = {};
   XrEnvironmentBlendMode m_environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;

   XrSpace m_referenceSpace = XR_NULL_HANDLE;
   XrSpace m_viewSpace = XR_NULL_HANDLE; // Head-locked space for the UI quad layer

   bool m_headsetViewCentering = false;
   bool m_controllerViewCentering = false;
   XrSpace m_leftControllerSpace = XR_NULL_HANDLE;
   XrSpace m_rightControllerSpace = XR_NULL_HANDLE;

   // UI panel, in the reference space (meters): top left corner, and vectors along its full width and height
   bool m_uiPanelPlaced = false;
   bool m_uiPanelReplace = false; // Placed again at the next update
   bool m_uiPanelFollowsHead = false;
   vec3 m_uiPanelOrigin, m_uiPanelRight, m_uiPanelDown;
   Matrix3D m_uiPanelViewProj[2]; // Reference space to each eye clip space
   // UI pointer: the aim ray of a controller, intersected with the UI panel
   XrSpace m_leftAimSpace = XR_NULL_HANDLE;
   XrSpace m_rightAimSpace = XR_NULL_HANDLE;
   bool m_uiPointerValid = false;
   bool m_uiPointerPressed = false;
   float m_uiPointerX = 0.f;
   float m_uiPointerY = 0.f;
   float m_uiPointerScroll = 0.f;
   std::atomic<TableCaptureView> m_tableCaptureView = TableCaptureView::None;
   vector<std::pair<int, vec3>> m_tableCaptureBounds;
   void SetTableCaptureViewPoses(std::vector<XrView>& views, float vpuToWorldScale, TableCaptureView captureView) const;
   vec3 m_uiPointerRayStart, m_uiPointerRayEnd, m_uiHeadPos; // Reference space (meters)
   vec3 m_uiHeadForward { 0.f, 0.f, -1.f }; // Reference space, unit length
   void UpdateUIPanel(const std::vector<XrView>& views, XrTime time);

   struct RenderLayerInfo
   {
      XrTime predictedDisplayTime = 0;
      std::vector<XrCompositionLayerBaseHeader*> layers;
      XrCompositionLayerProjection layerProjection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
      std::vector<XrCompositionLayerProjectionView> layerProjectionViews;
      std::vector<XrCompositionLayerDepthInfoKHR> depthInfoViews;
      XrCompositionLayerQuad layerQuad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
      XrCompositionLayerPassthroughFB layerPassthrough = { XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB };
   };

   bool m_depthExtensionSupported = false;
   bool m_colorSpaceExtensionSupported = false;
   #if BX_PLATFORM_WINDOWS
   bool m_win32PerfCounterExtensionSupported = false;
   PFN_xrConvertTimeToWin32PerformanceCounterKHR m_xrConvertTimeToWin32PerformanceCounterKHR = nullptr;
   #elif BX_PLATFORM_ANDROID || BX_PLATFORM_LINUX
   bool m_convertTimespecTimeExtensionSupported = false;
   PFN_xrConvertTimeToTimespecTimeKHR m_xrConvertTimeToTimespecTimeKHR = nullptr;
   #endif
   bool m_displayRefreshRateExtensionSupported = false;
   PFN_xrGetDisplayRefreshRateFB m_xrGetDisplayRefreshRateFB = nullptr;
   PFN_xrRequestDisplayRefreshRateFB m_xrRequestDisplayRefreshRateFB = nullptr;
   int m_displayRefreshRateMode = 0;
   void ApplyDisplayRefreshRate();

   // Foveated rendering through the runtime (XR_FB_foveation and XR_META_foveation_eye_tracked, see Settings PlayerVR/Foveation). The runtime builds the
   // fragment density maps and applies them to the color swapchain; which render passes of ours benefit is up to the runtime (on the Steam Frame, Valve's
   // FDM injection layer does it), so the result is checked with the performance counters below rather than assumed.
   bool m_foveationExtensionSupported = false;
   bool m_foveationEyeTrackedExtensionSupported = false;
   bool m_foveationEyeTrackedSystemSupported = false;
   PFN_xrCreateFoveationProfileFB m_xrCreateFoveationProfileFB = nullptr;
   PFN_xrDestroyFoveationProfileFB m_xrDestroyFoveationProfileFB = nullptr;
   PFN_xrUpdateSwapchainFB m_xrUpdateSwapchainFB = nullptr;
   PFN_xrGetFoveationEyeTrackedStateMETA m_xrGetFoveationEyeTrackedStateMETA = nullptr;
   int m_foveationMode = 0;
   bool m_foveationEyeTracked = true;
   bool m_foveationApplied = false; // The profile below is what the color swapchain currently uses
   bool m_foveationEyeTrackedActive = false; // Last state reported by the runtime
   bool m_foveationFlipX = false;
   bool m_foveationFlipY = true;
   XrVector2f m_foveationCenter[2] = { { 0.f, 0.f }, { 0.f, 0.f } }; // Last gaze reported by the runtime, normalized (-1..1), per eye
   XrFoveationProfileFB m_foveationProfile = XR_NULL_HANDLE;
   void ApplyFoveation();
   void UpdateFoveationState();
   // Our own density map, one layer per eye, with the high density area in the middle and moved to the gaze with offsets. Needed because
   // the driver only honors offsets on maps created for them, which the runtime's maps are not; the runtime's profile is still applied
   // to the swapchain, it is what turns the eye tracking on.
   bgfx::TextureHandle m_ownFoveationMap = BGFX_INVALID_HANDLE;
   uint32_t m_ownFoveationMapWidth = 0;
   uint32_t m_ownFoveationMapHeight = 0;
   void CreateOwnFoveationMap();
   void FillOwnFoveationMap();
   // Shading rate image (VK_KHR_fragment_shading_rate), preferred over the density map where the driver has it since Turnip applies it in
   // its direct render path too: one R8U rate code per texel of the scene buffer, a single image for both eyes, rewritten whenever the gaze
   // moves with full rate around each eye's gaze (see UpdateShadingRateMap)
   bgfx::TextureHandle m_shadingRateMap = BGFX_INVALID_HANDLE;
   uint32_t m_shadingRateMapWidth = 0;
   uint32_t m_shadingRateMapHeight = 0;
   float m_shadingRateMapKey[6] = { -1.f, 0.f, 0.f, 0.f, 0.f, 0.f }; // Level, scale and disc centers of the last upload
   bool m_foveationShadingRate = false; // The scene is foveated through the shading rate image (else the density map)
   bool ReadGaze(); // Updates m_foveationCenter from the runtime, true when the gaze is valid
   // Gaze as a ray (XR_EXT_eye_gaze_interaction), preferred over the runtime's foveation center since it can be taken with the head pose of its
   // sample and projected with the views of the frame: the point looked at stays put while the head moves. See LocateGaze.
   bool m_eyeGazeExtensionSupported = false;
   bool m_eyeGazeSystemSupported = false;
   XrSpace m_gazeSpace = XR_NULL_HANDLE;
   bool m_gazeRayValid = false; // For the current frame
   bool m_gazePointValid = false; // Smoothed fixated point, in the reference space
   XrVector3f m_gazePoint {};
   XrTime m_gazePointTime = 0;
   void LocateGaze(const std::vector<XrView>& views, XrTime displayTime);
   bool UpdateShadingRateMap(bool gazeValid); // False when the image is not available
   // VPX_FOVEATION_DEBUG: a ring drawn around the full quality spot of each eye, so the spot can be compared with the gaze in the headset
   XrFovf m_viewFov[2] = {};
   std::shared_ptr<MeshBuffer> m_gazeMarker;
   void UpdateGazeMarker(const float* cxNdc, const float* cyNdc, float radiusNdc); // Image space (y down), one center per eye

   // Performance counters of the runtime (XR_META_performance_metrics), logged periodically to compare settings on the device
   bool m_performanceMetricsExtensionSupported = false;
   PFN_xrEnumeratePerformanceMetricsCounterPathsMETA m_xrEnumeratePerformanceMetricsCounterPathsMETA = nullptr;
   PFN_xrSetPerformanceMetricsStateMETA m_xrSetPerformanceMetricsStateMETA = nullptr;
   PFN_xrQueryPerformanceMetricsCounterMETA m_xrQueryPerformanceMetricsCounterMETA = nullptr;
   vector<std::pair<string, XrPath>> m_performanceCounters;
   bool m_performanceCountersEnabled = false; // Enabled on the current session (see AttachTable and CloseSession)
   void EnablePerformanceCounters(); // On the current session, if the runtime has them and they are not enabled yet
   XrPath m_gpuFrameTimePath = XR_NULL_PATH; // Counter of the application's GPU time per frame (/perfmetrics_meta/app/gpu_frametime), if listed

   // Dynamic resolution (Settings PlayerVR/DynamicResolution): the scene is rendered into a part of its buffers (RenderDevice::BeginScaledRendering)
   // and the runtime is told which part of the swapchain image was drawn, so the resolution follows the cost of the table without a restart.
   // The render thread adjusts the scale every frame from the application's GPU time reported by the runtime (XR_META_performance_metrics)
   // against the target fraction of the frame period: down at once when the frame is over it, up slowly when it is well under; between
   // the runtime's recommended size and the size of the swapchain (the table's ResFactor)
   std::atomic<bool> m_dynamicResolution = false; // Set by the settings page while the render thread reads it
   std::atomic<float> m_dynamicResolutionTarget = 0.85f; // GPU time target, as a fraction of the frame period
   std::atomic<float> m_dynamicResolutionMinScale = 0.7f; // Lowest scale, as a fraction of the swapchain size
   std::atomic<float> m_dynamicRenderScale = 1.f; // Scale given to the renderer for the next frame
   std::atomic<float> m_lastGpuFrameTimeMs = 0.f; // Last reading of the counter, for the status
   std::atomic<float> m_frameBudgetMs = 0.f; // Frame period of the display: the shortest predicted display period of the session (see UpdateDynamicResolution)
   std::atomic<bool> m_dynamicResolutionHeld = false; // The last reading was taken for a frame pacing stall, not GPU work: the scale was kept
   XrTime m_lastPredictedDisplayTime = 0; // Of the previous frame: a step of more than one frame period means a frame missed its display
   unsigned int m_framesSinceMissed = 0;
   unsigned int m_missedFrames = 0, m_heldReadings = 0, m_stallBreaks = 0; // Since the last status log
   unsigned int m_stallWindowFrames = 0, m_stallWindowHeld = 0; // Readings, and held readings, in the current second
   unsigned int m_stallBreaksLeft = 0; // For the current table (see UpdateDynamicResolution)
   unsigned int m_framesSinceDecrease = 0; // The scale rises again a quarter of a second after it was lowered (see UpdateDynamicResolution)
   float m_smoothedGpuMs = 0.f; // GPU time the scale follows: the readings averaged over a few frames (0 until the first reading of the table)
   void UpdateDynamicResolution(const XrDuration predictedDisplayPeriod, const XrTime predictedDisplayTime); // Render thread, once per frame before the frame is prepared
   double m_nextStatusLogTime = 0.;
   void LogRuntimeStatus();

   #if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: the performance panel (src/steamframe/PerfPanelLayer.cpp). A small swapchain of its own, of which an image is acquired only
   // for the frames that draw new content (every 0.5 s): between them, the quad layer shows the image released last
   SwapchainInfo m_perfPanelSwapchainInfo = {};
   std::vector<std::unique_ptr<RenderTarget>> m_perfPanelRenderTargets;
   RenderTarget* m_perfPanelRenderTarget = nullptr; // As m_uiRenderTarget: written by the render thread before the frame is prepared
   bool m_perfPanelImageAcquired = false;
   std::atomic<bool> m_perfPanelDrawn = false; // The logic thread drew into the acquired image
   bool m_perfPanelHasImage = false; // An image drawn by the logic thread was released: the layer can be shown
   XrCompositionLayerQuad m_perfPanelLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
   void ReportPerfPanelFrame(int periods, float gpuMs); // From UpdateDynamicResolution
   void AcquirePerfPanelImage(class RenderDevice* rd); // Before the frame is prepared
   void ReleasePerfPanelImage(); // After the frame is submitted
   void AddPerfPanelLayer(std::vector<XrCompositionLayerBaseHeader*>& layers);
   void DestroyPerfPanel(); // With the session

   SwapchainInfo m_loadingSwapchainInfo = {};
   std::vector<std::unique_ptr<RenderTarget>> m_loadingRenderTargets;
   RenderTarget* m_loadingRenderTarget = nullptr; // As m_uiRenderTarget: written by the render thread before the frame is prepared
   bool m_loadingImageAcquired = false; // Kept acquired until drawn (see AcquireLoadingLayerImage)
   uint32_t m_loadingImageIndex = 0;
   std::atomic<bool> m_loadingDrawn = false; // The logic thread drew into the acquired image
   std::atomic<int> m_loadingDrawnWidth = 0, m_loadingDrawnHeight = 0;
   XrExtent2Di m_loadingLayerRect = { 0, 0 }; // Part of the last image released that the layer shows
   std::atomic<bool> m_loadingShown = false; // The loading screen is shown (by the last frame prepared, kept from one table to the next)
   bool m_loadingHasImage = false; // An image drawn by the logic thread was released: the layer can be shown
   XrExtent2Df m_loadingLayerSize = { 0.f, 0.f }; // Meters, and distance from the player: taken when an image is drawn, the bridge shows it as it was
   float m_loadingLayerDistance = 1.f;
   bool m_loadingLayerPlaced = false; // Placed when the message appears, kept until it is hidden (see AddLoadingLayer)
   XrPosef m_loadingLayerPose = { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } }; // In the reference space
   std::atomic<bool> m_loadingLayerUsable = false; // For the logic thread: the layer can be drawn for this table
   XrCompositionLayerQuad m_loadingLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
   void CreateLoadingLayerSwapchain(); // With the other swapchains
   void CreateLoadingLayerViews();
   void DestroyLoadingLayerViews();
   void DestroyLoadingLayerSwapchain();
   bool IsLoadingLayerAvailable() const;
   void AcquireLoadingLayerImage(class RenderDevice* rd); // Before the frame is prepared
   void ReleaseLoadingLayerImage(); // After the frame is submitted
   void AddLoadingLayer(std::vector<XrCompositionLayerBaseHeader*>& layers);

   std::thread m_bridgeThread;
   std::atomic<bool> m_bridgeStop = false;
   std::atomic<bool> m_bridgeRunning = false; // For the main thread (the thread object belongs to the render threads)
   std::mutex m_queueMutex;
   std::mutex m_tableReadyMutex;
   std::condition_variable m_tableReadyCondition;
   bool m_tableReady = false;
   bool m_runtimeExitDuringBridge = false; // The runtime asked the application to quit while the bridge submitted the frames
   std::chrono::steady_clock::time_point m_bridgeEndTime {}; // When the render thread took the frames back from the bridge, to log the time to its first frame
   void StartBridge(); // From the render thread, once the table is detached
   void StopBridge();
   void BridgeLoop();
   void BridgePollEvents();
   #endif

   Matrix3D m_nextProj[2];

   bool m_debugUtilsExtensionSupported = false;
   XrDebugUtilsMessengerEXT m_debugUtilsMessenger = XR_NULL_HANDLE;
   static XrBool32 OpenXRMessageCallbackFunction(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

   // Controller models (see GetControllerModels), with the runtime objects of each model at the same index
   bool m_renderModelExtensionSupported = false;
   std::atomic<bool> m_showControllers = true; // Set by the settings page while the render thread reads it
   bool m_controllerModelsDirty = true; // The list of models must be asked again to the runtime
   double m_controllerModelsRetryTime = 0.; // When the list is asked again after an asset was not available
   vector<ControllerModel> m_controllerModels;
   struct RenderModelHandles
   {
      #ifdef XR_EXT_render_model
      XrRenderModelEXT renderModel = XR_NULL_HANDLE;
      #endif
      XrSpace space = XR_NULL_HANDLE;
   };
   vector<RenderModelHandles> m_renderModelHandles;
   Matrix3D m_referenceToRoom;
   void UpdateControllerModels();
   void LocateControllerModels(XrTime time);
   void DestroyControllerModels();
   #if defined(XR_EXT_render_model) && defined(XR_EXT_interaction_render_model)
   PFN_xrCreateRenderModelEXT m_xrCreateRenderModelEXT = nullptr;
   PFN_xrDestroyRenderModelEXT m_xrDestroyRenderModelEXT = nullptr;
   PFN_xrGetRenderModelPropertiesEXT m_xrGetRenderModelPropertiesEXT = nullptr;
   PFN_xrCreateRenderModelSpaceEXT m_xrCreateRenderModelSpaceEXT = nullptr;
   PFN_xrCreateRenderModelAssetEXT m_xrCreateRenderModelAssetEXT = nullptr;
   PFN_xrDestroyRenderModelAssetEXT m_xrDestroyRenderModelAssetEXT = nullptr;
   PFN_xrGetRenderModelAssetDataEXT m_xrGetRenderModelAssetDataEXT = nullptr;
   PFN_xrGetRenderModelAssetPropertiesEXT m_xrGetRenderModelAssetPropertiesEXT = nullptr;
   PFN_xrGetRenderModelStateEXT m_xrGetRenderModelStateEXT = nullptr;
   PFN_xrEnumerateInteractionRenderModelIdsEXT m_xrEnumerateInteractionRenderModelIdsEXT = nullptr;
   vector<XrRenderModelNodeStateEXT> m_renderModelNodeStates; // Scratch buffer for LocateControllerModels
   struct ControllerModelDebug
   {
      XrResult locateResult;
      XrSpaceLocationFlags flags;
      XrPosef pose;
      XrResult stateResult;
      int visibleNodes;
   };
   ControllerModelDebug m_controllerModelsDebug[4] {}; // Last results of LocateControllerModels, logged by LogRuntimeStatus
   #endif

   bool m_visibilityMaskExtensionSupported = false;
   PFN_xrGetVisibilityMaskKHR xrGetVisibilityMaskKHR = nullptr;
   bool m_visibilityMaskDirty = true;
   std::shared_ptr<MeshBuffer> m_visibilityMask;

   bool m_passthroughExtensionSupported = false;
   XrPassthroughFB m_passthrough = XR_NULL_HANDLE;
   XrPassthroughLayerFB m_passthroughLayer = XR_NULL_HANDLE;
   bool m_passthroughEnabled = false;

   bgfx::RendererType::Enum m_rendererType;
   std::unique_ptr<class XRGraphicBackend> m_backend;

   float m_sceneSize = 0.f;

   XRInputHandler* m_xrInputHandler = nullptr;
   std::unique_ptr<XRInputHandler> m_xrInputHandlerOwned; // The handler while no table's input manager has it (see AttachTable and DetachTable)
#endif
};
