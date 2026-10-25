# Culling parts outside the view: how it works, and what an upstream pull request could take

*2026-10-25. Technical notes on fork commit 24a3b391b "Skip primitives entirely outside the view" (on fork `master`), with the
bounds fix ca181cc89 (a fixup of it, folded in at the next sync). Measurements and the study that led to it:
`Table Geometry on the Steam Frame.md`, sections 5 and 11. Paths are relative to the `vpinball` clone; line numbers are those of
upstream `master` 68cc62864 (2026-10-25), to which the commit applies unchanged.*

## 1. Summary

- **What it does.** Before drawing a primitive, VPX tests the bounding box of its mesh, placed by the part's matrix, against the view
  of every eye. When the box is outside all of them, the part is not drawn at all: no draw call, no vertex work, in the scene pass and
  in the reflection probe (whose mirrored view is tested the same way).
- **Why.** VPX had no culling: every visible part was drawn every frame, wherever it was. In VR, where both eyes are drawn by one
  instanced draw and there is no static prepass, a room around the player costs its full vertex work twice per frame even when most
  of it is behind the player.
- **Size.** 42 lines in two files: a test in `src/math/ModelViewProj.h` (30 lines) and a call in `Primitive::Render` (12 lines), plus
  one line that resets the cached bounds when a built-in primitive is rebuilt.
- **Effect.** On the Steam Frame (2160² per eye): Lord of the Rings, whose VR room surrounds the player, draws 242 000 fewer triangles
  per frame and saves 0.6 ms of GPU; tables without such a room are unchanged, the test itself costing nothing measurable. No visible
  change, on desktop captures and in the headset.
- **Upstream.** It is generic: it works with every renderer (bgfx, OpenGL, DX9), every view mode and every stereo mode, and helps the
  Quest build and PC VR as much as the Frame. It applies cleanly to upstream `master`. Section 7 lists what a pull request could take,
  from the commit as it is to a few extensions.

## 2. The problem

How VPX draws a primitive in VR, before this change:

| Fact | Where |
|---|---|
| Both eyes are drawn by one instanced draw into a 2-layer target; the vertex shader runs once per eye | `src/shaders/bgfx/vs_basic.sc:45-47` (`gl_Layer = gl_InstanceID`), `src/renderer/RenderCommand.cpp:230,267` |
| No static prepass in VR: static parts are drawn every frame | `Renderer::IsUsingStaticPrepass`, `src/renderer/Renderer.cpp:2053` |
| The playfield reflection probe redraws every part that has reflections enabled | `RenderProbe::RenderReflectionProbe`, `src/renderer/RenderProbe.cpp:491,495` |
| No culling: `Renderer::RenderItem` and `Primitive::Render` only filter on visibility, static/dynamic, reflection and light sequencer state | `src/renderer/Renderer.cpp:2022`, `src/parts/primitive.cpp:884` and following |

The bounding box of a primitive's mesh was already computed (`Mesh::UpdateBounds`), but only to narrow the area a reflection or
refraction probe renders (`ExtendAreaOfInterest`, `src/parts/primitive.cpp:1078,1109`), which clips pixels, not vertices.

VR room authors build rooms from many detailed primitives (Lord of the Rings: a Balrog of 120 000 triangles, Gandalf, towers), and
the player only ever sees part of them. Tables that hide their full room by script (Addams Family's `VR_Room_Minimal`) already avoid
the cost; tables that show it pay for all of it every frame.

## 3. The change

### 3.1 `ModelViewProj::IsBoxVisible`

```cpp
// Tells if an object space box, placed by the current model matrix, may be seen by one of the eyes. It is reported hidden only
// when, for every eye, its 8 corners are all beyond the same side plane of the view or all behind the eye (conservative: a box
// crossing a corner of the view outside of it is kept, and so is a box beyond the far plane).
bool IsBoxVisible(const Vertex3Ds& boxMin, const Vertex3Ds& boxMax) const
```

For each eye, the 8 corners of the box are transformed by that eye's model-view-projection matrix (`m_matModelViewProj`, the matrix
the shaders' projection is built from, flip of a mirrored table included) into clip space, and counted against five planes:

| Plane | Corner outside when |
|---|---|
| left | `x < -w` |
| right | `x > w` |
| bottom | `y < -w` |
| top | `y > w` |
| behind the eye | `w <= 0` |

The box is hidden from an eye when all 8 corners are outside the same plane; it is drawn if one eye may see it. The test never culls
something that could be visible:

- **Corners, not triangles.** A box whose corners are spread over several outside planes (across a corner of the view) is kept,
  even when it is in fact outside: the test can only err on the side of drawing.
- **No near or far plane.** The near plane is replaced by "behind the eye" (`w <= 0`), which is on the safe side of it; a part
  beyond the far plane is kept (it is clipped by the GPU as before). Neither depends on the depth convention of the backend
  (0..1 or -1..1), so the test is the same for bgfx, OpenGL and DX9.
- **The current view.** The test reads the matrices set for the draw, so it follows whatever is being rendered: each eye in VR or in
  stereo 3D, the desktop camera in every view mode, the mirrored view of the playfield reflection probe, a refraction probe, the
  static prepass of the desktop, mirrored tables (the flip is applied to `m_matModelViewProj` before the test reads it).

`Update()` recomputes the matrices only when one changed; `Primitive::Render` has just set the model matrix and uploaded the derived
uniforms, which already ran it, so the test costs the corner transforms: 8 corners × 3 dot products × the number of eyes, per part and
per pass.

### 3.2 The call in `Primitive::Render`

```cpp
   // set transform
   m_renderer->UpdateBasicShaderMatrix(m_fullMatrix);

   // Skip parts entirely outside the view (like VR room parts behind the player). The bounds are those of the static mesh: animated
   // meshes, whose frames may move outside of them, and grouped parts, drawn from a merged buffer, are always drawn.
   if (!isUIPass && !m_groupdRendering && m_mesh.m_animationFrames.empty())
   {
      m_mesh.UpdateBounds();
      if (!m_renderer->GetMVP().IsBoxVisible(m_mesh.m_minAABound, m_mesh.m_maxAABound))
      {
         m_renderer->UpdateBasicShaderMatrix();
         return;
      }
   }
```

It sits after every existing reason not to draw (visibility, static/dynamic pass, reflection flag, light sequencer, an additive part
with nothing to add) and after the reflection probe of the part has been requested, and before any render state, texture or draw.
Returning there skips everything the part would have done in this pass, including extending the area of interest of its probes, which
is right: a part that is not seen does not need its probe pixels.

Always drawn, by design:

| Case | Why |
|---|---|
| UI passes (editor fill and edges) | Left as they were: they draw the editor's selection overlay, where hiding a part would be confusing, and their cost does not matter |
| Animated meshes (`m_animationFrames` not empty) | The bounds are those of the base mesh; a frame can move vertices outside of them |
| Grouped parts (`m_groupdRendering`) | They are drawn from one merged buffer, already transformed to world space with an identity matrix: the part's own mesh bounds do not describe it |

### 3.3 Bounds that stay true

`Mesh::UpdateBounds` caches the box until `m_validBounds` is reset. The cases where the vertices of a primitive change:

| Change | Bounds reset |
|---|---|
| Table load, OBJ import, GLB (VPZ) load, `Mesh::Clear` | Yes (`src/math/Mesh.cpp`, `Primitive::LoadData`) |
| Implicit playfield mesh (`Player`) | Yes (`src/core/player.cpp:528`) |
| Built-in primitive rebuilt when its side count changes (`put_Sides` during play, or the editor) | **No, before ca181cc89**: `CalculateBuiltinOriginal` rewrote the vertices without resetting the cache |
| Animated mesh frames (`Mesh::UploadToVB`) | Not needed: animated meshes are never culled |
| Position, size, rotation, translation (`put_X`, `put_Size`, `RotAndTra`) | Not needed: the part's matrix changes, not the mesh |

The built-in primitive (the n-sided prism used when no mesh is imported) has its outer corners at a radius of `0.5 / cos(π / sides)`:
1.0 with 3 sides, 0.5 with many. A script lowering the side count during play made the shape grow past its cached box, so the part
could be culled while its new corners were still in view, at the edge of the screen. ca181cc89 resets the cache in
`CalculateBuiltinOriginal`. The same stale box also narrowed the probe area of interest of such a part before this change, so the fix
stands on its own.

## 4. Measurements

On the Steam Frame, headless (method of `Performance on the Steam Frame.md`, section 2), fixed 2160² per eye, the previous build and
this one alternated, two runs each:

| Table | Without culling | With culling | Difference |
|---|---|---|---|
| Lord of the Rings (VPW, full VR room) | 18.02 ms, 1 580 000 triangles | 17.43 ms, 1 344 000 triangles | −0.6 ms (scene pass 9.0 → 8.5 ms) |
| Addams Family (room hidden by its script) | 11.56 ms | 11.72 ms | none (bgfx passes 10.90 against 10.91 ms) |
| Iron Man (minimal room) | 10.17 ms | 10.12 ms | none |
| Ghostbusters (sky room, 98 triangles culled in the probe) | 9.75 ms | 9.77 ms | none |

The render thread's submission time is unchanged on the tables where nothing is culled: the test is too cheap to show. At the
measured position (a player's place, looking at the playfield) 16 % of LOTR's primitive triangles (242 000 of 1.51 million) are
outside both eyes' view; looking
around changes which ones, frame by frame.

Visual checks: desktop captures on the Mac (`-CaptureAttract`, a close-up camera at 0.18 mm per pixel, against two runs without the
change as the noise floor) show only lamps caught at another moment of the attract sequence; in the headset, looking around quickly on
Lord of the Rings, no part appears late at the edges of the view (owner's check, 2026-10-25).

## 5. Behaviour per renderer and mode

| | Effect |
|---|---|
| bgfx (Vulkan, Metal, D3D11/12, OpenGL through bgfx) | Applies; the probe passes included |
| Legacy OpenGL and DX9 builds | Applies: `Primitive::Render` and `ModelViewProj` are shared by all backends |
| VR (OpenXR: Quest, PC VR, Steam Frame) | Applies with both eyes: a part is drawn if either eye may see it |
| Stereo 3D on desktop (anaglyph, side by side) | Same, with the two eyes of the stereo setup |
| Desktop, cabinet (camera, window and legacy view modes) | Applies; few parts are ever outside a desktop view, so there is little to gain, and nothing to lose |
| Static prepass (desktop) | Applies during the prepass with its camera; the prepass is redone when the view changes |
| Live editor | Selection and edge passes are never culled; the edited scene follows the editor camera like any view |

## 6. Limits

- **Primitives only.** Other parts (walls, ramps, rubbers, flippers, bumpers, targets, lights, flashers, decals) are not tested. They
  are mostly on the playfield, always in view, and carry little geometry; the parts that build VR rooms are primitives.
- **Animated and grouped primitives are always drawn** (section 3.2).
- **Whole parts only.** A part partly in view is drawn whole; a room exported as one huge mesh gains nothing. Splitting rooms into
  several parts is up to table authors (it is how most are built).
- **No occlusion culling.** A part hidden behind the cabinet or a wall, but inside the view, is still drawn.

## 7. What an upstream pull request could take

### 7.1 The commit as it is (recommended first step)

Commit 24a3b391b with the fixup ca181cc89 folded in: the test, the call, and the bounds reset. It needs no setting, changes no
behaviour other than performance, applies to upstream `master` 68cc62864 without conflict (`git apply --check`), and the two files
it touches are identical to upstream's apart from it. Arguments for upstream:

- the Quest build (Android, OpenXR) has the same instanced stereo and the same mobile GPU constraints as the Frame, and VR room
  tables are what its users play;
- PC VR at high resolutions pays the same vertex cost;
- it removes nothing visible, and costs nothing measurable where it culls nothing.

What the reviewers will want to see, and how to show it:

| Concern | Answer |
|---|---|
| Can a visible part disappear? | Only if its mesh bounds are wrong: the cases are listed in section 3.3, the one that was wrong is fixed. The test itself only culls boxes entirely outside one plane for every eye |
| Reflections and refractions | The probe passes use their own (mirrored) matrices; a part reflected but not directly visible is still drawn in the probe |
| Editor | Its selection passes are excluded; the edited scene is culled with the editor camera like any view |
| Cost | Measured: no change in render thread time on four tables |
| Old tables with odd primitives | Animated meshes and grouped parts are excluded; static and dynamic primitives use the same test |

### 7.2 Extensions upstream might ask for, or accept with it

| Extension | What it takes | Value |
|---|---|---|
| A statistic in the performance overlay: parts and triangles culled per frame | A counter next to `m_curDrawnTriangles` (`RenderDevice.h:232`), shown with the other counters | Makes the effect visible to testers and reviewers; cheap |
| A setting or debug key to turn it off | One flag read in `Primitive::Render` | Lets users rule it out when a table looks wrong; upstream may prefer not to add a setting for something that should be invisible |
| Animated primitives | The union of the bounds of all frames, computed once when the frames are loaded (`Mesh::LoadAnimation`, `Primitive::LoadData`), used instead of the base mesh's | Animated room parts (fans, creatures) become cullable |
| Grouped primitives | The world-space bounds of the merged buffer, computed where the group is built (`Primitive::RenderSetup`, `src/parts/primitive.cpp:741`, the group loop at 751), tested with an identity model matrix | Rooms built with "group elements" become cullable |
| Other part types | The same test in `Rubber::Render`, `Ramp::Render`, `Surface::Render`, `Light::Render` (bulb meshes), `Flasher::Render`, `HitTarget::Render`: each has, or can cheaply compute, a world or object space box | Small on today's tables; consistent behaviour |
| Per-part bounding sphere test before the box | A sphere per part, tested first | Only if a table with thousands of parts shows the box test in a profile; not needed today |

### 7.3 Out of scope

Occlusion culling (hardware queries or a depth pyramid), per-eye culling (the instanced stereo draw serves both eyes at once, so a part
seen by one eye is drawn for both), and splitting meshes into culling clusters (a table authoring or an import-time task: see the mesh
work in `Table Geometry on the Steam Frame.md`).

### 7.4 Draft of the pull request

Following the fork's rule, drafted here for review before it is opened (`vpinball-pr/`, a branch from upstream `master` with the
commit and its fixup folded).

> **Title:** Skip primitives entirely outside the view
>
> Primitives are drawn whatever their position, so the parts of a VR room behind or beside the player still cost their full vertex
> work every frame, once per eye with instanced stereo, and again in the playfield reflection probe.
>
> `Primitive::Render` now tests the bounding box of the mesh, placed by the part's matrix, against the view of every eye
> (`ModelViewProj::IsBoxVisible`) and skips the part when it is outside all of them. The test is conservative: a box is culled only
> when its 8 corners are all beyond the same side plane, or all behind the eye, for every eye. It reads the matrices of the current
> pass, so the mirrored view of a reflection probe, stereo modes and every view mode are handled the same way. Animated meshes (whose
> frames can leave the base bounds), grouped parts (drawn from a merged world-space buffer) and the editor's UI passes are always
> drawn. `CalculateBuiltinOriginal` now resets the cached bounds when the side count of a built-in primitive changes, as a script
> can do during play.
>
> Measured on a standalone OpenXR headset (Adreno, 2160x2160 per eye): Lord of the Rings VPW draws 242k fewer triangles per frame and
> saves 0.6 ms of GPU per frame; tables without a surrounding room are unchanged, and the render thread time is unchanged everywhere.
> No visible difference in desktop captures or in the headset.
>
> 🤖 Generated with [Claude Code](https://claude.com/claude-code)

## 8. Reproduction

- Build: any platform; nothing to enable.
- Effect on triangles: the per-frame counter of the performance overlay (or `m_frameDrawnTriangles`) on a table with a surrounding
  VR room, before and after.
- Frame measurements: `Doc/test-hooks/geom-series.sh` with `perf-hooks.py` (headless, fixed resolution), A/B of two builds as in
  `Table Geometry on the Steam Frame.md`, section 11.
- Visual check on the Mac: section 3 of the same document (camera-mode `[TableOverride]` at 25°, 2880×1620, `-CaptureAttract`).
