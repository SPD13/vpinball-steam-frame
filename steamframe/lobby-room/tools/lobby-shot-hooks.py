# Temporary test hook (TEMP-LOBBY-SHOT markers): run from vpinball/, `apply` or `remove`.
# With VPX_TEST_SHOT=<file.png> in the environment, the launcher's table picker saves a screenshot of the lobby 4 s after it opens
# and quits; with VPX_TEST_NOUI=1 the menu is closed first, so that only the room shows.
import re, sys
P = 'src/steamframe/TablePickerPage.cpp'
s = open(P).read()
s = re.sub(r'[ \t]*// TEMP-LOBBY-SHOT begin\n.*?// TEMP-LOBBY-SHOT end\n', '', s, flags=re.S)
if sys.argv[1] == 'apply':
    old = '''   InGameUIPage::Render(elapsedS);
}

void TablePickerPage::SetTab(int tab)'''
    new = '''   InGameUIPage::Render(elapsedS);
   // TEMP-LOBBY-SHOT begin
   if (const char* shot = getenv("VPX_TEST_SHOT"); shot)
   {
      static uint32_t shown = 0;
      static bool done = false;
      if (shown == 0)
         shown = msec();
      if (!done && msec() - shown > 4000)
      {
         done = true;
         Player* const player = m_player;
         if (getenv("VPX_TEST_NOUI"))
            player->m_liveUI->m_inGameUI.Close();
         player->m_renderer->m_renderDevice->CaptureScreenshot({ player->m_playfieldWnd }, { std::filesystem::path(shot) },
            [player](bool ok) { PLOGI << "LOBBY-SHOT " << ok; g_launcher->m_launcherMode = false; player->SetCloseState(Player::CS_CLOSE_APP); }, 60);
      }
   }
   // TEMP-LOBBY-SHOT end
}

void TablePickerPage::SetTab(int tab)'''
    assert s.count(old) == 1
    s = s.replace(old, new)
open(P, 'w').write(s)
