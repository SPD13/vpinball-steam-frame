// license:GPLv3+

#pragma once

#include "FileLocator.h"

namespace VPX { class Window; }
class VRDevice;
class Launcher;


class VPApp final
{
public:
   VPApp();
   ~VPApp();

   void SetCommandLineCustomSettingsFileName(const std::filesystem::path& path) { m_commandLineCustomSettingsFileName = path; } // Must be defined before InitInstance() is called, otherwise it will be ignored
   void InitInstance(bool isPlay = false);

   FileLocator m_fileLocator;

   void LimitMultiThreading();
   int GetLogicalNumberOfProcessors() const;

   // Global custom parameters that can be set through command line and accessed in the script via GetCustomParam(X)
   wstring m_customParameters[MAX_CUSTOM_PARAM_INDEX];

#ifndef ENABLE_BGFX
   // FIXME Deprecated command line options (supposed to be handled through INI nowadays)
   bool m_bgles = false; // override global emission scale by m_fgles below
   float m_fgles = 0.f;
#endif

   // Script security level
   int m_securitylevel;

   // Display resources kept from one table to the next when tables are played one after another (m_keepDisplayBetweenTables), so that
   // switching tables neither closes the window nor makes the VR runtime see the application quit and start again. They are released
   // when the application closes. Otherwise, they are released when the table which used them is closed (editor, mobile builds).
   bool m_keepDisplayBetweenTables = false;
   // Window kept from the previous table if it was created with the same settings, otherwise a new one
   VPX::Window* AcquireWindow(int windowId, const string& title, const Settings& settings);
   void ReleaseWindow(VPX::Window* wnd);
#ifdef ENABLE_XR
   // VR device (OpenXR instance) kept from the previous table, or a new one (which may not be ready, see VRDevice::IsOpenXRReady)
   VRDevice* AcquireVRDevice(const Settings& settings);
   void ReleaseVRDevice(bool discard); // discard: destroy it even if devices are kept, for example when the headset was not found
   // The VR device (OpenXR instance) is kept with its graphics device and its session, not only the windows: SteamVR (2.17.10 on the Steam
   // Frame) ties its Vulkan objects to the OpenXR instance and reuses them for the next session after destroying them with the previous one
   // (crash in the driver at xrEndFrame), and creating a new Vulkan device under the same instance makes it call the destroyed VkInstance
   // (abort in the loader), found on the Frame on 2026-10-02 with the Vulkan API dump layer: one instance, one device, one session. BGFX
   // is started again for each table on the kept device. VPX_XR_SESSION_PER_TABLE=1 creates the device for each table.
   bool KeepVRDeviceBetweenTables() const;
#endif
   void ReleaseDisplayResources();
   uint64_t m_lastTableCloseTime = 0; // When the last table started to close (steady clock, in microseconds), to log how long switching tables takes

#ifdef VPX_STEAM_FRAME
   std::unique_ptr<Launcher> m_launcher; // Steam Frame Edition: table library, lobby, scores (see steamframe/Launcher.h)
#endif

#ifdef VPX_HAS_REGISTERED_TYPELIB
   static CComModule m_module;
#endif


private:
   struct KeptWindow
   {
      VPX::Window* window;
      int windowId;
      string config; // Settings the window was created with (see VPX::Window::GetConfigKey)
   };
   vector<KeptWindow> m_keptWindows; // Windows not used by the current table, kept for the next one
   vector<KeptWindow> m_usedWindows; // Windows acquired by the current table
   VRDevice* m_vrDevice = nullptr;

   std::filesystem::path m_commandLineCustomSettingsFileName; // Override default ini filename, must be defined before InitInstance
   int m_logicalNumberOfProcessors = -1;
};
