// license:GPLv3+

#pragma once

#include "PUPManager.h"
#include "LibAv.h"
#include "ThreadPool.h"

namespace PUP {

class PUPMediaPlayer final
{
public:
   explicit PUPMediaPlayer(const string& name);
   ~PUPMediaPlayer();

   void SetGameTime(double gameTime);

   // Decode videos with the hardware decoders of the system when there are (FFmpeg's V4L2 memory to memory decoders, on Linux)
   static void SetHardwareDecoding(bool enable) { s_hardwareDecoding = enable; s_hardwareStalls = 0; }

   void Play(const std::filesystem::path& filename, float volume, double startPosition = 0.0);
   double GetPosition() const { return m_position.load(); }
   const std::filesystem::path& GetFilename() const { return m_filename; }
   unsigned int GetLoopCount() const { return m_loopCount.load(); }
   bool IsPlaying();
   void Pause(bool pause);
   void Stop();
   void SetVolume(float volume);
   void SetLoop(bool loop);
   void Render(VPXRenderContext2D* const ctx, const SDL_Rect& destRect, float alpha = 1.f);

   void SetName(const string& name);
   void SetOnEndCallback(const std::function<void()>& onEndCallback) { std::lock_guard lock(m_mutex); m_onEndCallback = onEndCallback; }
   void SetBounds(const SDL_Rect& rect);
   void SetMask(std::shared_ptr<SDL_Surface> mask);

private:
   void StopBlocking();
   void ReleaseHoldTexture();
   void RenderHold(VPXRenderContext2D* const ctx, const SDL_Rect& destRect, float alpha);
   void RenderLastFrame(VPXRenderContext2D* const ctx, const SDL_Rect& destRect, float alpha);
   void Run();
   AVCodecContext* OpenStream(AVFormatContext* pInputFormatContext, int stream, bool allowHardware = true);
   AVCodecContext* OpenCodec(AVFormatContext* pInputFormatContext, int stream, const AVCodec* pCodec);
   static bool IsHardwareDecoder(const AVCodecContext* pContext);
   static std::atomic<bool> s_hardwareDecoding;
   static std::atomic<int> s_hardwareStalls;
   int SendVideoPacket(const AVPacket* pPacket);
   int ReceiveVideoFrame(AVFrame* pFrame);
   int AfterHardwareCall(int ret);
   bool SwitchToSoftwareDecoding();
   void CheckHardwareStall();
   void UnblockHardwareDecoder();
   void HandleAudioFrame(AVFrame* pFrame, bool sync);
   void HandleVideoFrame(AVFrame* pFrame);

   string m_name;
   SDL_Rect m_bounds = {};

   std::atomic<int> m_pendingPlay = 0;
   std::atomic<int> m_pendingStop = 0;

   std::function<void()> m_onEndCallback = [](){};

   double GetPlayTime() const;
   bool m_syncOnGameTime = false;
   double m_gameTime = -1.0;
   double m_lastGameTime = -1.0;
   double m_lastGameTimeWall = 0.0;
   double m_gameTimeOffset = 0.0;
   double m_gameTimeOffsetBase = 0.0;
   double m_startTimestamp = 0.0; // timestamp in seconds when the play command was called

   std::filesystem::path m_filename;
   bool m_loop = false;
   int m_playIndex = 0;
   std::atomic<unsigned int> m_loopCount = 0;
   float m_volume = 100.f;

   bool m_paused = false;
   double m_pauseTimestamp = 0.0;
   std::atomic<bool> m_firstRenderPending = false; // cleared by the render thread (first render) or the decoder thread (timeout)
   double m_lastDecodedPts = 0.0;

   AVFormatContext* m_pFormatContext = nullptr;

   int m_videoStream = -1;
   AVCodecContext* m_pVideoContext = nullptr;
   bool m_videoPtsBaseValid = false;
   double m_videoPtsBase = 0.0;
   bool m_audioPtsBaseValid = false;
   double m_audioPtsBase = 0.0;
   std::atomic<double> m_position = 0.0;

   // Slots of unordered decoded frames
   struct FrameInfo
   {
      bool valid = false;
      AVFrame* frame = nullptr;
      uint8_t* buffer = nullptr;
      VPXTexture texture = nullptr;
      double pts = -1.0;
      bool uploaded = false;
      int age = 0;
   };
   vector<FrameInfo> m_frames;
   std::atomic<VPXTexture> m_holdTexture = nullptr;
   std::atomic<uint64_t> m_endTicks = 0;
   SwsContext* m_swsContext = nullptr;
   uint64_t m_lastRenderTicks = 0;
   uint64_t m_playTicks = 0;

   std::shared_ptr<SDL_Surface> m_mask = nullptr;
   std::unique_ptr<SDL_Surface, void (*)(SDL_Surface*)> m_scaledMask;

   int m_audioStream = -1;
   AVCodecContext* m_pAudioContext = nullptr;
   struct SwrContext* m_pAudioConversionContext = nullptr;
   AVSampleFormat m_audioFormat = AV_SAMPLE_FMT_NONE;
   void* m_pAudioLoop = nullptr;
   int m_audioFreq = 0;
   uint32_t m_audioStreamId;
   static std::atomic<uint32_t> m_nextAudioStreamId;

   std::mutex m_mutex;
   std::thread m_thread;
   std::atomic<bool> m_running = false;
   std::atomic<bool> m_runDone = true; // The decoding thread has left Run

   // A V4L2 decoder of FFmpeg may block forever, waiting for the device without a timeout: the decoding thread tells when it is inside
   // one of its calls (SDL ticks, 0 when outside), and a call lasting too long is woken up by stopping the device's queues
   std::atomic<uint64_t> m_hardwareCallStart = 0;
   std::atomic<bool> m_hardwareUnblocked = false; // The queues were stopped: the decoder is dead, the video goes on in software
   std::atomic<bool> m_hardwareCheckQueued = false;

   const LibAV::LibAV& m_libAv;

   progschj::ThreadPool m_commandQueue;
};

}
