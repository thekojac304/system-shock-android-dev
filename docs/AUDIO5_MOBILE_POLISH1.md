# AUDIO5 + MOBILE-POLISH1 handoff

Status: tuning pass implemented; host tests and ARM64 QA build pass; hardware retest pending. Initial Android testing found weak Music slider response above 50% and touch assist too subtle on small legacy controls. This batch has no ambient media. It does not claim to resolve the previously investigated delayed audio after long backgrounding.

## Implemented audio settings

The existing full-surface touch settings overlay has an **Audio Settings** page. The four volume rows accept a tap anywhere in the row and live dragging. Original/Remake, Dynamic Music, and Enhanced Ambience are large toggle rows. Changes apply immediately and are saved to `prefs.txt` on slider release or toggle. The original game Sound screen and its music/SFX controls remain available. The page snapshots their current legacy values when opened, and writes through their original globals and quest variables; it does not add a competing music/SFX volume store.

`ShockPrefs` is the central persistent model. New keys, defaults, and domains:

| Setting | `prefs.txt` key | Default | Range |
| --- | --- | --- | --- |
| Master | `master-volume` | 100 | 0–100 |
| Music | `music-volume` | 75 | 0–100, existing key |
| SFX | `sfx-volume` | 100 | 0–100, existing key |
| Ambient | `ambient-volume` | 70 | 0–100 |
| Soundtrack | `soundtrack` | `original` | `original`, `remake` |
| Dynamic Music | `dynamic-music` | `true` | boolean |
| Enhanced Ambience | `enhanced-ambience` | `false` | boolean |

Absent new keys preserve old behavior: Master is neutral, Remake dynamic transitions stay enabled, and ambience stays off. Malformed percentages and toggles retain defaults. The existing `replacement_medical_music` migration and explicit `soundtrack` precedence remain intact. `exploration_music_breaks` is still an optional AUDIO4 preference only, default off; it is not exposed as a normal setting.

Mixer flow on Android: original MIDI/XMI uses MIDI controller 7 per channel and the SDL_mixer music hook; Remake uses the SDL_mixer music stream (`Mix_VolumeMusic`, 0–128). `ExternalMusicUpdate` owns the handoff so the hook and stream are not intended to play concurrently. Its existing missing-file fallback restores stock music once for the selected deck/visit. Dynamic Music Off holds Remake in Explore; On restores the existing threat-driven Explore/Combat state machine. Gameplay SFX retain their existing per-source gain and one SFX category gain. Ambient owns two additional SDL_mixer channels after the legacy SFX channel range. Legacy SFX first-free allocation is confined to its original fixed range, so it cannot claim those voices or overrun its bookkeeping arrays. Ambient has an independent gain and bounded local gain. The final postmix applies Master once to the complete Android buffer, including auxiliary cutscene/audiolog sound. Effective gains are therefore Master × category × source, with no Master multiplication in upstream categories.

### Hardware finding and volume curve fix

On the first phone pass, Music had little obvious change from 100% through roughly 50%. The audio path explains it: the Audio Settings slider stores an integer 0–100 as the existing `music-volume` preference and `curr_vol_lev`; Remake converted it linearly to SDL_mixer's 0–128 volume (75% became 96/128, only about 2.5 dB lower; 50% became 64/128, about 6 dB lower). Original XMI scaled each channel's MIDI CC7 by the same linear percentage. The legacy music quest variable stores `percentage²/100` and reads it through a square root; that round trip supports the original Sound screen and is not another gain stage. Master was a single linear S16 postmix multiplication. SFX and Ambient category volumes were also linear. There was no duplicate Master scaling; SDL_mixer step quantization and the linear amplitude mapping made the upper half feel weak.

`AudioVolumeCurve.h` now maps each saved UI percentage through a fixed, monotonic 30 dB table: for 1–100%, gain is `10^(-1.5 × (100 − percent)/100)` rounded to microgain; 0% is true mute. The table avoids runtime `pow` and gives deterministic integer conversions. At 100/75/50/25/10/0%, ideal gain is about 1/.422/.178/.075/.045/0 (0/−7.5/−15/−22.5/−27 dB/mute). Remake maps this once to SDL_mixer's 0–128 music volume; Original maps it once to each MIDI CC7 value while preserving the source channel level. SFX uses it once before its source/distance scaling; Ambient uses it once before local layer/one-shot gain. Master applies the curve once in the final postmix. Defaults and `prefs.txt` values remain unchanged, as do live updates and the legacy quest-variable round trip. Integer MIDI/mixer steps can still make adjacent one-percent positions identical near the low end; the six main checkpoints have distinct mapped values. Hardware listening must confirm perceived balance for both soundtracks.

## Ambient authoring and runtime

Install future content in the app's private files directory under `audio/ambient/`. Shockolate changes its working directory to that location. Use:

```text
audio/ambient/generic/ventilation/loop_air.wav
audio/ambient/generic/metal_creak/creak_01.wav
audio/ambient/medical/ventilation/loop_medical_air.wav
audio/ambient/reactor/machinery_heavy/turbine_01.mp3
```

Supported runtime formats are `.wav` and `.mp3` as decoded by the bundled SDL_mixer configuration. Use `loop_` or `layer_` prefixes for continuous layers; other names are randomized one-shots. Category and deck names are lowercase directory names; filenames are otherwise free. Categories are `ventilation`, `electrical`, `machinery_light`, `machinery_heavy`, `computer`, `steam`, `hydraulic`, `metal_creak`, `structural`, `distant_impact`, `distant_mechanical`, `organic`, and `environmental_drone`. Deck names are `medical`, `research`, `reactor`, `maintenance`, `storage`, `flightdeck`, `executive`, `groves`, `engineering`, `security`, and `bridge`. All three Grove levels share the `groves` profile.

The initial table in `AmbientDirector.c` weights categories for all 11 ordinary areas. Medical favors ventilation, light machinery, and electrical sound; Reactor and Engineering favor heavy machinery/electrical/structural sound; Maintenance is industrial and mechanical; Groves favors organic and environmental drone; other decks have distinct starting balances. These are authoring defaults pending listening, not final art direction. A deck-specific category replaces generic candidates for that category and event kind when it has usable assets; generic candidates fill absent or failed overrides. The index is built once on first enable and sorted by path for deterministic validation. New files installed while the app is running require an app restart or director reinitialization.

At most one persistent layer and one one-shot use the two dedicated voices. One-shots are weighted by category, spaced about 15–35 seconds, and suppressed during the existing combat signal. Categories have a 30-second cooldown and sources a 90-second cooldown; a recent clip is avoided when another eligible choice exists. Source gain is randomized in a bounded range, with modest one-shot stereo panning. Pitch variation is intentionally omitted because the current mixer path does not expose a safe per-channel pitch control. Layers fade in; deck transitions fade the old layer and stop its one-shot before selecting the new profile. Background/focus loss halts and frees ambient voices. Resume resets the next-event anchor rather than replaying missed events. The director indexes zero files without error and does no playback, leaving music and SFX untouched.

Runtime context is deliberately limited to the reliable deck number, cyberspace/gameplay eligibility, and existing combat timer. There is no robust generic room/material/nearby-object classification signal in this code path. No object-ID heuristics were added. Future work can extend the `AmbientDirectorUpdate` inputs after a stable environment classification exists.

Run `python scripts/validate-ambient-assets.py audio/ambient --json dist/ambient-validation.json` before packaging supplied media. The standard-library tool reports deck/category, kind, estimated duration, SHA-256 duplicates, invalid/zero-length files, and empty categories. An empty tree is a supported PASS with zero assets. The JSON is an optional QA manifest/report; runtime discovers files directly and does not require a desktop tool. WAV headers are read exactly; MP3 duration is an estimate from a readable MPEG-1 Layer III frame header. Use an external audio tool later for loudness matching, silence trimming, transcoding, and seamless loop inspection. To add a category, add its directory name and profile weights in `AmbientDirector.c`, then update the validator category list and this document. To add a deck override, place files under `<deck>/<category>/`; no filename table or rebuild is needed for new clips.

## MOBILE-POLISH1

The movement, FIRE, USE, LOOK/CURSOR, Sprint, AMMO, and other radial geometry and gesture model remain in their existing locations. Selected radial wedges now have a restrained amber edge and clearer label; the center shows the selected entry and release cue, reverting to Cancel when nothing is eligible. The controls still render over the full device surface, including black bars, and retain opacity and layout editing. The audio page uses large rows and a full-width slider gesture rather than a small handle.

Original game art and mouse hitboxes are unchanged. Only a **touch tap routed to the legacy UI** may be corrected after the established surface → 4:3 viewport → game transform, then the legacy game → UI coordinate conversion. The corrected point is converted back and fed into the original mouse-event queue. Touch-assist candidates are owned/visible side icons, active inventory page tabs (including software and logs pages where available), available MFD mode selectors, and MFD button-array cells such as small arrows. Each has an explicit bounded semantic cluster: separate left/right icon strips, the bottom inventory tab strip, separate left/right MFD selector strips, and each MFD button grid. A touch in a cluster resolves to the nearest eligible original rectangle within its maximum assist radius; exact ties use stable array order. The radii are 14 legacy UI pixels for side icons, 12 for inventory tabs, 11 for MFD selectors, and 10 for button grids. Cluster bounds stop assistance from reaching the central gameplay view or an unrelated panel. The bottom inventory tab strip now accepts touch up to 18 UI pixels above its three-pixel art, so a finger can land roughly 35–45 dp above the Android home-gesture edge on the tested viewport. Occupied inventory rows retain their original interaction and take priority over this touch-only proxy area. An object held on the cursor and cyberspace exclude the assist path. Raw taps in the black bars are rejected before the cursor transform clamps them to the game edge. The existing gesture owner prevents overlay touches leaking into legacy UI; legacy UI taps are never dispatched to FIRE/USE/movement. Mouse, keyboard, and controller input never call the assist function.

On the tested phone, the physical display reports 2340×1080 in landscape at 450 dpi. A 1440×1080 4:3 viewport makes a 320×200 legacy UI pixel about 4.5 screen pixels horizontally and 5.4 vertically, or roughly 1.6×1.9 dp. The earlier 3–5 UI-pixel margins amounted to only about 5–10 dp. The new 10–14 pixel assist reaches roughly 16–27 dp beyond a target, making small controls substantially more forgiving without expanding touch semantics across panels. The renderer's runtime viewport and scale are used for actual input and QA drawings; these display numbers are an approximate reasoning aid, not a hardcoded transform.

QA builds can enable `qa_cheats = true` in `prefs.txt`, then open Touch Settings → QA Tools → **TOUCH HITBOXES ON**. The toggle defaults off and affects drawing only. Markers persist until the next cursor touch or the toggle is switched off. The drawing shares the existing touch overlay render pass. A small key in the left letterbox names the event and colors: white is the raw touch, cyan outlines the semantic group, green outlines the assisted reach, and amber outlines the original target and snapped aim point. For inventory tabs the key shows the one-based tab number. It says **AIM**, since geometry alone cannot prove that the game's handler activated. It distinguishes a completed tap from a finger down, drag, non-tap release, or cancellation. The former translucent cluster fill was removed so diagnostics do not shade the game view. The existing `MOBILE-POLISH1 touch assist` log remains available with QA cheats; QA builds also log inventory button presses once per press. Normal builds omit the overlay. Freeform MFD handlers and object interactions remain pixel-faithful because they have no safe general semantic subdivision. Hardware QA should pay particular attention to adjacent MFD selectors, the bottom inventory tab strip, and the game viewport edges.

## Validation state

Automated PASS on the tuning pass: `scripts/test-touch.ps1` (full touch suite including QA toggle, audio menu, gesture ownership, and cluster selection), `scripts/test-audio5-linux.sh` (production preference parsing, AUDIO1/2/3/4 regressions, dynamic toggle, gain curve/master postmix, ambient mock scheduler, zero assets and validator), and standalone `scripts/test-audio1.ps1`. The complete `scripts/test-touch.ps1` suite passed again after the tab-routing and QA-display fix. ARM64 `qa` Gradle build passes. The QA APK is at `AndroidProject/app/build/outputs/apk/qa/app-qa.apk`, SHA-256 `444a22ae4a0a8c786bd873efd199551abedcd8fe408cd6e5fff59edfeedc4d9f`. Hardware/listening retest is pending; no ambient audio is expected until content is installed.

### Second hardware finding: bottom gesture bar and QA overlay

The first installed tuning build made touch targets somewhat better, but the bottom inventory tabs still competed with Android's home-swipe area. Their original graphics and mouse rectangles are at the bottom edge. The follow-up extends only the touch proxy above those tabs while refusing to redirect touches on populated inventory rows. This leaves the original rendered viewport, black-bar control space, mouse path, keyboard, and controller behavior unchanged. Android's home gesture still owns its system edge; the intent is to offer a reliable tap position above it. This adjustment needs hardware retest.

The hitbox mode also reportedly made the viewport black and showed only a brief dark outline. The diagnostic now uses bright labeled outlines without a game-area fill. It still needs direct hardware retest for the reported transient black frame; renderer-state host tests alone cannot prove that frame is gone.

The supplied screenshots and device log exposed a separate click-routing defect: the bottom tab assistant chose the correct original coordinates, but fullscreen's region classifier could still call that point a world target. The bridge then sent the world double-use sequence instead of one touch-tagged UI tap. A successful touch-assist selection now takes the direct UI-control path, leaving unassisted world/object taps, mouse, keyboard, and controller semantics unchanged. A host bridge regression verifies one direct press/release when the classifier otherwise reports WORLD. On the connected phone, a controlled Main-tab tap above the system gesture area reached `pagebutton_mouse_handler` at UI `11,52` and tab `0`. This confirms event delivery, not full user hardware acceptance. Android's home gesture still owns the bottom system edge; use the upper portion of the tab assist zone for retesting.

The final QA APK was installed with `adb install -r` on the connected `SM_S948U1` while preserving app data. A diagnostic screenshot after enabling the hitbox key and tapping Main shows the game viewport still visible with persistent white/cyan/green/amber markers and the tab label in the left letterbox. The reported brief black frame has not been captured or ruled out by this static check; user hardware retest remains pending.

### Focused hardware retest

The complete numbered device checklist is in `docs/AUDIO5_MOBILE_POLISH1_DEVICE_QA.md`.

- [ ] Music at 100, 75, 50, 25, 10, and 0%; confirm useful progression and true mute.
- [ ] Master sanity check; compare Original and Remake music volume and confirm live updates.
- [ ] Repeatedly tap near and between side selectors (weapon/info/software/logs) and inventory/MFD controls; confirm the intended neighbor only.
- [ ] Enable the QA hitbox overlay, inspect raw point, cluster, expanded candidate, and original target on those taps; disable it afterward.
- [ ] Tap the bottom inventory tabs from above the Android home-swipe bar; confirm occupied inventory rows still work and the home gesture does not fire during these taps.
- [ ] Leave hitbox mode on for several taps; confirm the game viewport remains visible and the markers persist until the next tap or toggle.
- [ ] Confirm ordinary gameplay taps, movement, FIRE, USE, wheels, mouse/controller if available, and black-bar gesture ownership remain normal.

## Device checklist

- [ ] Master, Music, and SFX sliders each change only the expected bus; Ambient slider remains silent with zero assets.
- [ ] Original plays stock music; Remake plays installed Remake tracks; Dynamic On changes Explore/Combat and Off holds Explore.
- [ ] Restart and confirm all settings persist; if Remake media are absent, confirm stock fallback with no silence or repeated error spam.
- [ ] Enable Enhanced Ambience with zero assets, visit several decks, background/resume and lock/unlock; confirm no crash and normal music/SFX.
- [ ] Test movement, FIRE, USE, LOOK/CURSOR, Sprint, AMMO and each enabled radial wheel; check short tap versus hold, highlighted/selected/latched states, low opacity, and layout editor.
- [ ] Tap weapon/info selectors, software and logs/email tabs, inventory tabs/buttons, MFD controls, and small arrows. Try both edges and midpoints; confirm the intended neighbor only.
- [ ] Confirm ordinary viewport taps, overlay gesture ownership, and no accidental FIRE/USE/movement or UI activation.
- [ ] If available, compare hardware mouse and controller behavior to the prior build.

No third-party ambient media was downloaded, generated for the app, or committed in this batch. No commit or push was made.
