# SABounce: how it works in the game

Notes for anyone who wants to continue this or port it to another version. Everything here was checked against
`gta_sa.exe` **1.0 US** (the executable the Steam "Downgrader" produces, MD5 `170b3a9108687b26da2d8901c6948a18`) and the
Plugin-SDK / gta-reversed headers. Addresses are for that executable only; SABounce checks every one of them at startup
and switches the affected part off if another mod has changed it.

## Hook points

| What | Address | How |
| --- | --- | --- |
| All vehicles and non-driving peds are drawn here: `CRenderer::RenderOneNonRoad(CEntity*)` | `0x553260` | safetyhook inline hook, vehicles are deformed right before the original runs |
| Ped render: `CEntity::Render` calls inside `CPed::Render` | `0x5E77FC`, `0x5E780A` | Plugin-SDK `pedRenderEvent.before` |
| End of the 3D frame (restore everything) | `0x53E293` inside `Render2dStuff` | `drawingEvent.before` |
| Per-class vehicle `PreRender` (vtable slot 17) | CAutomobile `0x871120`, CBike `0x871360`, CBmx `0x871528`, CBoat `0x8721A0`, CHeli `0x871680`, CMonsterTruck `0x8717D8`, CPlane `0x871948`, CQuadBike `0x871AE8`, CTrailer `0x871C28`, CTrain `0x872370` | vtable patched: marks the "lighting vehicle" so light calls can follow the body |
| `CCoronas::RegisterCorona` | `0x6FC180` | mid hook; rewrites the position pointer and size |
| `CPointLights::AddLight` | `0x7000E0` | mid hook; position, direction, radius |
| `CShadows::StoreCarLightShadow` | `0x70C500` | mid hook; road light pools |
| `CHeli::AddHeliSearchLight` | `0x6C45B0` | mid hook; the cone origin |
| `CVehicle::DoHeadLightBeam` | `0x6E0E20` | inline hook; passes a deformed matrix copy |

## Why it works

* A vehicle or ped has a `RwFrame` whose matrix is what gets rendered; physics and collision use the entity's own `CMatrix`.
  Scaling the frame matrix just before rendering and restoring it after the 3D pass therefore changes only what you see.
* Squash/stretch is `B * S * B^-1` (S scales the local right/up by `h` and at by `v`, with `h + v = 2`), so a tilted car or ped
  deforms along its own axes. The lowest wheel (or bone) is kept on the ground by comparing the lowest point before and
  after the deformation.
* Ped skeletons are **world-space** HAnim matrices in SA (flags `0x3000`), so the bone matrices are deformed with the same
  transform and restored afterwards. Weapons are attached later from the deformed hand bone and follow it.
* Cutscene actors and props are `CCutsceneObject`s that often share one origin; each gets an upright basis around its animated
  root bone (or its geometry centre). A prop attached to a humanoid shares that humanoid's transform, so it stays in the hand.
* A ped in a vehicle-entry task (open door, get in/out, shuffle, drag, jack) has no vehicle yet or any more. The vehicle is found
  from the ped's simplest active task by matching the task's fields against the live vehicle pool; task ids 802-808, 813, 814,
  817, 818, 820, 821, 826 and 834 are used. 801 (`TASK_SIMPLE_CAR_ALIGN`, the walk-up) is deliberately excluded.
* Coronas are registered attached to the vehicle in **object space** and transformed with the undeformed matrix at render
  time, so their local position is deformed directly (`DeformLocal`). Beams, point lights and road shadows are in world space.

## Gotchas

1. `III.VC.SA.LimitAdjuster` (shipped with Project2DFX) overwrites `RegisterCorona` with a jump to its own copy **after** other
   ASIs have initialised, which silently killed an init-time hook. SABounce installs its light hooks on the first drawing tick
   and accepts a foreign `E9` at the entry (safetyhook relocates it and the chain works).
2. Other plugins may already have replaced `CAutomobile::PreRender` or `CPlane::PreRender`; those slots are patched as a chain.
3. Never construct a `CMatrix` temporary for a copy: its destructor can free an attached `RwMatrix`. Copy the raw bytes and clear the attach pointer.
4. Hotkeys use `GetAsyncKeyState`, so tools that inject scancodes (some automation) do not trigger them.
5. The game can stop its clock (cutscene to gameplay): a recorded bounce keeps a constant 135 BPM within one stretch but the phase
   shifts between stretches. For video work, measure the phase per scene instead of assuming one grid for the whole clip.
6. The importer is the upstream ViceBounce importer (numpy + soundfile, PyInstaller). It writes `SABounceMusic/<hash>/audio.wav`
   and `beats.txt` next to the ASI; nothing is uploaded.

## Not verified

* Steam 3.0 / other executables (the mod refuses to start on them).
* Boats, quad bikes and monster trucks were hooked but not looked at on screen.
* Police sirens: the code path is the same as headlights (PreRender + RegisterCorona) and the hooks fire, but the siren cars were not captured on screen.
