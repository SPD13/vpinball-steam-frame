# PinUP Player: the labels of a screen positioned on another one are hidden by that screen's labels (upstream PR candidate)

*2026-10-28. For `vpinball/vpinball` (`plugins/pup/PUPManager.cpp`, `PUPManager::Render`). The fork's commit is "pup: labels of a
screen positioned on another one are drawn above the labels of that screen"; it applies to upstream `master` (`609d55068`, Oct 29) as it
is.*

The PUP plugin draws the screens of a window in two tiers, as Windows orders topmost windows: the screens that are not topmost first,
then the topmost ones (`ForceOn`, `ForcePop`). A screen positioned on another one (`CustomPos` naming a source screen) is drawn in its
own tier, like any other. When the screen it is positioned on is topmost and it is not, everything the parent draws comes after it,
**its labels included**. Packs that show a frame over their backglass as the overlay of a `ForceOn` screen 2, and their scores as
labels of a DMD screen positioned on screen 2, lose every score under the frame. The change below draws the labels of a screen after
the labels of the screens it is positioned on, and changes nothing else.

## Symptom

City Hunter (Original 2025, Tombg), pack `City_Hunter`: the backglass shows its artwork, the mode videos and the attract video, but no
score, ball, credit or player number, in attract mode or in a game. Nothing in the log: the labels exist, have their captions and are
visible on the current page.

## The pack

Every screen of the pack lies on the backglass (`screens.pup`):

```
ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos
2,"Backglass","PuPOverlays","Overlay.png",1,ForceOn,,""
1,"DMD - 16:9 FullDMD","PuPOverlays","Overlay3.png",1,ForcePopBack,,"2,0,0,100,100"
10,"VideoLBG","Videoscenes","MainBack.mp4",,ForceBack,,"2,0,0,100,100"
11,"VideoBG","","",,ForcePop,,"2,19.8,3.91,78.2,53.38"
12,"Video1","","",,ForcePopBack,,"2,20,3.91,25.2,25.7"
...                                                                 (13 to 17: the five other mode windows)
18,"ScoreBonus","","",1,ForcePopBack,,"2,19.8,3.91,78.2,53.38"
```

- Screen 2 is the backglass, `ForceOn`. Its playlist `PuPOverlays` makes `Overlay.png` its overlay, which the plugin shows as the image of
  the screen's `pBackground` label (as PinUP's `assignAlphaFramePNG`). `Overlay.png` (1920x1080) is the artwork with six transparent
  windows in the top right part, where screens 11 to 17 play; the rest is opaque, including the dark band at the bottom.
- Screen 1 is the DMD, positioned on the whole of screen 2. The script (`PuPDMDDriverType = 2`, "16:9 FullDMD") creates all its labels
  on it, in that band: `Credits` at 34 %, 62 %, `CurScore` at 52 %, 80 %, `CScoreP1` to `P4`, the high score pages, the XP gauge (an image
  label). Its own overlay, `Overlay3.png`, is fully transparent.
- Screen 10 plays `MainBack.mp4`, the same artwork, under everything.

So the author meant the scores to be read in the band of the frame, above it.

## How the plugin draws a window

`PUPManager::Render` collects the screens of the window's tree (the root and every screen positioned on it, directly or not) in
`m_screenOrder` order, then draws them in passes (0 underlay, 1 video, 2 unused, 3 labels):

```cpp
renderScreens(false, false, 0, 1);   // not popup, not topmost: underlay, video
renderScreens(true,  false, 0, 3);   // popup, not topmost: everything
renderScreens(false, false, 2, 3);   // not popup, not topmost: labels
renderScreens(false, true,  0, 1);   // not popup, topmost: underlay, video
renderScreens(false, true,  2, 3);   // not popup, topmost: labels
renderScreens(true,  true,  0, 3);   // popup, topmost: everything
```

Screen 1 (`ForcePopBack`: popup, not topmost) has its labels drawn by the second call. Screen 2 (`ForceOn`: topmost, not popup) has its
labels, the frame among them, drawn by the fifth. The frame covers the scores.

## Why the labels of the child should win

PinUP Player's source is not available, so this rests on the pack and on what the plugin already knows of PinUP:

- The pack is built for it: a score band left empty in the frame, labels placed in it, an overlay with windows cut for the videos. On
  PinUP the scores show, or the table would not have been released this way.
- The plugin itself describes the screens positioned on another one as **child controls** of it: FN 5 and FN 6 send a screen "to the
  back / front of its siblings (PinUP: `TControl.SendToBack` / `TControl.BringToFront`)", FN 4 with `FS=1` "pins the screen above its
  siblings". In the VCL, a child control is painted over its parent's client area, whatever the parent's rank among the top-level
  windows: topmost orders top-level windows, not a parent against its children.

The rule taken is the narrowest that follows from this: the labels of a screen are never covered by the labels of the screens it is
positioned on.

## The change

Only pass 3 is reordered, and only when it has to be: the labels of a screen wait until every screen it is positioned on (up to the
root of the window, `Off` screens skipped) has drawn its labels, then are drawn at once.

```cpp
   // A screen positioned on another one lies on it: its labels are never covered by the labels of the
   // screens it is positioned on, even when those are in a higher tier (e.g. a ForceOn backglass showing
   // its frame as an overlay, with the scores on a DMD screen positioned on it). Such labels wait for them.
   vector<const PUPScreen*> labelsDrawn;
   vector<PUPScreen*> labelsWaiting;
   auto labelsReady = [&labelsDrawn, root = rootScreen.get()](const PUPScreen* screen)
   {
      for (const PUPScreen* parent = screen; parent != root && (parent = parent->GetParent()) != nullptr;)
         if (parent->GetMode() != PUPScreen::Mode::Off && std::ranges::find(labelsDrawn, parent) == labelsDrawn.end())
            return false;
      return true;
   };
   auto renderLabels = [&renderCtx, &labelsDrawn, &labelsWaiting, &labelsReady](PUPScreen* screen)
   {
      if (!labelsReady(screen))
      {
         labelsWaiting.push_back(screen);
         return;
      }
      screen->Render(renderCtx, 3);
      labelsDrawn.push_back(screen);
      for (auto it = labelsWaiting.begin(); it != labelsWaiting.end();)
      {
         if (labelsReady(*it))
         {
            (*it)->Render(renderCtx, 3);
            labelsDrawn.push_back(*it);
            labelsWaiting.erase(it);
            it = labelsWaiting.begin();
         }
         else
            ++it;
      }
   };
```

`renderScreens` calls `renderLabels(screen.get())` for pass 3 and `screen->Render(renderCtx, pass)` for the others. Every screen of the
tree that is not `Off` goes through pass 3 in exactly one of the four calls that include it, so no label is left waiting at the end.

**What it changes**: the order of the label passes, only for a screen whose label pass came before that of a screen it is positioned on:
the parent in a higher tier (City Hunter), or in the same tier but later in `m_screenOrder`. Elsewhere the order is the same as today.

**What it does not change**: videos, underlays and static images keep their tiers. A frame drawn as the overlay of a backglass still
covers the videos of the screens positioned on it, as the comment above the tiers wants ("a full-window frame/overlay ... stays above
the background videos"); in City Hunter the frame stays above the mode videos of screens 12 to 17, which play through its windows.

## Other ways considered

- **Draw the root of the window first, then the screens positioned on it** (the child control model applied to everything). Every
  video of a child would then cover the root's frame: City Hunter's `MainBack.mp4` (screen 10, opaque, full size) would hide the frame,
  and the packs whose frame is meant to stay over their videos, the case the current order was written for, would lose it.
- **Children inherit the topmost rank of the screen they are positioned on.** Every child of a `ForceOn` backglass would move to the
  topmost tier, and its popup videos would be drawn over the backglass frame.

Both fix City Hunter but change what other packs show. Reordering only the labels does not touch any video.

## Evidence

On macOS (`VPinballX_BGFX`, fork master `92d538cf5`), with the backglass window open (`[Backglass] BackglassOutput = 1`) and a temporary
hook that adds a credit, presses Start and captures the window (`RenderDevice::CaptureScreenshot`) 6 s and 16 s after the start:

| Plugin | Attract mode | Game |
|---|---|---|
| As upstream | No label | No label |
| With this change | No label | No label |
| With upstream's `f0d73270b` (Oct 27) | No label | No label |
| With this change and `f0d73270b` | High score page | Credits, players, balls, score, player number, XP gauge |

The second row needs explaining. The fork's plugin then predated `f0d73270b` ("PUP: update label texture on API thread"), and City
Hunter showed the race it fixes: the XP gauge label (`XP0.png`, an image of the full backglass size drawn last) was drawn with the
frame's texture, so the frame came back over the scores. Skipping the labels one by one (`PUPLabel::Render`) and the screens one by one
(`renderScreens`) separated the two causes. Upstream `master` already has `f0d73270b`, so this change alone should restore the scores
there; it was not built and run on upstream `master` itself.

## Points for the reviewer (upstream)

- The rule follows from the pack and from the plugin's own child control wording, not from PinUP's code. A capture of City Hunter on
  PinUP Player would settle it, as would any pack with labels on a screen positioned on a `ForceOn` one.
- Only the label passes move. If PinUP also draws the *videos* of a child over its parent's overlay, this is a first step, not the whole
  rule; nothing seen so far asks for it.
- The waiting list is empty for every pack whose parents already draw their labels first, so the cost is a few vector lookups per frame.

## What the fork does meanwhile

The fork carries the commit, listed with the other plugin changes in [PinUP Player on the Steam Frame](PinUP%20Player%20on%20the%20Steam%20Frame.md),
section 9, and picked `f0d73270b` ahead of the next sync. Once upstream merges the change, the fork's commit becomes empty at the sync
and is dropped.

## Draft of the PR

**Title:** PUP: labels of a screen positioned on another one are drawn above the labels of that screen

**Body:**

> A screen positioned on another one (`CustomPos` with a source screen) is drawn in its own render tier. When the screen it is
> positioned on is topmost (`ForceOn`, `ForcePop`) and it is not, the parent's labels are drawn after its labels, overlay frame
> included, and cover them.
>
> City Hunter (Original 2025, Tombg) shows its frame as the overlay of the `ForceOn` backglass (screen 2, `PuPOverlays/Overlay.png`, opaque
> except for the video windows) and all its scores as labels of the DMD (screen 1, `ForcePopBack`, positioned on the whole of screen 2,
> in an empty band of the frame). The frame hides every score, in attract mode and in game.
>
> This change keeps the tiers and only delays the label pass of a screen until the screens it is positioned on have drawn theirs. Videos
> and images are not reordered, so a backglass frame still stays above the videos of its children, and packs whose parents already draw
> their labels first are unchanged. The plugin's notes on FN 5 / FN 6 describe positioned screens as child controls of their source
> screen (`TControl.SendToBack` / `BringToFront`): a child control is painted over its parent whatever the parent's topmost rank.
>
> Tested on macOS with City Hunter: with this change (and `f0d73270b`, already in master), the backglass shows the high score pages in
> attract mode and the credits, balls, player, score and XP gauge in a game; without it, none of them.

## Tools

- Extracting a script: `VPinballX_BGFX -ExtractVBS <table.vpx>` (it writes the `.vbs` next to the table).
- The capture method and the screen/label skipping are described in [PinUP Player on the Steam Frame](PinUP%20Player%20on%20the%20Steam%20Frame.md),
  section 6 ("What PuP draws, on the Mac").
