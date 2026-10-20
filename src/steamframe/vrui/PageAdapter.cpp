// license:GPLv3+

// Steam Frame Edition: the bridge between upstream's in-game UI pages and the new VR UI. InGameUIPage::Render calls RenderVRUI once it has done
// its model work (page rebuild, result of a path selection dialog, open/close animation): when it returns true, the new VR UI has drawn the page
// and handled its interactions, and upstream's drawing code is skipped. This file is the only one using the private members of InGameUIPage
// (it is its friend), so that an upstream change to them only breaks the build here.

#include "core/stdafx.h"
#include "ui/live/ingameui/InGameUIPage.h"

#include "VRUI.h"

namespace VPX::InGameUI
{

class InGameUIPageView final
{
public:
   explicit InGameUIPageView(InGameUIPage& page)
      : m_page(page)
   {
   }

   bool Render(float elapsedS)
   {
      // The new view is not drawing pages yet: upstream's view draws them
      return false;
   }

private:
   InGameUIPage& m_page;
};

bool InGameUIPage::RenderVRUI(float elapsedS)
{
   if (!VRUI::IsActive())
      return false;
   return InGameUIPageView(*this).Render(elapsedS);
}

}
