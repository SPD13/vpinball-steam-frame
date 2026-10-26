# Visual Pinball X: Steam Frame Edition

An independent fork of [Visual Pinball X](https://github.com/vpinball/vpinball) that runs as a standalone app on the Valve Steam Frame: natively on the headset (SteamOS on ARM64, OpenXR, Vulkan), with no PC and no cable. It adds a table launcher that works from inside the headset, a table library and picker, a lobby, uploading tables and ROMs from a browser, scores and leaderboards, eye-tracked foveated rendering, and a menu made for the VR controllers. It keeps following the upstream project and merges its work.

**Status: Working and tested as a standalone app on the Steam Frame.**

**No tables or ROMs are included in this package**: bring your own. To learn more about Visual Pinball and find tables to download, see [VPForums](https://www.vpforums.org/); VR tables are in its [VR tables download section](https://www.vpforums.org/index.php?app=downloads&showcat=56).

> [!CAUTION]
> **This fork is not endorsed by, affiliated with, or supported by the Visual Pinball team or community.** It is developed with extensive AI-assisted coding, which is why it is kept apart from the official project. Please do **not** ask support questions about it in the Visual Pinball repositories, Discord servers or community forums: report problems and ask questions in [this fork's issues](https://github.com/SPD13/vpinball-steam-frame/issues). The official project is [vpinball/vpinball](https://github.com/vpinball/vpinball).

[![Visual Pinball X, Steam Frame Edition: the trailer](/docs/img/steam-frame-trailer.jpg)](https://youtu.be/YvKycmiTYIY)

**▶ [Watch the trailer](https://youtu.be/YvKycmiTYIY)** (2:11): the VR version, with the VR menu and table search, Steam Frame tuning and eye-tracked foveated rendering, uploading tables over Wi-Fi, player profiles and online scores, the installer and one-click updates.

## What the player gets

In the standalone builds (the Steam Frame app, and the standalone desktop builds):

- **Eye-tracked foveated rendering, exclusive to this fork and to the Steam Frame build**: full shading only around the point the eyes look at, following the headset's eye tracker, through a fragment shading rate image that the Frame's driver applies in its fast direct render path: 2–4 ms per frame on heavy tables at native 2160×2160 per eye, on by default (Medium); a "Foveated rendering" level (Off / Low / Medium / High), an "Eye-tracked" switch and a status line in the VR settings page. Not in upstream Visual Pinball, and inert on Windows and macOS, whose builds lack the Vulkan driver support and the bgfx patches ([full technical reference](/docs/Foveated%20Rendering%20on%20the%20Steam%20Frame.md)).
- **Dynamic resolution in the headset**: the rendering resolution follows the GPU time of the frames, between a minimum and the table's resolution, so heavy tables and a hot headset keep their frame rate instead of judder on head movements, and light tables keep the full sharpness; a switch, a target and a minimum in the VR settings page, with a status line.
- **Scores and leaderboards**: the score of every game played on a library table is recorded automatically, for the player chosen in the lobby ("Player: <name>"), with a result screen (rank, personal best, table record) when the lobby comes back, a Scores tab with the leaderboard of each table, the 10 best scores in each table's page, and a Scores page in the browser to filter, reassign, delete or clear scores (with confirmation) and manage the players ([details](/docs/Table%20Scores%20and%20Leaderboards.md)).
- **Lobby rooms**: the lobby is a real room around the player, built in Blender with its lighting baked into its textures, so it costs almost nothing per frame on the Frame. Three rooms are shipped: the **Game room**, a lounge at night with arcade cabinets, a pinball machine, a neon sign and a city skyline through a big window (the default); the **Modern room**, with an oak slat wall, cove lighting and a sofa; and the **Sunlit studio**, with sunbeams from a garden through tall windows, dartboards and a desk. The previous lobby, a floor of tiles, is kept as the **Empty room**. All are made from CC0 assets and artwork drawn for them ([credits](/steamframe/lobby-room/CREDITS.md), and [how they are made](/steamframe/lobby-room/DESIGNING-ROOMS.md) to design more).
- **Lobby room selector**: "Choose a lobby room", in the Menu and lobby section at the top of the VR & Room screen (and in the System screen), shows a picture of each room; the room picked is used at once and kept for the next starts.
- **Use any VR table room as the lobby**: while playing a table that has a VR room, "Use this VR room in the lobby" in the menu makes the lobby load the room of that table (its walls, floor and scenery, without the table or its cabinet), so the tables are chosen in that room; "Use the default lobby room" brings back the lobby room chosen with the selector.
- **Table picker** in the in-game menu ("Tables"), which is also the start screen in launcher mode: choose and switch tables without leaving the player.
- **Lists of tables** as tabs: All, Recent, Newly added, Most played, Favorites, plus a MENU tab for the library and application options. The tabs are large buttons, easy to hit with a mouse or a VR pointer.
- **Thumbnails**: a grid of table images, loaded in the background with a spinner on each tile meanwhile; a list view is also available. A table without an image gets a screenshot when it is closed, and "Replace table image" takes a new one.
- **Custom thumbnails**: give any table the picture you want in the table picker, such as the wheel image of a front-end setup, its backglass, a flyer or your own artwork. In the Tables page of the browser, "..." > **Upload thumbnail** on the table picks an image file on your computer; it is resized for the picker and replaces the table's image in the headset at once. Transparent images, like most wheels, keep their transparency.
- **Favorites**: a star on each thumbnail, or "Add to favorites" in the table's page; the Favorites tab lists them.
- **Search** as you type (a virtual keyboard in VR), a letter filter and A–Z / Z–A sort in the All tab, and play statistics (added, last played, times played) for the hovered table.
- **Table actions**: play, restart, rename, reset the table settings, delete, and use the table's VR room in the lobby.
- **Lobby** in launcher mode, from which tables are started and to which they return, in the room chosen with the lobby room selector.
- **Web upload**: add tables and ROMs from a browser on the local network, with a pairing code that can be switched off, missing ROMs listed, and a link to the ROM folder. It can be on for the session only or always on (started with the application), and stays on while switching between the lobby and the tables.
- **Tables page in the browser**, the home page of the web server: the library as a grid of thumbnails with the picker's tabs and search, a star to add or remove a favorite, and a "..." menu on each table to set its **display name** (the name the picker shows, the files are not renamed), **upload a thumbnail** from the computer, or **delete** it with all its files. With an empty library it links to the file manager, which stays one click away.
- **Full backup of tables, ROMs, settings and scores**: a Backup page in the browser, next to Files, downloads everything the Frame holds to your computer (tables with their backglasses and PuP packs, table images, ROMs, machine saves, favorites and names, settings, scores and players, ROM aliases), or only the items and tables you choose. Restore it after a fresh installation or onto another Frame, item by item and table by table: scores, players and favorites are merged with the ones already there ([how it works](#backup-and-restore)).
- **Missing ROM message** naming the files to add and where.
- **Tables that rename their game**: some tables, often mods, ask PinMAME for their game under another name (the Simpsons Treehouse of Horror mod asks for `simpprtyh` instead of `simpprty`) and tell you to add an alias to `VPMAlias.txt`. The game finds that alias by itself in the table's files (readme, script, ROM, colorization folders) and adds it, and a ROM shipped in a folder of its own (`MutedRom/`) is moved to the ROM folder. The aliases can be checked and edited in the browser, in the file manager's **⋮ > Advanced > PinMAME ROM aliases** (see [PinMAME ROM aliases](#pinmame-rom-aliases)).
- **Fixes for broken tables and ROMs**: some tables and games do not work as they are, because of a bug in their script or a setting of their ROM. The game ships fixes for the ones found so far, each with the record of what it changes and why. Tables you add get the fixes they need by themselves; the ones already on the Frame get them with **Patch table** in their menu, in the headset or in the browser. The table files themselves are never changed (see [How to patch a broken table?](#how-to-patch-a-broken-table)).
- **VR**: the menu on a panel standing in the room, in the table's direction, at an adjustable distance, used with the controllers' pointer; notifications above the menu.
- **No "controller detected" prompt for the VR controllers**: Steam Input also presents the Frame controllers as an Xbox gamepad; in VR, VPX no longer asks to set up a layout for it, since the controllers already work through OpenXR.
- **Steadier chrome, smoother edges**: specular anti-aliasing in the material shader stops the shiny parts from sparkling when the image is resampled every frame, on every platform; the headset builds also default to Standard FXAA, the only anti-aliasing within the Frame's budget at native resolution ([details](/docs/Image%20Quality%20on%20the%20Steam%20Frame.md)).
- **Updates from the headset**: when a newer version is published, a yellow badge above the menu says "New version available, Click here to install". Selecting it downloads the new version, checks it and installs it in place of the game, while the menu is replaced by the progress (the buttons do nothing meanwhile, so no table can be started); then the game exits, to be started again from the Steam library. Tables, settings, scores and play statistics are kept. The first install, and the versions before this feature, still go through the installer app.

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

- **From a browser (easiest):** in the game, open the table picker. Under **Add tables from a browser**, turn **Wi-Fi upload** on, then open the address it shows from a phone or computer on the same network. Upload `.vpx` files, or a `.zip` of a table's folder to bring its backglass and other files along. Missing ROMs are listed there, with a link to the ROM folder. Upload a table's download as it is: when its readme asks to add an alias to `VPMAlias.txt`, the game does it.
- **Over ssh:** copy the tables to `~/Documents/VPinballX/Tables` on the Frame and the ROMs to `~/Documents/VPinballX/Tables/pinmame/roms`, for example:
  `scp "My Table.vpx" steamos@frame.local:Documents/VPinballX/Tables/`

### Updating

Open the installer app again and connect (step 1). When a newer version is out, step 2 shows **Update available** with a button **Update to &lt;version&gt;**: choose it. **Up to date** means there is nothing to do. Tables, settings, scores and play statistics are kept.

Or update from the headset: when a newer version is out, select the yellow **New version available, Click here to install** badge above the menu, wait for the download and the install, select **OK**, then start the game again from the Steam library.

### Uninstalling

In the installer app, after step 1: **Uninstall the game from the Frame...** at the bottom of step 2, then **Uninstall**. It removes the game, its library entry and artwork; tick **Also delete its settings, scores and play statistics** to remove those too. The tables and ROMs in `~/Documents/VPinballX/Tables` are never deleted. To keep everything for a new installation, make a [backup](#backup-and-restore) first.

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
| A table does not start, and its log says "Game name not found" | The table asks PinMAME for a game name it does not know, usually a mod that renames its game. In the browser, open the file manager, **⋮ > Advanced > PinMAME ROM aliases**: the tables in this case are listed at the top. Add a line `name,game` with the game from the table's readme (for example `simpprtyh,simpprty`), save, and start the table again. Its ROM (`game.zip`) must be in the ROM folder. See [PinMAME ROM aliases](#pinmame-rom-aliases). |
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

## Backup and restore

A fresh installation of the Frame, or a new headset, starts with an empty library. Back up what you keep on the Frame to your computer, and restore it there later.

### What a backup holds

| Item | Content |
| --- | --- |
| Tables | The tables and the files along them: backglasses, music, PinUP Player packs |
| Table images | The images shown by the table picker |
| ROMs | PinMAME ROMs, with the colorizations and the sound packs of the games |
| Machine saves | High scores and settings saved by the games (PinMAME nvram, VPReg) |
| Library | Favorites, table names and play counts |
| Settings | Settings of the application and of the tables, the lobby room |
| Scores and players | Leaderboards and player profiles |
| ROM aliases | The PinMAME alias file |

### Making a backup

1. In the game, open the table picker. Under **Add tables from a browser**, turn **Wi-Fi upload** on, then open the address it shows from a computer on the same network.
2. Open **Backup**, next to Files. Tick the items to save; each one shows its size. Keep **All tables**, or pick **Choose tables** to select them one by one (a table's ROM, saves and settings follow it).
3. Select **Create backup**. The page checks the files, then the browser downloads `VPX-Backup-<date>.vpxbackup`.

The backup is made while it downloads, so it takes no space on the Frame, even for tens of gigabytes. A broken download can be resumed from the browser's downloads list. The file is a zip. It is named `.vpxbackup` so that browsers do not extract it. Restore the file as it was downloaded: a zip made again from its content is refused.

### Restoring a backup

1. On the **Backup** page, under **Restore a backup**, select **Choose a backup...** and pick the `.vpxbackup` file. The browser reads it, without uploading it whole, and shows its items and tables.
2. Tick what to bring back: all of it, some items, or only some tables.
3. Select **Restore**, then confirm. Only the selected files are uploaded.

What happens to what is already on the Frame:

- **Files** (tables, images, ROMs, saves) replace the ones with the same name. Files already there with the same size and date are skipped, so an interrupted restore can simply be started again.
- **Scores, players, favorites and table names** are merged with the Frame's. Tables are matched by their file, so scores find their table again after a reinstallation.
- **Settings** apply when the next table or the lobby loads. The Frame's own settings are kept: folders, displays, audio, controllers, the position of the table in the room, and the Wi-Fi upload settings.

The page refuses to back up or restore while a table is being played: close it first (the lobby is fine). Keep the headset awake during a backup or a restore, because it leaves the network when it sleeps. The game also asks the Frame not to sleep while a transfer runs. More details: [Steam Frame Branch](/docs/Steam%20Frame%20Branch.md), section 16.

## PinMAME ROM aliases

A table that emulates a real machine asks PinMAME for its game by the name written in its script (`cGameName`). Some tables use a name that is not PinMAME's own: an older or shorter name (`simp` for `simp_a27`), or, for many mods, a name of their own (the Simpsons Treehouse of Horror mod asks for `simpprtyh`, which is the game `simpprty`). An **alias** tells PinMAME which game to run for that name. Without one, the table loads but does not play: PinMAME stops with "Game name not found". Original tables, which do not use PinMAME, never need an alias.

### What the game does by itself

- **The standard aliases** of Visual PinMAME (about 120, such as `simp,simp_a27`) are installed with the alias file, the first time the game starts.
- **Aliases of mods**: when a table asks for a name PinMAME does not know, the game looks for its alias in the table's files: an `alias,game` line in the readme of the download (`.txt`, `.md`, `.nfo`, `.rtf`), a game the script names (`.Games("startrp2")`), a ROM archive or a colorization folder named after a game, or a game the name starts with. It adds the line it found to the alias file, with a comment saying where it came from. A ROM shipped in a folder of its own (`MutedRom/simpprty.zip`) is moved to the ROM folder.
- **When nothing is found**, the table is reported: a yellow warning on the **Tables** page of the browser names it, its tile gets an **Unknown game** badge, and starting it in the headset shows a message saying it needs an alias.

So upload a table's download as it is (the `.zip` with its readme), and let the game find the alias first.

### Where the aliases are

| File on the Frame | Used by |
| --- | --- |
| `~/Documents/VPinballX/Tables/pinmame/alias.txt` | All the tables |
| `pinmame/alias.txt` in a table's folder | That table only, when its folder has its own `pinmame/roms` folder (PinMAME then uses that folder instead of the shared one) |

The file has one alias per line, `name,game`: `name` is what the table asks for, `game` is the PinMAME game, whose ROM is `game.zip` in the ROM folder (`~/Documents/VPinballX/Tables/pinmame/roms`). Lines starting with `#` are comments. A line can not be longer than 127 characters.

### Setting an alias

1. **Find the game.** The table's readme or download page usually gives it ("add `simpprtyh,simpprty` to `VPMAlias.txt`"). Otherwise, the table's backglass file (`.directb2s`, a text file) often names it in `<GameName Value="...">`.
2. **Open the alias editor in a browser.** In the game, open the table picker and, under **Add tables from a browser**, turn **Wi-Fi upload** on, then open the address it shows. On the **Tables** page, click the yellow warning or a table's **Unknown game** badge. The editor is also in the file manager (**Files**), under **⋮ > Advanced > PinMAME ROM aliases**.
3. **Add the line.** The editor lists the tables asking for a game PinMAME does not know: **Add a line** next to one adds `name,` at the end of the file; type the game after the comma (for example `simpprtyh,simpprty`).
4. **Save.** A line PinMAME would misread (a single name, or more than 127 characters) is refused, with its line number. A saved alias applies the next time the table starts.
5. **Add the ROM** of the game (`game.zip`) if it is missing: upload it in the file manager's **ROMs** tab, or copy it to the ROM folder over ssh.

Over ssh, the file can also be edited directly, for example `ssh steamos@frame.local 'echo "simpprtyh,simpprty" >> Documents/VPinballX/Tables/pinmame/alias.txt'` (the file must end with a new line before you add one). More details: [PinMAME ROM aliases](/docs/PinMAME%20ROM%20Aliases.md).

## How to patch a broken table?

Some tables do not work on the Frame as they are, even with their ROM: they close as soon as they start, or a game can not be started. Often the cause is not the game: a bug in the table script that recent versions of Visual Pinball refuse, or a ROM that needs to be set up from its service menu before it gives credits. For the ones found so far, the game ships a **fix**: a recipe that says which tables or ROMs it applies to, what it changes and why. New fixes come with the updates of the game.

The fixes shipped today:

| Table or ROM | Without the fix | What the fix does |
| --- | --- | --- |
| VR ROOM Back to the Future (Data East 1990) | The table closes as soon as it starts, back to the lobby, as if it crashed | Removes a second `vpmInit me` from its script, which made the table stop itself |
| Gottlieb System 3 games: Street Fighter II (`sfight2`), Stargate (`stargat5`), Super Mario Bros (`smb`), Super Mario Bros Mushroom World (`smbmush`) | Coins are accepted, but the display stays at CREDITS 0 | Loads the factory settings of the game from its service menu, as an operator would |

The full record of each fix (the cause, the exact change, how it was checked) is in [Table and ROM Fixes](/docs/Table%20and%20ROM%20Fixes.md).

### Tables you add

Nothing to do: a table added to the library, from the browser or by copying it over ssh, gets the fixes it needs as soon as the game finds it. Its tile on the **Tables** page of the browser then shows a green **Patched** badge.

### Tables already on the Frame

The tables that were on the Frame before a fix was released are not changed by themselves: you choose when to patch them. A table that has a fix available shows it:

- **In the headset**: open the table's page in the table picker. It shows **Patch table** (a wrench), and **Patched for compatibility: ...** once applied. Point at **Patch table** to read, in its tooltip, which fixes it applies and their state.
- **In the browser** (turn **Wi-Fi upload** on under **Add tables from a browser** in the table picker, then open the address it shows): on the **Tables** page, the table's tile shows a purple **Fix available** badge, and its **...** menu has **Patch table**.

To patch it:

1. Select **Patch table**, in the headset or in the browser. A message says what was applied.
2. Start the table (or restart it, if it was being played: a fix applies when the table starts).

That's all for a script fix. For a ROM fix (the Gottlieb System 3 games), the next start does the rest:

3. The table starts normally, then a notice says *"Patching this table for compatibility: its game loads its factory settings (one time only, about 40 seconds). Please don't press any button until the game restarts."* The game goes through its service menu by itself: the display shows its test and adjustment screens.
4. Don't press anything until the notice says *"Table patched for compatibility: the game restarts"*. The flippers and Start are not passed to the game meanwhile, so a press by mistake does not change a setting.
5. The game boots again, with its new settings: insert coins and play. If the table is closed before the end, the fix runs again at its next start.

A ROM fix keeps a copy of the machine's previous save (high scores, settings) as `<rom>.nv.bak-<date>` in `~/Documents/VPinballX/Tables/pinmame/nvram/`. A ROM without any save yet (a game never played on the Frame) gets its fix by itself, at its first start.

### Seeing every fix and its state

In the browser, the file manager (**Files**) has **⋮ > Advanced > Table and ROM fixes**. It lists:

- **Tables**: each table of the library that a fix applies to, with the state of each fix and a **Patch table** or **Re-patch table** button.
- **Available fixes**: every fix shipped with the game (and your own, below), with its symptom, its cause, what it changes, the tables or ROMs it applies to, and a link to its full record.

The state of a fix for a table:

| State | Meaning |
| --- | --- |
| Available | The fix applies to this table and is not applied yet: use **Patch table** |
| Applied | Applied, with the latest version of the fix |
| Update available | A newer version of the fix came with an update of the game: use **Re-patch table** |
| At the next start | A ROM fix waiting for the table to start |
| Not needed | The table looks like one the fix is for, but its script does not have the bug: often a newer version of the table, fixed by its author. Nothing is changed |

**Re-patch table** applies the fixes again. It is safe: a script fix is rebuilt from the table's original script, and a ROM fix loads the factory settings again (after keeping a copy of the save).

### What a fix changes on the Frame

- **A script fix** never changes the `.vpx` file. It writes the patched script next to the table as `<table name>.vbs`, a file that Visual Pinball always loads instead of the script inside the table. When the table came with its own `.vbs`, that one is kept as `<table name>.vbs.orig`, and the fix applies to it.
- **A table replaced by a new version** (uploaded again) is checked again: the patched script made for the old version is removed, so it never replaces the new script, and the fix is applied again only if the new version still needs it.
- **What was applied** is kept in `table-fixes.json`, in the game's settings folder (`~/.local/share/VPinballX/10.8/` on the Frame).
- **The log** (`~/.local/share/VPinballX/10.8/vpinball.log`) has a line for everything the fixes do, starting with `[Fixes]` or `ROM fix`.

To undo a script fix, delete the table's `.vbs` over ssh or in the file manager, and rename `.vbs.orig` back to `.vbs` if there is one. To undo a ROM fix, close the table and rename the `.nv.bak-<date>` file back to `<rom>.nv`.

### A table that is still broken

1. **Check the usual causes first**: a missing ROM (the game says so when the table starts, and the **ROMs** tab of the file manager lists the missing ones), or a game PinMAME does not know (an **Unknown game** badge, see [PinMAME ROM aliases](#pinmame-rom-aliases)).
2. **Read the end of the log** after the table failed: `ssh steamos@frame.local tail -200 .local/share/VPinballX/10.8/vpinball.log`, or the **Log Stream** of the file manager. A table that "crashes" at start is often a script that stops itself: look for a `MsgBox prompt=` line or a script error just before `Closing player`. The message a table shows on a computer only goes to the log on the Frame.
3. **Report it**, with the name and version of the table, where it comes from, and the end of the log: a fix found for one table is shipped to everyone with the next update.

### Writing your own fix

Fixes can also be added on your Frame, without waiting for an update: write them in `fixes.json` in the game's settings folder (`~/.local/share/VPinballX/10.8/fixes.json` on the Frame), then restart the game. A script fix replaces a text of the table script with another one:

```json
{
  "recipes": [
    {
      "id": "my-table-double-init",
      "version": 1,
      "title": "My table: vpmInit called twice",
      "symptom": "The table closes as soon as it starts",
      "cause": "Its init calls vpmInit twice, which core.vbs refuses",
      "change": "The second call is commented out",
      "match": { "files": ["my table"] },
      "script": [ { "find": "\tBackdrop_Init\n\n\tvpmInit me\n", "replace": "\tBackdrop_Init\n\n\t' vpmInit me\n" } ]
    }
  ],
  "disable": []
}
```

- `match.files`: a part of the table's file name, ignoring case (or `match.roms`, the PinMAME ROM names).
- `find` must be found exactly once in the script (or `count` times), otherwise nothing is changed and the fix shows as **Not needed**. Write line ends as `\n` and tabs as `\t`: they match the script whatever its line ends. To get the script of a table, run the table once on a computer with Visual Pinball (`-ExtractVBS <table.vpx>` writes it next to the table).
- A fix with the `id` of a shipped one replaces it, and `"disable": ["gts3-zero-settings"]` turns a shipped fix off.
- The file is saved by the **Backup** page, with the settings.

Your fixes show in **⋮ > Advanced > Table and ROM fixes**, marked **local**, and apply like the shipped ones: by themselves to the tables you add, with **Patch table** to the others. The format, including ROM fixes, is described in [Table and ROM Fixes](/docs/Table%20and%20ROM%20Fixes.md#how-it-works). A fix that works for you is worth sharing, so that it ships with the game.

## Controllers

The Steam Frame controllers work out of the box. Each button has a default role, shown below; the roles can be changed in the menu, under **Input Settings**.

![The Steam Frame controllers with the role of each control: on the left controller, the view button opens the menu, the d-pad navigates the menu, the thumbstick nudges, the grip is the left magna-save; on the right controller, the menu button quits the table, B inserts a coin, A starts a game, X and Y are free, the thumbstick is the plunger, the grip is the right magna-save; the Steam buttons stay with SteamOS; underneath, the triggers are the flippers and the bumpers nudge](/docs/img/steam-frame-controllers.svg)

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

What the fork changes, file by file, which builds contain what, and how far each part has been tested: [Steam Frame Edition, the technical documentation](/docs/Steam%20Frame%20Branch.md).

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

Documentation is currently sparse. Check the [docs](/docs) directory for various guides and references.

An [unofficial wiki](https://github.com/dekay/vpinball-wiki) is currently being developed. Community contributions, suggestions, and help are welcome to improve the resource for all users.

## How to build

Build instructions are available in the [make directory README](/make/README.md).

## Participate

Read through our [contribution](/CONTRIBUTING.md) rules carefully before opening PRs, please.
