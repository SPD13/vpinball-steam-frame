// license:GPLv3+

#pragma once

#include <bit>
#include <cstdint>

namespace VPX
{

// Steam Frame Edition: NaN marks the values not read (performance panel, simplified meshes). The Linux and windows-mingw builds compile with
// -ffast-math (make/CMakeLists_app.txt), under which GCC takes std::isnan for always false: the bits are tested instead (exponent all ones,
// mantissa not zero), which the optimizer keeps.
inline bool IsNaN(float value)
{
   return (std::bit_cast<uint32_t>(value) & 0x7fffffffu) > 0x7f800000u;
}

}
