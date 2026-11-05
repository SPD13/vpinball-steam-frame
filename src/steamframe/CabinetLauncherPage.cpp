// license:GPLv3+

#include "core/stdafx.h"
#include "CabinetLauncherPage.h"

#include "core/VPApp.h"
#include "renderer/Renderer.h"
#include "steamframe/CabinetBackglass.h"
#include "steamframe/Launcher.h"
#include "steamframe/vrui/Keyboard.h"
#include "steamframe/vrui/Theme.h"
#include "ui/live/LiveUI.h"

#include "fonts/IconsForkAwesome.h"

using LibraryTable = VPinballLib::Table; // 'Table' alone is ambiguous with VPX parts
using VPinballLib::TableLibrary;
namespace Keyboard = VPX::VRUI::Keyboard;
namespace Theme = VPX::VRUI::Theme;

namespace VPX::InGameUI
{

// Open/close animation of the pages which draw themselves instead of calling InGameUIPage::Render (same animation)
void InGameUIPage::UpdateOpenAnimation(float elapsedS)
{
   if (m_openAnimTarget != m_openAnimPos)
   {
      m_openAnimElapsed += elapsedS;
      m_openAnimPos = lerp(m_openAnimStart, m_openAnimTarget, smoothstep(0.f, 0.5f, m_openAnimElapsed));
      if (fabsf(m_openAnimTarget - m_openAnimPos) < 0.001f)
         m_openAnimPos = m_openAnimTarget;
   }
}

namespace
{
   // Magnifying glass: the scale of a tile is 1 + (kMagnification - 1) * bell(distance to the middle of the reel), the bell being a raised
   // cosine over kLensRadius tiles. Computed from the scrolling position, so tiles swell while they slide to the middle.
   constexpr float kMagnification = 1.8f;
   constexpr float kLensRadius = 2.f;
   constexpr float kTileAspect = 0.75f; // Height / width of the box a tile image fits in
   constexpr int kMinWrapCount = 7; // Fewer tables: the reel stops at its ends instead of looping, which would make tiles jump from one side to the other

   constexpr const char* kSearchOwner = "cabinet/search";

   // ImTextureID is a std::shared_ptr<Sampler> (imconfig.h), but ImGui copies draw commands with memcpy: when an image is drawn while the
   // current draw command is still empty, ImGui assigns the texture into that command, which takes a reference that is never released, so
   // the sampler and its GPU texture outlive the cache (measured: 38 images kept after browsing 40 tables, about 0.5 GB of GPU memory, until the lobby closes). Giving the current
   // command an element first (an empty triangle pair with the font texture, nothing drawn) makes ImGui open a new command for the image, the
   // path where references are balanced. Call before each AddImage*.
   void PrepareImageCommand(ImDrawList* drawList)
   {
      drawList->PrimReserve(6, 4);
      const ImVec2 uv = drawList->_Data->TexUvWhitePixel;
      for (int i = 0; i < 4; i++)
         drawList->PrimWriteVtx(ImVec2(0.f, 0.f), uv, 0);
      for (const int i : { 0, 1, 2, 0, 2, 3 })
         drawList->PrimWriteIdx(static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx - 4 + i));
   }

   constexpr const char* kOptionIcons[] = { ICON_FK_SEARCH, ICON_FK_SLIDERS, ICON_FK_SIGN_OUT, ICON_FK_POWER_OFF };
   constexpr const char* kOptionLabels[] = { "Search", "Options", "Quit", "Shutdown" };

   string s_search; // Kept while the application runs, across the tables played

   float Magnification(float distance)
   {
      const float d = min(fabsf(distance), kLensRadius);
      return 1.f + (kMagnification - 1.f) * 0.5f * (1.f + cosf(static_cast<float>(M_PI) * d / kLensRadius));
   }

   // Center of a tile, in tile spacings from the middle of the reel: the integral of the magnification, so that magnified tiles push their
   // neighbors aside instead of overlapping them
   float TileOffset(float distance)
   {
      const float d = fabsf(distance);
      const float x = min(d, kLensRadius);
      const float swell = (kMagnification - 1.f) * 0.5f * (x + kLensRadius / static_cast<float>(M_PI) * sinf(static_cast<float>(M_PI) * x / kLensRadius));
      return (distance < 0.f ? -1.f : 1.f) * (d + swell);
   }

   ImU32 WithAlpha(ImU32 color, float alpha)
   {
      const uint32_t a = static_cast<uint32_t>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * clamp(alpha, 0.f, 1.f));
      return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
   }

   ImU32 Gray(float level, float alpha) // level and alpha from 0 to 1
   {
      const int v = static_cast<int>(clamp(level, 0.f, 1.f) * 255.f);
      return IM_COL32(v, v, v, static_cast<int>(clamp(alpha, 0.f, 1.f) * 255.f));
   }

   ImVec2 TextSize(ImFont* font, float size, const char* text, float wrapWidth = 0.f) { return font->CalcTextSizeA(size, FLT_MAX, wrapWidth, text); }

   void CenteredText(ImDrawList* drawList, ImFont* font, float size, const ImVec2& center, ImU32 color, const char* text)
   {
      const ImVec2 textSize = TextSize(font, size, text);
      drawList->AddText(font, size, ImVec2(center.x - 0.5f * textSize.x, center.y - 0.5f * textSize.y), color, text);
   }

   // Text cut with an ellipsis to fit a width
   string Ellipsis(ImFont* font, float size, const string& text, float maxWidth)
   {
      if (TextSize(font, size, text.c_str()).x <= maxWidth)
         return text;
      string cut = text;
      while (!cut.empty() && TextSize(font, size, (cut + "...").c_str()).x > maxWidth)
         cut.pop_back();
      return cut + "...";
   }

   string FormatAge(int64_t time)
   {
      const int64_t seconds = static_cast<int64_t>(std::time(nullptr)) - time;
      if (seconds < 3600)
         return "less than an hour ago"s;
      if (seconds < 86400)
         return std::format("{} hour{} ago", seconds / 3600, seconds < 7200 ? "" : "s");
      const int64_t days = seconds / 86400;
      if (days < 31)
         return days == 1 ? "yesterday"s : std::format("{} days ago", days);
      const int64_t months = days / 30;
      if (months < 12)
         return std::format("{} month{} ago", months, months == 1 ? "" : "s");
      return std::format("{} year{} ago", months / 12, months < 24 ? "" : "s");
   }

   // The capture of the playfield window holds the window pixels, while the UI is turned with the playfield: the texture coordinates of the
   // corners of the upright screen ('nx', 'ny' from 0 to 1) in the window, so that the capture lands on the pixels it was taken from (same
   // mapping as LiveUI::AddMousePosEvent)
   ImVec2 WindowUV(int rotation, float nx, float ny)
   {
      switch (rotation)
      {
      case 1: return ImVec2(1.f - ny, nx);
      case 2: return ImVec2(1.f - nx, 1.f - ny);
      case 3: return ImVec2(ny, 1.f - nx);
      default: return ImVec2(nx, ny);
      }
   }

   // Spinner while a thumbnail is decoded
   void Spinner(ImDrawList* drawList, const ImVec2& center, float radius, ImU32 color)
   {
      const float start = static_cast<float>(ImGui::GetTime()) * 6.f;
      drawList->PathArcTo(center, radius, start, start + 4.f, 24);
      drawList->PathStroke(color, ImDrawFlags_None, max(2.f, radius * 0.2f));
   }

   // Backglass extraction of the selected table (one at a time, each table tried once while the application runs)
   std::future<void> s_extraction;
   ankerl::unordered_dense::set<string> s_extractionTried;
}

CabinetLauncherPage::CabinetLauncherPage()
   : InGameUIPage("Visual Pinball"s, ""s, SaveMode::None)
   , m_thumbnails(m_player, 80)
   , m_pictures(m_player, 6)
{
}

CabinetLauncherPage::~CabinetLauncherPage()
{
   if (Keyboard::IsEditing(kSearchOwner))
      Keyboard::Close();
}

void CabinetLauncherPage::Open(bool isBackwardAnimation)
{
   InGameUIPage::Open(isBackwardAnimation);
   if (!isBackwardAnimation)
      g_launcher->GetTableLibrary().RescanAsync();
}


///////////////////////////////////////////////////////////////////////////////
// List and selection

void CabinetLauncherPage::RefreshList(bool force)
{
   TableLibrary& library = g_launcher->GetTableLibrary();
   if (!force && m_listBuilt && m_revision == library.GetRevision())
      return;
   m_listBuilt = true;
   m_revision = library.GetRevision();
   const string selectedUuid = GetSelectedTable() ? GetSelectedTable()->uuid : g_launcher->m_cabinetSelectedUuid;

   vector<LibraryTable> tables = library.GetTables(true);
   if (!s_search.empty())
   {
      vector<std::pair<int, LibraryTable>> matches;
      for (LibraryTable& table : tables)
         if (const std::optional<int> score = TableLibrary::FuzzyScore(s_search, table.name); score)
            matches.emplace_back(*score, std::move(table));
      std::ranges::stable_sort(matches, [](const auto& a, const auto& b) { return a.first > b.first; });
      tables.clear();
      for (auto& [score, table] : matches)
         tables.push_back(std::move(table));
   }
   m_tables = std::move(tables);
   SetSelectionFromUuid(selectedUuid);
}

void CabinetLauncherPage::SetSelectionFromUuid(const string& uuid)
{
   int index = 0;
   for (int i = 0; i < GetCount(); i++)
      if (m_tables[i].uuid == uuid)
         index = i;
   m_targetPos = index;
   m_scrollPos = static_cast<float>(index);
   if (const LibraryTable* table = GetSelectedTable())
      g_launcher->m_cabinetSelectedUuid = table->uuid;
}

bool CabinetLauncherPage::IsWrapping() const { return GetCount() >= kMinWrapCount; }

const LibraryTable* CabinetLauncherPage::GetSelectedTable() const
{
   if (m_tables.empty())
      return nullptr;
   const int n = GetCount();
   return &m_tables[((m_targetPos % n) + n) % n];
}

void CabinetLauncherPage::Step(int direction)
{
   if (m_tables.empty())
      return;
   m_targetPos += direction;
   if (!IsWrapping())
      m_targetPos = clamp(m_targetPos, 0, GetCount() - 1);
   g_launcher->m_cabinetSelectedUuid = GetSelectedTable()->uuid;
}

void CabinetLauncherPage::SetSearch(const string& search)
{
   if (search == s_search)
      return;
   s_search = search;
   // The best match first
   RefreshList(true);
   if (!s_search.empty() && !m_tables.empty())
   {
      m_targetPos = 0;
      g_launcher->m_cabinetSelectedUuid = m_tables[0].uuid;
   }
}


///////////////////////////////////////////////////////////////////////////////
// Actions

void CabinetLauncherPage::Play()
{
   const LibraryTable* table = GetSelectedTable();
   if (table == nullptr || m_starting)
      return;
   m_starting = true;
   if (Keyboard::IsEditing(kSearchOwner))
      Keyboard::Close();
   g_launcher->m_cabinetSelectedUuid = table->uuid;
   g_launcher->m_nextTableFilename = g_launcher->GetTableLibrary().GetFullPath(*table);
   m_player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
}

void CabinetLauncherPage::Activate(Option option)
{
   switch (option)
   {
   case Option::Search: OpenSearch(); break;
   case Option::Options:
      if (Keyboard::IsEditing(kSearchOwner))
         Keyboard::Close();
      m_player->m_liveUI->m_inGameUI.Navigate("tables/picker"s);
      break;
   case Option::Quit: g_launcher->QuitApplication(m_player); break;
   case Option::Shutdown:
      m_confirmShutdown = true;
      m_confirmChoice = 0;
      break;
   default: break;
   }
}

void CabinetLauncherPage::OpenSearch()
{
   Keyboard::Open(kSearchOwner, "Search tables"s, s_search, 64, [this](const string& text) { SetSearch(text); }, []() { Keyboard::Close(); });
   Keyboard::Focus();
   m_band = Band::Reel; // Left/right browse the results once the keyboard is left with Back
}


///////////////////////////////////////////////////////////////////////////////
// Buttons

void CabinetLauncherPage::AdjustItem(float direction, bool isInitialPress)
{
   const int step = direction < 0.f ? -1 : 1;
   const uint32_t now = msec();
   if (m_confirmShutdown)
   {
      if (isInitialPress)
         m_confirmChoice = 1 - m_confirmChoice;
      return;
   }
   if (m_band == Band::Options)
   {
      if (isInitialPress)
         m_option = (m_option + step + static_cast<int>(Option::Count)) % static_cast<int>(Option::Count);
      return;
   }
   // Held: repeats, faster and faster
   if (isInitialPress)
   {
      Step(step);
      m_holdStartMs = now;
      m_nextRepeatMs = now + 400;
   }
   else if (now >= m_nextRepeatMs)
   {
      Step(step);
      const uint32_t held = now - m_holdStartMs;
      m_nextRepeatMs = now + (held > 2500 ? 45 : held > 1200 ? 80 : 140);
   }
}

void CabinetLauncherPage::SelectNextItem()
{
   if (m_confirmShutdown)
      return;
   m_band = Band::Options;
}

void CabinetLauncherPage::SelectPrevItem()
{
   if (m_confirmShutdown)
      return;
   if (m_band == Band::Options)
      m_band = Band::Reel;
   else if (Keyboard::IsEditing(kSearchOwner))
      Keyboard::Focus(); // Back to typing
}

void CabinetLauncherPage::ResetToDefaults()
{
   if (m_confirmShutdown)
      return;
   m_band = m_band == Band::Reel ? Band::Options : Band::Reel;
}

void CabinetLauncherPage::ActivateItem()
{
   if (m_confirmShutdown)
   {
      m_confirmShutdown = false;
      if (m_confirmChoice == 1)
      {
         g_launcher->RequestShutdown();
         g_launcher->QuitApplication(m_player);
      }
      return;
   }
   if (m_band == Band::Reel)
      Play();
   else
      Activate(static_cast<Option>(m_option));
}

bool CabinetLauncherPage::HandleBack()
{
   if (m_confirmShutdown)
      m_confirmShutdown = false;
   else if (Keyboard::IsEditing(kSearchOwner))
      Keyboard::Close();
   else if (m_band == Band::Options)
      m_band = Band::Reel;
   else if (!s_search.empty())
      SetSearch(""s);
   // The launcher is the start screen: it is never closed with Back
   return true;
}


///////////////////////////////////////////////////////////////////////////////
// Pictures

const CabinetLauncherPage::TableFiles& CabinetLauncherPage::GetTableFiles(const LibraryTable& table)
{
   TableFiles& files = m_files[table.uuid];
   const uint32_t now = msec();
   if (files.checkedAt == 0 || now - files.checkedAt > 2000)
   {
      files.checkedAt = max(now, 1u);
      const std::filesystem::path tablePath = g_launcher->GetTableLibrary().GetFullPath(table);
      files.playfield = Cabinet::GetPlayfieldImagePath(tablePath);
      files.playfieldStamp = Cabinet::GetFileStamp(files.playfield);
      if (files.playfieldStamp == 0)
         files.playfield.clear();
      files.backglass = Cabinet::FindBackglassImage(tablePath);
      files.backglassStamp = files.backglass.empty() ? 0 : Cabinet::GetFileStamp(files.backglass);
   }
   return files;
}

// The backglass of the selected table, then of its neighbors, is extracted from its .directb2s on a worker thread when it has none yet
void CabinetLauncherPage::UpdateBackglassExtraction()
{
   if (s_extraction.valid())
   {
      if (s_extraction.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
         return;
      s_extraction.get();
      for (auto& [uuid, files] : m_files)
         files.checkedAt = 0; // Look for the new image
   }
   if (m_tables.empty())
      return;
   const int n = GetCount();
   for (const int offset : { 0, 1, -1 })
   {
      const LibraryTable& table = m_tables[(((m_targetPos + offset) % n) + n) % n];
      if (s_extractionTried.contains(table.uuid))
         continue;
      if (!GetTableFiles(table).backglass.empty())
      {
         s_extractionTried.insert(table.uuid);
         continue;
      }
      s_extractionTried.insert(table.uuid);
      const std::filesystem::path tablePath = g_launcher->GetTableLibrary().GetFullPath(table);
      s_extraction = std::async(std::launch::async,
         [tablePath]()
         {
            if (const std::filesystem::path directB2S = Cabinet::FindDirectB2S(tablePath); !directB2S.empty())
               Cabinet::ExtractBackglass(tablePath, directB2S);
         });
      return;
   }
}


///////////////////////////////////////////////////////////////////////////////
// Drawing

CabinetLauncherPage::Layout CabinetLauncherPage::GetLayout(const ImVec2& size) const
{
   Layout layout;
   layout.unit = 0.01f * min(size.x, size.y);
   // About 6 tiles at rest across the screen
   layout.tileWidth = size.x / 6.6f;
   layout.spacing = layout.tileWidth * 1.06f;
   layout.optionHeight = 5.5f * layout.unit;
   layout.optionsCenterY = size.y - 7.5f * layout.unit;
   const float reelHeight = layout.tileWidth * kMagnification * kTileAspect;
   layout.reelCenterY = layout.optionsCenterY - 0.5f * layout.optionHeight * 1.3f - 3.f * layout.unit - 0.5f * reelHeight;
   layout.infoBottomY = layout.reelCenterY - 0.5f * reelHeight - 2.5f * layout.unit;
   return layout;
}

void CabinetLauncherPage::Render(float elapsedS)
{
   UpdateOpenAnimation(elapsedS);
   m_thumbnails.Update();
   m_pictures.Update();
   RefreshList(false);
   UpdateBackglassExtraction();

   // The reel eases to the selected position
   const float target = static_cast<float>(m_targetPos);
   m_scrollPos += (target - m_scrollPos) * (1.f - expf(-14.f * min(elapsedS, 0.1f)));
   if (fabsf(target - m_scrollPos) < 0.001f)
      m_scrollPos = target;

   Theme::BeginFrame();
   const ImGuiIO& io = ImGui::GetIO();
   const ImVec2 size = io.DisplaySize;
   const float alpha = 1.f - clamp(fabsf(GetOpenCloseAnimPos()), 0.f, 1.f);

   ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
   ImGui::SetNextWindowSize(size);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   ImGui::Begin("##CabinetLauncher", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
         | ImGuiWindowFlags_NoFocusOnAppearing);
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   RenderBackground(drawList, size, alpha);
   m_tileAreas.clear();
   m_optionAreas.clear();
   m_confirmAreas.clear();
   if (m_tables.empty())
      RenderEmpty(drawList, size, alpha);
   else
   {
      RenderInfo(drawList, size, alpha);
      RenderReel(drawList, size, alpha);
   }
   RenderOptions(drawList, size, alpha);
   if (m_confirmShutdown)
      RenderConfirm(drawList, size, alpha);
   if (IsActive())
      HandleMouse();
   ImGui::End();
   ImGui::PopStyleVar(2);

   if (Keyboard::IsEditing(kSearchOwner))
   {
      if (IsActive())
         RenderKeyboard(size);
      Keyboard::EndFrame(); // Closes it when it was not shown
   }

   // The backglass screen shows the backglass of the selected table
   if (IsActive())
   {
      const LibraryTable* const table = GetSelectedTable();
      const TableFiles* const files = table ? &GetTableFiles(*table) : nullptr;
      Cabinet::BackglassRenderer::Get().SetImage(files ? files->backglass : std::filesystem::path(), files ? files->backglassStamp : 0);
   }
}

void CabinetLauncherPage::RenderBackground(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   // The picture wanted: the playfield captured on this cabinet, otherwise the image of the table in the library, dimmed
   Backdrop wanted;
   if (const LibraryTable* table = GetSelectedTable())
   {
      const TableFiles& files = GetTableFiles(*table);
      if (!files.playfield.empty())
         wanted = { files.playfield, files.playfieldStamp, true };
      else if (!table->image.empty())
         wanted = { g_launcher->GetTableLibrary().GetImagePath(*table), table->modifiedAt, false };
   }
   // Switch once decoded, so that the screen never flashes
   if (wanted.path != m_backdrop.path || wanted.stamp != m_backdrop.stamp)
   {
      const bool ready = wanted.path.empty() || m_pictures.Get(wanted.path, wanted.stamp, 2048) != nullptr || !m_pictures.IsLoading(wanted.path);
      if (ready)
      {
         m_previousBackdrop = m_backdrop;
         m_backdrop = wanted;
         m_backdropFade = 0.f;
      }
   }
   m_backdropFade = min(1.f, m_backdropFade + ImGui::GetIO().DeltaTime / 0.3f);

   // Black under everything: the lobby is not shown
   drawList->AddRectFilled(ImVec2(0.f, 0.f), size, IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.f)));
   const int rotation = m_player->m_liveUI->GetUIOrientation();
   const auto draw = [&](const Backdrop& backdrop, float opacity)
   {
      if (backdrop.path.empty() || opacity <= 0.f)
         return;
      const ImTextureID texture = m_pictures.Get(backdrop.path, backdrop.stamp, 2048);
      if (texture == nullptr)
         return;
      if (backdrop.isPlayfield)
      {
         PrepareImageCommand(drawList);
         drawList->AddImageQuad(texture, ImVec2(0.f, 0.f), ImVec2(size.x, 0.f), size, ImVec2(0.f, size.y), WindowUV(rotation, 0.f, 0.f), WindowUV(rotation, 1.f, 0.f),
            WindowUV(rotation, 1.f, 1.f), WindowUV(rotation, 0.f, 1.f), Gray(1.f, opacity));
         return;
      }
      // Fills the screen, cropped, dimmed: an image of the table rather than its playfield
      const ImVec2 imageSize = m_pictures.GetSize(backdrop.path);
      if (imageSize.x <= 0.f || imageSize.y <= 0.f)
         return;
      const float imageAspect = imageSize.x / imageSize.y, screenAspect = size.x / size.y;
      ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
      if (imageAspect > screenAspect)
      {
         const float w = screenAspect / imageAspect;
         uv0.x = 0.5f - 0.5f * w;
         uv1.x = 0.5f + 0.5f * w;
      }
      else
      {
         const float h = imageAspect / screenAspect;
         uv0.y = 0.5f - 0.5f * h;
         uv1.y = 0.5f + 0.5f * h;
      }
      PrepareImageCommand(drawList);
      drawList->AddImage(texture, ImVec2(0.f, 0.f), size, uv0, uv1, Gray(0.45f, opacity));
   };
   draw(m_previousBackdrop, alpha * (1.f - m_backdropFade));
   draw(m_backdrop, alpha * (m_previousBackdrop.path.empty() ? 1.f : m_backdropFade));
   if (m_backdropFade >= 1.f)
      m_previousBackdrop = Backdrop();

   // Darker at the bottom, under the reel and the texts
   const float top = size.y * 0.45f;
   drawList->AddRectFilledMultiColor(ImVec2(0.f, top), size, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, static_cast<int>(alpha * 235.f)),
      IM_COL32(0, 0, 0, static_cast<int>(alpha * 235.f)));
}

void CabinetLauncherPage::RenderReel(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   const Layout layout = GetLayout(size);
   const int n = GetCount();
   const bool wrap = IsWrapping();
   const float centerX = 0.5f * size.x;
   const TableLibrary& library = g_launcher->GetTableLibrary();
   ImFont* const font = Theme::Font();
   const bool reelFocused = m_band == Band::Reel && !m_confirmShutdown;

   // The tiles on screen, drawn from the farthest to the middle one, which overlaps its neighbors
   struct Tile
   {
      int index;
      float distance;
   };
   vector<Tile> tiles;
   for (int i = 0; i < n; i++)
   {
      float distance = static_cast<float>(i) - m_scrollPos;
      if (wrap)
      {
         distance = fmodf(distance, static_cast<float>(n));
         if (distance < -0.5f * static_cast<float>(n))
            distance += static_cast<float>(n);
         else if (distance >= 0.5f * static_cast<float>(n))
            distance -= static_cast<float>(n);
      }
      const float x = centerX + TileOffset(distance) * layout.spacing;
      const float halfWidth = 0.5f * layout.tileWidth * Magnification(distance);
      if (x + halfWidth < 0.f || x - halfWidth > size.x)
         continue;
      tiles.push_back({ i, distance });
   }
   std::ranges::sort(tiles, [](const Tile& a, const Tile& b) { return fabsf(a.distance) > fabsf(b.distance); });

   const int selected = ((m_targetPos % n) + n) % n;
   for (const Tile& tile : tiles)
   {
      const LibraryTable& table = m_tables[tile.index];
      const float scale = Magnification(tile.distance);
      const float lens = (scale - 1.f) / (kMagnification - 1.f); // 1 in the middle, 0 outside the lens
      const ImVec2 boxSize(layout.tileWidth * scale, layout.tileWidth * scale * kTileAspect);
      const ImVec2 center(centerX + TileOffset(tile.distance) * layout.spacing, layout.reelCenterY + (1.f - lens) * 0.12f * boxSize.y);
      const ImVec2 boxMin = center - boxSize * 0.5f, boxMax = center + boxSize * 0.5f;
      const float rounding = 1.2f * layout.unit * scale;
      const float brightness = 0.55f + 0.45f * lens;
      const bool isSelected = tile.index == selected;

      // Soft shadow and frame of the magnified tile
      if (lens > 0.05f)
      {
         for (int i = 3; i >= 1; i--)
         {
            const float spread = static_cast<float>(i) * 0.6f * layout.unit;
            drawList->AddRectFilled(boxMin - ImVec2(spread, spread) + ImVec2(0.f, layout.unit), boxMax + ImVec2(spread, spread) + ImVec2(0.f, layout.unit),
               IM_COL32(0, 0, 0, static_cast<int>(alpha * lens * 40.f)), rounding + spread);
         }
      }
      drawList->AddRectFilled(boxMin, boxMax, IM_COL32(24, 28, 38, static_cast<int>(alpha * (150.f + 80.f * lens))), rounding);

      // The image fits in the box, keeping its proportions (and its transparency, like the wheels of front-ends)
      const std::filesystem::path imagePath = library.GetImagePath(table);
      const ImTextureID texture = m_thumbnails.Get(imagePath, table.modifiedAt, 512);
      const ImVec2 imageSize = m_thumbnails.GetSize(imagePath);
      if (texture && imageSize.x > 0.f && imageSize.y > 0.f)
      {
         const float fit = min(boxSize.x / imageSize.x, boxSize.y / imageSize.y);
         const ImVec2 drawSize = imageSize * fit;
         const ImVec2 imageMin = center - drawSize * 0.5f;
         PrepareImageCommand(drawList);
         drawList->AddImageRounded(texture, imageMin, imageMin + drawSize, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), Gray(brightness, alpha), rounding);
      }
      else if (m_thumbnails.IsLoading(imagePath))
         Spinner(drawList, center, 0.12f * boxSize.y, WithAlpha(Theme::kTextMuted, alpha));
      else
      {
         // No image: the name of the table
         const float textSize = 2.2f * layout.unit * scale;
         const float wrapWidth = boxSize.x - 2.f * layout.unit;
         const ImVec2 textExtent = TextSize(font, textSize, table.name.c_str(), wrapWidth);
         drawList->AddText(font, textSize, ImVec2(center.x - 0.5f * min(textExtent.x, wrapWidth), center.y - 0.5f * textExtent.y), Gray(brightness, alpha), table.name.c_str(),
            nullptr, wrapWidth);
      }
      if (table.favorite)
      {
         const float starSize = 2.4f * layout.unit * scale;
         drawList->AddText(font, starSize, ImVec2(boxMax.x - starSize - 0.6f * layout.unit, boxMin.y + 0.5f * layout.unit), WithAlpha(Theme::kWarning, alpha), ICON_FK_STAR);
      }
      if (isSelected && lens > 0.5f)
      {
         const ImU32 frame = reelFocused ? Theme::kAccent : IM_COL32(200, 205, 215, 255);
         drawList->AddRect(boxMin, boxMax, WithAlpha(frame, alpha * (lens - 0.5f) * 2.f), rounding, ImDrawFlags_None, 0.45f * layout.unit);
      }
      m_tileAreas.push_back({ boxMin, boxMax, tile.index });
   }
}

void CabinetLauncherPage::RenderInfo(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   const LibraryTable* const table = GetSelectedTable();
   if (table == nullptr)
      return;
   const Layout layout = GetLayout(size);
   ImFont* const font = Theme::Font();
   ImFont* const bold = Theme::BoldFont();
   const float margin = 4.f * layout.unit;

   // Name, then when it was played
   const float detailSize = 2.4f * layout.unit;
   const float nameSize = 4.6f * layout.unit;
   const string details = table->playCount == 0 ? "Never played"s
      : std::format("Played {} time{}, last {}", table->playCount, table->playCount == 1 ? "" : "s", FormatAge(table->lastPlayedAt));
   const float detailsY = layout.infoBottomY - detailSize;
   CenteredText(drawList, font, detailSize, ImVec2(0.5f * size.x, detailsY + 0.5f * detailSize), WithAlpha(Theme::kTextMuted, alpha), details.c_str());
   const string name = Ellipsis(bold, nameSize, table->name, size.x - 2.f * margin - (table->favorite ? 2.f * nameSize : 0.f));
   const float nameY = detailsY - 0.6f * layout.unit - nameSize;
   CenteredText(drawList, bold, nameSize, ImVec2(0.5f * size.x, nameY + 0.5f * nameSize), WithAlpha(Theme::kText, alpha), name.c_str());
   if (table->favorite)
   {
      const float nameWidth = TextSize(bold, nameSize, name.c_str()).x;
      drawList->AddText(font, 0.8f * nameSize, ImVec2(0.5f * (size.x + nameWidth) + 0.8f * layout.unit, nameY + 0.1f * nameSize), WithAlpha(Theme::kWarning, alpha), ICON_FK_STAR);
   }

   // Position in the list, and the search
   const float smallSize = 2.f * layout.unit;
   const int n = GetCount();
   const string position = std::format("{} / {}", ((m_targetPos % n) + n) % n + 1, n);
   const float rowY = nameY - 1.5f * layout.unit - smallSize;
   drawList->AddText(font, smallSize, ImVec2(size.x - margin - TextSize(font, smallSize, position.c_str()).x, rowY), WithAlpha(Theme::kTextMuted, alpha), position.c_str());
   if (!s_search.empty())
   {
      const string chip = string(ICON_FK_SEARCH) + "  " + s_search + "   (Back: clear)";
      const ImVec2 chipSize = TextSize(font, smallSize, chip.c_str());
      const ImVec2 chipPos(margin, rowY);
      drawList->AddRectFilled(chipPos - ImVec2(layout.unit, 0.5f * layout.unit), chipPos + chipSize + ImVec2(layout.unit, 0.5f * layout.unit), WithAlpha(Theme::kSurface, alpha * 0.9f),
         layout.unit);
      drawList->AddText(font, smallSize, chipPos, WithAlpha(Theme::kText, alpha), chip.c_str());
   }
}

void CabinetLauncherPage::RenderEmpty(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   const Layout layout = GetLayout(size);
   ImFont* const font = Theme::Font();
   ImFont* const bold = Theme::BoldFont();
   const float wrapWidth = size.x - 12.f * layout.unit;
   const string title = s_search.empty() ? "No tables yet"s : "No table matches '" + s_search + '\'';
   const string text = s_search.empty()
      ? "Copy tables to " + PathToUTF8(g_launcher->GetTableLibrary().GetTablesPath()) + ", or upload them from a browser: Options, then Wi-Fi upload."
      : "Press Back to clear the search."s;
   const float titleSize = 4.f * layout.unit, textSize = 2.4f * layout.unit;
   const float y = layout.reelCenterY - 4.f * layout.unit;
   CenteredText(drawList, bold, titleSize, ImVec2(0.5f * size.x, y), WithAlpha(Theme::kText, alpha), title.c_str());
   const ImVec2 extent = TextSize(font, textSize, text.c_str(), wrapWidth);
   drawList->AddText(font, textSize, ImVec2(0.5f * (size.x - min(extent.x, wrapWidth)), y + titleSize), WithAlpha(Theme::kTextMuted, alpha), text.c_str(), nullptr, wrapWidth);
}

void CabinetLauncherPage::RenderOptions(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   const Layout layout = GetLayout(size);
   ImFont* const font = Theme::Font();
   const bool focused = m_band == Band::Options && !m_confirmShutdown;
   constexpr int count = static_cast<int>(Option::Count);
   const float textSize = 2.4f * layout.unit;
   const float gap = 1.6f * layout.unit;
   // The focused option is magnified too, less than the tiles, pushing the others aside
   constexpr float focusScale = 1.2f;
   const float pillWidth = min((size.x - 6.f * layout.unit - static_cast<float>(count - 1) * gap) / (static_cast<float>(count) + focusScale - 1.f), 22.f * layout.unit);
   float totalWidth = static_cast<float>(count - 1) * gap;
   for (int i = 0; i < count; i++)
      totalWidth += pillWidth * (focused && i == m_option ? focusScale : 1.f);
   float x = 0.5f * (size.x - totalWidth);
   for (int i = 0; i < count; i++)
   {
      const bool isFocused = focused && i == m_option;
      const float scale = isFocused ? focusScale : 1.f;
      const ImVec2 center(x + 0.5f * pillWidth * scale, layout.optionsCenterY);
      x += pillWidth * scale + gap;
      const ImVec2 pillSize = ImVec2(pillWidth, layout.optionHeight) * scale;
      const ImVec2 pillMin = center - pillSize * 0.5f, pillMax = center + pillSize * 0.5f;
      const ImU32 background = isFocused ? Theme::kAccent : Theme::kSurface;
      drawList->AddRectFilled(pillMin, pillMax, WithAlpha(background, alpha * (isFocused ? 1.f : focused ? 0.9f : 0.7f)), 0.5f * pillSize.y);
      const string label = string(kOptionIcons[i]) + "  " + kOptionLabels[i];
      const ImU32 color = isFocused ? IM_COL32(8, 18, 34, 255) : focused ? Theme::kText : Theme::kTextMuted;
      CenteredText(drawList, font, textSize * scale, center, WithAlpha(color, alpha), label.c_str());
      m_optionAreas.push_back({ pillMin, pillMax, i });
   }

   // What the buttons do
   const char* const hint = m_confirmShutdown ? "Flippers: choose     Start: confirm     Back: cancel"
      : focused ? "Flippers: choose     Start: select     Launch ball: back to the tables"
      : Keyboard::IsEditing(kSearchOwner) ? "Flippers: browse the results     Up: type     Back: close the keyboard"
      : "Flippers: browse     Start: play     Launch ball: options";
   CenteredText(drawList, font, 1.9f * layout.unit, ImVec2(0.5f * size.x, size.y - 2.2f * layout.unit), WithAlpha(Theme::kTextMuted, alpha * 0.9f), hint);
}

void CabinetLauncherPage::RenderConfirm(ImDrawList* drawList, const ImVec2& size, float alpha)
{
   const Layout layout = GetLayout(size);
   ImFont* const font = Theme::Font();
   ImFont* const bold = Theme::BoldFont();
   drawList->AddRectFilled(ImVec2(0.f, 0.f), size, IM_COL32(0, 0, 0, static_cast<int>(alpha * 150.f)));
   const ImVec2 panelSize(min(size.x - 8.f * layout.unit, 70.f * layout.unit), 24.f * layout.unit);
   const ImVec2 center(0.5f * size.x, 0.5f * size.y);
   drawList->AddRectFilled(center - panelSize * 0.5f, center + panelSize * 0.5f, WithAlpha(Theme::kPanelBg, alpha), 2.f * layout.unit);
   CenteredText(drawList, bold, 3.6f * layout.unit, ImVec2(center.x, center.y - 6.5f * layout.unit), WithAlpha(Theme::kText, alpha), "Shut down the computer?");
   const char* const labels[] = { "Cancel", ICON_FK_POWER_OFF "  Shut down" };
   const ImVec2 buttonSize(24.f * layout.unit, 6.f * layout.unit);
   for (int i = 0; i < 2; i++)
   {
      const ImVec2 buttonCenter(center.x + (i == 0 ? -1.f : 1.f) * (0.5f * buttonSize.x + 1.5f * layout.unit), center.y + 4.f * layout.unit);
      const bool isFocused = i == m_confirmChoice;
      const ImU32 background = isFocused ? (i == 1 ? Theme::kError : Theme::kAccent) : Theme::kSurface;
      drawList->AddRectFilled(buttonCenter - buttonSize * 0.5f, buttonCenter + buttonSize * 0.5f, WithAlpha(background, alpha), 0.5f * buttonSize.y);
      CenteredText(drawList, font, 2.6f * layout.unit, buttonCenter, WithAlpha(isFocused ? IM_COL32(8, 18, 34, 255) : Theme::kText, alpha), labels[i]);
      m_confirmAreas.push_back({ buttonCenter - buttonSize * 0.5f, buttonCenter + buttonSize * 0.5f, i });
   }
}

void CabinetLauncherPage::RenderKeyboard(const ImVec2& size)
{
   // Above the name of the table, so that the reel shows the results while typing
   const Layout layout = GetLayout(size);
   const float width = min(size.x - 8.f * layout.unit, 90.f * layout.unit);
   ImGui::PushFont(Theme::Font(), Theme::Px(Theme::kTextSize));
   const float height = Keyboard::GetHeight(width);
   const float bottom = layout.infoBottomY - 14.f * layout.unit;
   const ImVec2 pos(0.5f * (size.x - width), max(2.f * layout.unit, bottom - height));
   ImGui::SetNextWindowPos(pos);
   ImGui::SetNextWindowSize(ImVec2(width, height));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   ImGui::Begin("##CabinetKeyboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
   Keyboard::KeepOpen(kSearchOwner, pos, pos);
   Keyboard::Render(pos, width);
   ImGui::End();
   ImGui::PopStyleVar(2);
   ImGui::PopFont();
}

void CabinetLauncherPage::HandleMouse()
{
   const ImGuiIO& io = ImGui::GetIO();
   if (io.MouseWheel != 0.f && !m_confirmShutdown)
      Step(io.MouseWheel > 0.f ? -1 : 1);
   if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      return;
   const ImVec2 mouse = io.MousePos;
   const auto hit = [&mouse](const HitArea& area) { return mouse.x >= area.min.x && mouse.y >= area.min.y && mouse.x <= area.max.x && mouse.y <= area.max.y; };
   if (m_confirmShutdown)
   {
      for (const HitArea& area : m_confirmAreas)
         if (hit(area))
         {
            m_confirmChoice = area.index;
            ActivateItem();
         }
      return;
   }
   for (const HitArea& area : m_optionAreas)
      if (hit(area))
      {
         m_band = Band::Options;
         m_option = area.index;
         Activate(static_cast<Option>(area.index));
         return;
      }
   // The tiles were drawn from the farthest to the middle one: the last one hit is on top
   for (auto it = m_tileAreas.rbegin(); it != m_tileAreas.rend(); ++it)
      if (hit(*it))
      {
         m_band = Band::Reel;
         const int n = GetCount();
         const int selected = ((m_targetPos % n) + n) % n;
         if (it->index == selected)
            Play();
         else
         {
            // The short way, as the reel shows it
            int delta = it->index - selected;
            if (IsWrapping())
            {
               if (delta > n / 2)
                  delta -= n;
               else if (delta < -n / 2)
                  delta += n;
            }
            Step(delta);
         }
         return;
      }
}

}
