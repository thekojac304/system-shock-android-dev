AUDIO4 LOCAL LOOP ANALYZER
=========================
Scope: local analysis, candidate generation, and human audition only.
No runtime code, settings, source media, installs, or Android behavior changed.
Never commit or redistribute generated WAVs or the user's soundtrack.

DEPENDENCIES
Python 3.11+ and NumPy; FFmpeg and ffprobe on PATH or explicit executable paths.
No scipy/librosa/soundfile required. No packages installed for the initial run.
Existing tested executables on this PC:
  C:\Users\<user>\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe
  C:\Program Files\Shutter Encoder\Library\ffmpeg.exe
  C:\Program Files\Shutter Encoder\Library\ffprobe.exe
On another machine use your normal Python with NumPy and a local FFmpeg install.

POWERSHELL QUICK START (from repository root)
$audioPython = 'C:\Users\<user>\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$audioSource = 'C:\Users\<user>\Downloads\system_shock_soundtrack_mp3\system_shock_soundtrack_digital'
$audioTools = @('--ffmpeg','C:\Program Files\Shutter Encoder\Library\ffmpeg.exe',
                '--ffprobe','C:\Program Files\Shutter Encoder\Library\ffprobe.exe')

# Inventory only, no decoding or audition rendering:
& $audioPython scripts/audio4/analyze.py $audioSource @audioTools --inventory-only
# All 22, or one track (does not require a whole-pack rerun):
& $audioPython scripts/audio4/analyze.py $audioSource @audioTools
& $audioPython scripts/audio4/analyze.py $audioSource @audioTools --track medical_explore
# Narrower follow-up; use a new ignored output folder to preserve old auditions:
& $audioPython scripts/audio4/analyze.py $audioSource @audioTools --track medical_explore --start-min 145 --start-max 155 --end-min 228 --end-max 235 --min-loop 70 --output dist/audio4-refine
# Render the shortlisted loop three times, including two real joins:
& $audioPython scripts/audio4/analyze.py $audioSource @audioTools --track medical_explore --full-loop A
# Tests use generated signals, not commercial media:
& $audioPython -m unittest discover -s scripts/audio4 -p 'test_*.py'

FILES AND HUMAN REVIEW
dist/audio4/listen.html: local listening page, A/B/C per track, no network.
Open in a browser. Primary WAVs: six seconds before/after the join, repeated
with one second of silent reset in between. Joins at 6 and 19 seconds.
The silence is a preview reset, not another natural loop. The short preview
cannot validate long-range musical form; use --full-loop for shortlisted pairs.
Comparison reels: hard, 25, 50, 100, 150, 250 ms in order. Each segment has
four seconds before/after and a one-second silent reset. Join times are
4, 13, 22, 31, 40, 49 seconds. Listen to each at a consistent volume.
For short custom loops, consult the JSON markers rather than these defaults.

Choose best / acceptable / bad / manual refinement, adjust the preferred fade,
and download your review JSON. Browser choices are in memory until downloaded.
The page does not write to your source files or send reviews anywhere.
Preserve the downloaded review file separately from generated outputs.
review-template.csv is a fresh blank template on every run; save edits under
a different filename. Reruns replace generated results for the selected tracks.
An output directory is intended for one soundtrack source set. Use a new folder
when changing packs or source files; stale source fingerprints are rejected.

inventory.json: all names, paths, MP3 metadata, SHA-256, tool versions, missing IDs.
candidates.json: consolidated full precision times, sample indices, all scores,
variants, gains, cue times, provenance and parameters. Per-track JSON alongside.
loops.proposed.ini: DISABLED, unreviewed Candidate A examples, NOT approved choices.
Auditions are 16-bit lossless PCM WAV, source sample rate, stereo (mono duplicated).
Peak protection only reduces gain if necessary; exact gains are recorded. No MP3
re-encoding or intermediate PCM file. Source hashes checked again after analysis.

ALGORITHM / LIMITS
1. Resolve <deck>_explore.mp3, then legacy <deck>.mp3; <deck>_combat.mp3.
   Original OST '(Deck [Explore/Combat])' titles are fallback aliases. Systems
   Engineering and Flight Deck normalize to engineering and flightdeck. No rename.
2. FFprobe inventory; decode full native-rate PCM for join refinement/rendering.
   Separately decode 11025-Hz stereo features: 2048-sample Hann FFT, 220-sample hop.
   32 log-frequency bands; positive spectral flux; frame RMS and channel balance;
   12 pitch classes with entropy-derived harmonic weight (less weight for noise).
3. Conservative body: exclude first/last 10% and low-energy head/tail (18dB
   below the 65th percentile, with 4s/5s margins). This is a heuristic, not
   semantic intro/outro detection. Stable bridges, breakdowns or tails inside
   these bounds can remain. Inspect/listen and override bounds per track.
4. Flux autocorrelation estimates 65-180 BPM, subframe period and onset phase.
   Tempo confidence gates rhythm weights; four-beat span is a prior, not proven
   meter/downbeat/phrase detection. Half/double-tempo and drift remain possible.
5. Min loop defaults to one quarter of actual decoded duration: 48-66s for this
   supplied pack. --min-loop overrides. Half-second and beat-phase boundary grids;
   cheap spectral prefilter, then nine-frame context around both musical instants.
6. Context cost = 3*(1-spectral cosine) + 2*tonality*(1-chroma cosine)
   + .025*absolute RMS-dB difference before->after + .12*stereo balance difference
   + .06*flux difference + .035*onset penalty + confidence-weighted beat/phrase
   mismatch. Weak .025*minimum/span prior favors longer loops.
7. Refine 24 distinct regions +/-160ms (40ms start / 20ms end). Native-rate
   stereo correlation, sample-jump and derivative continuity refine within 8ms
   plus 4ms relative lag. This cannot prove a musical phrase is correct.
8. Keep three diverse pairs (endpoints differ by >6s in at least one dimension).
   Overall rank = 100/(1+cost), a within-track heuristic, NOT a quality percent.
9. Test hard and five short linear crossfades. Penalize rendered join roughness,
   residual jump, overlap energy dip and duration; keep all comparisons for ears.
   Linear overlap avoids correlated-signal gain boosts but can dip or cancel.
   Long fades can smear/double transients; no fade is automatically approved.

TIMING CONTRACT -- READ BEFORE ANY FUTURE RUNTIME IMPLEMENTATION
Times are decoded PCM seconds, samples are integer frame indices at sample_rate.
Intervals are half-open [start,end). Decoded duration can differ from container
duration due to MP3 delay/padding handling. FFmpeg is not Android minimp3.

Each pair has a hard_loop_end E (the musical splice), loop_start S, and fade F.
Hard: play to E, jump to S. Faded: metadata loop_end = E+F. Overlap source
[E,E+F) with [S,S+F), linear tail->head, then continue at S+F. Thus effective
period = loop_end-loop_start-F = E-S for ALL six variants. Do NOT resume at S
after the overlap or subtract F twice. Variant-specific sample indices are in
JSON. Search end bounds refer to hard E; faded end may extend up to 250ms later.
This intentionally preserves meter while comparing fade lengths. Review export
recomputes loop_end_sample if the listener chooses another fade.
The safe-resume suggestion is [S,E-0.25s], not a guarantee of safe arbitrary
mid-note seeking. It is not used to randomize starts or alter existing resumes.

METADATA PROPOSAL
Use INI-like sections to fit existing prefs.txt key=value conventions. The
runtime currently has no reader for this proposal. [pack] identifies schema,
pack ID, decoded_pcm timebase, linear_overlap semantics and disabled status.
[medical_explore], etc. hold status, source SHA-256, sample rate, loop_start/end
seconds AND integer samples, fade milliseconds, safe_resume_start/end. Integer
samples are canonical; a future parser should reject inconsistent times, wrong
hashes/rates, invalid ranges and unapproved status. New packs get another id.
Avoid absolute PC paths in runtime metadata; the deck/variant ID maps to the
existing pack registry. The analysis JSON retains exact local provenance.

NEXT MILESTONE
Human selections -> freeze approved, versioned metadata -> separate integration
with Android SDL_mixer 2.8.1/minimp3 -> listen and measure actual audible joins,
decoder seek behavior and resume bounds in game. Immediate seek-position reports
are not evidence of sample-perfect output. No AUDIO4 result is hardware-validated.
