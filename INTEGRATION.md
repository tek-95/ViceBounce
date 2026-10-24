# Local 0.19 development snapshot

Based on upstream merge `2d3ffc1a258ce6f0f6e1cfa8268a47f00df5b82d` (PR #2 by mrpulpe99), with the unpublished local 0.18.3 functionality preserved.

One ASI now selects Steam or 1.0 EN addresses. The PR's executable layout verification and plain-vector light ABI are retained. The current building, vegetation, street-object and menu music-picker code is retained. Additional retail scenery vtables, light callsites, ground trace, pool references and menu calls are checked before installing hooks. Steam keeps its previous checks and address values.

Validation completed:

- Visual Studio Win32 Release build.
- Current effect/math/restoration tests and music playback tests.
- Original PR compatibility/layout tests, using the current headers.
- Offline inspection of the Deluxe 1.0 executable: eight scenery vtable slots and sixteen additional light calls match their expected functions; additional ground/pool/wind/menu addresses were inspected against the executable and SDK.

This is a local development build, not a release. It has not replaced either installed ASI, has not been pushed, and has not had an in-game regression test on either executable. The earlier Deluxe `RwMatrixMultiply` crash remains unresolved; this integration is not a crash fix. The previous diagnostic build remains available in the Deluxe test installation.

Run `Verify.ps1` to rebuild and run all three test executables. Do not replace the installed Steam build before runtime regression testing.
