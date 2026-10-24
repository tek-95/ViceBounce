# ViceBounce 0.19 integration notes

Based on upstream merge `2d3ffc1a258ce6f0f6e1cfa8268a47f00df5b82d` (PR #2 by mrpulpe99), with the unpublished local 0.18.3 functionality preserved.

One ASI now selects Steam or 1.0 EN addresses. The PR's executable layout verification and plain-vector light ABI are retained. The current building, vegetation, street-object and menu music-picker code is retained. Additional retail scenery vtables, light callsites, ground trace, pool references and menu calls are checked before installing hooks. Steam keeps its previous checks and address values.

Validation completed:

- Visual Studio Win32 Release build.
- Current effect/math/restoration tests and music playback tests.
- Original PR compatibility/layout tests, using the current headers.
- Offline inspection of the Deluxe 1.0 executable: eight scenery vtable slots and sixteen additional light calls match their expected functions; additional ground/pool/wind/menu addresses were inspected against the executable and SDK.

The user confirmed gameplay with the same diagnostic ASI on both Steam and Deluxe 1.0 EN. The earlier Avery-to-Malibu crash did not recur on the integrated build; its original root cause was not established. The release build disables the optional stack/watchdog logger and changes the startup version label; effect logic is unchanged. All three C++ suites and all twelve current importer tests passed for release preparation. The importer EXE matches the one used in the gameplay tests.

The 0.19 ZIP is prepared locally and has not been published. Test-installation backup and validation reports are stored outside this repository under outputs/DeluxeTest and outputs/Releases/ViceBounce-0.19.

Run `Verify.ps1` to rebuild and run all three test executables. Use `Build.ps1 -Diagnostics` when collecting crash stacks during further testing.
