# ViceBounce 0.19 — draft

- One ASI for classic Steam and retail 1.0 EN, with executable-specific address checks.
- Independent vegetation, building and street-object modes: F5, F4 and F3.
- Buildings deform vertically, with shared anchors for related parts and ground/terrain filtering.
- Street-light effects follow their moving objects.
- F11 music selection is available in gameplay, pause and the main menu.

Thanks to @mrpulpe99 for retail 1.0 support and verification in [#2](https://github.com/tek-95/ViceBounce/pull/2), and @mattpannella for proposing building bounce in [#1](https://github.com/tek-95/ViceBounce/pull/1). The building implementation in this release is the existing local implementation; PR #1 was not merged.

Maintainer preparation notes (remove before publishing):

- Preserve the current upstream README's demo, social links, support links and other-game credits. README baseline blob: `56052e476b1d7b4b37316adeca07ff94889edfa5`.
- Refresh upstream immediately before publishing to preserve any newer README edits.
- Replace the 0.11.1 download badge/asset link only once the new asset is uploaded. Keep downloads pointing to the existing release until then.
- The merged 0.19-dev ASI has passed the build and three test suites; in-game regression on Steam and retail is pending. The Deluxe RwMatrixMultiply crash remains unresolved.
- PR #1 can be closed with an explanation and a link to the newly published implementation after source publication.
