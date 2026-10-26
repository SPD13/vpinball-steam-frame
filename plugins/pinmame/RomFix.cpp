// license:GPLv3+

#include "RomFix.h"
#include "Controller.h"

#include "plugins/VPXPlugin.h"
#include "nlohmann/json.hpp"

#include <ctime>

namespace PinMAME {

namespace {
   struct Timer {
      RomFix* fix;
      uint64_t generation;
      int step;
   };
}

RomFix::RomFix(const MsgPluginAPI* msgApi, unsigned int endpointId, Controller* controller)
   : m_msgApi(msgApi)
   , m_endpointId(endpointId)
   , m_controller(controller)
{
}

RomFix::~RomFix()
{
   Stop();
}

VPXPluginAPI* RomFix::GetVpxApi() const
{
   VPXPluginAPI* vpxApi = nullptr;
   const unsigned int getVpxApiId = m_msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   m_msgApi->BroadcastMsg(m_endpointId, getVpxApiId, &vpxApi);
   m_msgApi->ReleaseMsgID(getVpxApiId);
   return vpxApi;
}

void RomFix::Prepare(const string& rom, const string& vpmPath)
{
   Stop();
   m_procedure.reset();
   m_firstStart = false;
   VPXPluginAPI* const vpxApi = GetVpxApi();
   if (vpxApi == nullptr || rom.empty())
      return;
   const std::filesystem::path nvramPath = vpmPath.empty() ? std::filesystem::path() : std::filesystem::path(vpmPath) / "nvram"sv / (rom + ".nv");
   std::error_code ec;
   const bool hasNvram = !nvramPath.empty() && std::filesystem::is_regular_file(nvramPath, ec);
   m_firstStart = !nvramPath.empty() && !hasNvram;
   if (vpxApi->GetRomFix == nullptr)
      return;
   const char* const fix = vpxApi->GetRomFix(rom.c_str(), hasNvram ? 1 : 0);
   if (fix == nullptr)
      return;
   try
   {
      const nlohmann::json json = nlohmann::json::parse(fix);
      Procedure procedure;
      procedure.rom = rom;
      procedure.id = json.at("id").get<string>();
      procedure.version = json.value("version", 0);
      procedure.notice = json.value("notice", ""s);
      procedure.bootMs = json.value("bootMs", 0);
      procedure.holdMs = json.value("holdMs", 100);
      procedure.waitMs = json.value("waitMs", 1000);
      procedure.reset = json.value("reset", false);
      procedure.switches = json.at("switches").get<vector<int>>();
      m_procedure = std::move(procedure);
   }
   catch (const std::exception& e)
   {
      LOGE("Invalid ROM fix: "s + e.what());
      return;
   }

   // The NVRAM the procedure changes is kept, as <rom>.nv.bak-<date>
   if (hasNvram)
   {
      char date[32];
      const std::time_t now = std::time(nullptr);
      std::strftime(date, sizeof(date), "%Y%m%d-%H%M%S", std::localtime(&now));
      std::filesystem::path backupPath = nvramPath;
      backupPath += ".bak-"s + date;
      std::filesystem::copy_file(nvramPath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec)
         LOGW("Failed to keep a copy of " + nvramPath.string() + ": " + ec.message());
      else
         LOGI("NVRAM kept as " + backupPath.string() + " before the ROM fix '" + m_procedure->id + '\'');
   }
}

void RomFix::Start()
{
   if (!m_procedure)
   {
      // Many ROMs initialize a new NVRAM, then only work from the next start: WPC games stay on "Factory settings restored", Spinball games
      // wait for a watchdog reset that PinMAME does not emulate (a procedure shows its own notice instead)
      if (m_firstStart)
         if (VPXPluginAPI* const vpxApi = GetVpxApi(); vpxApi && vpxApi->PushNotification)
         {
            LOGI("First start of a ROM without NVRAM: showing the first launch notice");
            vpxApi->PushNotification("First launch. The table is initializing, you may need to restart the table after the initialization is complete", 10000);
         }
      m_firstStart = false;
      return;
   }
   if (m_running)
      return;
   m_running = true;
   m_heldSwitches = std::set<int>(m_procedure->switches.begin(), m_procedure->switches.end());
   m_heldStates.clear();
   const int durationMs = m_procedure->bootMs + static_cast<int>(m_procedure->switches.size()) * (m_procedure->holdMs + m_procedure->waitMs);
   LOGI("Running the ROM fix '" + m_procedure->id + "' for '" + m_procedure->rom + "' (" + std::to_string(durationMs / 1000) + " s)");
   if (VPXPluginAPI* const vpxApi = GetVpxApi(); vpxApi && !m_procedure->notice.empty())
      m_notification = vpxApi->PushNotification(m_procedure->notice.c_str(), durationMs + 3000);
   // Step 2n presses the switch n, step 2n+1 releases it
   Schedule(m_procedure->bootMs, 0);
}

void RomFix::Schedule(int delayMs, int step)
{
   m_msgApi->RunOnMainThread(m_endpointId, delayMs / 1000.0, OnTimer, new Timer { this, m_generation, step });
}

void MSGPIAPI RomFix::OnTimer(void* userData)
{
   const Timer* const timer = static_cast<Timer*>(userData);
   if (timer->generation == timer->fix->m_generation && timer->fix->m_running)
      timer->fix->RunStep(timer->step);
   delete timer;
}

void RomFix::RunStep(int step)
{
   const size_t index = static_cast<size_t>(step / 2);
   if (index >= m_procedure->switches.size())
   {
      Finish();
      return;
   }
   const int switchNo = m_procedure->switches[index];
   const bool press = (step % 2) == 0;
   PinmameSetSwitch(switchNo, press ? 1 : 0);
   Schedule(press ? m_procedure->holdMs : m_procedure->waitMs, step + 1);
}

void RomFix::Finish()
{
   if (m_procedure->reset)
      PinmameReset();
   m_running = false;
   m_generation++;
   // The switches changed by the table script while they were held
   for (const auto& [switchNo, state] : m_heldStates)
      m_controller->SetSwitch(switchNo, state);
   m_heldStates.clear();
   LOGI("ROM fix '" + m_procedure->id + "' done for '" + m_procedure->rom + '\'');
   if (VPXPluginAPI* const vpxApi = GetVpxApi())
   {
      if (m_notification != 0 && vpxApi->UpdateNotification)
         vpxApi->UpdateNotification(m_notification, "Table patched for compatibility: the game restarts", 5000);
      if (vpxApi->ReportRomFix)
         vpxApi->ReportRomFix(m_procedure->rom.c_str(), m_procedure->id.c_str(), m_procedure->version, 1);
   }
   m_notification = 0;
   m_procedure.reset();
}

void RomFix::Stop()
{
   m_generation++;
   if (!m_running)
      return;
   m_running = false;
   LOGW("ROM fix '" + m_procedure->id + "' interrupted for '" + m_procedure->rom + '\'');
   if (VPXPluginAPI* const vpxApi = GetVpxApi(); vpxApi && vpxApi->ReportRomFix)
      vpxApi->ReportRomFix(m_procedure->rom.c_str(), m_procedure->id.c_str(), m_procedure->version, 0);
   m_procedure.reset();
   m_heldStates.clear();
}

bool RomFix::HoldsSwitch(int switchNo, bool state)
{
   if (!m_running || !m_heldSwitches.contains(switchNo))
      return false;
   m_heldStates[switchNo] = state;
   return true;
}

}
