# Temporary test hook (TEMP-LOBBY-SHOT markers): run from vpinball/, `apply` or `remove`.
# With VPX_TEST_SHOT=<file.png> in the environment, the in-game menu saves a screenshot 4.5 s after it is first shown and quits. With
# VPX_TEST_PAGE=<page> (like lobby/rooms) it navigates to that page first; with VPX_TEST_NOUI=1 the menu is closed before the
# screenshot, so that only the room shows (the thumbnails of the room chooser are made this way).
import re, sys
P = 'src/ui/live/ingameui/InGameUIPage.cpp'
s = open(P, newline='').read()
s = re.sub(r'[ \t]*// TEMP-LOBBY-SHOT begin\r?\n.*?// TEMP-LOBBY-SHOT end\r?\n', '', s, flags=re.S)
if sys.argv[1] == 'apply':
    nl = '\r\n' if '\r\n' in s else '\n'
    includes = '''#include "ui/live/LiveUI.h"
'''
    hook_includes = '''// TEMP-LOBBY-SHOT begin
#include "renderer/Renderer.h"
#include "steamframe/Launcher.h"
// TEMP-LOBBY-SHOT end
'''
    old = '''void InGameUIPage::Render(float elapsedS)
{
'''
    hook = '''   // TEMP-LOBBY-SHOT begin
   if (const char* shot = getenv("VPX_TEST_SHOT"); shot)
   {
      static uint32_t shown = 0;
      static int step = 0;
      if (shown == 0)
         shown = msec();
      if (step == 0 && getenv("VPX_TEST_PAGE") && msec() - shown > 2000)
      {
         step = 1;
         m_player->m_liveUI->m_inGameUI.Navigate(getenv("VPX_TEST_PAGE"));
      }
      if (step < 2 && msec() - shown > 4500)
      {
         step = 2;
         Player* const player = m_player;
         if (getenv("VPX_TEST_NOUI"))
            player->m_liveUI->m_inGameUI.Close();
         player->m_renderer->m_renderDevice->CaptureScreenshot({ player->m_playfieldWnd }, { std::filesystem::path(shot) },
            [player](bool ok) { PLOGI << "LOBBY-SHOT " << ok; g_launcher->m_launcherMode = false; player->SetCloseState(Player::CS_CLOSE_APP); }, 60);
      }
   }
   // TEMP-LOBBY-SHOT end
'''
    assert s.replace('\r\n', '\n').count(old) == 1 and s.replace('\r\n', '\n').count(includes) == 1
    s = s.replace(old.replace('\n', nl), (old + hook).replace('\n', nl))
    s = s.replace(includes.replace('\n', nl), (includes + hook_includes).replace('\n', nl))
open(P, 'w', newline='').write(s)
