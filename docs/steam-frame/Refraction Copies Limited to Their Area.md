# Refraction copies limited to their area: how it works, and what an upstream pull request could take

*2026-11-04. Technical notes on fork commit d7c13abf8 "Renderer: refraction probes copy only the area of interest of their parts" (on
fork `master`). The study that led to it: `docs/Performance on the Steam Frame.md`, section 8.15. Paths are relative to the `vpinball`
clone; line numbers are those of upstream `master` 42fd8405a (2026-11-04), to which the commit applies after the change of two lines
described in section 7.1.*

## 1. Summary

- **What it does.** A screen space transparency probe (the refraction of a primitive, "Refraction probe" in its properties) copies the
  scene rendered so far, color and depth, so that its parts can sample what is behind them. The copy now covers only the area of
  interest of the parts using the probe, with a margin, instead of the whole render target.
- **Why.** The parts that refract are usually small on screen (ramps, plastic covers), while the copy was always the full target, both
  eyes in VR, once per probe and per frame. On a GPU with little bandwidth these copies are the most expensive thing in the frame after
  the scene pass itself, and no profiler view shows them, as they happen between render passes.
- **Size.** 37 lines in three files: a method of `RenderProbe` with its comment and its call where the probe applies its area of
  interest (33 lines in `RenderProbe.cpp`, 1 in `RenderProbe.h`), and two small members of `RenderCommand` to recognise a copy and set
  its region (3 lines).
- **Effect.** On the Steam Frame (Adreno, 2160 x 2160 per eye), Medieval Madness VPW (three probes): 4.0 ms of copies per frame become
  0.93 ms, the GPU time per frame drops by 2.8 ms (11.86-12.22 to 9.07-9.17 ms), and with every light map on from 14.31 to 10.31 ms, the
  missed frames from 12 per 5 s to none. The same image: identical captures on a Mac, and the ramps look as before in the headset.
- **Upstream.** Generic: any table with refraction, any platform with the bgfx renderer. The gain grows with the resolution and the
  number of probes, and is largest on mobile GPUs (the Quest build, Android, the Steam Frame).

## 2. The problem

How a screen space transparency probe worked before this change:

| Fact | Where |
|---|---|
| A primitive with a refraction probe renders the probe before drawing itself: `Primitive::Render` calls `RenderProbe::Render`, which for this type calls `RenderScreenSpaceTransparency` once per frame (the probe is dirty until then) | `src/parts/primitive.cpp`, `src/renderer/RenderProbe.cpp:249` |
| `RenderScreenSpaceTransparency` creates a render target the size of the scene buffer and copies the whole current pass into it, color and depth (`BlitRenderTarget(renderedPass->m_rt, m_dynamicRT, true, true)`) | `RenderProbe.cpp:261`, `:266` |
| The copy is a `bgfx::blit` of each layer (each eye in VR), color then depth, of the full texture | `RenderTarget::CopyTo`, `src/renderer/RenderTarget.cpp:626` |
| Each part using the probe extends the probe's area of interest with the screen bounds of its mesh, for every eye | `src/parts/primitive.cpp:1113`, `RenderProbe::ExtendAreaOfInterest` (`RenderProbe.cpp:160`) |
| At the end of the frame's recording, `ApplyAreaOfInterest` scissors the probe's blur passes to that area, with a margin of 0.05 for the refraction offset, but skips the copy pass ("we don't know where the sampling will occur") | `RenderProbe.cpp:178`, `:190`; called from `Renderer.cpp:2367` |

So the copy was always full size, while the parts that sample it, and the blur that reads it, only use their own area.

What it costs, on Medieval Madness in the Steam Frame:

- The GPU's busy time (DRM `fdinfo`) and the OpenXR runtime's GPU counter read about 4 ms more per frame than bgfx's GPU time (the
  sum of its views' timer queries), on every run, whatever was drawn, and less at a smaller size: work outside bgfx's views that
  scales with the pixels.
- The Vulkan driver's own trace of its render stages (Mesa Turnip, `MESA_GPU_TRACES=print`) showed what it is: 14 copies of 2160 x
  2160 images per frame, about 0.28 ms each. Three pairs of color and depth copies of both eyes are the three refraction probes of
  the table (`Layer3`, `Layer4` and `LayerCaps Refractions`), 3.4 ms per frame; the last two are the depth handed to the OpenXR depth
  swapchain (out of scope here).
- The parts using these probes cover 8.1, 5.3 and 0.8 % of the desktop view.

## 3. The change

### 3.1 `RenderProbe::LimitCopyToAreaOfInterest`

Called by `ApplyAreaOfInterest` for the copy pass instead of skipping it, so it runs once the area of interest of the frame is complete
(every part using the probe has added its bounds) and before the frame's commands are executed:

```cpp
// in RenderProbe::ApplyAreaOfInterest, for SCREEN_SPACE_TRANSPARENCY
if (pass == m_copyPass)
{
   LimitCopyToAreaOfInterest(pass);
   continue;
}
```

```cpp
void RenderProbe::LimitCopyToAreaOfInterest(RenderPass* copyPass) const
{
#if defined(ENABLE_BGFX)
   if (m_reflection_clip_bounds.x == FLT_MAX)
      return;
   constexpr float margin = 0.1f; // In clip space: 5 % of the width and of the height on each side
   const float x0 = max(m_reflection_clip_bounds.x - margin, -1.f), x1 = min(m_reflection_clip_bounds.z + margin, 1.f);
   const float y0 = max(m_reflection_clip_bounds.y - margin, -1.f), y1 = min(m_reflection_clip_bounds.w + margin, 1.f);
   if (x0 >= x1 || y0 >= y1)
      return; // Not on screen: the parts are not drawn either
   const int w = copyPass->m_rt->GetScaledWidth(copyPass->m_renderScale), h = copyPass->m_rt->GetScaledHeight(copyPass->m_renderScale);
   const int left = clamp(static_cast<int>(floorf((x0 + 1.f) * 0.5f * static_cast<float>(w))), 0, w);
   const int right = clamp(static_cast<int>(ceilf((x1 + 1.f) * 0.5f * static_cast<float>(w))), 0, w);
   // Rows from the first one of the textures: the top, except on the backends whose origin is at the bottom left (OpenGL)
   const bool bottomLeft = bgfx::getCaps()->originBottomLeft;
   const float r0 = bottomLeft ? (y0 + 1.f) * 0.5f : (1.f - y1) * 0.5f, r1 = bottomLeft ? (y1 + 1.f) * 0.5f : (1.f - y0) * 0.5f;
   const int first = clamp(static_cast<int>(floorf(r0 * static_cast<float>(h))), 0, h), last = clamp(static_cast<int>(ceilf(r1 * static_cast<float>(h))), 0, h);
   if (left >= right || first >= last)
      return;
   for (RenderCommand* cmd : copyPass->m_commands)
      if (cmd->IsCopyCommand())
         cmd->SetCopyRegion(left, first, right - left, last - first);
#endif
}
```

- **The area.** `m_reflection_clip_bounds` is the union, over the parts using the probe and over the eyes, of the screen bounds of their
  meshes, already gathered for the blur passes. A part whose box reaches behind the eye sets it to the whole screen, so the copy stays
  full in that case.
- **The margin.** The refraction shader samples the copy at the pixel's position shifted by the refraction, and the blur (rough
  refraction) reads around it, with a kernel of up to 39 pixels. The blur passes use a margin of 0.05; the copy uses 0.1, so that what
  the blur reads was copied too.
- **The rest of the copy.** The render target is reused from frame to frame, so outside the region it keeps the content of an earlier
  frame. Nothing samples it there: the parts only cover their own area.
- **The region.** The same in the source and the destination, and for every layer (both eyes share the union of their bounds), so
  `RenderTarget::CopyTo` keeps its `bgfx::blit` path (same size) with a texture region.

### 3.2 `RenderCommand`

```cpp
bool IsCopyCommand() const { return m_command == RC_COPY; }
// Limits a copy to a region, the same in the source and the destination (pixels from the first row of the textures)
void SetCopyRegion(const int x, const int y, const int w, const int h) { m_copySrcRect = m_copyDstRect = vec4((float)x, (float)y, (float)w, (float)h); }
```

## 4. Measurements

Medieval Madness VPW 1.0.1 on the Steam Frame, headless (rendering forced with the headset not worn, the eyes pinned at the lockbar),
2160 x 2160 per eye, fixed size, an autoplayed game, light-map pruning of the fork on Medium, 100 s runs. "Runtime" is the OpenXR
runtime's GPU time per frame (`XR_META_performance_metrics`), the one dynamic resolution follows; it matches the GPU's busy time.

| Game window | Whole copies | Copies of the area of interest |
| --- | --- | --- |
| Runtime's GPU time per frame | 11.86 / 12.07 / 12.22 ms (3 runs) | 9.16 / 9.07 / 9.17 ms (3 runs) |
| bgfx's GPU time (its views) | 7.84 / 7.82 / 8.08 ms | 8.06 / 7.98 / 7.94 ms |
| Every light map forced on | 14.31 ms, 12.3 missed frames per 5 s | 10.31 ms, none missed |
| Copies per frame, from Turnip's trace | 4.0 ms (14 copies) | 0.93 ms (of which 0.6 ms is the OpenXR depth) |

bgfx's own time does not move: the copies were never in it. The frame period is 13.9 ms (72 Hz) and the target of dynamic resolution
11.8 ms: the table went from just over it to 2.7 ms under it.

**Same image.** On a Mac (Metal), with every light map forced on so that the frames repeat exactly, a capture at 2880 x 1620 with the
whole copy and one with the limited copy differ by 3/255 at most, on one pixel (the attract mode's score display, which changes from
run to run, masked). In the headset, the owner looked at the ramps of Medieval Madness from several positions: as before.

## 5. Behaviour per renderer and platform

| Renderer | Behaviour |
|---|---|
| bgfx, Vulkan (Linux, Android, Windows) | Limited copy; checked on the Steam Frame (Turnip) |
| bgfx, Metal (macOS, iOS) | Limited copy; checked on a Mac |
| bgfx, Direct3D 11/12 | Limited copy, texture origin at the top as with Vulkan and Metal; not run |
| bgfx, OpenGL | Limited copy, rows taken from the bottom (`originBottomLeft`); not run |
| OpenGL and DX9 builds without bgfx | Unchanged (whole copy): the method is compiled for bgfx only |
| VR (instanced stereo, both eyes in layers) | One region for both layers, the union of both eyes' bounds |
| Dynamic resolution (fork only) | The region is taken in the rendered part of the target (`GetScaledWidth`, `m_renderScale`) |

## 6. Limits

- **The margin is a choice.** A refraction that shifts the image by more than 5 % of the screen, or a blur wider than the margin, would
  read the earlier frame at the edge of the area. The refraction offsets of the tables seen are far smaller; a table with a very thick,
  very refractive part on a small screen area would be the case to look at.
- **One rectangle for all the parts of a probe.** Parts far apart on screen sharing one probe give a large rectangle that also covers
  what lies between them. Still never more than the whole copy.
- **Both eyes share the rectangle.** Per-eye regions would copy a little less in VR; `CopyTo` would need a region per layer.
- **The depth is still copied with the color**, over the same region; whether a probe needs its depth is not tested.
- **The OpenXR depth copy** (0.6 ms on the Frame) is a different copy, not touched here.

## 7. What an upstream pull request could take

### 7.1 The commit, adapted to upstream (recommended first step)

Commit d7c13abf8 applies to upstream `master` 42fd8405a without conflict (checked with `git cherry-pick --no-commit` in a worktree of
upstream), but one line uses the fork's dynamic resolution, which upstream does not have (`RenderTarget::GetScaledWidth`,
`GetScaledHeight`, `RenderPass::m_renderScale`). Upstream takes the size of the whole target:

```cpp
// fork
const int w = copyPass->m_rt->GetScaledWidth(copyPass->m_renderScale), h = copyPass->m_rt->GetScaledHeight(copyPass->m_renderScale);
// upstream
const int w = copyPass->m_rt->GetWidth(), h = copyPass->m_rt->GetHeight();
```

and the comment above it drops the mention of dynamic resolution. The upstream branch is to be built and run on a desktop platform
before it is opened (not done yet). It needs no setting and changes nothing but performance. Arguments for upstream:

- every table with refraction pays one full copy per probe per frame, both eyes in VR, and bgfx's view timers do not include the
  copies, so its profiler does not show them;
- the Quest and Android builds have the same bandwidth-limited GPUs as the Steam Frame, where this was 3.4 ms of 12 on one table;
- the area of interest already exists and is already used by the blur passes: the copy only stops being the exception.

What the reviewers will want to see, and how to show it:

| Concern | Answer |
|---|---|
| Can a refraction show stale content? | Only beyond the margin (section 6). A capture with and without the change, on a table with refraction, shows the same image |
| Parts partly behind the eye | Their bounds become the whole screen (`primitive.cpp`), the copy stays full |
| Rough refraction (blur) | The blur passes are scissored to the area with 0.05; the copy's 0.1 covers what they read |
| OpenGL's origin | Taken from `bgfx::getCaps()->originBottomLeft`; to be run once on the OpenGL backend |
| Editor and desktop | Unchanged code path: the same area of interest, desktop views included |

### 7.2 Extensions upstream might ask for, or accept with it

| Extension | What it takes | Value |
|---|---|---|
| A region per eye | Per-layer rectangles in `RenderCommand` and `CopyTo` (each eye's bounds kept apart in `ExtendAreaOfInterest`) | A little less copied in VR |
| No depth copy when the probe does not use it | Know from the material whether the refraction reads the depth | Halves the copy where it applies |
| The non-bgfx renderers | The same region in the OpenGL and DX9 paths of `CopyTo` | Consistency; those builds are not the ones with weak GPUs |

### 7.3 Out of scope

The copy of the depth into the OpenXR depth swapchain (fork-specific measurement, a different code path), and sharing one copy between
probes (it would change the image: each probe sees the parts drawn before it).

### 7.4 Draft of the pull request

Following the fork's rule, drafted here for review before it is opened (`vpinball-pr/`, a branch from upstream `master` with the commit
adapted as in 7.1).

> **Title:** Refraction probes: copy only the area of interest of their parts
>
> A screen space transparency probe copies the scene rendered so far, color and depth, so that its parts can sample what is behind them.
> The copy was always the whole render target, both eyes in VR, once per probe and per frame, while the parts that refract are usually
> a small part of the screen. `ApplyAreaOfInterest` already scissors the probe's blur passes to the area of the parts using it, but
> skipped the copy.
>
> The copy is now limited to that area, with a margin of 0.1 in clip space for the refraction offset and the blur kernel (bgfx renderer;
> the texture origin is taken from the caps). The render target keeps an earlier frame outside the region, which nothing samples.
>
> Measured on a standalone OpenXR headset (Adreno, 2160x2160 per eye) on Medieval Madness VPW, three probes: the copies went from 4.0
> to 0.9 ms per frame in the driver's trace, and the GPU time per frame from about 12.0 to 9.1 ms; with every light on, from 14.3 to
> 10.3 ms. The same image in captures with and without the change.
>
> 🤖 Generated with [Claude Code](https://claude.com/claude-code)

## 8. Reproduction

- **The copies in a trace (Mesa Turnip).** Run VPX with `MESA_GPU_TRACES=print MESA_GPU_TRACEFILE=<file>` and look for `start_copy_image`
  / `end_copy_image` between the render passes of a frame's command buffer (the one with `renderpasses=16` on Medieval Madness). In the
  fork, the run `utrace` of `Doc/test-hooks/lightcost-series.sh` (workspace repository) does it with `DURATION=50`.
- **The gap without a trace.** Compare the GPU's busy time (DRM `fdinfo`, sampled by `Doc/test-hooks/perf-sampler.py`) or the runtime's
  GPU counter with the sum of bgfx's view times (`bgfx::getStats`): the copies are the difference.
- **The image.** Capture the same frame with the whole and the limited copy, with frames that repeat (no blinking lights), and compare.
