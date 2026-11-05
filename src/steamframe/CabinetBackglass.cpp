// license:GPLv3+

#include "core/stdafx.h"
#include "CabinetBackglass.h"

#include "core/VPXPluginAPIImpl.h"
#include "renderer/Texture.h"

#include <fstream>

namespace VPX::Cabinet
{

static std::filesystem::path WithSuffix(const std::filesystem::path& tablePath, const char* suffix)
{
   std::filesystem::path path = tablePath;
   return path.replace_extension(suffix);
}

std::filesystem::path GetPlayfieldImagePath(const std::filesystem::path& tablePath) { return WithSuffix(tablePath, ".playfield.jpg"); }

std::filesystem::path GetBackglassCapturePath(const std::filesystem::path& tablePath) { return WithSuffix(tablePath, ".backglass.jpg"); }

std::filesystem::path FindBackglassImage(const std::filesystem::path& tablePath)
{
   for (const char* suffix : { ".backglass.png", ".backglass.jpg" })
   {
      std::filesystem::path path = WithSuffix(tablePath, suffix);
      std::error_code ec;
      if (std::filesystem::is_regular_file(path, ec))
         return path;
   }
   return {};
}

bool SaveUpright(const std::filesystem::path& windowImage, const std::filesystem::path& output, int rotation)
{
   const std::shared_ptr<BaseTexture> image = BaseTexture::CreateFromFile(windowImage);
   if (image == nullptr || (image->m_format != BaseTexture::SRGB && image->m_format != BaseTexture::SRGBA))
      return false;
   rotation &= 3;
   if (rotation == 0)
      return image->Save(output);
   const unsigned int ww = image->width(), wh = image->height();
   const unsigned int uw = (rotation & 1) ? wh : ww, uh = (rotation & 1) ? ww : wh;
   const std::shared_ptr<BaseTexture> upright = BaseTexture::Create(uw, uh, image->m_format);
   if (upright == nullptr)
      return false;
   const unsigned int bpp = image->m_format == BaseTexture::SRGBA ? 4 : 3;
   const uint8_t* const src = static_cast<const uint8_t*>(image->data());
   uint8_t* const dst = static_cast<uint8_t*>(upright->data());
   // Inverse of the UI rotation, as LiveUI::AddMousePosEvent maps window positions to the UI
   for (unsigned int uy = 0; uy < uh; uy++)
      for (unsigned int ux = 0; ux < uw; ux++)
      {
         unsigned int wx, wy;
         switch (rotation)
         {
         case 1: wx = uh - 1 - uy; wy = ux; break;
         case 2: wx = uw - 1 - ux; wy = uh - 1 - uy; break;
         default: wx = uy; wy = uw - 1 - ux; break;
         }
         memcpy(dst + (static_cast<size_t>(uy) * uw + ux) * bpp, src + (static_cast<size_t>(wy) * ww + wx) * bpp, bpp);
      }
   return upright->Save(output);
}

int64_t GetFileStamp(const std::filesystem::path& path)
{
   std::error_code ec;
   const auto time = std::filesystem::last_write_time(path, ec);
   return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

std::filesystem::path FindDirectB2S(const std::filesystem::path& tablePath)
{
   const std::filesystem::path folder = tablePath.parent_path();
   const string names[] = { lowerCase(PathToUTF8(tablePath.stem())) + ".directb2s", lowerCase(PathToUTF8(folder.filename())) + ".directb2s" };
   std::filesystem::path found[2];
   std::error_code ec;
   for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
   {
      if (!entry.is_regular_file(ec))
         continue;
      const string name = lowerCase(PathToUTF8(entry.path().filename()));
      for (int i = 0; i < 2; i++)
         if (name == names[i])
            found[i] = entry.path();
   }
   return found[0].empty() ? found[1] : found[0];
}

static vector<uint8_t> DecodeBase64(const char* src, size_t length)
{
   static const auto decodeTable = []()
   {
      std::array<int8_t, 256> table;
      table.fill(-1);
      const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      for (int i = 0; i < 64; i++)
         table[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
      return table;
   }();
   vector<uint8_t> out;
   out.reserve(length / 4 * 3);
   uint32_t bits = 0;
   int nBits = 0;
   for (size_t i = 0; i < length; i++)
   {
      // Line breaks written as XML character references (&#xD;&#xA;), as some tools wrap the data: their letters are not data
      if (src[i] == '&')
      {
         while (i < length && src[i] != ';')
            i++;
         continue;
      }
      const int8_t value = decodeTable[static_cast<uint8_t>(src[i])];
      if (value < 0) // Padding, line breaks and spaces
         continue;
      bits = (bits << 6) | static_cast<uint32_t>(value);
      nBits += 6;
      if (nBits >= 8)
      {
         nBits -= 8;
         out.push_back(static_cast<uint8_t>((bits >> nBits) & 0xFF));
      }
   }
   return out;
}

std::filesystem::path ExtractBackglass(const std::filesystem::path& tablePath, const std::filesystem::path& directB2S)
{
   std::ifstream file(directB2S, std::ios::binary);
   if (!file)
      return {};
   const string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

   // The images are base64 attributes: <Images><BackglassImage Value="..." FileName="..." /> (the backglass without its lights). Searched as text
   // rather than parsed: the files are large, and only this attribute is needed.
   for (const char* tag : { "<BackglassImage", "<BackglassOffImage", "<BackglassOnImage" })
   {
      const size_t tagPos = content.find(tag);
      if (tagPos == string::npos)
         continue;
      const size_t tagEnd = content.find('>', tagPos);
      const size_t valuePos = content.find(" Value=\"", tagPos);
      if (valuePos == string::npos || valuePos > tagEnd)
         continue;
      const size_t dataStart = valuePos + 8;
      const size_t dataEnd = content.find('"', dataStart);
      if (dataEnd == string::npos || dataEnd == dataStart)
         continue;
      const vector<uint8_t> image = DecodeBase64(content.data() + dataStart, dataEnd - dataStart);
      // Stored as PNG or JPEG
      const bool isPng = image.size() > 8 && image[0] == 0x89 && image[1] == 'P' && image[2] == 'N' && image[3] == 'G';
      const bool isJpeg = image.size() > 3 && image[0] == 0xFF && image[1] == 0xD8;
      if (!isPng && !isJpeg)
      {
         PLOGW << "Unknown image format in " << directB2S << " (" << tag << ')';
         continue;
      }
      const std::filesystem::path outPath = WithSuffix(tablePath, isPng ? ".backglass.png" : ".backglass.jpg");
      // Written to a temporary file then renamed, so that a partial file is never shown
      std::filesystem::path tmpPath = outPath;
      tmpPath += ".tmp";
      {
         std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
         out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
         if (!out)
            return {};
      }
      std::error_code ec;
      std::filesystem::rename(tmpPath, outPath, ec);
      if (ec)
      {
         std::filesystem::remove(tmpPath, ec);
         return {};
      }
      PLOGI << "Backglass image extracted from " << directB2S.filename() << " to " << outPath.filename();
      return outPath;
   }
   PLOGI << "No backglass image in " << directB2S;
   return {};
}


///////////////////////////////////////////////////////////////////////////////
// Backglass window renderer

BackglassRenderer& BackglassRenderer::Get()
{
   static BackglassRenderer instance;
   return instance;
}

void BackglassRenderer::Attach(Player* player)
{
   if (m_player)
      Detach(m_player);
   m_player = player;
   m_api = &player->m_pluginAPI.getAPI();
   m_def = { "CabinetLauncher", "Cabinet launcher", "Backglass of the table selected in the cabinet launcher", this, Render };
   const MsgPluginAPI& msgApi = player->m_pluginManager.GetMsgAPI();
   m_getRendererMsgId = msgApi.GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_AUX_RENDERER);
   msgApi.SubscribeMsg(player->m_pluginAPI.GetVPXEndPointId(), m_getRendererMsgId, OnGetRenderer, this);
   // The player lists the renderers when it starts, before this: ask it to list them again
   const unsigned int changedMsgId = msgApi.GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_AUX_RENDERER_CHG);
   msgApi.BroadcastMsg(player->m_pluginAPI.GetVPXEndPointId(), changedMsgId, nullptr);
   msgApi.ReleaseMsgID(changedMsgId);
}

void BackglassRenderer::Detach(const Player* player)
{
   if (m_player == nullptr || m_player != player)
      return;
   const MsgPluginAPI& msgApi = m_player->m_pluginManager.GetMsgAPI();
   msgApi.UnsubscribeMsg(m_getRendererMsgId, OnGetRenderer, this);
   msgApi.ReleaseMsgID(m_getRendererMsgId);
   m_getRendererMsgId = 0;
   if (m_loading.valid())
      m_loading.wait();
   ReleaseTextures();
   m_wantedPath.clear();
   m_loadingPath.clear();
   m_shownPath.clear();
   m_player = nullptr;
}

void BackglassRenderer::ReleaseTextures()
{
   if (m_texture)
      m_api->DeleteTexture(m_texture);
   if (m_previousTexture)
      m_api->DeleteTexture(m_previousTexture);
   m_texture = nullptr;
   m_previousTexture = nullptr;
}

void BackglassRenderer::SetImage(const std::filesystem::path& path, int64_t stamp)
{
   m_wantedPath = path;
   m_wantedStamp = stamp;
   TakeLoadedImage();
   // One image decoded at a time: the latest one wanted is decoded once the current decode is done (the reel may move fast)
   if (!m_loading.valid() && (m_wantedPath != m_shownPath || m_wantedStamp != m_shownStamp))
   {
      if (m_wantedPath.empty())
      {
         // Nothing to show: fade out
         if (m_texture)
         {
            if (m_previousTexture)
               m_api->DeleteTexture(m_previousTexture);
            m_previousTexture = m_texture;
            m_texture = nullptr;
            m_fade = 0.f;
         }
         m_shownPath.clear();
         m_shownStamp = 0;
         return;
      }
      m_loadingPath = m_wantedPath;
      m_loadingStamp = m_wantedStamp;
      // At most the size of a 2K screen: some backglass images are larger (2500 x 2400)
      m_loading = std::async(std::launch::async, [path = m_wantedPath]() { return BaseTexture::CreateFromFile(path, 2048); });
   }
}

void BackglassRenderer::TakeLoadedImage()
{
   if (!m_loading.valid() || m_loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
   const std::shared_ptr<BaseTexture> image = m_loading.get();
   VPXTexture texture = nullptr;
   if (image && (image->m_format == BaseTexture::SRGB || image->m_format == BaseTexture::SRGBA))
      m_api->UpdateTexture(&texture, static_cast<int>(image->width()), static_cast<int>(image->height()),
         image->m_format == BaseTexture::SRGBA ? VPXTextureFormat::VPXTEXFMT_sRGBA8 : VPXTextureFormat::VPXTEXFMT_sRGB8, image->data());
   else if (image)
      PLOGW << "Backglass image not shown, unsupported format: " << m_loadingPath;
   m_shownPath = m_loadingPath;
   m_shownStamp = m_loadingStamp;
   if (m_previousTexture)
      m_api->DeleteTexture(m_previousTexture);
   m_previousTexture = m_texture;
   m_texture = texture;
   m_fade = 0.f;
}

void MSGPIAPI BackglassRenderer::OnGetRenderer(const unsigned int, void* userData, void* msgData)
{
   BackglassRenderer* const me = static_cast<BackglassRenderer*>(userData);
   GetAncillaryRendererMsg* const msg = static_cast<GetAncillaryRendererMsg*>(msgData);
   if (msg->window != VPXWindowId::VPXWINDOW_Backglass)
      return;
   if (msg->count < msg->maxEntryCount)
      msg->entries[msg->count] = me->m_def;
   msg->count++;
}

int MSGPIAPI BackglassRenderer::Render(VPXRenderContext2D* ctx, void* context)
{
   BackglassRenderer* const me = static_cast<BackglassRenderer*>(context);
   if (me->m_player == nullptr || (me->m_texture == nullptr && me->m_previousTexture == nullptr))
      return false;
   const uint32_t now = msec();
   const float elapsed = me->m_lastRenderMs == 0 ? 0.f : min(static_cast<float>(now - me->m_lastRenderMs) / 1000.f, 0.1f);
   me->m_lastRenderMs = now;
   me->m_fade = min(1.f, me->m_fade + elapsed / 0.25f);
   if (me->m_fade >= 1.f && me->m_previousTexture)
   {
      me->m_api->DeleteTexture(me->m_previousTexture);
      me->m_previousTexture = nullptr;
   }
   const VPXPluginAPI* const api = me->m_api;
   // Stretched to the window, as the B2S plugin draws the backglass while the table is played
   const auto draw = [ctx, api](VPXTexture texture, float alpha)
   {
      const VPXTextureInfo* const info = api->GetTextureInfo(texture);
      if (info == nullptr)
         return;
      ctx->DrawImage(ctx, texture, 1.f, 1.f, 1.f, alpha, 0.f, 0.f, static_cast<float>(info->width), static_cast<float>(info->height), 0.f, 0.f, 0.f, 0.f, 0.f, ctx->srcWidth,
         ctx->srcHeight);
   };
   if (me->m_previousTexture)
      draw(me->m_previousTexture, me->m_texture ? 1.f : 1.f - me->m_fade); // Under the new image, or fading out when there is none
   if (me->m_texture)
      draw(me->m_texture, me->m_previousTexture ? me->m_fade : 1.f);
   return true;
}

}
