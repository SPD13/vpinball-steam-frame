// license:GPLv3+

#include "core/stdafx.h"
#include "LightMapPruning.h"

#include "MeshSimplifier.h"
#include "parts/primitive.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <thread>

namespace VPX
{

LightMapPruning& LightMapPruning::Get()
{
   static LightMapPruning instance;
   return instance;
}

void LightMapPruning::SetLevel(int level)
{
   m_level = std::clamp(level, 0, kLevels);
}

void LightMapPruning::Trim(const Primitive* part, uint32_t first, uint32_t& count) const
{
   if (m_level == 0)
      return;
   const auto it = m_counts.find(part);
   if (it == m_counts.end())
      return;
   const int k = m_level - 1;
   if (first == 0)
   {
      count = std::min(count, it->second.mesh[k]);
      return;
   }
   if (part->m_meshLevels == nullptr)
      return;
   const auto& levels = part->m_meshLevels->levels;
   for (size_t i = 0; i < levels.size() && i < it->second.levels.size(); i++)
      if (levels[i].first == first)
      {
         count = std::min(count, it->second.levels[i][k]);
         return;
      }
}

const char* LightMapPruning::GetLevelName(int level)
{
   static const char* const names[kLevels + 1] = { "Off", "Low", "Medium", "High" };
   return names[std::clamp(level, 0, kLevels)];
}

float LightMapPruning::GetLeftOutShare() const
{
   if (m_level == 0 || m_triangles[0] == 0)
      return 0.f;
   return 1.f - static_cast<float>(static_cast<double>(m_triangles[m_level]) / static_cast<double>(m_triangles[0]));
}

std::string LightMapPruning::GetStatus() const
{
   if (m_offAtLoad)
      return m_level == 0 ? "Off when the table loaded: nothing sorted"s : "Off when the table loaded: applies to the next table started"s;
   if (m_lightMaps == 0)
      return "No light map on this table"s;
   return m_level == 0 ? std::format("{} light maps, sorted in {:.1f} s while the table loaded: drawn in full (Off)", m_lightMaps, m_prepareMs / 1000.)
                       : std::format("{} light maps, sorted in {:.1f} s while the table loaded: {} leaves out {:.0f} % of their triangles", m_lightMaps, m_prepareMs / 1000.,
                            GetLevelName(m_level), GetLeftOutShare() * 100.f);
}

namespace
{

// Brightest channel of each texel of an image, in linear units, as a max pyramid (level 0 the image, each next level half its size)
struct MaxPyramid
{
   std::vector<std::vector<float>> levels;
   std::vector<unsigned int> widths, heights;

   bool Build(const Texture* image)
   {
      std::shared_ptr<BaseTexture> tex = BaseTexture::CreateFromData(image->GetFileRaw(), image->GetFileSize());
      if (!tex)
         return false;
      const unsigned int w = tex->width(), h = tex->height();
      const size_t n = static_cast<size_t>(w) * h;
      std::vector<float> base(n);
      const uint8_t* const d = static_cast<const uint8_t*>(tex->datac());
      static const std::array<float, 256> srgb = []()
      {
         std::array<float, 256> lut;
         for (int i = 0; i < 256; i++)
         {
            const float c = static_cast<float>(i) / 255.f;
            lut[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
         }
         return lut;
      }();
      switch (tex->m_format)
      {
      case BaseTexture::BW: for (size_t i = 0; i < n; i++) base[i] = d[i] / 255.f; break;
      case BaseTexture::BW_FP32: for (size_t i = 0; i < n; i++) base[i] = reinterpret_cast<const float*>(d)[i]; break;
      case BaseTexture::RGB:
      case BaseTexture::RGBA:
      {
         const int c = tex->m_format == BaseTexture::RGB ? 3 : 4;
         for (size_t i = 0; i < n; i++)
            base[i] = std::max(std::max(d[i * c], d[i * c + 1]), d[i * c + 2]) / 255.f;
         break;
      }
      case BaseTexture::SRGB:
      case BaseTexture::SRGBA:
      {
         const int c = tex->m_format == BaseTexture::SRGB ? 3 : 4;
         for (size_t i = 0; i < n; i++)
            base[i] = srgb[std::max(std::max(d[i * c], d[i * c + 1]), d[i * c + 2])];
         break;
      }
      case BaseTexture::RGB_FP16:
      case BaseTexture::RGBA_FP16:
      {
         const int c = tex->m_format == BaseTexture::RGB_FP16 ? 3 : 4;
         const uint16_t* const s = reinterpret_cast<const uint16_t*>(d);
         for (size_t i = 0; i < n; i++)
            base[i] = std::max(std::max(half2float(s[i * c]), half2float(s[i * c + 1])), half2float(s[i * c + 2]));
         break;
      }
      case BaseTexture::RGB_FP32:
      case BaseTexture::RGBA_FP32:
      {
         const int c = tex->m_format == BaseTexture::RGB_FP32 ? 3 : 4;
         const float* const s = reinterpret_cast<const float*>(d);
         for (size_t i = 0; i < n; i++)
            base[i] = std::max(std::max(s[i * c], s[i * c + 1]), s[i * c + 2]);
         break;
      }
      default: return false;
      }
      tex.reset(); // the decoded image is the largest buffer: freed before the pyramid
      levels.push_back(std::move(base));
      widths.push_back(w);
      heights.push_back(h);
      while (widths.back() > 1 || heights.back() > 1)
      {
         const unsigned int pw = widths.back(), ph = heights.back(), nw = (pw + 1) / 2, nh = (ph + 1) / 2;
         std::vector<float> next(static_cast<size_t>(nw) * nh);
         const std::vector<float>& prev = levels.back();
         for (unsigned int y = 0; y < nh; y++)
            for (unsigned int x = 0; x < nw; x++)
            {
               float m = 0.f;
               for (unsigned int dy = 0; dy < 2; dy++)
                  for (unsigned int dx = 0; dx < 2; dx++)
                     if (2 * x + dx < pw && 2 * y + dy < ph)
                        m = std::max(m, prev[static_cast<size_t>(2 * y + dy) * pw + 2 * x + dx]);
               next[static_cast<size_t>(y) * nw + x] = m;
            }
         levels.push_back(std::move(next));
         widths.push_back(nw);
         heights.push_back(nh);
      }
      return true;
   }

   // Brightest texel within one texel of a triangle, in texture space, at the level where its box spans 64 texels at most. A triangle reaching
   // outside the image counts as bright (kept).
   float Brightest(const Vertex3D_NoTex2& a, const Vertex3D_NoTex2& b, const Vertex3D_NoTex2& e) const
   {
      const float u0 = std::min({ a.tu, b.tu, e.tu }), v0 = std::min({ a.tv, b.tv, e.tv }), u1 = std::max({ a.tu, b.tu, e.tu }), v1 = std::max({ a.tv, b.tv, e.tv });
      if (u0 < -0.001f || v0 < -0.001f || u1 > 1.001f || v1 > 1.001f)
         return FLT_MAX;
      size_t l = 0;
      while (l + 1 < levels.size() && ((u1 - u0) * static_cast<float>(widths[l]) > 64.f || (v1 - v0) * static_cast<float>(heights[l]) > 64.f))
         l++;
      const float fw = static_cast<float>(widths[l]), fh = static_cast<float>(heights[l]);
      const int x0 = std::max(static_cast<int>(floorf(u0 * fw)) - 1, 0), x1 = std::min(static_cast<int>(floorf(u1 * fw)) + 1, static_cast<int>(widths[l]) - 1);
      const int y0 = std::max(static_cast<int>(floorf(v0 * fh)) - 1, 0), y1 = std::min(static_cast<int>(floorf(v1 * fh)) + 1, static_cast<int>(heights[l]) - 1);
      // Edges in texels of the level, oriented so that the inside is positive, normalized: signed distances
      const float px[3] = { a.tu * fw, b.tu * fw, e.tu * fw }, py[3] = { a.tv * fh, b.tv * fh, e.tv * fh };
      const float area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
      float ex[3], ey[3], ec[3];
      bool edge[3];
      for (int k = 0; k < 3; k++)
      {
         const int k1 = (k + 1) % 3;
         const float dx = px[k1] - px[k], dy = py[k1] - py[k], len = sqrtf(dx * dx + dy * dy);
         edge[k] = len > 1e-6f && fabsf(area) > 1e-9f;
         const float sgn = area > 0.f ? 1.f : -1.f;
         ex[k] = edge[k] ? -dy * sgn / len : 0.f;
         ey[k] = edge[k] ? dx * sgn / len : 0.f;
         ec[k] = -(ex[k] * px[k] + ey[k] * py[k]);
      }
      float m = 0.f;
      const std::vector<float>& lv = levels[l];
      for (int y = y0; y <= y1; y++)
         for (int x = x0; x <= x1; x++)
         {
            const float cx = static_cast<float>(x) + 0.5f, cy = static_cast<float>(y) + 0.5f;
            bool in = true;
            for (int k = 0; k < 3 && in; k++)
               in = !edge[k] || ex[k] * cx + ey[k] * cy + ec[k] >= -1.f;
            if (in)
               m = std::max(m, lv[static_cast<size_t>(y) * widths[l] + x]);
         }
      return m;
   }
};

}

void LightMapPruning::Prepare(PinTable* table)
{
   const auto t0 = std::chrono::steady_clock::now();
   m_counts.clear();
   m_lightMaps = 0;
   m_triangles = {};
   m_prepareMs = 0.;
   m_offAtLoad = false;
   m_level = std::clamp(table->GetSettings().GetPlayer_LightMapPruning(), 0, kLevels);
   if (m_level == 0)
   {
      // Nothing sorted, no time added to the loading: switching it on applies to the next table
      m_offAtLoad = true;
      return;
   }

   // The light maps, by image: additive parts linked to a light, with a 3D mesh of their own and no animation. A part whose levels are shared with
   // a coinciding part is left as it is (sorting them would reorder the other part's draws).
   std::map<string, std::vector<Primitive*>> byImage;
   for (IEditable* item : table->GetParts())
      if (item->GetItemType() == eItemPrimitive)
      {
         Primitive* const p = static_cast<Primitive*>(item);
         if (p->m_d.m_addBlend && !p->m_d.m_szLightmap.empty() && p->m_d.m_use3DMesh && p->m_mesh.NumIndices() >= 3 && p->m_mesh.m_animationFrames.empty()
            && !(p->m_meshLevels && p->m_meshLevels.use_count() > 1) && table->GetImage(p->m_d.m_szImage) != nullptr)
            byImage[p->m_d.m_szImage].push_back(p);
      }
   if (byImage.empty())
      return;
   // Entries made before the threads, which then only fill their own
   for (const auto& [name, parts] : byImage)
      for (Primitive* p : parts)
         m_counts[p];

   std::vector<const std::pair<const string, std::vector<Primitive*>>*> work;
   for (const auto& entry : byImage)
      work.push_back(&entry);
   std::atomic<size_t> next { 0 };
   std::mutex statsMutex;
   size_t lightMaps = 0;
   std::array<size_t, kLevels + 1> triangles {};
   const auto worker = [&]()
   {
      for (size_t w = next++; w < work.size(); w = next++)
      {
         const auto& [name, parts] = *work[w];
         MaxPyramid pyramid;
         if (!pyramid.Build(table->GetImage(name)))
         {
            PLOGW << "Light map pruning: image " << name << " not decoded, its light maps are drawn in full";
            continue;
         }
         size_t maps = 0;
         std::array<size_t, kLevels + 1> tris {};
         for (Primitive* p : parts)
         {
            const uint32_t c = p->m_d.m_color;
            const float scale = static_cast<float>(std::max({ c & 0xFFu, (c >> 8) & 0xFFu, (c >> 16) & 0xFFu })) / 255.f * p->m_d.m_alpha / 100.f;
            const Mesh& m = p->m_mesh;
            std::array<uint32_t, kLevels> counts {};
            // Sorts the triangles from the one adding the most light down, and counts those kept at each level (at least one)
            const auto sortByLight = [&](std::vector<unsigned int>& indices)
            {
               std::vector<std::pair<float, size_t>> order;
               order.reserve(indices.size() / 3);
               for (size_t i = 0; i + 2 < indices.size(); i += 3)
                  order.emplace_back(pyramid.Brightest(m.m_vertices[indices[i]], m.m_vertices[indices[i + 1]], m.m_vertices[indices[i + 2]]) * scale, i);
               std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
               std::vector<unsigned int> sorted;
               sorted.reserve(indices.size());
               for (const auto& [light, i] : order)
                  sorted.insert(sorted.end(), { indices[i], indices[i + 1], indices[i + 2] });
               indices = std::move(sorted);
               for (int k = 0; k < kLevels; k++)
               {
                  size_t n = 0;
                  while (n < order.size() && order[n].first >= kThresholds[k])
                     n++;
                  counts[k] = static_cast<uint32_t>(std::max(n, static_cast<size_t>(1)) * 3);
               }
            };
            Counts& entry = m_counts[p];
            sortByLight(p->m_mesh.m_indices);
            entry.mesh = counts;
            maps++;
            if (p->m_meshLevels == nullptr)
            {
               tris[0] += m.NumIndices() / 3;
               for (int k = 0; k < kLevels; k++)
                  tris[k + 1] += counts[k] / 3;
               continue;
            }
            p->m_meshLevels->sourceIndices = m.NumIndices(); // the same count: the levels still belong to the mesh
            for (auto& level : p->m_meshLevels->levels)
            {
               sortByLight(level.indices);
               entry.levels.push_back(counts);
               tris[0] += level.indices.size() / 3;
               for (int k = 0; k < kLevels; k++)
                  tris[k + 1] += counts[k] / 3;
            }
         }
         std::lock_guard lock(statsMutex);
         lightMaps += maps;
         for (int k = 0; k <= kLevels; k++)
            triangles[k] += tris[k];
      }
   };
   // A few images at a time: each decoded image and its pyramid take hundreds of MB on the tables with large light-map atlases
   const size_t nThreads = std::min<size_t>(3, work.size());
   std::vector<std::thread> threads;
   for (size_t i = 1; i < nThreads; i++)
      threads.emplace_back(worker);
   worker();
   for (auto& t : threads)
      t.join();

   m_lightMaps = lightMaps;
   m_triangles = triangles;
   m_prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   const auto pct = [&](int k) { return triangles[0] ? 100. * (1. - static_cast<double>(triangles[k]) / static_cast<double>(triangles[0])) : 0.; };
   PLOGI << "Light map pruning: " << lightMaps << " light maps on " << work.size() << " images sorted in " << m_prepareMs << " ms; triangles left out at Low "
         << pct(1) << " %, Medium " << pct(2) << " %, High " << pct(3) << " % (of " << triangles[0] << "); level " << m_level;
}

}
