# Updates and developer announcements

The F11 panel shows a newer-version row in the HUD's money green (0,207,133, lifted to 60,236,164 for the thin panel type) and a separate From the developer block. View release opens the fixed GitHub Releases page. Read more appears only when the two-line summary is truncated and opens the full announcement. An optional announcement link adds its own named button on both screens. There is no Dismiss action; old DismissedAnnouncement INI values are ignored. Remove or replace the news in the server file to withdraw it.

The player remains compact: track / Change, then BPM / playback. The track name uses the measured width left of Change; a long name continues on a second line and only a name longer than two lines is shortened with "...". Notification-disable switches are absent from More settings. Existing advanced INI compatibility remains unchanged.

## Publish metadata

Endpoint: `https://raw.githubusercontent.com/tek-95/ViceBounce/main/update.ini`.

The repository-root update.ini is a local publication template until committed publicly. No version or announcement is automatically published by building the ASI.

```ini
schema=1
latest=1.0.0
announcement_id=controls-1.0.0
message=Controls updated in 1.0.0. Check the new hotkeys.
announcement_url=https://github.com/tek-95/ViceBounce#controls
announcement_label=View controls
```

- `latest`: stable major.minor.patch, optional v prefix; numeric comparison with the installed version.
- `announcement_id`: up to 40 letters/digits, hyphens, underscores or dots. Identifies editorial content; it no longer controls dismissal.
- `message`: up to 160 printable ASCII characters, no game font tokens (`~`). Empty message/id and cleared link fields remove news.
- `announcement_url`: optional HTTPS web URL, up to 1024 characters. Empty or missing means no link button. URLs require announcement text. Query strings, anchors and percent-encoded non-ASCII paths are supported. Use a normal DNS host and a valid optional port. Userinfo, executable/file/custom protocols, backslashes, control bytes and malformed escaping are rejected.
- `announcement_label`: optional button text, up to 20 printable ASCII characters, no game font tokens. Defaults to Open link. A nonempty label requires a URL.
- Total payload at most 4 KiB. Duplicate/unknown fields or malformed content are rejected. Older metadata with only the four original fields still parses.

The URL is passed directly to Windows' browser opener only after the user selects its button and presses Enter. Metadata cannot run a command or launch a custom URL protocol. Opening a link closes the mod panel and releases its controls before handing focus to the browser. It does not install or download an update automatically. The release-page URL remains fixed and independent of announcement URLs.

## Network and caching

A worker thread performs bounded HTTPS requests to the fixed raw.githubusercontent.com metadata endpoint when the panel is opened. No network work runs in DllMain or waits on the render thread. Normal certificate checks apply; metadata redirects are disabled. Resolve/connect/send/receive timeouts are 2 seconds each, with an additional response elapsed-time/byte cap.

`scripts/ViceBounceUpdate.cache` stores the last attempt, successful response and payload. The retry interval defaults to 24 hours, clamped to 1-168 hours; failures also receive a retry delay. Offline/invalid responses retain the last valid payload. Announcements expire from view seven days after the last successful fetch. A cached newer-version notice may remain visible. Missing or read-only cache storage does not stop the game.

The request uses a static ViceBounce user-agent and does not upload music, game files, paths or user identifiers. As with any HTTPS request, the hosting service receives the connecting IP address. Existing `[Online] Enabled=0` disables metadata requests.

Font renderer addresses are verified independently for classic retail 1.0, 1.1 and Steam. An unexpected font implementation disables the mod panel, with a log entry, without disabling the bounce effect. Font settings are restored after drawing.

## Validation

ViceBounceOnlineTests covers optional/legacy metadata, URL validation, offline cache retention, parser fuzz inputs, panel sizing, navigation, conditional link visibility and the three executable renderer tables. The live UI validation notes and captures are in ONLINE-VALIDATION.md and the release preview directory. No game files are bundled with the tests.

News layout uses conservative proportional-font wrapping, 16-unit text lines and a separate action gap. The announcement link and Read more are ordinary full-width menu rows below the paragraph: cyan text, the same 12-unit row and the same selection as every other item. Every selection is a cyan trapezoid that slides to the new row with fresh random corners, like the frontend's option highlight, without crossing letters of its own or the neighbouring rows. The panel's left corners take new random points only when the panel opens; the right corners stay on the screen edge. Short announcements have no redundant Read action. Typography and panel padding are defined in the shared native/preview menu model.
