// license:GPLv3+

// Simulated OpenXR runtime of the macOS build, see XRSim.h. Only what VRDevice, XRInputHandler and the UI panel use is implemented, the
// other functions and extensions are reported as unsupported.

#include "core/stdafx.h"
#include "XRSim.h"

#include <openxr/openxr.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace
{

constexpr int64_t kColorFormatSRGB = 1, kColorFormat = 2, kDepthFormat = 3; // Opaque formats, see XRSimBackend
constexpr const char* kFrameProfile = "/interaction_profiles/valve/frame_controller_valve";

struct Quat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };

Quat Mul(const Quat& a, const Quat& b)
{
   return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

Quat AxisAngle(float ax, float ay, float az, float angle)
{
   const float s = sinf(angle * 0.5f);
   return { ax * s, ay * s, az * s, cosf(angle * 0.5f) };
}

// Yaw around +y (to the left), then pitch around the rotated +x (up), then roll around the rotated -z (to the right)
Quat FromEuler(float yawDeg, float pitchDeg, float rollDeg)
{
   constexpr float d2r = 3.14159265f / 180.f;
   return Mul(Mul(AxisAngle(0.f, 1.f, 0.f, yawDeg * d2r), AxisAngle(1.f, 0.f, 0.f, pitchDeg * d2r)), AxisAngle(0.f, 0.f, -1.f, rollDeg * d2r));
}

void Rotate(const Quat& q, float& x, float& y, float& z)
{
   // v + 2w(u x v) + 2u x (u x v)
   const float tx = 2.f * (q.y * z - q.z * y), ty = 2.f * (q.z * x - q.x * z), tz = 2.f * (q.x * y - q.y * x);
   const float rx = x + q.w * tx + (q.y * tz - q.z * ty), ry = y + q.w * ty + (q.z * tx - q.x * tz), rz = z + q.w * tz + (q.x * ty - q.y * tx);
   x = rx; y = ry; z = rz;
}

XrPosef ToXr(const XRSim::Pose& p)
{
   const Quat q = FromEuler(p.yaw, p.pitch, p.roll);
   return { { q.x, q.y, q.z, q.w }, { p.x, p.y, p.z } };
}

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

XRSim::Pose Lerp(const XRSim::Pose& a, const XRSim::Pose& b, float k)
{
   auto l = [k](float u, float v) { return u + (v - u) * k; };
   return { l(a.x, b.x), l(a.y, b.y), l(a.z, b.z), l(a.yaw, b.yaw), l(a.pitch, b.pitch), l(a.roll, b.roll) };
}

// A pose eased from one value to another
struct Animated
{
   XRSim::Pose from, to;
   double start = 0., duration = 0.;
   XRSim::Pose Get(double t) const
   {
      if (duration <= 0. || t >= start + duration)
         return to;
      float k = static_cast<float>((t - start) / duration);
      k = k * k * (3.f - 2.f * k);
      return Lerp(from, to, k);
   }
   void Set(const XRSim::Pose& p, float dur, double t)
   {
      from = Get(t);
      to = p;
      start = t;
      duration = dur;
   }
};

struct ControllerModel
{
   struct Motion
   {
      std::string input; // Input path (without the hand), its value moves the node
      bool rotate = false;
      float pivot[3] {}, axis[3] {}; // Model space (meters)
      float amount = 0.f; // Degrees or meters when the input is 1
   };
   std::vector<uint8_t> glb;
   std::vector<std::string> nodes; // Animatable nodes: the components of the asset, at rest when they have no motion
   std::vector<Motion> motions;
   XrPosef aim { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } }, grip { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } }; // OpenXR aim and grip poses in model space
};

struct Space { enum Kind { Reference, Action, RenderModel } kind; XrReferenceSpaceType refType = XR_REFERENCE_SPACE_TYPE_STAGE; std::string path; int hand = -1; XrPosef offset {}; };
struct Action { std::string path; XrActionType type; float current = 0.f; bool changed = false; }; // State of the last sync
struct Swapchain { uint32_t count = 3, next = 0; };

struct State
{
   std::mutex mutex;
   bool instance = false;
   bool session = false;
   std::deque<XrEventDataSessionStateChanged> events;
   std::vector<std::string> paths;
   uint32_t eyeWidth = 1920, eyeHeight = 1080;
   float fovH = 90.f;
   double fps = 60.;
   double nextFrame = 0.;
   double epoch = Now();
   Animated head, aim[2];
   bool handVisible[2] = { false, true };
   float sway = 0.f;
   std::map<std::string, float> inputs;
   std::vector<Action*> actions;
   ControllerModel models[2];
   bool modelsLoaded = false;

   XrTime ToXrTime(double t) const { return static_cast<XrTime>((t - epoch) * 1e9) + 1000000000LL; }
   double FromXrTime(XrTime t) const { return static_cast<double>(t - 1000000000LL) * 1e-9 + epoch; }

   // Small head motion: a few sines of unrelated periods, like a head held still
   XRSim::Pose Sway(double t) const
   {
      XRSim::Pose s { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
      if (sway <= 0.f)
         return s;
      const float a = sway;
      s.x = 0.01f * a * static_cast<float>(sin(t * 0.61) * 0.6 + sin(t * 1.37 + 1.) * 0.4);
      s.y = 0.01f * a * static_cast<float>(sin(t * 0.83 + 2.) * 0.5 + sin(t * 1.9) * 0.2);
      s.z = 0.01f * a * static_cast<float>(sin(t * 0.47 + 0.5) * 0.5);
      s.yaw = a * static_cast<float>(sin(t * 0.37 + 0.3) * 0.7 + sin(t * 1.13) * 0.3);
      s.pitch = a * static_cast<float>(sin(t * 0.53 + 1.7) * 0.6 + sin(t * 1.61 + 0.2) * 0.25);
      s.roll = 0.4f * a * static_cast<float>(sin(t * 0.29 + 2.2));
      return s;
   }
   XRSim::Pose HeadAt(double t) const
   {
      XRSim::Pose p = head.Get(t);
      const XRSim::Pose s = Sway(t);
      p.x += s.x; p.y += s.y; p.z += s.z; p.yaw += s.yaw; p.pitch += s.pitch; p.roll += s.roll;
      return p;
   }
   // The hands move with the body (the sway of the head, damped)
   XRSim::Pose AimAt(int hand, double t) const
   {
      XRSim::Pose p = aim[hand].Get(t);
      const XRSim::Pose s = Sway(t - 0.15);
      p.x += 0.5f * s.x; p.y += 0.5f * s.y; p.z += 0.5f * s.z; p.yaw += 0.3f * s.yaw; p.pitch += 0.3f * s.pitch;
      return p;
   }
   XrPath ToPath(const std::string& s)
   {
      for (size_t i = 0; i < paths.size(); i++)
         if (paths[i] == s)
            return static_cast<XrPath>(i + 1);
      paths.push_back(s);
      return static_cast<XrPath>(paths.size());
   }
   void Push(XrSession session, XrSessionState s)
   {
      XrEventDataSessionStateChanged e { XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED };
      e.session = session;
      e.state = s;
      e.time = ToXrTime(Now());
      events.push_back(e);
   }
};

State& S()
{
   static State s;
   return s;
}

// SteamVR component frame: origin and rotate_xyz (degrees, around x then y then z)
XrPosef ComponentPose(const nlohmann::json& local)
{
   constexpr float d2r = 3.14159265f / 180.f;
   const auto& r = local["rotate_xyz"];
   const Quat q = Mul(Mul(AxisAngle(0.f, 0.f, 1.f, r[2].get<float>() * d2r), AxisAngle(0.f, 1.f, 0.f, r[1].get<float>() * d2r)), AxisAngle(1.f, 0.f, 0.f, r[0].get<float>() * d2r));
   const auto& o = local["origin"];
   return { { q.x, q.y, q.z, q.w }, { o[0].get<float>(), o[1].get<float>(), o[2].get<float>() } };
}

// Models made by trailer-vr/tools/obj2glb.py from the SteamVR render models of the Frame controllers: the root node gives the aim and grip
// poses in model space, the component nodes their motions
void LoadControllerModels(State& s)
{
   if (s.modelsLoaded)
      return;
   s.modelsLoaded = true;
   const char* dir = getenv("VPX_XRSIM_CONTROLLERS");
   if (dir == nullptr)
      return;
   for (int hand = 0; hand < 2; hand++)
   {
      const std::string path = std::string(dir) + (hand == 0 ? "/left.glb" : "/right.glb");
      std::ifstream f(path, std::ios::binary);
      if (!f)
      {
         PLOGW << "XR simulator: no controller model " << path;
         continue;
      }
      ControllerModel& m = s.models[hand];
      m.glb.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      // GLB: 12 bytes header, then the JSON chunk (length, type, data)
      if (m.glb.size() < 20)
         continue;
      uint32_t jsonLength;
      memcpy(&jsonLength, m.glb.data() + 12, 4);
      try
      {
         const nlohmann::json gltf = nlohmann::json::parse(m.glb.begin() + 20, m.glb.begin() + 20 + jsonLength);
         const auto& nodes = gltf["nodes"];
         if (const auto& extras = nodes[0].value("extras", nlohmann::json::object()); extras.contains("openxr_aim"))
         {
            m.aim = ComponentPose(extras["openxr_aim"]);
            m.grip = ComponentPose(extras["openxr_grip"]);
         }
         for (size_t i = 1; i < nodes.size(); i++)
         {
            m.nodes.push_back(nodes[i]["name"].get<std::string>());
            ControllerModel::Motion motion;
            if (nodes[i].contains("extras"))
            {
               const auto& mo = nodes[i]["extras"]["motion"];
               const std::string type = mo["type"].get<std::string>();
               if ((type == "rotate" || type == "translate") && mo.contains("axis"))
               {
                  motion.rotate = type == "rotate";
                  if (mo.contains("trigger_path"))
                  {
                     const std::string p = mo["trigger_path"].get<std::string>();
                     motion.input = p == "/input/grip" ? "/input/squeeze/value"s : p + "/value";
                  }
                  else if (mo.contains("pressed_path"))
                     motion.input = mo["pressed_path"].get<std::string>();
                  for (int k = 0; k < 3; k++)
                  {
                     motion.axis[k] = mo["axis"][k].get<float>();
                     motion.pivot[k] = mo.contains("pivot") ? mo["pivot"][k].get<float>() : 0.f;
                  }
                  motion.amount = mo["value_mapping"][1].get<float>();
               }
            }
            m.motions.push_back(motion);
         }
      }
      catch (const std::exception& e)
      {
         PLOGE << "XR simulator: controller model " << path << ": " << e.what();
         m.glb.clear();
         continue;
      }
      PLOGI << "XR simulator: controller model " << path << ", " << m.glb.size() / 1024 << " KB, " << m.nodes.size() << " components";
   }
}

XrPosef Compose(const XrPosef& a, const XrPosef& b) // b given in the space of a
{
   const Quat qa { a.orientation.x, a.orientation.y, a.orientation.z, a.orientation.w }, qb { b.orientation.x, b.orientation.y, b.orientation.z, b.orientation.w };
   float x = b.position.x, y = b.position.y, z = b.position.z;
   Rotate(qa, x, y, z);
   const Quat q = Mul(qa, qb);
   return { { q.x, q.y, q.z, q.w }, { a.position.x + x, a.position.y + y, a.position.z + z } };
}

XrPosef Inverse(const XrPosef& p)
{
   const Quat q { -p.orientation.x, -p.orientation.y, -p.orientation.z, p.orientation.w };
   float x = -p.position.x, y = -p.position.y, z = -p.position.z;
   Rotate(q, x, y, z);
   return { { q.x, q.y, q.z, q.w }, { x, y, z } };
}

template <typename T> T* Get(void* handle) { return reinterpret_cast<T*>(handle); }

XrResult CopyString(const std::string& s, uint32_t capacity, uint32_t* count, char* buffer)
{
   *count = static_cast<uint32_t>(s.size() + 1);
   if (capacity == 0)
      return XR_SUCCESS;
   if (capacity < *count)
      return XR_ERROR_SIZE_INSUFFICIENT;
   memcpy(buffer, s.c_str(), s.size() + 1);
   return XR_SUCCESS;
}

template <typename T> XrResult CopyArray(const std::vector<T>& v, uint32_t capacity, uint32_t* count, T* out)
{
   *count = static_cast<uint32_t>(v.size());
   if (capacity == 0)
      return XR_SUCCESS;
   if (capacity < v.size())
      return XR_ERROR_SIZE_INSUFFICIENT;
   std::copy(v.begin(), v.end(), out);
   return XR_SUCCESS;
}

const char* const kExtensions[] = { "XR_KHR_metal_enable", "XR_VALVE_frame_controller_interaction", "XR_EXT_uuid", "XR_EXT_render_model", "XR_EXT_interaction_render_model" };

enum class HandSpace { Aim, Grip, Model };

// The script sets the aim pose: the grip pose and the model follow from the model's frames (identity without a model)
XrPosef HandPose(State& s, int hand, double t, HandSpace space)
{
   const XrPosef aim = ToXr(s.AimAt(hand, t));
   if (space == HandSpace::Aim)
      return aim;
   const ControllerModel& m = s.models[hand];
   const XrPosef model = Compose(aim, Inverse(m.aim));
   return space == HandSpace::Model ? model : Compose(model, m.grip);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------- application API

namespace XRSim
{
void SetHeadPose(const Pose& pose, float duration) { std::lock_guard lock(S().mutex); S().head.Set(pose, duration, Now()); }
Pose GetHeadPose() { std::lock_guard lock(S().mutex); return S().head.Get(Now()); }
void SetHeadSway(float amplitude) { std::lock_guard lock(S().mutex); S().sway = amplitude; }
void SetAimPose(Hand hand, const Pose& pose, float duration) { std::lock_guard lock(S().mutex); S().aim[hand].Set(pose, duration, Now()); S().handVisible[hand] = true; }
void SetHandVisible(Hand hand, bool visible) { std::lock_guard lock(S().mutex); S().handVisible[hand] = visible; }
void AimAt(Hand hand, float x, float y, float z, float duration)
{
   std::lock_guard lock(S().mutex);
   Pose p = S().aim[hand].to;
   const float dx = x - p.x, dy = y - p.y, dz = z - p.z;
   p.yaw = atan2f(-dx, -dz) * 180.f / 3.14159265f;
   p.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) * 180.f / 3.14159265f;
   S().aim[hand].Set(p, duration, Now());
   S().handVisible[hand] = true;
}
void SetInput(const std::string& path, float value) { std::lock_guard lock(S().mutex); S().inputs[path] = value; }
float GetInput(const std::string& path) { std::lock_guard lock(S().mutex); const auto it = S().inputs.find(path); return it == S().inputs.end() ? 0.f : it->second; }
bool IsActive() { return S().instance; }
}

// ---------------------------------------------------------------------------------------------------------------- OpenXR

extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
   snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_RESULT_%d", static_cast<int>(value));
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateApiLayerProperties(uint32_t, uint32_t* count, XrApiLayerProperties*)
{
   *count = 0;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char*, uint32_t capacity, uint32_t* count, XrExtensionProperties* properties)
{
   std::vector<const char*> extensions(std::begin(kExtensions), std::end(kExtensions));
   if (getenv("VPX_XRSIM_CONTROLLERS") == nullptr)
      extensions.resize(2);
   *count = static_cast<uint32_t>(extensions.size());
   if (capacity == 0)
      return XR_SUCCESS;
   if (capacity < extensions.size())
      return XR_ERROR_SIZE_INSUFFICIENT;
   for (size_t i = 0; i < extensions.size(); i++)
   {
      strncpy(properties[i].extensionName, extensions[i], XR_MAX_EXTENSION_NAME_SIZE);
      properties[i].extensionVersion = 1;
   }
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo*, XrInstance* instance)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   if (const char* eye = getenv("VPX_XRSIM_EYE"); eye)
      sscanf(eye, "%ux%u", &s.eyeWidth, &s.eyeHeight);
   if (const char* fov = getenv("VPX_XRSIM_FOV"); fov)
      s.fovH = static_cast<float>(atof(fov));
   if (const char* fps = getenv("VPX_XRSIM_FPS"); fps)
      s.fps = atof(fps);
   if (const char* pose = getenv("VPX_XRSIM_POSE"); pose)
   {
      XRSim::Pose p;
      sscanf(pose, "%f,%f,%f,%f,%f", &p.x, &p.y, &p.z, &p.yaw, &p.pitch);
      s.head.Set(p, 0.f, Now());
   }
   if (s.aim[1].to.y == 1.65f) // Default controllers, held in front of the player at the waist, pointing ahead and a little up
   {
      s.aim[0].Set({ -0.2f, 1.1f, -0.3f, 0.f, 10.f, 0.f }, 0.f, Now());
      s.aim[1].Set({ 0.2f, 1.1f, -0.3f, 0.f, 10.f, 0.f }, 0.f, Now());
   }
   s.instance = true;
   *instance = reinterpret_cast<XrInstance>(&s);
   PLOGI << "XR simulator: " << s.eyeWidth << 'x' << s.eyeHeight << " per eye, " << s.fovH << " degrees, " << s.fps << " Hz";
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance)
{
   S().instance = false;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance, XrInstanceProperties* properties)
{
   strncpy(properties->runtimeName, "VPX XR Simulator", XR_MAX_RUNTIME_NAME_SIZE);
   properties->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* eventData)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   if (s.events.empty())
      return XR_EVENT_UNAVAILABLE;
   memcpy(eventData, &s.events.front(), sizeof(XrEventDataSessionStateChanged));
   s.events.pop_front();
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char* pathString, XrPath* path)
{
   std::lock_guard lock(S().mutex);
   *path = S().ToPath(pathString);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(XrInstance, XrPath path, uint32_t capacity, uint32_t* count, char* buffer)
{
   std::lock_guard lock(S().mutex);
   if (path == XR_NULL_PATH || path > S().paths.size())
      return XR_ERROR_PATH_INVALID;
   return CopyString(S().paths[path - 1], capacity, count, buffer);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo* getInfo, XrSystemId* systemId)
{
   if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
      return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
   if (getenv("VPX_XRSIM") == nullptr) // The headset is only there when asked for
      return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
   *systemId = 1;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId systemId, XrSystemProperties* properties)
{
   properties->systemId = systemId;
   properties->vendorId = 0;
   strncpy(properties->systemName, "VPX XR Simulator", XR_MAX_SYSTEM_NAME_SIZE);
   properties->graphicsProperties = { 8192, 8192, 16 };
   properties->trackingProperties = { XR_TRUE, XR_TRUE };
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types)
{
   return CopyArray(std::vector<XrViewConfigurationType> { XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO }, capacity, count, types);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrViewConfigurationView* views)
{
   *count = 2;
   if (capacity == 0)
      return XR_SUCCESS;
   if (capacity < 2)
      return XR_ERROR_SIZE_INSUFFICIENT;
   for (int i = 0; i < 2; i++)
   {
      views[i].recommendedImageRectWidth = views[i].maxImageRectWidth = S().eyeWidth;
      views[i].recommendedImageRectHeight = views[i].maxImageRectHeight = S().eyeHeight;
      views[i].recommendedSwapchainSampleCount = views[i].maxSwapchainSampleCount = 1;
   }
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes)
{
   return CopyArray(std::vector<XrEnvironmentBlendMode> { XR_ENVIRONMENT_BLEND_MODE_OPAQUE }, capacity, count, modes);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   s.session = true;
   *session = reinterpret_cast<XrSession>(&s.session);
   s.Push(*session, XR_SESSION_STATE_IDLE);
   s.Push(*session, XR_SESSION_STATE_READY);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession)
{
   S().session = false;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo*)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   s.Push(session, XR_SESSION_STATE_SYNCHRONIZED);
   s.Push(session, XR_SESSION_STATE_VISIBLE);
   s.Push(session, XR_SESSION_STATE_FOCUSED);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession session)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   s.Push(session, XR_SESSION_STATE_VISIBLE);
   s.Push(session, XR_SESSION_STATE_SYNCHRONIZED);
   s.Push(session, XR_SESSION_STATE_STOPPING);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession session)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   s.Push(session, XR_SESSION_STATE_IDLE);
   s.Push(session, XR_SESSION_STATE_EXITING);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession, uint32_t capacity, uint32_t* count, XrReferenceSpaceType* spaces)
{
   return CopyArray(std::vector<XrReferenceSpaceType> { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE }, capacity, count, spaces);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space)
{
   Space* sp = new Space { Space::Reference };
   sp->refType = createInfo->referenceSpaceType;
   *space = reinterpret_cast<XrSpace>(sp);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo* createInfo, XrSpace* space)
{
   const Action* action = Get<Action>(createInfo->action);
   Space* sp = new Space { Space::Action };
   sp->path = action->path;
   sp->hand = action->path.starts_with("/user/hand/left") ? 0 : action->path.starts_with("/user/hand/right") ? 1 : -1;
   *space = reinterpret_cast<XrSpace>(sp);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace space)
{
   delete Get<Space>(space);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime time, XrSpaceLocation* location)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   const Space* sp = Get<Space>(space);
   const double t = s.FromXrTime(time);
   location->locationFlags = 0;
   location->pose = { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } };
   constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
   if (sp->kind == Space::Reference)
   {
      if (sp->refType == XR_REFERENCE_SPACE_TYPE_VIEW)
         location->pose = ToXr(s.HeadAt(t));
      location->locationFlags = valid;
      return XR_SUCCESS;
   }
   if (sp->hand < 0 || !s.handVisible[sp->hand])
      return XR_SUCCESS;
   LoadControllerModels(s);
   location->pose = HandPose(s, sp->hand, t, sp->kind == Space::RenderModel ? HandSpace::Model : sp->path.find("/grip/") != std::string::npos ? HandSpace::Grip : HandSpace::Aim);
   location->locationFlags = valid;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats)
{
   return CopyArray(std::vector<int64_t> { getenv("VPX_XRSIM_SRGB") ? kColorFormatSRGB : kColorFormat, getenv("VPX_XRSIM_SRGB") ? kColorFormat : kColorFormatSRGB, kDepthFormat }, capacity, count, formats);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo*, XrSwapchain* swapchain)
{
   *swapchain = reinterpret_cast<XrSwapchain>(new Swapchain());
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain)
{
   delete Get<Swapchain>(swapchain);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader*)
{
   *count = Get<Swapchain>(swapchain)->count;
   return XR_SUCCESS; // The images are created by the backend
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index)
{
   Swapchain* sc = Get<Swapchain>(swapchain);
   *index = sc->next;
   sc->next = (sc->next + 1) % sc->count;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState)
{
   State& s = S();
   const double period = 1. / s.fps;
   double now = Now();
   if (s.nextFrame == 0. || s.nextFrame < now - period)
      s.nextFrame = now;
   if (s.nextFrame > now)
      std::this_thread::sleep_for(std::chrono::duration<double>(s.nextFrame - now));
   const double display = s.nextFrame + period;
   s.nextFrame += period;
   frameState->predictedDisplayTime = s.ToXrTime(display);
   frameState->predictedDisplayPeriod = static_cast<XrDuration>(period * 1e9);
   frameState->shouldRender = XR_TRUE;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t capacity, uint32_t* count, XrView* views)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   *count = 2;
   if (capacity == 0)
      return XR_SUCCESS;
   if (capacity < 2)
      return XR_ERROR_SIZE_INSUFFICIENT;
   viewState->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
   const XrPosef head = ToXr(s.HeadAt(s.FromXrTime(viewLocateInfo->displayTime)));
   const Quat q { head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w };
   const float tanH = tanf(s.fovH * 0.5f * 3.14159265f / 180.f), tanV = tanH * static_cast<float>(s.eyeHeight) / static_cast<float>(s.eyeWidth);
   for (int eye = 0; eye < 2; eye++)
   {
      float dx = (eye == 0 ? -0.5f : 0.5f) * 0.063f, dy = 0.f, dz = 0.f;
      Rotate(q, dx, dy, dz);
      views[eye].pose = head;
      views[eye].pose.position = { head.position.x + dx, head.position.y + dy, head.position.z + dz };
      views[eye].fov = { -atanf(tanH), atanf(tanH), atanf(tanV), -atanf(tanV) };
   }
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* actionSet)
{
   *actionSet = reinterpret_cast<XrActionSet>(new int(0));
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet actionSet)
{
   delete Get<int>(actionSet);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo* createInfo, XrAction* action)
{
   // XRInputHandler gives the input path as the localized name of each action
   Action* a = new Action { createInfo->localizedActionName, createInfo->actionType };
   std::lock_guard lock(S().mutex);
   S().actions.push_back(a);
   *action = reinterpret_cast<XrAction>(a);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyAction(XrAction action)
{
   std::lock_guard lock(S().mutex);
   std::erase(S().actions, Get<Action>(action));
   delete Get<Action>(action);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
   // The simulated controllers are Steam Frame controllers: every input of the list is theirs
   std::lock_guard lock(S().mutex);
   return S().paths[suggestedBindings->interactionProfile - 1] == kFrameProfile ? XR_SUCCESS : XR_ERROR_PATH_UNSUPPORTED;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession, XrPath, XrInteractionProfileState* state)
{
   std::lock_guard lock(S().mutex);
   state->interactionProfile = S().ToPath(kFrameProfile);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo*)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   for (Action* action : s.actions)
   {
      const auto it = s.inputs.find(action->path);
      const float v = it == s.inputs.end() ? 0.f : it->second;
      action->changed = v != action->current;
      action->current = v;
   }
   return XR_SUCCESS;
}

static bool IsHandActive(const std::string& path)
{
   const int hand = path.starts_with("/user/hand/left") ? 0 : path.starts_with("/user/hand/right") ? 1 : -1;
   return hand >= 0 && S().handVisible[hand];
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo* getInfo, XrActionStateBoolean* state)
{
   std::lock_guard lock(S().mutex);
   const Action* action = Get<Action>(getInfo->action);
   state->isActive = IsHandActive(action->path);
   state->currentState = action->current > 0.5f;
   state->changedSinceLastSync = action->changed;
   state->lastChangeTime = 0;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo* getInfo, XrActionStateFloat* state)
{
   std::lock_guard lock(S().mutex);
   const Action* action = Get<Action>(getInfo->action);
   state->isActive = IsHandActive(action->path);
   state->currentState = action->current;
   state->changedSinceLastSync = action->changed;
   state->lastChangeTime = 0;
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo* getInfo, XrActionStatePose* state)
{
   std::lock_guard lock(S().mutex);
   state->isActive = IsHandActive(Get<Action>(getInfo->action)->path);
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader*) { return XR_SUCCESS; }

XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession, const XrHapticActionInfo*) { return XR_SUCCESS; }

// XR_EXT_render_model / XR_EXT_interaction_render_model: one model per hand, id 1 (left) and 2 (right)

static XrResult xrEnumerateInteractionRenderModelIdsEXT(XrSession, const XrInteractionRenderModelIdsEnumerateInfoEXT*, uint32_t capacity, uint32_t* count, XrRenderModelIdEXT* ids)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   LoadControllerModels(s);
   std::vector<XrRenderModelIdEXT> list;
   for (int hand = 0; hand < 2; hand++)
      if (!s.models[hand].glb.empty())
         list.push_back(static_cast<XrRenderModelIdEXT>(hand + 1));
   return CopyArray(list, capacity, count, ids);
}

struct RenderModel { int hand; };

static XrResult xrCreateRenderModelEXT(XrSession, const XrRenderModelCreateInfoEXT* createInfo, XrRenderModelEXT* renderModel)
{
   *renderModel = reinterpret_cast<XrRenderModelEXT>(new RenderModel { static_cast<int>(createInfo->renderModelId) - 1 });
   return XR_SUCCESS;
}

static XrResult xrDestroyRenderModelEXT(XrRenderModelEXT renderModel)
{
   delete Get<RenderModel>(renderModel);
   return XR_SUCCESS;
}

static XrResult xrGetRenderModelPropertiesEXT(XrRenderModelEXT renderModel, const XrRenderModelPropertiesGetInfoEXT*, XrRenderModelPropertiesEXT* properties)
{
   const int hand = Get<RenderModel>(renderModel)->hand;
   memset(&properties->cacheId, 0, sizeof(properties->cacheId));
   properties->cacheId.data[0] = static_cast<uint8_t>(hand + 1);
   properties->animatableNodeCount = static_cast<uint32_t>(S().models[hand].nodes.size());
   return XR_SUCCESS;
}

static XrResult xrCreateRenderModelSpaceEXT(XrSession, const XrRenderModelSpaceCreateInfoEXT* createInfo, XrSpace* space)
{
   Space* sp = new Space { Space::RenderModel };
   sp->hand = Get<RenderModel>(createInfo->renderModel)->hand;
   *space = reinterpret_cast<XrSpace>(sp);
   return XR_SUCCESS;
}

static XrResult xrCreateRenderModelAssetEXT(XrSession, const XrRenderModelAssetCreateInfoEXT* createInfo, XrRenderModelAssetEXT* asset)
{
   *asset = reinterpret_cast<XrRenderModelAssetEXT>(new RenderModel { createInfo->cacheId.data[0] - 1 });
   return XR_SUCCESS;
}

static XrResult xrDestroyRenderModelAssetEXT(XrRenderModelAssetEXT asset)
{
   delete Get<RenderModel>(asset);
   return XR_SUCCESS;
}

static XrResult xrGetRenderModelAssetDataEXT(XrRenderModelAssetEXT asset, const XrRenderModelAssetDataGetInfoEXT*, XrRenderModelAssetDataEXT* buffer)
{
   const std::vector<uint8_t>& glb = S().models[Get<RenderModel>(asset)->hand].glb;
   buffer->bufferCountOutput = static_cast<uint32_t>(glb.size());
   if (buffer->bufferCapacityInput == 0)
      return XR_SUCCESS;
   if (buffer->bufferCapacityInput < glb.size())
      return XR_ERROR_SIZE_INSUFFICIENT;
   memcpy(buffer->buffer, glb.data(), glb.size());
   return XR_SUCCESS;
}

static XrResult xrGetRenderModelAssetPropertiesEXT(XrRenderModelAssetEXT asset, const XrRenderModelAssetPropertiesGetInfoEXT*, XrRenderModelAssetPropertiesEXT* properties)
{
   const ControllerModel& m = S().models[Get<RenderModel>(asset)->hand];
   for (uint32_t i = 0; i < properties->nodePropertyCount && i < m.nodes.size(); i++)
      strncpy(properties->nodeProperties[i].uniqueName, m.nodes[i].c_str(), XR_MAX_RENDER_MODEL_ASSET_NODE_NAME_SIZE_EXT - 1);
   return XR_SUCCESS;
}

static XrResult xrGetRenderModelStateEXT(XrRenderModelEXT renderModel, const XrRenderModelStateGetInfoEXT*, XrRenderModelStateEXT* state)
{
   State& s = S();
   std::lock_guard lock(s.mutex);
   const int hand = Get<RenderModel>(renderModel)->hand;
   const ControllerModel& m = s.models[hand];
   for (uint32_t i = 0; i < state->nodeStateCount && i < m.nodes.size(); i++)
   {
      // Components are in model space (their nodes have no transform): a motion is a rotation around a pivot, or a translation
      XrPosef pose { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } };
      const ControllerModel::Motion& mo = m.motions[i];
      if (!mo.input.empty())
      {
         const auto it = s.inputs.find((hand == 0 ? "/user/hand/left"s : "/user/hand/right"s) + mo.input);
         const float v = it == s.inputs.end() ? 0.f : it->second;
         if (mo.rotate)
         {
            const Quat q = AxisAngle(mo.axis[0], mo.axis[1], mo.axis[2], v * mo.amount * 3.14159265f / 180.f);
            float x = mo.pivot[0], y = mo.pivot[1], z = mo.pivot[2];
            Rotate(q, x, y, z);
            pose = { { q.x, q.y, q.z, q.w }, { mo.pivot[0] - x, mo.pivot[1] - y, mo.pivot[2] - z } };
         }
         else
            pose.position = { mo.axis[0] * v * mo.amount, mo.axis[1] * v * mo.amount, mo.axis[2] * v * mo.amount };
      }
      state->nodeStates[i].nodePose = pose;
      state->nodeStates[i].isVisible = XR_TRUE;
   }
   return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function)
{
   static const std::map<std::string, PFN_xrVoidFunction> functions = {
      { "xrEnumerateInteractionRenderModelIdsEXT", reinterpret_cast<PFN_xrVoidFunction>(xrEnumerateInteractionRenderModelIdsEXT) },
      { "xrCreateRenderModelEXT", reinterpret_cast<PFN_xrVoidFunction>(xrCreateRenderModelEXT) },
      { "xrDestroyRenderModelEXT", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyRenderModelEXT) },
      { "xrGetRenderModelPropertiesEXT", reinterpret_cast<PFN_xrVoidFunction>(xrGetRenderModelPropertiesEXT) },
      { "xrCreateRenderModelSpaceEXT", reinterpret_cast<PFN_xrVoidFunction>(xrCreateRenderModelSpaceEXT) },
      { "xrCreateRenderModelAssetEXT", reinterpret_cast<PFN_xrVoidFunction>(xrCreateRenderModelAssetEXT) },
      { "xrDestroyRenderModelAssetEXT", reinterpret_cast<PFN_xrVoidFunction>(xrDestroyRenderModelAssetEXT) },
      { "xrGetRenderModelAssetDataEXT", reinterpret_cast<PFN_xrVoidFunction>(xrGetRenderModelAssetDataEXT) },
      { "xrGetRenderModelAssetPropertiesEXT", reinterpret_cast<PFN_xrVoidFunction>(xrGetRenderModelAssetPropertiesEXT) },
      { "xrGetRenderModelStateEXT", reinterpret_cast<PFN_xrVoidFunction>(xrGetRenderModelStateEXT) },
   };
   const auto it = functions.find(name);
   if (it == functions.end())
   {
      *function = nullptr;
      return XR_ERROR_FUNCTION_UNSUPPORTED;
   }
   *function = it->second;
   return XR_SUCCESS;
}

} // extern "C"
