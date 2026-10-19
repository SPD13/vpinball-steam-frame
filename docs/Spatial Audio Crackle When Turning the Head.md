# Spatial audio crackle when turning the head

*2026-10-25, fork `master` after 8af2b1a49. libspatialaudio d149ed9744fd (the version pinned by `platforms/config.sh`, same code in
videolan master on that date).*

## 1. Summary

- **Symptom.** In VR on heavy tables (Ready Player One, GoldenEye, 2001: A Space Odyssey), the sound crackles while the head turns
  fast. It sounded like the audio was starved by a blocked process.
- **Cause.** It is not a blocked thread: it is a bug in libspatialaudio's soundfield rotator (`AmbisonicRotator::Process`). While the
  rotator fades from one head orientation to the next, it drops every matrix coefficient that was zero at start, which is all the
  terms that move sound between front, sides and back. When a fade completes, those terms jump in at full value at once.
- **Why graphics load matters.** The head pose reaches the audio mixer once per rendered frame. As long as a new pose arrives within
  the 20 ms fade, the fade never completes: no click, but the side component of the soundfield is missing while the head moves. When
  frames are 20 ms apart or more (heavy tables at 36 fps on the 72 Hz Frame, dropped frames), fades complete between poses, and the
  output switches between "without" and "with" those terms at every pose: near full-scale steps, heard as crackle. A fast turn makes
  every pose pass the 0.004 rad threshold of the mixer, and makes the two states differ most.
- **Fix on the fork.** A patch of the rotator (`steamframe/patches/libspatialaudio/01-rotator-fade.patch`), applied when the desktop
  builds of the fork build libspatialaudio. In an offline simulation of the mixer, the largest sample step goes from 0.99 (full scale)
  to the step of the clean tone (0.03), in every case tested.
- **Upstream.** The patch is generic: it belongs in libspatialaudio, then vpinball only moves its pinned version (section 7). Not sent
  yet.

## 2. How VPX plays sound

| Part | Where | Thread |
|---|---|---|
| Two miniaudio engines, one per output device (backglass, playfield) | `src/audio/AudioPlayer.cpp` | created on the main thread |
| miniaudio backend on SDL3 audio streams (`SDL_OpenAudioDeviceStream` with a callback) | `src/audio/AudioPlayer.cpp` | SDL's audio thread |
| Sounds (`ma_sound` nodes), music, PinMAME/PuP audio streams | `src/audio/SoundPlayer.cpp`, `AudioStreamPlayer.cpp` | started from the game logic thread |
| VR spatial audio: one ambisonic encoder node per sound, one mixer node per engine (rotation, binaural rendering) | `src/audio/SpatialAudio.cpp` | mixing on SDL's audio thread |
| Head pose given to the mixer (`AudioPlayer::SetListenerPose`) | `src/renderer/VRDevice.cpp` | render thread, once per frame |

All the mixing happens in SDL's callback (`ma_audio_callback_playback__sdl`): miniaudio pulls every sound through the node graph,
the spatial sources encode each sound to 2nd order B-format (9 channels) at its position relative to the listener, and the spatial
mixer rotates the whole soundfield to the head orientation, then binauralizes it (FABIAN HRTF from SOFA, FFT convolution in blocks
of up to 512 frames) to the headset's stereo output.

On Linux (the Frame), SDL3 uses its PipeWire backend: the callback runs on the thread of SDL's own `pw_thread_loop`, which loads
PipeWire's realtime module (`client-rt.conf`) to raise its priority. SDL asks for a 1024 frame latency at 48 kHz (21 ms,
`SDL_GetDefaultSampleFramesFromFreq`); the size of each callback is what PipeWire's graph gives.

**What the render thread shares with the audio thread.** The listener pose and the source parameters are atomics
(`SpatialAudioListener`, `SpatialAudioSource::Impl`), read by the audio thread at each block: no lock, nothing the render thread can
hold while the audio thread waits. The only coupling to the graphics load is the *rate* of the pose updates: one per rendered frame.

## 3. The bug

The mixer (`SpatialAudioMixer::Impl::Process`) reads the latest pose at each block of at most 512 frames, and retargets the rotator
when the yaw, pitch or roll moved more than 0.004 rad (`kOrientationEpsilon`, against tracker jitter). The rotator then fades its
9×9 rotation matrix from the current one to the new target in 20 ms (`kRotationFadeMs`, 960 frames at 48 kHz):

```cpp
// libspatialaudio source/AmbisonicRotator.cpp, AmbisonicRotator::Process
if (m_fadingCounter < m_fadingSamples)
{
    for (unsigned iOut = 0; iOut < m_nChannelCount; ++iOut)
        for (unsigned iIn = 0; iIn < m_nChannelCount; ++iIn)
            if (std::abs(m_currentMatrix[iOut][iIn]) > 1e-6f && std::abs(m_targetMatrix[iOut][iIn]) > 1e-6f)
                for (unsigned iSamp = 0; iSamp < nFadeSamp; ++iSamp)
                {
                    pBFSrcDst->m_ppfChannels[iOut][iSamp] += m_currentMatrix[iOut][iIn] * m_tempBuffer.m_ppfChannels[iIn][iSamp];
                    m_currentMatrix[iOut][iIn] += m_deltaMatrix[iOut][iIn];
                }
    m_fadingCounter += nFadeSamp;
}
// after the fade: m_targetMatrix for the rest of the block
```

A coefficient is rendered *and advanced* only when both its current and its target values are non-zero. The skip was meant to save
work on coefficients that stay zero, but it uses `&&` where `||` was needed:

1. `Configure` calls `Reset()`, which sets the current matrix to the rotation of the initial orientation (0, 0, 0): the identity. Its
   off-diagonal coefficients are zero, including the 26 inside the 3×3 (1st order) and 5×5 (2nd order) blocks that a rotation fills.
2. When the head turns, the target matrix gets non-zero off-diagonal coefficients (for a yaw, `sin(yaw)` between X and Y, `sin(2·yaw)`
   between the 2nd order pairs...). Their current value is zero, so the fade skips them: they are not rendered, and their current value
   is not advanced either.
3. So the current off-diagonal coefficients stay at zero forever, and every fade renders the soundfield without them: a front source
   turned 90° away should move fully to the side channel (Y); during the fade it is only scaled by `cos(90°)` = 0 on X, and nothing
   reaches Y.
4. When the fade completes, the rest of the block uses the target matrix: the off-diagonal terms jump from zero to their full value in
   one sample.
5. At the next retarget, a new fade starts from the same stale current matrix: the terms drop back to zero in one sample.

Whether a fade completes depends on the time between two retargets:

| Pose updates | What the listener hears |
|---|---|
| Within 20 ms of each other (steady 72 fps: 13.9 ms) and callbacks of 512 frames or less | Fades never complete: no steps, but the side component of every sound is missing while the head moves (wrong placement, quieter) |
| 20 ms or more apart (36 fps, a dropped frame), or 1024 frame callbacks (two blocks, the second without a new pose) | Fades complete between poses: the off-diagonal terms switch on and off at every pose, near full-scale steps at 36–72 Hz: crackle |

Heavy tables are exactly the case of the second row: on the Frame they drop to 36 fps (reprojection) or miss frames
(`docs/Performance on the Steam Frame.md`), and a fast turn changes the orientation by more than 0.004 rad at every frame (300°/s is
0.07 rad per frame at 72 fps). Light tables holding 72 fps stay in the first row, which is why they seemed fine.

The rest of the chain was checked: the encoder of each source fades its position with `GainInterp`, which advances every coefficient
whatever its value (correct); no other class of libspatialaudio has this skip.

## 4. Measurements

`steamframe/tools/spatial-audio-rotator-sim.cpp` reproduces the mixer offline: a 217 Hz tone in front of the listener (2nd order,
48 kHz), the head turning at 300°/s, poses published at the render rate (optionally dropping frames), read once per block with the
mixer's threshold and block size. It measures the largest step between two samples of the left/right component (ACN 1); a clean tone
moves at most 0.028 per sample, and steps above 3× that are counted as clicks. "Error" is the mean distance to the ideal rotation
(the remaining 0.05–0.07 with the fix is the lag of the pose: one frame plus the 20 ms fade).

| Callback | Render | Stock rotator: max step, clicks in 2 s, error | Patched rotator |
|---|---|---|---|
| 480 frames | 72 fps | 0.000, 0, 0.395 (side component absent) | 0.029, 0, 0.050 |
| 480 frames | 72 fps, 10 % frames dropped | 0.903, 22, 0.368 | 0.029, 0, 0.054 |
| 480 frames | 36 fps | 0.989, 89, 0.302 | 0.029, 0, 0.060 |
| 1024 frames | 72 fps | 0.989, 150, 0.373 | 0.028, 0, 0.058 |
| 256 frames | 72 fps, 10 % frames dropped | 0.745, 24, 0.368 | 0.028, 0, 0.051 |
| 256 frames | 36 fps | 0.985, 119, 0.300 | 0.028, 0, 0.056 |

A step of 0.99 is a full-scale click. With the fade turned off instead (`FADE=0`), the largest step is 0.072 at 480 frames and 72 fps:
the fade is needed (without it the matrix changes once per block, which zippers), only its bookkeeping was wrong.

## 5. The fix on the fork

`steamframe/patches/libspatialaudio/01-rotator-fade.patch` changes the fade loop of `AmbisonicRotator::Process`:

```cpp
if (std::abs(m_currentMatrix[iOut][iIn]) > 1e-6f || std::abs(m_targetMatrix[iOut][iIn]) > 1e-6f)
    for (unsigned iSamp = 0; iSamp < nFadeSamp; ++iSamp)
    {
        pBFSrcDst->m_ppfChannels[iOut][iSamp] += m_currentMatrix[iOut][iIn] * m_tempBuffer.m_ppfChannels[iIn][iSamp];
        m_currentMatrix[iOut][iIn] += m_deltaMatrix[iOut][iIn];
    }
else // Negligible on both ends: not rendered, but kept in step with the fade
    m_currentMatrix[iOut][iIn] += m_deltaMatrix[iOut][iIn] * (float)nFadeSamp;
m_fadingCounter += nFadeSamp;
if (m_fadingCounter >= m_fadingSamples)
    m_currentMatrix = m_targetMatrix;
```

- A coefficient is faded when it is non-zero at *either* end, so terms appearing or disappearing ramp in or out.
- Coefficients that are negligible at both ends are still not rendered (the saving the skip was for), but their current value follows
  the fade, so the next fade starts from the right place.
- At the end of a fade the current matrix is set to the target, so the per-sample increments do not accumulate rounding errors over
  a long session.

The cost does not change: a 2nd order rotation matrix is block diagonal (1 + 3×3 + 5×5 = 35 coefficients at most), the 46 others are
zero at both ends of every fade and are still skipped. That is 35 multiply-adds per frame at most, on one mixer per
device, next to the binaural FFT convolution.

The patch is applied by the platforms on which the fork builds itself (macos-arm64, macos-x64, linux-aarch64, linux-x64,
windows-mingw-x64), in their `external.sh`, right after unpacking libspatialaudio, like the bgfx patches:

```sh
LIBSPATIALAUDIO_EXPECTED_SHA="${LIBSPATIALAUDIO_SHA}-${LIBMYSOFA_SHA}_001-rotfix1" # Steam Frame Edition: ...
...
   for p in ../../../../../steamframe/patches/libspatialaudio/*.patch; do patch -p1 < "$p"; done # Steam Frame Edition: ...
```

The `-rotfix1` suffix of the cache key makes the next `external.sh` run rebuild libspatialaudio with the patch (for the Frame:
`Doc/steam-frame-build/build-linux-arm64.sh`, which runs the dependencies step first). A further patch of this folder bumps the
suffix. The mobile and Windows (MSVC) builds keep upstream's libspatialaudio, as they keep upstream's code everywhere else.

No change to `src/audio/SpatialAudio.cpp`: its threshold and fade times are right once the rotator fades correctly.

### Verification

- Offline: the patch applied to the pristine d149ed9744fd tarball, compiled into the simulation: table of section 4.
- On the Frame: **pending**, the owner in the headset on Ready Player One, GoldenEye or 2001, turning the head fast; also on a light
  table, where sounds should now follow the head to the sides during the turn instead of fading.

## 6. Not ruled out: the audio thread missing its deadline

The crackle explained above needs no CPU starvation, but starvation could add to it on heavy tables. To check on the Frame while a
heavy table runs (VPX started by the owner):

- the priority of SDL's audio thread: `ps -L -o tid,cls,rtprio,pri,comm -p <VPX pid>` (the PipeWire loop thread should be `FF` or
  `RR` with a realtime priority, if rtkit granted it);
- PipeWire underruns: `pw-top`, the `ERR` column of VPX's streams growing while the head turns;
- the callback size actually used: the `QUANT` column of `pw-top`.

If underruns show up with the fix, the next steps would be a larger latency hint (`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`), checking
that rtkit grants the realtime priority, or moving the binaural convolution to a smaller FFT size.

## 7. What an upstream fix would look like

Nothing has been sent upstream yet. When it is:

### 7.1 libspatialaudio (github.com/videolan/libspatialaudio)

A pull request with the change of section 5 to `source/AmbisonicRotator.cpp`. Its description: the fade in `Process` only renders
and advances coefficients non-zero at both ends, so coefficients that are zero in the current matrix (all the off-diagonal ones
after `Reset()` at the default orientation) never advance; they are missing for the whole of every fade, and jump to their target
when a fade completes. With `SetOrientation` called more often than the fade time (head tracking), the rotation is wrong during
movement and clicks at every completed fade.

A test to go with it, next to the existing ones of `tests/` (`TestAmbisonicShelfFilters.cpp`...), say `TestAmbisonicRotator.cpp`: configure a 1st order rotator (any fade time), encode a source at the front,
call `SetOrientation` with a yaw of 90° and process for longer than the fade, in blocks shorter than the fade:

- stock: the Y channel stays at zero for the whole fade, then jumps to the full signal on the first sample after it;
- fixed: Y ramps from zero to the full signal across the fade, and the largest step between two samples stays below the signal's own.

A second case worth covering: `SetOrientation` called again before the fade completes (the fade restarts from the current matrix,
which must then hold the advanced values).

### 7.2 vpinball (github.com/vpinball/vpinball)

Once libspatialaudio merges it:

- move `LIBSPATIALAUDIO_SHA` in `platforms/config.sh` to the merge commit (all platforms rebuild it from the new cache key);
- nothing to change in `src/audio/SpatialAudio.cpp`;
- the fork then drops `steamframe/patches/libspatialaudio/` and the two `Steam Frame Edition` lines of each `external.sh`.

If vpinball wants the fix before libspatialaudio merges it, the same patch can be applied by every `platforms/*/external.sh` (all ten
build libspatialaudio from the same tarball; the Windows and mobile ones are bash scripts too, but whether their CI images have
`patch` was not checked: `git apply` is an alternative), with the cache key bumped. An
alternative that avoids patching the library would be to stop rotating the soundfield in the mixer and apply the head orientation to
each source's direction in `SpatialAudioSource::Impl::UpdatePosition` (the encoder fades positions correctly). It costs a few more
trigonometric functions per source and block, and moves the pitch and roll handling into the source; the one-line library fix is
simpler and keeps the design as it is.

The vpinball pull request should mention the symptom (crackle on head turns in VR on tables that miss frames) and point to the
libspatialaudio pull request.

### 7.3 At the next upstream syncs

The patch lives in the fork's files and two lines of each `external.sh`; if upstream changes the libspatialaudio block or the pinned
version, check that the patch still applies: the `external.sh` scripts run with `set -e`, so a patch that no longer applies stops the
dependency build with `FAILED` in its log. Drop the patch once upstream's pinned version contains the fix.
