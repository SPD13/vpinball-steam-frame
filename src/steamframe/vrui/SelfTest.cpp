// license:GPLv3+

#include "core/stdafx.h"
#include "SelfTest.h"

#include "MainMenuPage.h"
#include "Shell.h"

#include "renderer/Renderer.h"
#include "steamframe/Launcher.h"
#include "steamframe/TableLibrary.h"
#include "ui/live/LiveUI.h"

namespace VPX::VRUI::SelfTest
{

using namespace VPX::InGameUI;

namespace
{
   enum class Step { Wait, Screens, DPad, SubPage, Quit, Done };
   Step g_step = Step::Wait;
   uint32_t g_stepStart = 0;
   int g_screen = -1; // Index of the screen being captured
   int g_dpadStep = 0;
   int g_shotCount = 0;
   std::atomic<bool> g_capturing = false;

   const char* GetFolder() { return getenv("VPX_VRUI_SHOTS"); }

   string FileName(const string& name)
   {
      string fileName;
      for (const char c : name)
      {
         if (std::isalnum(static_cast<unsigned char>(c)))
            fileName += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
         else if (!fileName.empty() && fileName.back() != '-')
            fileName += '-';
      }
      while (!fileName.empty() && fileName.back() == '-')
         fileName.pop_back();
      return std::format("{:02d}-{}{}.png", ++g_shotCount, g_pplayer->m_isLobby ? "lobby-" : "table-", fileName);
   }

   void Capture(Player* player, const string& name, Step next)
   {
      const std::filesystem::path path = std::filesystem::path(GetFolder()) / FileName(name);
      g_capturing = true;
      player->m_renderer->m_renderDevice->CaptureScreenshot({ player->m_playfieldWnd }, { path },
         [path, next](bool ok)
         {
            PLOGI << "VRUI self-test: " << (ok ? "captured " : "failed to capture ") << path.string();
            g_step = next;
            g_stepStart = msec();
            g_capturing = false;
         });
   }

   // Next screen of the rail, or -1 after the last one
   int NextScreen(int screen)
   {
      for (int i = screen + 1; i < kScreenCount; i++)
         if (IsScreenAvailable(static_cast<Screen>(i)))
            return i;
      return -1;
   }
}

bool IsEnabled() { return GetFolder() != nullptr; }

void OnPlayerCreated(Player* player, bool isLobby)
{
   if (!IsEnabled())
      return;
   std::error_code ec;
   std::filesystem::create_directories(GetFolder(), ec);
   g_step = Step::Wait;
   g_stepStart = 0;
   g_screen = -1;
   // In the lobby, the menu opens by itself on the table library
   if (!isLobby)
      player->m_liveUI->OpenInGameUI();
}

void Update(InGameUIPage& page)
{
   if (!IsEnabled() || g_capturing)
      return;
   Player* const player = page.m_player;
   VPX::InGameUI::InGameUI& ui = player->m_liveUI->m_inGameUI;
   const uint32_t now = msec();
   if (g_stepStart == 0)
      g_stepStart = now;
   switch (g_step)
   {
   case Step::Wait:
      // Table library scanned, thumbnails loaded
      if (g_launcher->GetTableLibrary().IsScanning() || now - g_stepStart < 3000)
         return;
      g_screen = NextScreen(-1);
      g_step = Step::Screens;
      g_stepStart = now;
      break;

   case Step::Screens:
   {
      if (g_screen < 0)
      {
         g_step = Step::DPad;
         g_dpadStep = 0;
         g_stepStart = now;
         return;
      }
      const Screen screen = static_cast<Screen>(g_screen);
      MainMenuPage* const menu = dynamic_cast<MainMenuPage*>(ui.GetActivePage());
      if (menu == nullptr)
      {
         // For example the 'Who is playing?' page opened over the lobby the first time
         MainMenuPage::SetLastScreen(screen);
         Shell::NavigateWithFreshHistory(ui, "homepage"s);
         g_stepStart = now;
         return;
      }
      if (menu->GetScreen() != screen)
      {
         menu->SetScreen(screen);
         g_stepStart = now;
         return;
      }
      if (now - g_stepStart < 2000)
         return;
      g_screen = NextScreen(g_screen);
      Capture(player, GetScreenName(screen), Step::Screens);
      break;
   }

   case Step::DPad:
   {
      // Button navigation, as the controllers do it: on the System screen (its first items are actions), down selects an item, left gets
      // to the rail, down then changes the screen
      MainMenuPage* const menu = dynamic_cast<MainMenuPage*>(ui.GetActivePage());
      if (menu == nullptr || now - g_stepStart < 800)
         return;
      g_stepStart = now;
      switch (g_dpadStep++)
      {
      case 0: menu->SetScreen(Screen::System); break;
      case 1: ui.OnUIDownAction(); break;
      case 2: Capture(player, "dpad-content"s, Step::DPad); break;
      case 3: ui.OnUILeftAction(); break;
      case 4: Capture(player, "dpad-rail"s, Step::DPad); break;
      case 5: ui.OnUIDownAction(); break;
      case 6: Capture(player, "dpad-rail-down"s, Step::DPad); break;
      case 7: ui.OnUIRightAction(); break;
      default: g_step = Step::SubPage; break;
      }
      break;
   }

   case Step::SubPage:
      // A page of upstream opened from a screen, drawn in the frame of the menu
      if (dynamic_cast<MainMenuPage*>(ui.GetActivePage()) != nullptr)
      {
         ui.Navigate("settings/nudge"s);
         g_stepStart = now;
         return;
      }
      if (now - g_stepStart < 1500)
         return;
      Capture(player, "sub-page"s, Step::Quit);
      break;

   case Step::Quit:
      PLOGI << "VRUI self-test: done";
      g_step = Step::Done;
      g_launcher->m_launcherMode = false;
      player->SetCloseState(Player::CS_CLOSE_APP);
      break;

   case Step::Done: break;
   }
}

}
