// license:GPLv3+

#pragma once

// Whether a newer release of the Steam Frame Edition is published on GitHub than the one running, checked once per launch on a worker
// thread. The installed version is the VERSION file next to the executable (written by make-release.sh, "<date>-<commit>"), which
// development builds do not have: they are not checked, unless VPX_UPDATE_CHECK_VERSION gives a version to compare with (to test).
// The request goes through the libcurl of the system (the Steam Linux Runtime on the Frame), loaded when needed: no check without it.
namespace VPX::VRUI::UpdateCheck
{

// Starts the check, the first time only
void Start();

bool IsUpdateAvailable();
string GetLatestVersion();

}
