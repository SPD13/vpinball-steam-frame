// Offline test of libspatialaudio's AmbisonicRotator as VPX's spatial audio mixer drives it in VR
// (see docs/Spatial Audio Crackle When Turning the Head.md).
//
// A 217 Hz tone is encoded in front of the listener (2nd order, 48 kHz) while the head turns at 300 deg/s. The head pose is
// published at the render frame rate (optionally dropping frames), and read once per mixer block like
// src/audio/SpatialAudio.cpp does (retarget the rotator when the yaw moved more than 0.004 rad, blocks of at most 512 frames).
// The left/right component of the rotated soundfield (ACN 1) is then checked for sample steps larger than the tone can make.
//
// Build against the libspatialaudio of external/ (stock), or with a patched AmbisonicRotator.cpp before the library (patched):
//   L=external/macos-arm64/Release/libspatialaudio/libspatialaudio
//   c++ -std=c++17 -O2 -I$L/include -I$L/build/include -I$L/source steamframe/tools/spatial-audio-rotator-sim.cpp \
//      [<patched>/source/AmbisonicRotator.cpp] $L/build/libspatialaudio.a -o rotator-sim
// Run: rotator-sim <audio callback frames> <render fps> <probability of a dropped frame>   (FADE=<ms> overrides the 20 ms fade)

#include "AmbisonicEncoder.h"
#include "AmbisonicRotator.h"
#include "BFormat.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace spaudio;

int main(int argc, char** argv)
{
   if (argc < 4)
   {
      printf("Usage: %s <audio callback frames> <render fps> <dropped frame probability>\n", argv[0]);
      return 1;
   }
   const unsigned sr = 48000, order = 2, maxBlock = 512;
   const unsigned period = atoi(argv[1]);
   const float renderHz = (float)atof(argv[2]);
   const float dropProb = (float)atof(argv[3]);
   const float fadeMs = getenv("FADE") ? (float)atof(getenv("FADE")) : 20.f;
   const float toneHz = 217.f; // not a divisor of the sample rate, so block boundaries do not fall on zero crossings
   const float yawSpeed = 300.f * (float)M_PI / 180.f; // rad/s, a fast head turn

   AmbisonicEncoder enc;
   enc.Configure(order, true, sr, 10.f);
   enc.SetPosition({ 0.f, 0.f, 1.f });
   AmbisonicRotator rot;
   rot.Configure(order, true, maxBlock, sr, fadeMs);
   BFormat bf;
   bf.Configure(order, true, maxBlock);

   std::mt19937 rng(1);
   std::uniform_real_distribution<float> uniform(0.f, 1.f);
   const unsigned total = sr * 2;
   std::vector<float> outY(total), mono(maxBlock), tmp(maxBlock);
   float poseYaw = 0.f, appliedYaw = 0.f;
   bool applied = false;
   double nextFrame = 0.0;
   unsigned t = 0;
   while (t + period <= total)
   {
      // Render thread: a new head pose for each displayed frame, none for a dropped one
      while (nextFrame <= (double)t / sr)
      {
         if (uniform(rng) >= dropProb)
            poseYaw = yawSpeed * (float)nextFrame;
         nextFrame += 1.0 / renderHz;
      }
      // Audio callback: the mixer node processes it in blocks of at most 512 frames
      for (unsigned off = t, remaining = period; remaining > 0;)
      {
         const unsigned n = remaining < maxBlock ? remaining : maxBlock;
         for (unsigned i = 0; i < n; i++)
            mono[i] = sinf(2.f * (float)M_PI * toneHz * (float)(off + i) / (float)sr);
         bf.Reset();
         enc.Process(mono.data(), n, &bf);
         const float rotYaw = -poseYaw;
         if (!applied || fabsf(rotYaw - appliedYaw) > 0.004f)
         {
            rot.SetOrientation({ rotYaw, 0.f, 0.f });
            appliedYaw = rotYaw;
            applied = true;
         }
         rot.Process(&bf, n);
         bf.ExtractStream(tmp.data(), 1, n);
         for (unsigned i = 0; i < n; i++)
            outY[off + i] = tmp[i];
         off += n;
         remaining -= n;
      }
      t += period;
   }

   // A unit tone moves at most 2 pi f / sr per sample: anything 3x above is a click
   const double toneStep = 2. * M_PI * toneHz / sr;
   double maxStep = 0., sumErr = 0.;
   int clicks = 0;
   for (unsigned i = 1; i < t; i++)
   {
      const double step = fabs(outY[i] - outY[i - 1]);
      maxStep = fmax(maxStep, step);
      if (step > 3. * toneStep)
         clicks++;
      // Expected: the front source turned by the head yaw, its left/right component is sin(yaw) times the tone
      const double ref = sin(yawSpeed * (double)i / sr) * sin(2. * M_PI * toneHz * i / sr);
      sumErr += fabs(fabs(outY[i]) - fabs(ref));
   }
   printf("callback %4u frames, render %5.1f fps, drops %.2f: max step %.3f (tone %.3f), clicks %4d, mean error %.3f\n", period, renderHz, dropProb,
      maxStep, toneStep, clicks, sumErr / t);
   return 0;
}
