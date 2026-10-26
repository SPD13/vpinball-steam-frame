// license:GPLv3+

#pragma once

#include "common.h"

#include <map>
#include <optional>
#include <set>

struct VPXPluginAPI;

namespace PinMAME {

class Controller;

// Steam Frame Edition: a procedure of the ROM fixes of VPX (VPXPluginAPI::GetRomFix, see src/steamframe/TableFixes.h). Some ROMs only work
// once set up from their service menu, like the Gottlieb System 3 games, whose new NVRAM holds zero prices: the procedure presses the switches
// an operator would, once the emulation started, then resets the machine so that the game starts again with its new settings.
//
// The steps run on the main thread (RunOnMainThread), like the table script. While the procedure runs, the switches it presses are held:
// their changes from the table script (a player pressing a flipper would change a setting) are applied at its end.
class RomFix final
{
public:
   RomFix(const MsgPluginAPI* msgApi, unsigned int endpointId, Controller* controller);
   ~RomFix();

   // Before the start of the emulation of 'rom': ask VPX for a procedure, and keep a copy of the NVRAM it changes
   void Prepare(const string& rom, const string& vpmPath);
   // Once the emulation runs
   void Start();
   // Before the emulation stops: a procedure that did not end is reported as interrupted (it runs again at the next start)
   void Stop();
   // Whether a change of a switch from the table script is held, to be applied at the end of the procedure
   bool HoldsSwitch(int switchNo, bool state);

private:
   struct Procedure {
      string rom;
      string id;
      int version = 0;
      string notice;
      int bootMs = 0;
      int holdMs = 0;
      int waitMs = 0;
      bool reset = false;
      vector<int> switches;
   };

   static void MSGPIAPI OnTimer(void* userData);
   void Schedule(int delayMs, int step);
   void RunStep(int step);
   void Finish();
   ::VPXPluginAPI* GetVpxApi() const;

   const MsgPluginAPI* const m_msgApi;
   const unsigned int m_endpointId;
   Controller* const m_controller;
   std::optional<Procedure> m_procedure;
   bool m_running = false;
   uint64_t m_generation = 0; // Steps scheduled before a Stop do nothing
   std::set<int> m_heldSwitches;
   std::map<int, bool> m_heldStates;
   unsigned int m_notification = 0;
};

}
