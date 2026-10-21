// license:GPLv3+

#include "core/stdafx.h"
#include "SelfTest.h"

#include "Keyboard.h"
#include "MainMenuPage.h"
#include "PageView.h"
#include "Shell.h"

#include "renderer/Renderer.h"
#include "steamframe/Launcher.h"
#include "steamframe/TableLibrary.h"
#include "steamframe/TablePickerPage.h"
#include "ui/live/LiveUI.h"

namespace VPX::VRUI::SelfTest
{

using namespace VPX::InGameUI;

namespace
{
   enum class Step { Wait, Screens, DPad, SubPage, TextEntry, Quit, Done };
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
         // For example the result page opened over the lobby, back from a table
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
      if (g_dpadStep == 100)
      {
         // The page of a table: captured, then back to the menu
         if (now - g_stepStart < 1500)
            return;
         g_dpadStep = 101;
         Capture(player, "table-page"s, Step::DPad);
         return;
      }
      if (g_dpadStep == 101)
      {
         g_dpadStep = 102;
         ui.NavigateBack();
         g_stepStart = now;
         return;
      }
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
      // Search of the Tables screen, typed with the keyboard of the menu
      case 8: menu->SetScreen(Screen::Tables); break;
      case 9:
         if (auto* picker = dynamic_cast<TablePickerPage*>(menu->GetContent()); picker && Keyboard::IsUsed())
         {
            picker->OpenSearchKeyboard();
            Keyboard::TypeForTest("po"s);
         }
         break;
      case 10: Capture(player, "keyboard-search"s, Step::DPad); break;
      case 11: Keyboard::Close(); break;
      // Search field reached with the buttons: its keyboard opens
      case 12:
         for (int i = 0; i < 10; i++)
            if (const InGameUIItem* selected = menu->GetContent() ? menu->GetContent()->GetSelectedItem() : nullptr; selected && selected->m_label == "##search"s)
               break;
            else
               ui.OnUIDownAction();
         break;
      case 13: Capture(player, "search-focused"s, Step::DPad); break;
      case 14: Keyboard::Close(); break;
      // A value of a composed screen changed with the buttons, then undone (the footer follows)
      case 15: menu->SetScreen(Screen::Sound); break;
      case 16: ui.OnUIDownAction(); break;
      case 17: ui.OnUILeftAction(); ui.OnUILeftAction(); break;
      case 18: Capture(player, "composed-modified"s, Step::DPad); break;
      case 19: ui.OnUICancelChanges(); break;
      case 20: Capture(player, "composed-undone"s, Step::DPad); break;
      // The page of a table (play, favorite, rename, scores...), opened from the Tables screen
      case 21: menu->SetScreen(Screen::Tables); break;
      case 22:
         if (InGameUIPage* const tables = menu->GetContent(); tables)
            for (const auto& item : InGameUIPageView::GetItems(*tables))
               if (item->m_type == InGameUIItem::Type::Navigation && item->m_path.starts_with("tables/"))
               {
                  ui.Navigate(item->m_path);
                  g_dpadStep = 100; // Captured below, as the menu is no longer the active page
                  break;
               }
         break;
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
      Capture(player, "sub-page"s, Step::TextEntry);
      break;

   case Step::TextEntry:
      // The entry of a name (new profile, rename table): the keyboard opens with the page
      if (dynamic_cast<TextEntryPage*>(ui.GetActivePage()) == nullptr)
      {
         ui.AddPage("selftest/textentry"s, []() { return std::make_unique<TextEntryPage>("New profile"s, ""s, [](const string&) { }, false, 32); });
         ui.Navigate("selftest/textentry"s);
         g_stepStart = now;
         return;
      }
      if (now - g_stepStart < 1500)
         return;
      Keyboard::TypeForTest("Seb"s);
      Capture(player, "text-entry"s, Step::Quit);
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
