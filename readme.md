[![Actions Status: Release](https://github.com/ThirteenAG/MaxPayne3.FusionFix/actions/workflows/msvc_x86.yml/badge.svg)](https://github.com/ThirteenAG/MaxPayne3.FusionFix/actions)

<p align="center">
  <a href="https://github.com/ThirteenAG/MaxPayne3.FusionFix" target="_blank"><img width="600" src="https://github.com/user-attachments/assets/6b150869-9e5c-4463-aeb8-18a1d19076b0"></a>
  <br />
  <a href="https://patreon.fusionfix.io/" target="_blank"><picture><source media="(max-width: 768px) and (prefers-color-scheme: dark)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-ffmp3-mobile-dark.svg"><source media="(max-width: 768px)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-ffmp3-mobile.svg"><source media="(prefers-color-scheme: dark)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-ffmp3-dark.svg"><img width="100%" src="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-ffmp3.svg"></picture></a>
  <br />
  <a href="https://github.com/sponsors/ThirteenAG"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/github-dark.svg"><img src="https://thirteenag.github.io/img/buttons/github.svg" width="250"></picture></a>
  <a href="https://ko-fi.com/thirteenag"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/kofi-dark.svg"><img src="https://thirteenag.github.io/img/buttons/kofi.svg" width="250"></picture></a>
  <a href="https://paypal.me/SergeyP13"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/paypal-dark.svg"><img src="https://thirteenag.github.io/img/buttons/paypal.svg" width="250"></picture></a>
  <a href="https://www.patreon.com/ThirteenAG"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/patreon-dark.svg"><img src="https://thirteenag.github.io/img/buttons/patreon.svg" width="250"></picture></a>
  <a href="https://boosty.to/thirteenag"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/boosty-dark.svg"><img src="https://thirteenag.github.io/img/buttons/boosty.svg" width="250"></picture></a><br><br>
  <a href="https://discord.gg/2ckFCS572Z" target="_blank"><img width="50" src="https://raw.githubusercontent.com/ThirteenAG/GTAIV.EFLC.FusionFix/refs/heads/master/installer/discord.svg"></a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://t.me/fusionfix" target="_blank"><img width="50" src="https://raw.githubusercontent.com/ThirteenAG/GTAIV.EFLC.FusionFix/refs/heads/master/installer/telegram.svg"></a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://www.youtube.com/@FusionFix10" target="_blank"><img width="50" src="https://raw.githubusercontent.com/ThirteenAG/GTAIV.EFLC.FusionFix/refs/heads/master/installer/youtube.svg"></a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://x.com/fusionfix10" target="_blank"><img width="50" src="https://raw.githubusercontent.com/ThirteenAG/GTAIV.EFLC.FusionFix/refs/heads/master/installer/x.svg"></a>
  &nbsp;&nbsp;&nbsp;
</p>

This projects aims to add new features and fix some issues in Max Payne 3. Also available for [GTA IV: The Complete Edition](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix#readme).

<p align="center">
  <img src="https://github.com/ThirteenAG/MaxPayne3.FusionFix/assets/4904157/e0cdd907-f554-4685-93f7-17183237fe56">
</p>

## Installation:

> [!NOTE]
> Install Max Payne 3: The Complete Edition (v1.0.0.255 and above required)
>
> **Download**: [MaxPayne3.FusionFix](https://github.com/ThirteenAG/MaxPayne3.FusionFix/releases/latest/download/MaxPayne3.FusionFix.zip)
>
> Unpack content of the archive to your **Max Payne 3** root directory.

> [!WARNING]
> Non-Windows users (Proton/Wine) need to perform a [DLL override](https://cookieplmonster.github.io/setup-instructions/#proton-wine).

> [!IMPORTANT]
> This fix was tested only with latest official update and latest [ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest/download/Ultimate-ASI-Loader.zip) (included in the archive).

---

### New menu options

Fusion Fix options are available under **Settings -> Fusion Fix Options**, immediately after Graphics. The page's **Restore Defaults** resets only Fusion Fix settings and saves them to CFG.

FOV, subtitle size, and outline size use sliders. SMAA is an On/Off toggle; blur and diagnostic SMAA modes are CFG-only settings. Changes save automatically to `plugins/MaxPayne3.FusionFix.cfg`. If saving fails, Fusion Fix tries the game folder, `%LOCALAPPDATA%/Rockstar Games/Max Payne 3/`, `%LOCALAPPDATA%/MaxPayne3.FusionFix/`, then `Documents/MaxPayne3.FusionFix/`. Missing folders are created. Startup loads the newest readable CFG across these locations, and subsequent saves keep that location when writable. Existing INI values are imported for settings without a CFG value; the CFG takes precedence afterward. Edit the CFG while the game is closed.

Menu labels and descriptions follow the game language in all ten supported languages. Separate UTF-8 GXT-layout files in `text/` are embedded at build time and extend native GXT lookup with English fallback. Plugins can register pages and translations through the [C menu API](docs/menu-api.md).

Xbox Rain Droplets has its own page after Fusion Fix when the updated plugin is installed. Its INI settings save to its own CFG, and its Restore Defaults affects only Rain Droplets.

### New options

> [!NOTE]
> Use the settings menus to change options while playing. SkipIntro takes effect on the next launch.

- **SkipIntro**, added an option to skip intro
- **HideSkipButton**, added an option to hide ![skip](https://i.imgur.com/vwELI93.png) in cutscenes
- **DisableGlobalLeaderboards**, prevents Hoboken Alleys coop map from crashing the game
- **OutlinesSizeMultiplier**, added an option to increase the size of subtitle text outlines
- **DisableDeviceChangeEvent**, fixes an issue when the game randomly enters pause menu
- **LightSyncRGB**, only Logitech hardware is supported, requires Logitech G HUB app

  ![LightSyncRGB](https://github.com/ThirteenAG/MaxPayne3.FusionFix/assets/4904157/64f8da07-eef0-4410-a412-a0d1c0e0f3e0)

  [**Watch full clip on YouTube**](https://youtu.be/-gucoqZh0mI)

- **ConsoleGamma**, emulates the gamma curve of the console versions, 1 is the Xbox 360 preset, 2 is the PlayStation 3 preset. Works on every renderer the game supports (DirectX 9, 10, 10.1 and 11), the preset can be switched at any time
- **SMAA**, adds enhanced subpixel morphological antialiasing as a post-processing effect

# Building

Run `premake5.bat` to generate the Visual Studio solution in `build`, then build it.

To deploy to your game automatically after each build, create a `.env` file in the repository root pointing `MAX_PAYNE_3_DIR` at the folder containing `MaxPayne3.exe`, then run `premake5.bat` again, for example:

    MAX_PAYNE_3_DIR=C:\Program Files (x86)\Steam\steamapps\common\Max Payne 3\Max Payne 3

The build then copies the built `MaxPayne3.FusionFix.asi` into the `plugins` folder of that game folder and debugging launches `MaxPayne3.exe` from there. The `.env` file is ignored by git, only a plugin that is already installed is replaced, and without the file nothing is copied.

# Contributing

If you have an idea for a fix, add a module with its implementation to [source](https://github.com/ThirteenAG/MaxPayne3.FusionFix/tree/main/source) directory and open a pull request. See [contributing.ixx](https://github.com/ThirteenAG/MaxPayne3.FusionFix/blob/main/source/contributing.ixx) for reference.

# Reporting issues

If you've encountered an issue, report it [here](https://github.com/ThirteenAG/MaxPayne3.FusionFix/issues).
