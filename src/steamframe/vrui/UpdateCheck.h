// license:GPLv3+

#pragma once

// Whether a newer release of the Steam Frame Edition is published on GitHub than the one running, checked once per launch on a worker
// thread. The installed version is the VERSION file next to the executable (written by make-release.sh, "<major.minor.patch>", and
// "<date>-<commit>" before 0.5.0), which development builds do not have: they are not checked, unless VPX_UPDATE_CHECK_VERSION gives a version to compare with (to test).
// The request goes through the libcurl of the system (the Steam Linux Runtime on the Frame), loaded when needed: no check without it.
//
// On the Frame, the release can then be installed from the game: its archive is downloaded next to the game folder, checked against the
// SHA-256 GitHub gives, unpacked with tar, and its game folder replaces the one running (Linux keeps the files of the running game open).
// The new version runs from the next start, which deletes the previous folder. The Steam shortcut, its name and the library artwork are
// left as they are (the installer app sets them). VPX_UPDATE_INSTALL_DIR installs into another folder instead, on any system (to test).
namespace VPX::VRUI::UpdateCheck
{

// Starts the check, the first time only
void Start();

bool IsUpdateAvailable();
string GetLatestVersion();
// The version of the release that runs, empty for a development build
string GetInstalledVersion();

enum class InstallPhase
{
   None,
   Downloading,
   Installing, // Checked and unpacked, then the game folder is replaced
   Done,
   Failed
};

struct InstallStatus
{
   InstallPhase phase;
   float progress; // Of the download, 0 to 1
   string message; // The reason of a failure
};

// Whether the update can be installed from the game (the Frame build, or the test folder)
bool CanInstall();
// Starts the install on a worker thread, unless one runs or is done
void StartInstall();
InstallStatus GetInstallStatus();
// From the start of the install until the application exits: the menu shows only its progress, then the message that ends it, and the
// buttons do nothing (no table can be started, nor the menu closed), only the pointer presses its OK
bool IsInstalling();

}
