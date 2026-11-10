# AUDIO5 + MOBILE-POLISH1 device QA

QA package: `io.github.raposomiguel50.systemshock.qa`. The installed build is
`1.0.0-qa` (APK SHA-256 `444a22ae4a0a8c786bd873efd199551abedcd8fe408cd6e5fff59edfeedc4d9f`).
Record observations rather than assuming a PASS. No ambient clips are expected.

## Audio settings

1. [ ] Master slider changes overall loudness live; 0% mutes.
2. [ ] Music slider changes loudness across 100, 75, 50, 25, 10, and 0%.
3. [ ] SFX slider changes effects without changing music.
4. [ ] Ambient slider is operable; with zero assets it remains silent.
5. [ ] Original soundtrack plays stock music.
6. [ ] Remake soundtrack plays installed Remake tracks.
7. [ ] Dynamic Music On responds to exploration/combat when Remake is active.
8. [ ] Dynamic Music Off holds the non-dynamic Remake behavior.
9. [ ] Restart and confirm all audio settings persist.
10. [ ] If Remake media are missing, confirm stock fallback and no repeated errors.

## Enhanced ambience with zero assets

11. [ ] Enable Enhanced Ambience with an empty ambient media directory.
12. [ ] Confirm no crash or unexpected audible error; no ambience is expected.
13. [ ] Visit several decks and confirm music/SFX remain normal.
14. [ ] Background and resume the app; confirm music/SFX remain normal.
15. [ ] Lock and unlock the screen; confirm clean recovery.
16. [ ] Disable and re-enable ambience; confirm unrelated audio stays normal.

## Touch controls

17. [ ] Move in all directions.
18. [ ] Press and release FIRE.
19. [ ] Tap USE on a world target.
20. [ ] Switch LOOK/CURSOR and verify each mode.
21. [ ] Toggle Sprint latch and check its visual state.
22. [ ] Open/select/cancel AMMO and each other enabled radial wheel.
23. [ ] Compare short taps with holds on wheel host controls.
24. [ ] Lower opacity and confirm text/selection remain readable.
25. [ ] Open layout editor, select a control, move it, and save/cancel.
26. [ ] Confirm pressed, held, selected, and latched states are distinct.

## Original UI touch assist

27. [ ] Tap weapon/info selectors, including their edges.
28. [ ] Tap Software when that tab is available.
29. [ ] Tap Logs/Email when available.
30. [ ] Tap inventory tabs from above the Android home-swipe edge.
31. [ ] Tap MFD selectors and small button-array controls.
32. [ ] Tap small arrows and map/interface controls that are present.
33. [ ] Tap between neighboring targets and note the chosen target.
34. [ ] Confirm no unintended neighboring activation or inventory-row theft.
35. [ ] Confirm ordinary gameplay viewport touches still work.

## Input preservation and QA display

36. [ ] If available, compare physical mouse clicks with the previous build.
37. [ ] If available, verify controller behavior is unchanged.
38. [ ] Enable Touch Settings → QA Tools → TOUCH HITBOXES ON: white marks raw touch, cyan the group, green reach, amber aim. Check that an intended bottom tab activates, a canceled/drag touch is labeled, the viewport stays visible through several taps, and overlay gestures never leak into the game. Disable the display afterward.

Report any brief black frame with the exact preceding tap/menu action and whether
the legend said **AIM**, **NO TAP SENT**, or **TOUCH CANCELED**. The Android home
gesture still owns the bottom system edge; testing just above it is important.
