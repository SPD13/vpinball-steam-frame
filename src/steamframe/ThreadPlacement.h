// license:GPLv3+

#pragma once

// Steam Frame Edition: which cores of the Steam Frame the threads of VPX run on while a table is played (Settings Player/ThreadPlacement, on in the build of the Steam
// Frame, no menu entry; docs/Performance on the Steam Frame.md, section 8.10).
//
// Steam starts a game with cores 3 to 7 allowed, which leaves the scheduler to move the logic and render threads between these cores, and share them with what else may run there: the
// compositor's busiest thread may use cores 4 to 6. VPX cannot raise its priority on the Frame, but it can choose its cores. Worn, Darkest Dungeon went from 1.9-19.4 % (three plays) to 62 % (two
// plays) of its time at full resolution, with:
// - the render thread alone on core 7, the fastest, where neither the compositor nor the tracking may run,
// - the logic thread on cores 5 and 6,
// - everything else (PinMAME, sound, PuP...) on cores 2 to 4. Cores 0 and 1, about a quarter as fast, are left to the system.
// While a table loads, the threads stay on what Steam gave them, so that the workers that decompress textures and simplify meshes have all the cores. Only on this core layout.
//
// A thread takes the cores of the one that starts it, and most threads are not VPX's, so the threads are given their cores from outside, by name, a few times per second.

class Settings;

namespace VPX::ThreadPlacement
{

void Start(const Settings& settings); // On the logic thread, when the table is loaded
void Stop(); // When the table closes: the threads get back the cores they had

}
