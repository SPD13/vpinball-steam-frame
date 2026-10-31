// license:GPLv3+

#pragma once

// Steam Frame Edition: the scene buffers in RG11B10F (4 bytes per pixel) instead of RGBA16F (8), switched live in the Steam Frame screen of
// the VR menu (Settings Player/CompactSceneBuffer, off by default until checked in the headset; docs/Performance on the Steam Frame.md, item
// 12, measured −0.66 ms on Addams and −1.28 ms on Ghostbusters at 2160x2160).
//
// The render targets VPX asks as colorFormat::RGB16F are the ones whose alpha nothing reads: the scene buffers, bloom, screen space
// reflection and motion blur buffers, the static prerender and the reflection and refraction probes (checked 2026-10-31: every shader reads
// them as .rgb, no blend uses the destination alpha). RG11B10F keeps the range of half floats with 6 bits of mantissa for red and green and
// 5 for blue (against 10), and no sign: the lights and additive flashers, which output a negative color into a reverse subtract blend,
// still store a positive result (dst * (1 + |src|) + |src| * alpha), since the blend runs on the unclamped shader output.
//
// The format changes at the start of a frame (Renderer::ApplySceneBufferFormat): all these targets are made again at the same size, the
// previous ones released once the frames that use them are done, as when the playfield window is resized.

#include "renderer/typedefs3D.h"

class Settings;

namespace VPX::SceneBufferFormat
{

void Load(const Settings& settings); // Table start, before the render targets are made
bool IsCompactRequested(); // The setting, as the menu shows it
void SetCompactRequested(bool compact); // From the menu, applied at the start of the next frame
bool TakeChange(); // Start of a frame: true when the targets have to be made again in the requested format, which then becomes the current one

#if defined(ENABLE_BGFX)
// Format of a render target asked as colorFormat::RGB16F (RenderTarget's constructor), in the current choice when the GPU can render to it
bgfx::TextureFormat::Enum GetRGB16FFormat(int nMSAASamples);
#endif
string GetStatus(); // One line for the menu

}
