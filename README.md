# Visual Pinball X: Steam Frame Edition

An independent fork of [Visual Pinball X](https://github.com/vpinball/vpinball) that runs as a standalone app on the Valve Steam Frame: natively on the headset (SteamOS on ARM64, OpenXR, Vulkan), with no PC and no cable. It adds a table launcher that works from inside the headset, a table library and picker, a lobby, uploading tables and ROMs from a browser, scores and leaderboards, eye-tracked foveated rendering, and a menu made for the VR controllers. It keeps following the upstream project and merges its work.

**Status: Working and tested as a standalone app on the Steam Frame.**

**No tables or ROMs are included in this package**: bring your own. To learn more about Visual Pinball and find tables to download, see [VPForums](https://www.vpforums.org/); VR tables are in its [VR tables download section](https://www.vpforums.org/index.php?app=downloads&showcat=56).

> [!CAUTION]
> **This fork is not endorsed by, affiliated with, or supported by the Visual Pinball team or community.** It is developed with extensive AI-assisted coding, which is why it is kept apart from the official project. Please do **not** ask support questions about it in the Visual Pinball repositories, Discord servers or community forums: report problems and ask questions in [this fork's issues](https://github.com/SPD13/vpinball-steam-frame/issues). The official project is [vpinball/vpinball](https://github.com/vpinball/vpinball).

[![Visual Pinball X, Steam Frame Edition: the trailer](docs/img/steam-frame-trailer.jpg)](https://youtu.be/g_C8ryf-UP0)

**▶ [Watch the trailer](https://youtu.be/g_C8ryf-UP0)** (2:35): tables in the headset, the table picker, the web companion, scores and leaderboards, the Frame controllers, performance and eye-tracked foveated rendering.

## What the player gets

In the standalone builds (the Steam Frame app, and the standalone desktop builds):

- **Eye-tracked foveated rendering, exclusive to this fork and to the Steam Frame build**: full shading only around the point the eyes look at, following the headset's eye tracker, through a fragment shading rate image that the Frame's driver applies in its fast direct render path: 2–4 ms per frame on heavy tables at native 2160×2160 per eye, on by default (Medium); a "Foveated rendering" level (Off / Low / Medium / High), an "Eye-tracked" switch and a status line in the VR settings page. Not in upstream Visual Pinball, and inert on Windows and macOS, whose builds lack the Vulkan driver support and the bgfx patches ([full technical reference](docs/Foveated%20Rendering%20on%20the%20Steam%20Frame.md)).
- **Dynamic resolution in the headset**: the rendering resolution follows the GPU time of the frames, between a minimum and the table's resolution, so heavy tables and a hot headset keep their frame rate instead of judder on head movements, and light tables keep the full sharpness; a switch, a target and a minimum in the VR settings page, with a status line.
- **Scores and leaderboards**: the score of every game played on a library table is recorded automatically, for the player chosen in the lobby ("Player: <name>"), with a result screen (rank, personal best, table record) when the lobby comes back, a Scores tab with the leaderboard of each table, the 10 best scores in each table's page, and a Scores page in the browser to filter, reassign, delete or clear scores (with confirmation) and manage the players ([details](docs/Table%20Scores%20and%20Leaderboards.md)).
- **Use any VR table room as the lobby**: while playing a table that has a VR room, "Use this VR room in the lobby" in the menu makes the lobby load the room of that table (its walls, floor and scenery, without the table or its cabinet), so the tables are chosen in that room; "Use the default lobby room" in the MENU tab brings back the plain lobby.
- **Table picker** in the in-game menu ("Tables"), which is also the start screen in launcher mode: choose and switch tables without leaving the player.
- **Lists of tables** as tabs: All, Recent, Newly added, Most played, Favorites, plus a MENU tab for the library and application options. The tabs are large buttons, easy to hit with a mouse or a VR pointer.
- **Thumbnails**: a grid of table images, loaded in the background with a spinner on each tile meanwhile; a list view is also available. A table without an image gets a screenshot when it is closed, and "Replace table image" takes a new one.
- **Favorites**: a star on each thumbnail, or "Add to favorites" in the table's page; the Favorites tab lists them.
- **Pagination**: 12 tables per page, with a pager above and below the list, so large libraries stay fast.
- **Search** as you type (a virtual keyboard in VR), a letter filter and A–Z / Z–A sort in the All tab, and play statistics (added, last played, times played) for the hovered table.
- **Table actions**: play, restart, rename, reset the table settings, delete, and use the table's VR room in the lobby.
- **Lobby** in launcher mode, from which tables are started and to which they return.
- **Web upload**: add tables and ROMs from a browser on the local network, with a pairing code that can be switched off, missing ROMs listed, and a link to the ROM folder. It can be on for the session only or always on (started with the application), and stays on while switching between the lobby and the tables.
- **Tables page in the browser**, the home page of the web server: the library as a grid of thumbnails with the picker's tabs and search, a star to add or remove a favorite, and a "..." menu on each table to set its **display name** (the name the picker shows, the files are not renamed) or **delete** it with all its files. With an empty library it links to the file manager, which stays one click away.
- **Missing ROM message** naming the files to add and where.
- **VR**: the menu on a panel standing in the room, in the table's direction, at an adjustable distance, used with the controllers' pointer; notifications above the menu.
- **No "controller detected" prompt for the VR controllers**: Steam Input also presents the Frame controllers as an Xbox gamepad; in VR, VPX no longer asks to set up a layout for it, since the controllers already work through OpenXR.
- **Steadier chrome, smoother edges**: specular anti-aliasing in the material shader stops the shiny parts from sparkling when the image is resampled every frame, on every platform; the headset builds also default to Standard FXAA, the only anti-aliasing within the Frame's budget at native resolution ([details](docs/Image%20Quality%20on%20the%20Steam%20Frame.md)).

## How to install

The game is not on the Steam store. It is installed on the Frame from a computer on the same network: the **installer app** finds the Frame, checks that it is ready, then downloads the latest version of the game and installs it, with its entry and artwork in the Steam library. The same app updates the game later. Nothing is installed on the computer itself.

### What you need

- A Steam Frame and a computer on the **same network**.
- A computer with **Windows 10 or 11** (64-bit), **macOS 12 or later** (Apple silicon or Intel) or **Linux**. Nothing else is needed: the installer app brings everything it uses.
- An internet connection: the app downloads the game (about 170 MB) from this repository's releases.
- Your own tables (`.vpx`) and, for the tables that need them, their ROMs: none are included.

### 1. Turn on Developer Mode on the Frame

1. On the Frame, open **Settings > System** and turn on **Enable Developer Mode**.
2. In the **Developer** section that appears, choose **Set User Password** and pick a password. The installer asks for it.

Keep the headset on (or awake) while installing: it leaves the network when it goes to sleep.

### 2. Download the installer app

| Computer | Download |
| --- | --- |
| Windows 10, 11 | [VPX-Frame-Installer-Windows.exe](https://github.com/SPD13/vpinball-steam-frame/releases/latest/download/VPX-Frame-Installer-Windows.exe) |
| macOS 12 or later | [VPX-Frame-Installer-macOS.zip](https://github.com/SPD13/vpinball-steam-frame/releases/latest/download/VPX-Frame-Installer-macOS.zip) |
| Linux (PC, Steam Deck) | [VPX-Frame-Installer-Linux-x86_64.tar.gz](https://github.com/SPD13/vpinball-steam-frame/releases/latest/download/VPX-Frame-Installer-Linux-x86_64.tar.gz) |
| Linux (ARM64) | [VPX-Frame-Installer-Linux-arm64.tar.gz](https://github.com/SPD13/vpinball-steam-frame/releases/latest/download/VPX-Frame-Installer-Linux-arm64.tar.gz) |

The links always give the latest version; they are also on the [releases page](https://github.com/SPD13/vpinball-steam-frame/releases). The app does not need updating with each new version of the game: it always installs the latest one.

### 3. Run the installer app

The app is not signed with a paid certificate, so Windows and macOS warn the first time it is opened:

- **Windows:** double-click `VPX-Frame-Installer-Windows.exe`. If Windows says "Windows protected your PC", choose **More info**, then **Run anyway**. (If the browser warns that the file is not commonly downloaded, choose to keep it.)
- **macOS:** open the `.zip` (Safari may already have done it) and move **Visual Pinball X Installer** to your Applications folder if you like, then double-click it. If macOS says it cannot verify the app ("Not Opened"), choose **Done**, open **System Settings > Privacy & Security**, scroll down to **Security**, choose **Open Anyway** next to the app's name and confirm. When the app asks to **find devices on local networks**, choose **Allow**: it is how it reaches the Frame.
- **Linux:** unpack the download and run the app, for example in a terminal: `tar -xzf VPX-Frame-Installer-Linux-x86_64.tar.gz && ./VPX-Frame-Installer`. It opens in your web browser; it quits when the page is closed.

The app has two steps.

**Step 1, connect to the Frame.** The app looks for the Frame on the network and shows it as found (`frame`, with its address). Type **the password set in step 1** and choose **Connect**. Four checks turn green:

1. **Frame found on the network**
2. **Developer Mode is on**
3. **Signed in to the Frame**
4. **Frame ready for the game** (Steam running, enough free space)

If a check turns red, it says what is wrong and what to do, for example: wake the headset, turn Developer Mode on, use the right password, or type the Frame's IP address (shown on the Frame in **Settings > Network**) in **Frame address**. Fix it, then choose **Check again**.

**Step 2, install.** The app shows what is on the Frame and the latest version on GitHub. Choose **Install**: the game is downloaded and sent to the Frame (a progress bar shows), then each step of the installation is listed. It ends with **Installed! Put the headset on and play.**

### 4. Play

On the Frame, open **Library > Non-Steam > Visual Pinball X - Steam Frame Edition**. The library artwork appears after Steam restarts (restarting the headset does it). The first launch may wait while Steam downloads "Steam Linux Runtime 4.0 ARM64".

> [!NOTE]
> **The Steam dashboard opens when the game starts.** The game is already running behind it: select **Resume Game** with the controller's pointer to close the dashboard and reach the lobby. This happens every time the game is started: SteamVR shows its dashboard over the games added outside the Steam store ("Non-Steam" games) when they start. There is no workaround for the moment.

### 5. Add tables

- **From a browser (easiest):** in the game, open the table picker. Under **Add tables from a browser**, turn **Wi-Fi upload** on, then open the address it shows from a phone or computer on the same network. Upload `.vpx` files, or a `.zip` of a table's folder to bring its backglass and other files along. Missing ROMs are listed there, with a link to the ROM folder.
- **Over ssh:** copy the tables to `~/Documents/VPinballX/Tables` on the Frame and the ROMs to `~/Documents/VPinballX/Tables/pinmame/roms`, for example:
  `scp "My Table.vpx" steamos@frame.local:Documents/VPinballX/Tables/`

### Updating

Open the installer app again and connect (step 1). When a newer version is out, step 2 shows **Update available** with a button **Update to &lt;version&gt;**: choose it. **Up to date** means there is nothing to do. Tables, settings, scores and play statistics are kept.

### Uninstalling

In the installer app, after step 1: **Uninstall the game from the Frame...** at the bottom of step 2, then **Uninstall**. It removes the game, its library entry and artwork; tick **Also delete its settings, scores and play statistics** to remove those too. The tables and ROMs in `~/Documents/VPinballX/Tables` are never deleted.

### Troubleshooting

| Problem | What to do |
| --- | --- |
| The Frame is not found, or "No answer from" its address | Wake the headset (put it on). Check that the Frame and the computer are on the same network (a guest Wi-Fi often keeps devices apart) and that Developer Mode is on. Type the Frame's IP address in **Frame address**. |
| "The Frame does not accept connections" | Developer Mode is off: turn it on (step 1). |
| "The password was refused" | Type the password set in **Settings > Developer > Set User Password**, or set a new one there. |
| "The Frame's identity has changed" | Expected after the Frame was reset: choose **Trust the Frame's new key**. |
| "Steam is not running on the Frame" | Put the headset on so it is in its normal mode, then choose **Check again**. |
| macOS: the app "cannot be opened" or is "Not Opened" | **System Settings > Privacy & Security > Open Anyway** (see step 3). If it cannot find the Frame, allow it in **System Settings > Privacy & Security > Local Network**. |
| The game is missing from the library | Look under **Library > Non-Steam**. Restart Steam on the Frame if it is still missing. |
| The Steam dashboard stays open when the game starts | Expected: select **Resume Game** (see step 4). |
| The game closes at once | Read its log on the Frame: `ssh steamos@frame.local cat .local/share/VPinballX/10.8/vpinball.log` |
| Something else | The installer app keeps a log of what it did: `installer.log` in `%LOCALAPPDATA%\vpx-frame-installer` (Windows), `~/Library/Caches/vpx-frame-installer` (macOS) or `~/.cache/vpx-frame-installer` (Linux). Attach it when you [report the problem](https://github.com/SPD13/vpinball-steam-frame/issues). |

Developer Mode stays on after the install: it is how the game is installed and updated. Whether the game keeps working with Developer Mode turned off again has not been checked.

### Installing with the scripts instead

Each release also has the game in an archive with installer scripts, for macOS 11 and earlier, for scripted installs, or to choose the name and id of the game in the library. The scripts need nothing extra either: on Windows, they use PowerShell and the `ssh` and `tar` commands that Windows includes; on macOS and Linux, a terminal. To update, download the new version and install it the same way; to uninstall, run `Uninstall on Windows.bat` or `./uninstall.sh` (`-Settings` / `--settings` also removes the settings, scores and statistics).

#### Scripts on Windows

Download `vpx-steam-frame-<version>.zip` from the [releases page](https://github.com/SPD13/vpinball-steam-frame/releases).

1. **Unpack the download:** right-click `vpx-steam-frame-<version>.zip` in your Downloads folder, choose **Extract All...**, then **Extract**. A folder of the same name opens.
2. **Double-click `Install on Windows.bat`** in that folder. If Windows warns that the file comes from the internet ("Windows protected your PC"), choose **More info**, then **Run anyway**.
3. A window opens and the installer starts. When asked, type **the password set in step 1** and press <kbd>Enter</kbd>: nothing shows while you type, this is normal. It is asked **twice**: once to send the game, once to install it.
4. The game is sent (about 165 MB, a progress line shows), then installed. The window ends with:

   ```text
   ==> Done
       On the Frame: Library > Non-Steam > "Visual Pinball X - Steam Frame Edition".
   ```

   Press a key to close the window.

- **If the Frame is not found** (`frame.local` is not found on the network), the installer asks for its IP address instead, shown in the Frame's network settings (for example `192.168.1.42`).
- **If something goes wrong**, the installer stops with a message saying what to check (see [Script messages](#script-messages)). Running it again is always safe.
- The Windows installer only uses what Windows includes: PowerShell, and its `ssh` and `tar` commands. Windows Subsystem for Linux (WSL) and Git Bash are not needed, but they can also run `install.sh`, as on macOS and Linux.

#### Scripts on macOS or Linux

Download `vpx-steam-frame-<version>.tar.gz` from the [releases page](https://github.com/SPD13/vpinball-steam-frame/releases).

The installer is `install.sh`, a script in the download that you run in a terminal.

**Open a terminal:**

- **macOS:** open **Terminal** (press <kbd>Cmd</kbd>+<kbd>Space</kbd>, type `Terminal`, press <kbd>Enter</kbd>).
- **Linux:** open your terminal application.

**Go to the folder of the download,** usually your Downloads folder:

```sh
cd ~/Downloads
```

**Unpack the download and enter its folder** (replace `<version>` with the version you downloaded; press <kbd>Tab</kbd> after typing `vpx-steam-frame-` to complete the name):

```sh
tar -xzf vpx-steam-frame-<version>.tar.gz
cd vpx-steam-frame-<version>
```

If your browser already unpacked the download into a folder (Safari can), skip `tar` and `cd` into that folder.

**Run the installer**, with the headset awake:

```sh
./install.sh
```

What happens next:

1. **The first time only**, ssh asks whether to trust the Frame: `Are you sure you want to continue connecting (yes/no/[fingerprint])?`. Type `yes` and press <kbd>Enter</kbd>.
2. ssh asks for **the password set in step 1** (`steamos@frame.local's password:`). Type it and press <kbd>Enter</kbd>: nothing shows while you type, this is normal.
3. The installer goes through its steps and prints each one. The first install sends about 400 MB, which takes a few minutes; updates only send what changed.

A successful install ends like this:

```text
==> Connecting to steamos@frame.local
    type the Frame's user password when asked (the one set in Developer Mode)
    Steam Frame found

==> Valve's devkit scripts

==> Game files
    -> /home/steamos/devkit-game/VisualPinball (this takes a while the first time)

==> Steam library
    registered as "Visual Pinball X - Steam Frame Edition"

==> Library artwork
    Visual Pinball X - Steam Frame Edition: artwork installed for appid ...

==> Done
    On the Frame: Library > Non-Steam > "Visual Pinball X - Steam Frame Edition".
```

If something goes wrong, the installer stops with a message saying what to check (see [Script messages](#script-messages)). Running it again is always safe.

- **If it cannot find the Frame** (`cannot connect to steamos@frame.local`), give the Frame's IP address instead, shown in the Frame's network settings: `./install.sh 192.168.1.42`.
- **If the terminal says `Permission denied`** when starting the installer, run it with `bash install.sh` instead.
- **To see the installer's options:** `./install.sh --help` (see also [Script options](#script-options)).

#### Script options

```sh
./install.sh [--name NAME] [--gameid ID] [--no-art] [user@]host                                  # macOS, Linux
powershell -ExecutionPolicy Bypass -File install.ps1 [-Name NAME] [-GameId ID] [-NoArt] [-Frame host]   # Windows
```

The options can also follow `Install on Windows.bat` when it is started from a terminal, for example `& '.\Install on Windows.bat' 192.168.1.42` in PowerShell.

- The host (`[user@]host`, `-Frame`) is the Frame's address: `frame.local` by default, or its IP address.
- `--name` / `-Name` changes the name of the game in the library. An empty name (`--name ''`, `-Name ''`) keeps the name Steam gives it, "Devkit Game: VisualPinball".
- `--gameid` / `-GameId` installs another copy beside the first one (letters, digits, `_` and `.` only). Pass the same id to the uninstaller.
- `--no-art` / `-NoArt` skips the library artwork.
- `--help` / `-Help` lists the options.
- An ssh key avoids typing the password at each install: run `ssh-copy-id steamos@frame.local` once (macOS, Linux).

#### Script messages

| Message | What to do |
| --- | --- |
| `cannot connect to steamos@frame.local`, `cannot send the game` | Wake the headset (put it on). Check that Developer Mode is on and that a password is set. Use the IP address instead of `frame.local`. |
| `ssh.exe is missing` (Windows) | In Windows **Settings > Apps > Optional features**, add **OpenSSH Client**, then install again. |
| `REMOTE HOST IDENTIFICATION HAS CHANGED` | The Frame was reset. Run `ssh-keygen -R frame.local` (or with the IP address) in a terminal (PowerShell on Windows), then install again. |
| `Steam did not register the game` | Steam is not running on the Frame: put the headset on so that it is in its normal mode, then install again. |

## Controllers

The Steam Frame controllers work out of the box. Each button has a default role, shown below; the roles can be changed in the menu, under **Input Settings**.

![The Steam Frame controllers with the role of each control: on the left controller, the view button opens the menu, the d-pad navigates the menu, the thumbstick nudges, the grip is the left magna-save; on the right controller, the menu button quits the table, B inserts a coin, A starts a game, X and Y are free, the thumbstick is the plunger, the grip is the right magna-save; the Steam buttons stay with SteamOS; underneath, the triggers are the flippers and the bumpers nudge](docs/img/steam-frame-controllers.svg)

| Control | Controller | During a game | In the menu |
| --- | --- | --- | --- |
| Trigger | Left | Left flipper; pulled further, also the staged (upper) flipper of the tables that have one | Click the item the controller points at |
| Trigger | Right | Right flipper; pulled further, also the staged flipper | Click the item the controller points at |
| Bumper | Left / right | Nudge the table to the left / to the right | |
| Grip | Left / right | Left / right magna-save | |
| Thumbstick | Left | Tilt it to nudge the table (the further, the harder) | Up / down: move the selection |
| Thumbstick click | Left | Align the view using the controllers (on / off) | |
| Thumbstick | Right | Plunger: pull the stick down (towards you) and let go | Left / right: change a value |
| Thumbstick click | Right | Launch ball (tables with a launch button) | Settings pages: reset to the defaults |
| D-pad | Left | | Move the selection; left / right changes a value |
| View button | Left | Open the menu | |
| Menu button | Right | Quit the table, back to the lobby | |
| A | Right | Start a game | Settings pages: save the changes |
| B | Right | Insert a coin | Settings pages: undo the changes |
| X, Y | Right | Not used: free to map to another action | |
| Steam button | Left / right | Kept by SteamOS, not used by the game | |

## More details

What the fork changes, file by file, which builds contain what, and how far each part has been tested: [Steam Frame Edition, the technical documentation](docs/Steam%20Frame%20Branch.md).

## Credits

- **Visual Pinball X**, by the Visual Pinball team and its contributors: [vpinball/vpinball](https://github.com/vpinball/vpinball), the upstream project this fork is built on and keeps merging.
- Created with [Claude Code](https://claude.com/claude-code).

The rest of this file is the upstream README.

# Visual Pinball

*An open source pinball table editor and simulator.*

This project was started by Randy Davis, open sourced in 2010 and continued by the Visual Pinball development team. This is the official repository.

## Features

- Simulates pinball table physics and renders the table with DirectX, OpenGL or [bgfx](https://bkaradzic.github.io/bgfx/overview.html)
- Simple editor to (re-)create any kind of pinball table
- Live editing of most content within the rendered viewport
- Table logic (and game rules) can be controlled via Visual Basic Script
- Over 1050 real/unique pinball machines from ~100 manufacturers, plus over 550 original creations were rebuilt/designed using the Visual Pinball X editor (over 3000 if one counts all released tables, incl. MODs and different variants), and even more when including its predecessor versions (Visual Pinball 9.X)
- Emulation of real pinball machines via [PinMAME](https://github.com/vpinball/pinmame) is possible via Visual Basic Script (Visual PinMAME), or via the libPinMAME-API/plugin
- Supports configurable camera views (e.g. to allow for correct display in virtual pinball cabinets)
- Support for Tablet/Touch input, Joypads, or specialized pinball controllers
- Support for Stereo3D output
- Support for Head tracking via BAM
- Support for VR/XR HMD rendering (including [PUP](https://www.nailbuster.com/wikipinup), [B2S](https://github.com/vpinball/b2s-backglass) backglass and DMD output support)
- Support for WCG/HDR rendering (for now only via the BGFX (D3D11/12) build)
- Support for Windows (x86), Linux (x86/Arm, incl. RaspberryPi and RK3588), macOS, iOS/tvOS, Android (the latter builds are also available via the respective app stores for free)
- Plugin system to drive/fuel all kinds of displays (DMD, backglass, etc), add custom/dynamically-changed content (PUP, Serum, etc), direct output framework (DOF), sensors, and much more (WIP)

## Download

All releases are available on the [releases page](https://github.com/vpinball/vpinball/releases).

## Documentation

Documentation is currently sparse. Check the [docs](docs) directory for various guides and references.

An [unofficial wiki](https://github.com/dekay/vpinball-wiki) is currently being developed. Community contributions, suggestions, and help are welcome to improve the resource for all users.

## How to build

Build instructions are available in the [make directory README](make/README.md).

## Participate

Read through our [contribution](CONTRIBUTING.md) rules carefully before opening PRs, please.
