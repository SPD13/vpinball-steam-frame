// license:GPLv3+

#pragma once

// Steam Frame Edition, cabinet launcher ('-Cabinet'): the pictures shown for the table selected in the reel, behind the reel (its playfield)
// and on the backglass window (its backglass), and the backglass window renderer of the launcher.
//
// Pictures, next to the table file (deleted with it, see TableLibrary):
// - <table>.playfield.jpg: the playfield window, captured when the table is closed in cabinet mode if it has none (Player::CaptureTableImageBeforeClosing)
// - <table>.backglass.png|jpg: the main image of the table's .directb2s, extracted once by ExtractBackglass, or the backglass window
//   captured when the table is closed in cabinet mode, for the tables without a .directb2s

#include "plugins/VPXPlugin.h"

#include <filesystem>
#include <future>

class Player;
class BaseTexture;

namespace VPX::Cabinet
{

std::filesystem::path GetPlayfieldImagePath(const std::filesystem::path& tablePath);
std::filesystem::path FindBackglassImage(const std::filesystem::path& tablePath); // Empty if none yet
std::filesystem::path GetBackglassCapturePath(const std::filesystem::path& tablePath);
// The .directb2s used by the B2S plugin for a table: <table>.directb2s, or <folder name>.directb2s (case insensitive), empty if none
std::filesystem::path FindDirectB2S(const std::filesystem::path& tablePath);
// Writes the main image of a .directb2s as <table>.backglass.png or .jpg (the image as stored, not encoded again). Slow for large files (tens of
// MB to read): call it from a worker thread. Returns the written file, empty if the file has no image.
std::filesystem::path ExtractBackglass(const std::filesystem::path& tablePath, const std::filesystem::path& directB2S);

// Saves a capture of a window turned upright as the player sees it: 'rotation' is the quarter turns of the UI on that window
// (LiveUI::GetUIOrientation), as the playfield of a cabinet lies sideways in its window. Used for the image of the table in the library.
bool SaveUpright(const std::filesystem::path& windowImage, const std::filesystem::path& output, int rotation);

// File modification date, as a stamp for TableImageCache (0 if missing)
int64_t GetFileStamp(const std::filesystem::path& path);

// Draws the backglass of the selected table on the backglass window (when it is a window of its own) while the cabinet launcher is shown, as an
// ancillary renderer like the B2S plugin's. One instance, attached to the lobby player.
class BackglassRenderer final
{
public:
   static BackglassRenderer& Get();

   void Attach(Player* player);
   void Detach(const Player* player);

   // The image to show (empty for none), cross-faded from the previous one. Called each frame by the launcher page.
   void SetImage(const std::filesystem::path& path, int64_t stamp);

private:
   static void MSGPIAPI OnGetRenderer(const unsigned int msgId, void* userData, void* msgData);
   static int MSGPIAPI Render(VPXRenderContext2D* ctx, void* context);
   void TakeLoadedImage();
   void ReleaseTextures();

   Player* m_player = nullptr;
   const VPXPluginAPI* m_api = nullptr; // Of the player
   unsigned int m_getRendererMsgId = 0;
   AncillaryRendererDef m_def {};

   std::filesystem::path m_wantedPath;
   int64_t m_wantedStamp = 0;
   std::filesystem::path m_loadingPath;
   int64_t m_loadingStamp = 0;
   std::future<std::shared_ptr<BaseTexture>> m_loading; // Decoded on a worker thread, made a texture of the plugin API on the main thread
   std::filesystem::path m_shownPath;
   int64_t m_shownStamp = 0;
   VPXTexture m_texture = nullptr;
   VPXTexture m_previousTexture = nullptr; // Fading out
   float m_fade = 1.f; // Of the current image, from 0 to 1
   uint32_t m_lastRenderMs = 0;
};

}
