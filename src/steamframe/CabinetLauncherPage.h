// license:GPLv3+

#pragma once

// Steam Frame Edition, cabinet launcher ('-Cabinet'): the start screen of a pinball cabinet, driven by its buttons. A reel of the tables of the
// library at the bottom of the playfield screen, the selected one magnified in the middle, over a picture of its playfield, its backglass being
// shown on the backglass screen; below it, a band of options: Search (keyboard driven by the buttons), Options (the full table picker), Quit
// (back to the operating system) and Shutdown (turns the computer off, after a confirmation).
//
// Buttons: left/right (flippers) move in the focused band, held to scroll faster; up/down (magna-saves) and Launch Ball switch between the reel
// and the options; Start plays the table or selects the option; Back (Credit, Exit) closes the keyboard, gets back to the reel, then clears the
// search.

#include "ui/live/ingameui/InGameUIPage.h"
#include "steamframe/TableImageCache.h"
#include "steamframe/TableLibrary.h"

#include <future>

namespace VPX::InGameUI
{

class CabinetLauncherPage final : public InGameUIPage
{
public:
   CabinetLauncherPage();
   ~CabinetLauncherPage() override;

   void Open(bool isBackwardAnimation) override;
   void Render(float elapsedS) override;
   void SelectNextItem() override; // Down: to the options
   void SelectPrevItem() override; // Up: to the reel
   void AdjustItem(float direction, bool isInitialPress) override; // Left/right, called each frame while held
   void ReleaseAdjustedItem() override { }
   void ActivateItem() override; // Start
   void ResetToDefaults() override { } // Launch Ball: unused (Back switches the bands), not to reset anything by mistake
   bool HandleBack(bool wasButtonNav) override;
   bool IsFlipperNavNeeded() const override { return true; } // Buttons only: moving the mouse does not switch to pointer navigation
   bool IsPlayerPauseAllowed() const override { return false; } // The lobby keeps running (no pause sign)

private:
   void BuildPage() override { }

   enum class Option { Search, Options, Quit, Shutdown, Count };
   enum class Band { Reel, Options };

   void RefreshList(bool force);
   void SetSelectionFromUuid(const string& uuid);
   void Step(int direction);
   int GetCount() const { return static_cast<int>(m_tables.size()); }
   bool IsWrapping() const;
   const VPinballLib::Table* GetSelectedTable() const;
   void Play();
   void Activate(Option option);
   void OpenSearch();
   void SetSearch(const string& search);

   // Pictures of a table (see CabinetBackglass.h), looked for again from time to time, as they are made while the launcher is shown (backglass
   // extracted, uploads from a browser)
   struct TableFiles
   {
      std::filesystem::path playfield;
      int64_t playfieldStamp = 0;
      std::filesystem::path backglass;
      int64_t backglassStamp = 0;
      uint32_t checkedAt = 0;
   };
   const TableFiles& GetTableFiles(const VPinballLib::Table& table);
   void UpdateBackglassExtraction();

   void RenderBackground(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderReel(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderInfo(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderOptions(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderEmpty(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderConfirm(ImDrawList* drawList, const ImVec2& size, float alpha);
   void RenderKeyboard(const ImVec2& size);
   void HandleMouse();

   // Layout, in UI pixels, from the size of the screen (upright, the UI turning with the playfield)
   struct Layout
   {
      float unit; // 1 % of the shortest side
      float tileWidth; // Of a tile at rest, magnified up to kMagnification in the middle
      float spacing;
      float reelCenterY;
      float optionsCenterY;
      float optionHeight;
      float infoBottomY;
   };
   Layout GetLayout(const ImVec2& size) const;

   VPX::TableImageCache m_thumbnails;
   VPX::TableImageCache m_pictures; // Playfields, larger: a few only

   vector<VPinballLib::Table> m_tables; // Shown in the reel: all the tables by name, or those matching the search, best first
   uint64_t m_revision = 0;
   bool m_listBuilt = false;
   ankerl::unordered_dense::map<string, TableFiles> m_files;

   int m_targetPos = 0; // Selected position, not wrapped (the selected table is m_targetPos modulo the count), so that the reel always scrolls the short way
   float m_scrollPos = 0.f; // Animated towards m_targetPos
   Band m_band = Band::Reel;
   int m_option = 0;
   bool m_confirmShutdown = false;
   int m_confirmChoice = 0; // 0: cancel, 1: shut down
   bool m_starting = false; // A table was chosen: the lobby is closing

   uint32_t m_nextRepeatMs = 0; // Left/right held
   uint32_t m_holdStartMs = 0;

   // Background: the picture shown, and the previous one while it fades out
   struct Backdrop
   {
      std::filesystem::path path;
      int64_t stamp = 0;
      bool isPlayfield = false; // A capture of the playfield window, drawn as it was captured, otherwise an image of the table filling the screen, dimmed
   };
   Backdrop m_backdrop, m_previousBackdrop;
   float m_backdropFade = 1.f;

   // Hit areas of the last frame, for the mouse
   struct HitArea
   {
      ImVec2 min, max;
      int index;
   };
   vector<HitArea> m_tileAreas, m_optionAreas, m_confirmAreas;
};

}
