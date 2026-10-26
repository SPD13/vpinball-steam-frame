// license:GPLv3+

// Scripted sessions of the simulated runtime (see XRSim.h): VPX_XRSIM_SCRIPT=<file> plays timed commands, one scene per table played
// (the lobby, then each table the script starts). Used to capture the VR version (trailer) and to look at the VR UI on a Mac.
//
// Script: '#' comments, "scene" starts the commands of the next table played, then "<seconds from the table start> <command> <args>":
//   head x y z yaw pitch [roll] [dur]   head pose (meters, degrees), eased over dur seconds
//   look dyaw dpitch [dur]              turns the head from its current target
//   sway amplitude                      small head motion (about cm and degrees), 0 to stop
//   aim l|r x y z yaw pitch [dur]       aim pose of a controller (it is shown)
//   aimat l|r x y z [dur]               points a controller at a position of the room, from where it is
//   aimui l|r u v [dur]                 points a controller at a position of the UI panel (0..1 from its top left)
//   hand l|r 0|1                        hides or shows a controller
//   trig l|r [ms]                       pulls the trigger (click on the UI), released after ms (default 150)
//   input <path> <value>                any input of the Steam Frame controller profile
//   key <scancode> [ms], keydown <scancode>, keyup <scancode>   keyboard
//   waitfile <path>                     the scene time stops until the file exists
//   menu [page], table <name>, exit, autoflip 0|1 [keyboard] (flips with the triggers), lobbyonscore, code, mark <label>
// Marks are logged as "XRSIM-MARK <epoch ms> <label>" to align the recorded video with the script.

#include "core/stdafx.h"
#include "XRSim.h"

#include "core/player.h"
#include "parts/ball.h"
#include "parts/flipper.h"
#include "lib/src/WebServer.h"
#include "steamframe/Launcher.h"
#include "steamframe/ScoreTracker.h"
#include "ui/live/LiveUI.h"

#include <fstream>
#include <sstream>

namespace
{

struct Command
{
   float t;
   string op;
   vector<string> args;
   float Arg(size_t i, float def = 0.f) const { return i < args.size() ? std::stof(args[i]) : def; }
   XRSim::Hand HandArg(size_t i) const { return i < args.size() && args[i] == "l" ? XRSim::Left : XRSim::Right; }
};

struct Runner
{
   vector<vector<Command>> scenes;
   bool parsed = false;
   const Player* player = nullptr;
   bool playerClosing = false; // The next player may be allocated at the address of the closing one
   int scene = -1;
   uint32_t start = 0;
   size_t next = 0;
   bool autoflip = false, autoflipKeyboard = false, lobbyOnScore = false;
   uint32_t scoredAt = 0;
   uint32_t flipRelease[2] = { 0, 0 }, flipCooldown[2] = { 0, 0 };
   vector<std::pair<uint32_t, uint16_t>> keyReleases;
   vector<std::pair<uint32_t, string>> inputReleases;
   // UI panel, in the reference space (see VRDevice::UpdateUIPanel)
   bool panelPlaced = false;
   float panel[9] {};
};

Runner& R()
{
   static Runner r;
   return r;
}

long long Epoch() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

void Parse(Runner& r, const char* path)
{
   std::ifstream f(path);
   string line;
   while (std::getline(f, line))
   {
      if (const size_t h = line.find('#'); h != string::npos)
         line = line.substr(0, h);
      std::istringstream ss(line);
      string first;
      if (!(ss >> first))
         continue;
      if (first == "scene")
      {
         r.scenes.emplace_back();
         continue;
      }
      Command c;
      c.t = std::stof(first);
      ss >> c.op;
      string arg;
      while (ss >> arg)
         c.args.push_back(arg);
      if (!r.scenes.empty())
         r.scenes.back().push_back(c);
   }
   for (auto& scene : r.scenes) // Commands run in time order, whatever their order in the file
      std::ranges::stable_sort(scene, {}, &Command::t);
   PLOGI << "XR simulator script " << path << ": " << r.scenes.size() << " scenes";
}

void AutoFlip(Runner& r, Player* player, uint32_t now)
{
   InputManager& im = player->m_pininput;
   bool want[2] = { false, false };
   for (IEditable* part : player->m_ptable->GetParts())
   {
      Flipper* fl = part->GetItemType() == eItemFlipper ? static_cast<Flipper*>(part) : nullptr;
      if (fl == nullptr)
         continue;
      const int side = fl->m_d.m_StartAngle > 0.f ? 0 : 1;
      const float radius = fl->m_d.m_FlipperRadius, cx = fl->m_d.m_Center.x, cy = fl->m_d.m_Center.y;
      const float tipX = side == 0 ? cx + radius * 0.85f : cx - radius * 0.85f;
      for (const Ball* b : player->m_vball)
      {
         const vec3& p = b->GetPosition();
         const float lo = std::min(cx, tipX) - 15.f, hi = std::max(cx, tipX) + 15.f;
         if (p.x > lo && p.x < hi && p.y > cy - radius * 0.55f && p.y < cy + radius * 0.45f && b->GetVelocity().y > -2.f)
            want[side] = true;
      }
   }
   // The controller triggers flip, like a player does (the controller models show them pulled), or the keyboard
   const auto press = [&](int side, bool down)
   {
      if (r.autoflipKeyboard)
         im.PushButtonEvent(im.GetKeyboardDeviceId(), side == 0 ? SDL_SCANCODE_LSHIFT : SDL_SCANCODE_RSHIFT, SDL_GetTicksNS(), down);
      else
         XRSim::SetInput(side == 0 ? "/user/hand/left/input/trigger/value" : "/user/hand/right/input/trigger/value", down ? 1.f : 0.f);
   };
   for (int s = 0; s < 2; s++)
   {
      if (r.flipRelease[s] != 0 && now >= r.flipRelease[s])
      {
         press(s, false);
         r.flipRelease[s] = 0;
         r.flipCooldown[s] = now + 260;
      }
      else if (want[s] && r.flipRelease[s] == 0 && now >= r.flipCooldown[s])
      {
         press(s, true);
         r.flipRelease[s] = now + 190;
      }
   }
}

const char* TriggerPath(XRSim::Hand hand) { return hand == XRSim::Left ? "/user/hand/left/input/trigger/value" : "/user/hand/right/input/trigger/value"; }

} // namespace

namespace XRSim
{

void SetUIPanel(const float origin[3], const float right[3], const float down[3])
{
   Runner& r = R();
   for (int i = 0; i < 3; i++)
   {
      r.panel[i] = origin[i];
      r.panel[3 + i] = right[i];
      r.panel[6 + i] = down[i];
   }
   r.panelPlaced = true;
   PLOGI << "XR simulator: UI panel at " << origin[0] << ',' << origin[1] << ',' << origin[2] << ", right " << right[0] << ',' << right[1] << ',' << right[2] << ", down "
         << down[0] << ',' << down[1] << ',' << down[2];
}

void UpdateScript(Player* player)
{
   const char* path = getenv("VPX_XRSIM_SCRIPT");
   if (path == nullptr || player == nullptr || player->m_liveUI == nullptr) // Also called while the UI is created
      return;
   Runner& r = R();
   if (!r.parsed)
   {
      r.parsed = true;
      Parse(r, path);
   }
   if (r.player != player || (r.playerClosing && player->GetCloseState() == Player::CS_PLAYING))
   {
      r.player = player;
      r.playerClosing = false;
      r.start = 0;
      r.next = 0;
      r.autoflip = false;
      r.lobbyOnScore = false;
      r.scoredAt = 0;
      r.keyReleases.clear();
      r.panelPlaced = false;
   }
   if (player->GetCloseState() != Player::CS_PLAYING)
      r.playerClosing = true;
   InputManager& im = player->m_pininput;
   const uint32_t now = msec();
   if (r.start == 0 && !player->m_liveUI->IsLoadingScreenShown() && player->GetCloseState() == Player::CS_PLAYING)
   {
      r.start = now;
      r.scene++;
      PLOGI << "XRSIM-MARK " << Epoch() << " scene" << r.scene << "-start";
   }
   if (r.start != 0 && r.scene >= 0 && r.scene < static_cast<int>(r.scenes.size()))
   {
      const auto& cmds = r.scenes[r.scene];
      while (r.next < cmds.size() && (now - r.start) >= static_cast<uint32_t>(cmds[r.next].t * 1000.f))
      {
         const Command& c = cmds[r.next++];
         if (c.op == "head")
            SetHeadPose({ c.Arg(0), c.Arg(1, 1.65f), c.Arg(2), c.Arg(3), c.Arg(4), c.Arg(5) }, c.Arg(6));
         else if (c.op == "look")
         {
            Pose p = GetHeadPose();
            p.yaw += c.Arg(0);
            p.pitch += c.Arg(1);
            SetHeadPose(p, c.Arg(2));
         }
         else if (c.op == "sway")
            SetHeadSway(c.Arg(0));
         else if (c.op == "aim")
            SetAimPose(c.HandArg(0), { c.Arg(1), c.Arg(2), c.Arg(3), c.Arg(4), c.Arg(5), 0.f }, c.Arg(6));
         else if (c.op == "aimat")
            AimAt(c.HandArg(0), c.Arg(1), c.Arg(2), c.Arg(3), c.Arg(4));
         else if (c.op == "aimui")
         {
            if (!r.panelPlaced)
            {
               PLOGW << "XR simulator script: aimui before the UI panel is placed";
               continue;
            }
            const float u = c.Arg(1), v = c.Arg(2);
            AimAt(c.HandArg(0), r.panel[0] + r.panel[3] * u + r.panel[6] * v, r.panel[1] + r.panel[4] * u + r.panel[7] * v, r.panel[2] + r.panel[5] * u + r.panel[8] * v, c.Arg(3));
         }
         else if (c.op == "hand")
            SetHandVisible(c.HandArg(0), c.Arg(1) != 0.f);
         else if (c.op == "trig")
         {
            SetInput(TriggerPath(c.HandArg(0)), 1.f);
            r.inputReleases.emplace_back(now + static_cast<uint32_t>(c.Arg(1, 150.f)), TriggerPath(c.HandArg(0)));
         }
         else if (c.op == "input" && c.args.size() >= 2)
            SetInput(c.args[0], c.Arg(1));
         else if (c.op == "key")
         {
            im.PushButtonEvent(im.GetKeyboardDeviceId(), static_cast<uint16_t>(c.Arg(0)), SDL_GetTicksNS(), true);
            r.keyReleases.emplace_back(now + static_cast<uint32_t>(c.Arg(1, 120.f)), static_cast<uint16_t>(c.Arg(0)));
         }
         else if (c.op == "keydown")
            im.PushButtonEvent(im.GetKeyboardDeviceId(), static_cast<uint16_t>(c.Arg(0)), SDL_GetTicksNS(), true);
         else if (c.op == "keyup")
            im.PushButtonEvent(im.GetKeyboardDeviceId(), static_cast<uint16_t>(c.Arg(0)), SDL_GetTicksNS(), false);
         else if (c.op == "autoflip") // autoflip 0|1 [keyboard]
         {
            r.autoflip = c.Arg(0) != 0.f;
            r.autoflipKeyboard = c.args.size() > 1 && c.args[1] == "keyboard";
         }
         else if (c.op == "lobbyonscore")
            r.lobbyOnScore = true;
         else if (c.op == "menu")
            player->m_liveUI->OpenInGameUI(c.args.empty() ? "homepage"s : c.args[0]);
         else if (c.op == "mark")
            PLOGI << "XRSIM-MARK " << Epoch() << ' ' << (c.args.empty() ? ""s : c.args[0]);
         else if (c.op == "code")
         {
            std::ofstream o(getenv("VPX_XRSIM_CODE") ? getenv("VPX_XRSIM_CODE") : "/tmp/vpx-code.txt");
            o << g_launcher->GetWebServer().GetPairingCode() << '\n' << g_launcher->GetWebServer().GetUrl() << '\n';
         }
         else if (c.op == "table")
         {
            string name;
            for (const auto& s : c.args)
               name += (name.empty() ? "" : " ") + s;
            bool found = false;
            for (const auto& t : g_launcher->GetTableLibrary().GetTables())
               if (t.name == name)
               {
                  found = true;
                  g_launcher->m_nextTableFilename = g_launcher->GetTableLibrary().GetFullPath(t);
                  player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
                  break;
               }
            PLOGI << "XRSIM-MARK " << Epoch() << " table-" << (found ? "go " : "notfound ") << name;
            if (!found)
               r.next = cmds.size();
         }
         else if (c.op == "waitfile" && !c.args.empty())
         {
            // The scene time stops here until the file exists (written by another tool, a browser script for example)
            if (!std::filesystem::exists(c.args[0]))
            {
               r.next--;
               r.start = now - static_cast<uint32_t>(c.t * 1000.f);
               break;
            }
            PLOGI << "XRSIM-MARK " << Epoch() << " waited-" << std::filesystem::path(c.args[0]).filename().string();
         }
         else if (c.op == "exit")
         {
            g_launcher->m_launcherMode = false;
            player->SetCloseState(Player::CS_CLOSE_APP);
         }
         else
            PLOGW << "XR simulator script: unknown command " << c.op;
      }
   }
   if (r.lobbyOnScore && player->m_scoreTracker && !player->m_scoreTracker->GetRecordedScoreIds().empty())
   {
      if (r.scoredAt == 0)
      {
         r.scoredAt = now;
         PLOGI << "XRSIM-MARK " << Epoch() << " scored";
      }
      else if (now - r.scoredAt > 2500)
      {
         r.lobbyOnScore = false;
         r.autoflip = false;
         player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
      }
   }
   for (auto it = r.keyReleases.begin(); it != r.keyReleases.end();)
      if (now >= it->first)
      {
         im.PushButtonEvent(im.GetKeyboardDeviceId(), it->second, SDL_GetTicksNS(), false);
         it = r.keyReleases.erase(it);
      }
      else
         ++it;
   for (auto it = r.inputReleases.begin(); it != r.inputReleases.end();)
      if (now >= it->first)
      {
         SetInput(it->second, 0.f);
         it = r.inputReleases.erase(it);
      }
      else
         ++it;
   if (r.autoflip && !player->m_liveUI->IsInGameUIOpened())
      AutoFlip(r, player, now);
}

}
