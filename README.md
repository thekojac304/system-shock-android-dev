# System Shock Touch Edition: development history

> **Looking for the game? This isn't the place to download it.**
> System Shock Touch Edition lives at **[thekojac304/system-shock-android](https://github.com/thekojac304/system-shock-android)**.
> That repository has the APK, install steps, feature list and release source:
> [download 1.1.0-beta.2](https://github.com/thekojac304/system-shock-android/releases/tag/v1.1.0-beta.2).

This repository is the **engineering archive** of Touch Edition. It contains every development commit, from the upstream
baseline through the touch-control alphas, the audio and lighting research, and the 1.1.0 betas. It's for people who
want to see how the work was done. It isn't a separate product and isn't updated as a live mirror.

## What's here

| Branch / tag | Contents |
| --- | --- |
| `touch-controls` (default) | The main development line: Touch Alpha 1.1–4, cursor and menu work, QA tools, the AUDIO1/2 soundtrack routing, the OpenGL ES 3 renderer prototypes, and generated lighting (HORROR and DECKLIGHT) |
| `release/custom-1.1.0-beta.1` | The release line: `touch-controls`, plus the Touch Edition package identity (`v1.1.0-beta.1` = `6d5a39e`) and the beta.2 import-crash fix (`v1.1.0-beta.2` = `8245002`) |
| `backup/workspace-preservation-2026-11-10` | A snapshot of uncommitted work as of 2026-11-10, **not in any release**: dynamic exploration/combat music, fades and resume (AUDIO3/3.1), loop points (AUDIO4), an audio settings page and touch assist (AUDIO5) |
| `main`, `touch-baseline`, `v1.0.0` and earlier | Upstream System Shock — Android by raposomiguel50, which this work starts from |

The released APK was built from `v1.1.0-beta.2`. The official repository's tag `v1.1.0-beta.2` holds the same source as
a single reviewed snapshot.

## Which work shipped

The official repository keeps the inventory: [FEATURES.md](https://github.com/thekojac304/system-shock-android/blob/main/docs/FEATURES.md).
It marks each feature as released, preview, QA builds only, or development only. In short:

- **In the 1.1.0-beta.2 APK:**
  - the full touch control scheme: stick, look, buttons, cursor mode, radial wheels, haptics, layout editor;
  - the Visuals preview;
  - the intro-decoder, 16 KB alignment and import-crash fixes;
  - the app identity.
- **Only in QA builds:** the GPU renderer, generated deck lighting, atmosphere post-processing and the QA tools. These
  are compiled with `RP5NP_QA_TOOLS` in the `qa` build type.
- **Only on the backup branch:** the later audio work and touch assist.

## Notes on this history

- **Redactions:** phone serial numbers and local user paths were replaced with `<device-serial>` and `<user>`. Because
  of that, commits after the upstream baseline have different hashes from the original private history. Upstream
  commits and tags keep their original hashes and signatures.
- **Archived downloads:** the Releases page here keeps archived copies of the beta APKs next to the tags they were built
  from. They're marked as pre-releases. Download from the
  [official repository](https://github.com/thekojac304/system-shock-android/releases) instead.
- **QA notes:** the engineering notes are in `docs/`. Many describe QA builds (package suffix `.qa`).
- **Old name:** this repository used to be called `system-shock-android-dev`. Old links redirect here.

## Credits and license

The engine is [Shockolate](https://github.com/Interrupt/systemshock) by Interrupt and contributors. The Android port this
work starts from is [System Shock — Android](https://github.com/raposomiguel50/system-shock-android) by raposomiguel50;
its original README is in [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md). SDL2 and SDL_mixer are covered in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

GPL-3.0-or-later ([LICENSE](LICENSE)). System Shock is a trademark of its owners. This project is not affiliated with or
endorsed by Night Dive Studios, Looking Glass Studios or the upstream authors.
