# ViceBounce

Make Vice City bounce to your music.

<p align="center">
  <a href="https://www.instagram.com/p/DepL4Fptbyn/">
    <img src="docs/demo.gif" width="420" alt="Vice City bouncing to the beat">
  </a>
  <br>
  <a href="https://www.instagram.com/p/DepL4Fptbyn/">▶ Sound on: watch the full clip on Instagram</a>
</p>

<p align="center">
  <a href="https://github.com/tek-95/ViceBounce/releases/latest/download/ViceBounce-0.11.1.zip">
    <img src="https://img.shields.io/badge/Download-ViceBounce_0.11.1_·_Windows_x64-2ea44f?style=for-the-badge" alt="Download ViceBounce 0.11.1">
  </a>
  <br>
  <sub><a href="https://github.com/tek-95/ViceBounce/releases">all releases</a></sub>
  <br><br>
  <a href="https://buymeacoffee.com/tek95">
    <img src="https://img.shields.io/badge/Buy_me_a_coffee-FFDD00?style=flat&logo=buymeacoffee&logoColor=000000" alt="Buy me a coffee">
  </a>
  <a href="https://pay.cloudtips.ru/p/b0d7d75d">
    <img src="https://img.shields.io/badge/%D0%9F%D0%BE%D0%B4%D0%B4%D0%B5%D1%80%D0%B6%D0%B0%D1%82%D1%8C_%D0%B0%D0%B2%D1%82%D0%BE%D1%80%D0%B0-CloudTips-1E88E5?style=flat" alt="Поддержать автора через CloudTips">
  </a>
</p>

Squash and stretch cars, bikes, boats, aircraft and people — including Tommy, passengers and cutscene actors.

## Install

Requires **GTA Vice City** (**classic Steam** or the **retail 1.0 English** `gta-vc.exe`), **Windows 10/11 x64** and an **ASI loader** that loads plugins from `scripts`.

Download the ZIP from [Releases](https://github.com/tek-95/ViceBounce/releases), close the game and copy its `scripts` contents into the game's `scripts` folder. Keep your existing `ViceBounce.ini` when updating.

Other game versions (including the 1.1 patch) are currently unsupported. On startup the plugin checks the executable against byte signatures and disables itself, noting the reason in `ViceBounce.log`, if anything does not match.

To check an executable without launching the game, build `tools/VerifyExe.cpp` (`clang++ -std=c++17 tools/VerifyExe.cpp -o VerifyExe`, or `cl /std:c++17 /EHsc`) and run `VerifyExe "path\to\gta-vc.exe"`.

## Music

Press **F11** during gameplay and select your own **MP3, FLAC, WAV or OGG**. The importer analyzes the beat and prepares looping playback locally. No Python installation needed. Music is not included or uploaded. The game minimizes while you choose a file and returns when the importer closes.

Use **F9** to adjust alignment by ear. Automatic beat detection can make mistakes.

## Controls

| Key | Action |
| --- | --- |
| F6 | Cars and cutscenes |
| F8 | Bikes, boats and aircraft |
| F10 | Everyone, including Tommy |
| F11 | Import music |
| F7 | Reload settings |
| F9 | Align the beat |
| Page Up / Down | Manual BPM ±5; disables beat map |
| Home / End | Amplitude ±0.025 |

Settings save automatically. Ctrl+Shift+F11 stays available for OBS.

## Support me

[Buy me a coffee](https://buymeacoffee.com/tek95) · [Support from Russia · CloudTips](https://pay.cloudtips.ru/p/b0d7d75d)

More of my stuff: [tek-95.com](https://tek-95.com) · [Instagram](https://instagram.com/tek__95)

## Credits

Inspired by [CarbonBounce](https://github.com/giovannosaur/CarbonBounce). Built with [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk).

[MIT license](LICENSE.txt). Dependencies retain their own licenses. [Importer build instructions](importer/BUILD.md).
