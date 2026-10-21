# SABounce

**GTA San Andreas, but everything bounces to your music.**

Cars, bikes, boats, aircraft, trains and people squash and stretch on the beat. It also covers people getting in and out of
cars, cutscene actors and props, replays, and every vehicle light (headlights, beams, sirens, the light pools on the road, the
police helicopter's searchlight). Only what you see is changed: physics, collisions and saves are untouched.

A port of [ViceBounce](https://github.com/tek-95/ViceBounce) (Vice City) to San Andreas.

## Install

You need three things:

1. **GTA San Andreas 1.0 US.** The current Steam release is a newer version (3.0) and has to be downgraded first. I used
   [gtasa-open-downgrader](https://github.com/xxanqw/gtasa-open-downgrader) (open source, MIT; a third-party tool, not part of
   SABounce). **If you live in Russia, turn on a VPN while you use it, otherwise you may run into problems.**
2. **An ASI loader**, so that the game loads `.asi` plugins from the `scripts` folder:
   [Silent's ASI Loader](https://github.com/GTAmodding/ASI-Loader). The downgrader above can download it for you: tick **ASI Loader** in its list of mods.
3. **Windows 10 or 11.**

Then:

1. From [Releases](../../releases) download **`SABounce-0.3.7.zip`**. This exact file, not `SABounce-source-0.3.7.zip`: the second
   one has no mod in it, only the code and licenses.
2. If the game is running, close it.
3. Drag the **`scripts` folder from the ZIP** (it holds `SABounce.asi`, `SABounce.ini` and `SABounceMusic.exe`) into your game's
   folder, next to `gta_sa.exe`. If Windows asks whether to merge the folders, say yes.
4. Start the game and load a save. Cars and other vehicles bounce right away.

To update, drag the `scripts` folder in again; when Windows asks about files with the same name, replace the `.asi` and the
`.exe` but skip `SABounce.ini` to keep your settings. To uninstall, delete the three files and the `SABounceMusic` folder
(the cache of imported tracks).

## Music

Press **F11** in the game and pick your own **MP3, FLAC, WAV or OGG**. The game minimizes while you choose the file and returns
when the importer closes. The importer finds the beat and prepares a looping track on your computer. Nothing is downloaded or
uploaded, and no music is included.

* **F9** aligns the squash to a beat you hear. Automatic beat detection can be wrong, and this is the fix.
* Without a track the bounce runs at the BPM from `SABounce.ini` (135 by default). Page Up / Page Down change it.
* The importer's windows are in English, or in Russian on a Russian Windows. Set the environment variable
  `SABOUNCE_LANG=en` or `ru` to force one.

## Controls

| Key | What it does | Default |
| --- | --- | --- |
| F6 | Cars, monster trucks, trailers | on |
| F8 | Bikes, quads, boats, aircraft, trains | on |
| F10 | Everyone on foot, including CJ | **off** |
| F11 | Import your music | |
| F9 | Align the beat by ear | |
| F7 | Reload `SABounce.ini` | |
| Page Up / Page Down | BPM ±5 (turns the beat map off) | |
| Home / End | Strength ±0.025 | |

People sitting in a vehicle bounce together with it even while F10 is off. Settings you change with the keys are saved to
`SABounce.ini`.

## If something does not work

* **No `scripts\SABounce.log` appears.** The game never loaded the plugin: check that the ASI loader is installed and that the
  three files are really in the `scripts` folder next to `gta_sa.exe`.
* **The log says `unsupported executable`.** Your `gta_sa.exe` is not 1.0 US. Downgrade it (see Install).
* **F10 or F11 also change the graphics.** [SkyGFX](https://github.com/aap/skygfx) uses the same keys by default. Open its
  `skygfx*.ini` files and set `keySwitch=0x00` and `keyReload=0x00` (or pick other keys).
* **Something else breaks.** `SABounce.log` lists everything the plugin hooked and why it turned a part off. Open an issue and
  paste it.

## Compatibility

Tested with SilentPatch, modloader, WidescreenFix, Project2DFX, III.VC.SA.LimitAdjuster and SkyGFX. SABounce checks every game
address it touches. If another mod has changed one, only that part switches itself off and the log says why.

Known limits: boats, quad bikes and monster trucks are hooked but were not checked on screen, and police sirens go through the
same code as headlights but were not captured on screen. Other versions of the game are refused at startup.

How it works, with all addresses and the problems found on the way: [docs/ENGINE_NOTES.md](docs/ENGINE_NOTES.md).

## Build

Visual Studio 2022 C++ build tools and [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) with `Plugin.lib` built for SA (set
`PLUGIN_SDK_DIR`), then run `Verify.ps1` (or `Build.ps1`). The importer builds with Python 3.12, see
[importer/BUILD.md](importer/BUILD.md).

## Credits and license

* The idea, effect math, beat map, playback and importer come from [ViceBounce](https://github.com/tek-95/ViceBounce) by tek-95
  (MIT), itself inspired by [CarbonBounce](https://github.com/giovannosaur/CarbonBounce).
* Built with [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) and safetyhook. San Andreas internals were cross-checked against
  [gta-reversed](https://github.com/gta-reversed/gta-reversed).
* The San Andreas port was written with an AI assistant (Claude) and tested in the real game.

Not affiliated with Rockstar Games. No game files, music or decompiled code are included.
[MIT license](LICENSE.txt). Dependencies keep their own licenses, see [ThirdParty/NOTICE.txt](ThirdParty/NOTICE.txt).

---

## По-русски

**GTA San Andreas, но всё прыгает под твою музыку.**

Машины, мотоциклы, лодки, самолёты, поезда и люди сжимаются и растягиваются в такт музыке. Это работает и для людей, которые
садятся в машину и выходят из неё, для актёров и предметов в катсценах, для повторов и для всех огней транспорта (фары, лучи,
мигалки, световые пятна на дороге, прожектор полицейского вертолёта). Меняется только то, что видно на экране: физика,
столкновения и сохранения не затронуты.

Это порт [ViceBounce](https://github.com/tek-95/ViceBounce) (Vice City) на San Andreas.

### Установка

Тебе нужны три вещи:

1. **GTA San Andreas 1.0 US.** Текущая Steam-версия новее (3.0), её сначала нужно даунгрейднуть. Я пользовался
   [gtasa-open-downgrader](https://github.com/xxanqw/gtasa-open-downgrader) (открытый исходный код, MIT; сторонняя программа,
   не часть SABounce). **Если ты в России, включи VPN на время работы с ним, иначе могут быть проблемы.**
2. **ASI loader**, чтобы игра загружала `.asi`-плагины из папки `scripts`:
   [Silent's ASI Loader](https://github.com/GTAmodding/ASI-Loader). Даунгрейдер может скачать его за тебя: отметь **ASI Loader**
   в его списке модов.
3. **Windows 10 или 11.**

Затем:

1. Из раздела [Releases](../../releases) скачай файл **`SABounce-0.3.7.zip`**. Именно его, а не `SABounce-source-0.3.7.zip`:
   во втором самого мода нет, только код и лицензии.
2. Если игра открыта, закрой её.
3. Перетащи **папку `scripts` из архива** (в ней `SABounce.asi`, `SABounce.ini` и `SABounceMusic.exe`) в папку игры, рядом с
   `gta_sa.exe`. Если Windows спросит про объединение папок, согласись.
4. Запусти игру и загрузи сохранение. Машины и остальной транспорт прыгают сразу.

Чтобы обновить мод, перетащи папку `scripts` ещё раз; когда Windows спросит про файлы с одинаковыми именами, замени `.asi` и
`.exe`, а `SABounce.ini` пропусти, чтобы сохранить настройки. Чтобы удалить мод, удали три файла и папку `SABounceMusic` (кэш
импортированных треков).

### Музыка

Нажми **F11** в игре и выбери свой файл **MP3, FLAC, WAV или OGG**. Пока ты выбираешь файл, игра сворачивается, а когда
импортёр закроется, возвращается сама. Импортёр находит ритм и готовит зацикленный трек на твоём компьютере. Ничего не
скачивается и не загружается, музыка в мод не входит.

* **F9** подгоняет сжатие под удар, который ты слышишь. Автоматическое определение ритма может ошибаться, это его исправление.
* Без трека эффект идёт в темпе из `SABounce.ini` (по умолчанию 135 BPM). Page Up / Page Down меняют его.
* Окна импортёра на английском, а на русской Windows на русском. Чтобы выбрать язык вручную, задай переменную среды
  `SABOUNCE_LANG=en` или `ru`.

### Клавиши

| Клавиша | Что делает | По умолчанию |
| --- | --- | --- |
| F6 | Машины, монстр-траки, прицепы | включено |
| F8 | Мотоциклы, квадроциклы, лодки, самолёты, поезда | включено |
| F10 | Все люди на ногах, включая CJ | **выключено** |
| F11 | Импорт твоей музыки | |
| F9 | Подогнать бит на слух | |
| F7 | Перечитать `SABounce.ini` | |
| Page Up / Page Down | Темп ±5 BPM (отключает карту ударов) | |
| Home / End | Сила эффекта ±0.025 | |

Люди, сидящие в машине, прыгают вместе с ней, даже когда F10 выключена. Настройки, которые ты меняешь клавишами, сохраняются в
`SABounce.ini`.

### Если не работает

* **Файла `scripts\SABounce.log` нет.** Игра не загрузила плагин: проверь, что ASI loader установлен и что три файла лежат в
  папке `scripts` рядом с `gta_sa.exe`.
* **В логе `unsupported executable`.** Твой `gta_sa.exe` не 1.0 US. Даунгрейдни игру (см. «Установка»).
* **F10 или F11 заодно меняют графику.** У [SkyGFX](https://github.com/aap/skygfx) по умолчанию те же клавиши. Открой его
  `skygfx*.ini` и поставь `keySwitch=0x00` и `keyReload=0x00` (или выбери другие клавиши).
* **Что-то ещё ломается.** В `SABounce.log` записано всё, что плагин перехватил, и почему он отключил какую-то часть. Открой
  issue и вставь его туда.

### Совместимость

Проверено с SilentPatch, modloader, WidescreenFix, Project2DFX, III.VC.SA.LimitAdjuster и SkyGFX. SABounce проверяет каждый
адрес игры, которого касается. Если другой мод изменил какой-то из них, отключается только эта часть, а лог объясняет почему.

Известные ограничения: лодки, квадроциклы и монстр-траки подключены, но на экране не проверялись; сирены полиции проходят через
тот же код, что и фары, но на экране не снимались. Другие версии игры мод отвергает при запуске.

Как это работает, со всеми адресами и найденными по пути проблемами: [docs/ENGINE_NOTES.md](docs/ENGINE_NOTES.md) (на английском).

### Сборка

Нужны инструменты сборки Visual Studio 2022 C++ и [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) с собранной `Plugin.lib`
для SA (задай `PLUGIN_SDK_DIR`), затем запусти `Verify.ps1` (или `Build.ps1`). Импортёр собирается на Python 3.12, см.
[importer/BUILD.md](importer/BUILD.md).

### Авторы и лицензия

* Идея, математика эффекта, карта ударов, воспроизведение и импортёр взяты из
  [ViceBounce](https://github.com/tek-95/ViceBounce) автора tek-95 (MIT), который сам вдохновлён
  [CarbonBounce](https://github.com/giovannosaur/CarbonBounce).
* Сделано на [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) и safetyhook. Внутренности San Andreas сверялись с
  [gta-reversed](https://github.com/gta-reversed/gta-reversed).
* Порт на San Andreas написан с помощью ИИ-ассистента (Claude) и проверен в настоящей игре.

Проект не связан с Rockstar Games. Файлов игры, музыки и декомпилированного кода в нём нет.
[Лицензия MIT](LICENSE.txt). У зависимостей свои лицензии, см. [ThirdParty/NOTICE.txt](ThirdParty/NOTICE.txt).
