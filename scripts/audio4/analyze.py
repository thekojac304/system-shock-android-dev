"""Local AUDIO4 loop candidate search. Requires Python, NumPy, FFmpeg/ffprobe.

Reads user-owned MP3s; writes only to an ignored output directory. No runtime edits.
See README.txt for timing/crossfade semantics and human review instructions.
"""
import argparse
import configparser
import csv
import hashlib
import html
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import wave

import numpy as np

DECKS = ('medical', 'research', 'reactor', 'maintenance', 'storage', 'flightdeck',
         'executive', 'groves', 'engineering', 'security', 'bridge')
FADES = (0, 25, 50, 100, 150, 250)
RATE, HOP, FFT = 11025, 220, 2048
VERSION = 1


def run(command):
    return subprocess.check_output(command, stderr=subprocess.PIPE)


def sha(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def discover(root):
    """Runtime names win; original album titles are fallback aliases only."""
    found, ignored = {}, []
    for path in sorted(root.glob('*')):
        if path.suffix.lower() != '.mp3':
            continue
        stem = path.stem.lower()
        match = re.fullmatch(r'(' + '|'.join(DECKS) + r')(?:_(explore|combat))?', stem)
        if match:
            deck, variant = match.group(1), match.group(2) or 'explore'
            priority = 3 if match.group(2) else 2
        else:
            match = re.search(r'\((.*?)\s*\[(Explore|Combat)\]\)', path.stem, re.I)
            if not match:
                ignored.append(path.name)
                continue
            deck = match.group(1).lower().replace(' ', '').replace('systemsengineering', 'engineering')
            if deck not in DECKS:
                ignored.append(path.name)
                continue
            variant, priority = match.group(2).lower(), 1
        key = deck + '_' + variant
        if key in found and found[key][0] == priority:
            raise ValueError('Ambiguous source for ' + key)
        if key not in found or priority > found[key][0]:
            found[key] = (priority, path)
    expected = [d + '_' + v for d in DECKS for v in ('explore', 'combat')]
    return {k: found[k][1] for k in expected if k in found}, [k for k in expected if k not in found], ignored


def probe(path, ffprobe):
    data = json.loads(run([ffprobe, '-v', 'error', '-select_streams', 'a:0',
                          '-show_entries', 'stream=sample_rate,channels,bit_rate,duration:format=duration',
                          '-of', 'json', str(path)]))
    stream = data['streams'][0]
    return dict(filename=path.name, source=str(path.resolve()), sha256=sha(path),
                duration=float(data['format']['duration']), sample_rate=int(stream['sample_rate']),
                channels=int(stream['channels']), bitrate=int(stream.get('bit_rate', 0)))


def decode(path, ffmpeg, rate=None, channels=2):
    command = [ffmpeg, '-v', 'error', '-i', str(path), '-map', '0:a:0', '-vn', '-ac', str(channels)]
    if rate:
        command += ['-ar', str(rate)]
    return np.frombuffer(run(command + ['-f', 'f32le', '-acodec', 'pcm_f32le', '-']),
                         dtype='<f4').reshape(-1, channels).copy()


def norm(a):
    return a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1e-12)


def features(pcm):
    mono = pcm.mean(axis=1)
    frames = np.lib.stride_tricks.sliding_window_view(mono, FFT)[::HOP]
    power = np.abs(np.fft.rfft(frames * np.hanning(FFT), axis=1)).astype(np.float32) ** 2
    freq = np.fft.rfftfreq(FFT, 1 / RATE)
    edges = np.geomspace(35, 5500, 33)
    bands = np.stack([power[:, (freq >= a) & (freq < b)].sum(axis=1)
                      for a, b in zip(edges[:-1], edges[1:])], axis=1)
    spec = norm(np.log1p(bands / np.maximum(bands.mean(axis=1, keepdims=True), 1e-10) * 10))
    chroma = np.zeros((len(power), 12), np.float32)
    tonal = (freq >= 65) & (freq <= 2500)
    bins = np.round(69 + 12 * np.log2(np.maximum(freq, 1) / 440)).astype(int) % 12
    for i in range(12):
        chroma[:, i] = power[:, tonal & (bins == i)].sum(axis=1)
    # Entropy reduces harmonic weight on diffuse/noisy material.
    prob = chroma / np.maximum(chroma.sum(axis=1, keepdims=True), 1e-12)
    tonality = np.clip(1 + (prob * np.log(np.maximum(prob, 1e-12))).sum(axis=1) / np.log(12), 0, 1)
    chroma = norm(np.sqrt(chroma))
    stereo_frames = np.lib.stride_tricks.sliding_window_view(pcm, FFT, axis=0)[::HOP]
    energy = np.mean(stereo_frames ** 2, axis=-1)
    db = 10 * np.log10(np.maximum(energy.mean(axis=1), 1e-12))
    balance = (energy[:, 0] - energy[:, 1]) / np.maximum(energy.sum(axis=1), 1e-12)
    positive = np.maximum(np.diff(np.sqrt(bands), axis=0, prepend=np.sqrt(bands[:1])), 0).sum(axis=1)
    novelty = positive / max(float(np.percentile(positive, 95)), 1e-9)
    times = (np.arange(len(power)) * HOP + FFT / 2) / RATE
    return dict(times=times, spec=spec, chroma=chroma, tonality=tonality, db=db,
                balance=balance, novelty=novelty)


def tempo(f, lo, hi):
    y = f['novelty'][(f['times'] >= lo) & (f['times'] <= hi)].copy()
    y = np.maximum(y - np.median(y), 0)
    y -= y.mean()
    size = 1 << (2 * len(y) - 1).bit_length()
    ac = np.fft.irfft(np.abs(np.fft.rfft(y, n=size)) ** 2)[:len(y)]
    ac /= np.maximum(np.arange(len(y), 0, -1), 1)
    ac /= max(ac[0], 1e-9)
    lower, upper = int(60 / 180 * RATE / HOP), int(60 / 65 * RATE / HOP)
    lag = lower + int(np.argmax(ac[lower:upper+1]))
    # Sub-frame parabola helps avoid accumulated beat-period rounding error.
    a, b, c = ac[lag-1:lag+2]
    fraction = .5 * (a-c) / (a-2*b+c) if abs(a-2*b+c) > 1e-9 else 0
    period = (lag + float(np.clip(fraction, -.5, .5))) * HOP / RATE
    confidence = float(max(0, ac[lag]))
    phases = np.arange(0, period, .005)
    weights = np.maximum(f['novelty'] - .3, 0) * ((f['times'] > lo) & (f['times'] < hi))
    phase_scores = [np.sum(weights * np.exp(-((np.mod(f['times'] - p + period/2, period)-period/2)/.035)**2)) for p in phases]
    return dict(bpm=60/period, period=period, phase=float(phases[np.argmax(phase_scores)]),
                confidence=confidence, meter='unverified; 4-beat multiples are a search prior, not a detected meter')


def body(f, duration):
    active = f['times'][f['db'] > np.percentile(f['db'], 65) - 18]
    if len(active) == 0:
        raise ValueError('No active audio')
    # Conservative bounds, not semantic intro/outro detection. User can override.
    lo = max(float(active[0]) + 4, duration * .10)
    hi = min(float(active[-1]) - 5, duration * .90)
    return lo, hi


def index(f, t):
    return np.clip(np.searchsorted(f['times'], t), 0, len(f['times'])-1)


def metrics(f, start, end, beat):
    """Both endpoints describe the same musical instant in adjacent iterations.

    Compare context sequences around those instants, plus energy BEFORE -> AFTER
    the actual join. Comparing only arbitrary last/first samples rewards accidents.
    """
    offsets = np.array([-1.5, -.75, -.3, -.1, 0, .1, .3, .75, 1.5])
    a, b = index(f, start + offsets), index(f, end + offsets)
    spectral = float(np.mean(np.sum(f['spec'][a] * f['spec'][b], axis=1)))
    harmonic = float(np.mean(np.sum(f['chroma'][a] * f['chroma'][b], axis=1)))
    tonal = float(np.mean(f['tonality'][np.r_[a, b]]))
    pre = index(f, end + np.arange(-.5, -.05, .05))
    post = index(f, start + np.arange(.05, .5, .05))
    loudness = float(abs(f['db'][pre].mean() - f['db'][post].mean()))
    stereo = float(abs(f['balance'][a].mean() - f['balance'][b].mean()))
    rhythm = float(np.mean(np.abs(f['novelty'][a] - f['novelty'][b])))
    s, e = index(f, start), index(f, end)
    transient = float(max(f['novelty'][s], f['novelty'][e]))
    beats = (end - start) / beat['period']
    alignment = abs(beats - round(beats))
    phrase = abs(beats / 4 - round(beats / 4))
    beat_weight = np.clip((beat['confidence'] - .12) / .35, 0, 1)
    score = (3 * (1-spectral) + 2 * tonal * (1-harmonic) + .025 * loudness +
             .12 * stereo + .06 * min(rhythm, 3) + .035 * min(transient, 3) +
             beat_weight * (.25 * alignment + .12 * phrase))
    return dict(spectral_similarity=spectral, harmonic_similarity=harmonic,
                harmonic_weight=tonal, loudness_delta_db=loudness,
                stereo_balance_delta=stereo, rhythmic_envelope_delta=rhythm,
                beat_alignment=1-2*alignment, phrase_alignment=1-2*phrase,
                transient_penalty=transient, feature_cost=float(score))


def waveform(pcm, sr, start, end):
    s, e = round(start*sr), round(end*sr)
    radius = round(.04*sr)
    a, b = pcm[s-radius:s+radius], pcm[e-radius:e+radius]
    correlation = float(np.sum(a*b) / max(math.sqrt(float(np.sum(a*a)*np.sum(b*b))), 1e-12))
    rms = math.sqrt(float(np.mean(np.r_[a,b]**2)))
    jump = float(np.max(np.abs(pcm[e-1] - pcm[s]))) / max(rms, 1e-8)
    slope = float(np.max(np.abs((pcm[e-1]-pcm[e-2]) - (pcm[s+1]-pcm[s])))) / max(rms, 1e-8)
    return correlation, jump, slope


def search(pcm, sr, f, beat, lo, hi, min_span, args):
    start_lo = args.start_min if args.start_min is not None else lo
    start_hi = args.start_max if args.start_max is not None else hi-min_span
    end_lo = args.end_min if args.end_min is not None else lo+min_span
    end_hi = args.end_max if args.end_max is not None else hi
    if not (1 <= start_lo <= start_hi < len(pcm)/sr-2 and 1 <= end_lo <= end_hi < len(pcm)/sr-2):
        raise ValueError('Search bounds must leave 1s before and 2s after endpoints')
    # Half-second grid plus beat-phase grid. Never an all-sample Cartesian search.
    def grid(low, high):
        coarse = np.arange(low, high+.001, .5)
        if beat['confidence'] > .16:
            rhythmic = np.arange(beat['phase'] - .035, high, beat['period'])
            coarse = np.r_[coarse, rhythmic[(rhythmic >= low) & (rhythmic <= high)]]
        return np.unique(np.round(coarse, 3))
    starts, ends = grid(start_lo, start_hi), grid(end_lo, end_hi)
    pairs = []
    # Cheap vector spectral prefilter, then contextual multi-signal score.
    for s in starts:
        eligible = ends[ends-s >= min_span]
        if not len(eligible):
            continue
        sims = f['spec'][index(f, eligible)] @ f['spec'][index(f, s)]
        for e in eligible[np.argsort(sims)[-8:]]:
            m = metrics(f, s, e, beat)
            cost = m['feature_cost'] + .025 * min_span / (e-s)
            pairs.append((cost, s, e))
    if not pairs:
        raise ValueError('No pairs satisfy bounds and minimum span')
    # Retain distinct regions before refinement to avoid 3 near-identical results.
    seeds = []
    for row in sorted(pairs):
        if all(abs(row[1]-x[1]) > 2 or abs(row[2]-x[2]) > 2 for x in seeds):
            seeds.append(row)
        if len(seeds) == 24:
            break
    refined = []
    for _, s, e in seeds:
        best = (float('inf'), s, e)
        for ds in np.arange(-.16, .161, .04):
            for de in np.arange(-.16, .161, .02):
                ss, ee = s+ds, e+de
                if not (start_lo <= ss <= start_hi and end_lo <= ee <= end_hi and ee-ss >= min_span):
                    continue
                m = metrics(f, ss, ee, beat)
                cost = m['feature_cost'] + .025 * min_span / (ee-ss)
                if cost < best[0]:
                    best = (cost, ss, ee)
        cost, s, e = best
        # Native-rate stereo refinement within 8ms: too small to move a beat.
        best_wave = None
        for shift in np.arange(-.008, .0081, .001):
            ss = round((s+shift)*sr)/sr
            for relative in np.arange(-.004, .0041, .0005):
                ee = round((e+shift+relative)*sr)/sr
                if not (start_lo <= ss <= start_hi and end_lo <= ee <= end_hi and ee-ss >= min_span):
                    continue
                corr, jump, slope = waveform(pcm, sr, ss, ee)
                wcost = .035 * (1-corr) + .012 * min(jump, 4) + .005 * min(slope, 4)
                if best_wave is None or wcost < best_wave[0]:
                    best_wave = (wcost, ss, ee, corr, jump)
        if best_wave is None:
            continue
        wc, s, e, corr, jump = best_wave
        m = metrics(f, s, e, beat)
        m.update(waveform_similarity=corr, discontinuity_rms_ratio=jump)
        total = m['feature_cost'] + wc + .025 * min_span/(e-s)
        refined.append((total, s, e, m))
    selected = []
    for row in sorted(refined, key=lambda x:x[0]):
        if all(abs(row[1]-x[1]) > 6 or abs(row[2]-x[2]) > 6 for x in selected):
            selected.append(row)
        if len(selected) == args.candidates:
            break
    return selected


def fade_audio(pcm, s, e, n, context):
    """e = hard musical boundary. Fade overlaps [e,e+n) with [s,s+n).

    Metadata loop_end for a faded variant is e+n; its effective period remains
    e-s. A later runtime MUST resume at s+n after consuming the overlapping head.
    """
    before = pcm[e-context:e]
    if n:
        weight = np.linspace(0, 1, n, endpoint=True, dtype=np.float32)[:,None]
        blend = pcm[e:e+n]*(1-weight) + pcm[s:s+n]*weight
        return np.concatenate((before, blend, pcm[s+n:s+context]))
    return np.concatenate((before, pcm[s:s+context]))


def fade_cost(pcm, s, e, sr, ms):
    n = round(ms*sr/1000)
    context = round(.5*sr)
    joined = fade_audio(pcm, s, e, n, context)
    # Measure actual rendered join; do not rank just on lower single-sample jump.
    a, b = pcm[e:e+max(n,256)], pcm[s:s+max(n,256)]
    corr = float(np.sum(a*b)/max(math.sqrt(float(np.sum(a*a)*np.sum(b*b))),1e-12))
    rms = math.sqrt(float(np.mean(joined**2)))
    zone = joined[context-128:context+n+128]
    roughness = float(np.percentile(np.abs(np.diff(zone, axis=0)), 99.5))/max(rms,1e-9)
    if n:
        blend = joined[context:context+n]
        expected = .5*(float(np.mean(a[:n]**2))+float(np.mean(b[:n]**2)))
        dip = abs(10*math.log10(max(float(np.mean(blend**2)),1e-12)/max(expected,1e-12)))
    else:
        dip = 0.
    jump = float(np.max(np.abs(joined[context]-joined[context-1])))/max(rms,1e-9)
    cost = .20*roughness + .12*jump + .035*dip + .12*ms/250
    return dict(crossfade_ms=ms, cost=cost, overlap_correlation=corr,
                join_roughness=roughness, join_jump=jump, overlap_energy_change_db=dip,
                loop_start_sample=s, loop_end_sample=e+n, effective_period_samples=e-s)


def write_wav(path, pcm, sr):
    peak = float(np.max(np.abs(pcm)))
    gain = min(1., .999/max(peak, 1e-12))
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(pcm.shape[1]); wav.setsampwidth(2); wav.setframerate(sr)
        wav.writeframes(np.round(pcm*gain*32767).astype('<i2').tobytes())
    return gain


def auditions(pcm, sr, s, e, fade, prefix):
    ctx = round(6*sr)
    ctx = min(ctx, e-s, e, len(pcm)-s-round(.25*sr)-1)
    clip = fade_audio(pcm, s, e, round(fade*sr/1000), ctx)
    # The silence explicitly marks a preview reset, not a second genuine loop.
    silence = np.zeros((sr, pcm.shape[1]), np.float32)
    primary = np.concatenate((clip, silence, clip))
    gain = write_wav(prefix.with_suffix('.wav'), primary, sr)
    comparison, chapters = [], []
    offset = 0.
    for ms in FADES:
        context = min(4*sr,ctx)
        c = fade_audio(pcm, s, e, round(ms*sr/1000), context)
        comparison += [c, silence]
        chapters.append(dict(crossfade_ms=ms, start_seconds=offset, join_seconds=offset+context/sr))
        offset += len(c)/sr+1
    comparison_gain = write_wav(prefix.with_name(prefix.name+'_compare.wav'), np.concatenate(comparison), sr)
    return dict(file=prefix.name+'.wav', gain=gain, boundary_seconds=[ctx/sr, len(clip)/sr+1+ctx/sr],
                reset_silence_seconds=[len(clip)/sr, len(clip)/sr+1],
                comparison_file=prefix.name+'_compare.wav', comparison_gain=comparison_gain, comparison_chapters=chapters)


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def analyze(key, path, entry, args):
    print('Analyzing '+key, flush=True)
    sr = entry['sample_rate']
    pcm = decode(path, args.ffmpeg, channels=entry['channels'])
    if pcm.shape[1] == 1:
        pcm = np.repeat(pcm, 2, axis=1)
    if pcm.shape[1] != 2:
        raise ValueError('Only mono/stereo sources supported')
    duration = len(pcm)/sr
    f = features(decode(path, args.ffmpeg, RATE))
    lo, hi = body(f, duration)
    beat = tempo(f, lo, hi)
    # Inspected supplied tracks are 193-265s; defaults yield spans 48-66s minimum.
    minimum = args.min_loop if args.min_loop is not None else duration*.25
    rows = search(pcm, sr, f, beat, lo, hi, minimum, args)
    result = dict(track=key, source=entry, decoded_samples=len(pcm), decoded_duration=duration,
                  provisional_body=[lo,hi], minimum_loop_seconds=minimum, tempo=beat,
                  parameters={k:getattr(args,k) for k in ('start_min','start_max','end_min','end_max','min_loop')},
                  algorithm_sha256=sha(Path(__file__)),
                  status='UNREVIEWED: numerical candidates only', candidates=[])
    for i, (cost, start, end, m) in enumerate(rows):
        label = chr(65+i)
        s, e = round(start*sr), round(end*sr)
        variants = [fade_cost(pcm,s,e,sr,ms) for ms in FADES]
        best = min(variants,key=lambda v:v['cost'])
        ms = best['crossfade_ms']
        endpoint = best['loop_end_sample']
        notes = ['Unreviewed; score ranks auditions, not musical correctness.',
                 'Resume body is provisional; arbitrary mid-note resumes can still be audible.']
        if beat['confidence'] < .25:
            notes.append('Weak tempo evidence: manual rhythm/phrase assessment required.')
        if m['loudness_delta_db'] > 3:
            notes.append('Loudness mismatch above 3 dB: manual review priority.')
        if m['harmonic_similarity'] < .8:
            notes.append('Possible tonal mismatch: manual review priority.')
        if m['transient_penalty'] > 1:
            notes.append('Boundary near strong onset: check cut/doubled attacks.')
        row = dict(label=label, loop_start=s/sr, loop_end=endpoint/sr,
                   loop_duration=(endpoint-s)/sr, effective_period=(e-s)/sr,
                   hard_loop_end=e/sr, loop_start_sample=s, loop_end_sample=endpoint,
                   hard_loop_end_sample=e, suggested_crossfade_ms=ms,
                   safe_resume_start=s/sr, safe_resume_end=(e-round(.25*sr))/sr,
                   overall_score=100/(1+cost), metrics=m, variants=variants, notes=notes)
        row['audition'] = auditions(pcm,sr,s,e,ms,args.output/(key+'_candidate_'+label))
        result['candidates'].append(row)
    if sha(path) != entry['sha256']:
        raise RuntimeError('Source changed during analysis: '+str(path))
    save_json(args.output/(key+'.json'),result)
    print(key+': '+', '.join(f"{r['label']} {r['loop_start']:.3f}-{r['loop_end']:.3f}s / {r['suggested_crossfade_ms']}ms" for r in result['candidates']),flush=True)
    return result


def index_outputs(args, inventory):
    results = []
    for key in inventory['tracks']:
        path = args.output/(key+'.json')
        if path.exists():
            result = json.loads(path.read_text(encoding='utf-8'))
            if result['source']['sha256'] != inventory['tracks'][key]['sha256']:
                raise ValueError('Stale result/source mismatch for '+key+'; use a fresh output directory')
            results.append(result)
    save_json(args.output/'candidates.json',dict(schema_version=VERSION, inventory=inventory,
              crossfade_semantics='linear overlap tail [end-f,end) with head [start,start+f); resume at start+f; effective period=end-start-f',
              tracks=results))
    with (args.output/'review-template.csv').open('w',newline='',encoding='utf-8') as handle:
        fields=['track','candidate','loop_start','loop_end','crossfade_ms','overall_score','status','comment']
        writer=csv.DictWriter(handle,fieldnames=fields); writer.writeheader()
        for r in results:
            for c in r['candidates']:
                writer.writerow(dict(track=r['track'],candidate=c['label'],loop_start=c['loop_start'],loop_end=c['loop_end'],
                                     crossfade_ms=c['suggested_crossfade_ms'],overall_score=c['overall_score'],status='unreviewed',comment=''))
    # Deliberately disabled draft. Runtime does not read this file.
    ini=configparser.ConfigParser()
    ini['pack']={'schema_version':'1','id':'remake','timebase':'decoded_pcm','status':'unreviewed','enabled':'false',
                 'crossfade_mode':'linear_overlap','resume_after_crossfade':'loop_start_plus_crossfade'}
    for r in results:
        if not r['candidates']:
            continue
        c=r['candidates'][0]
        ini[r['track']]={'status':'unreviewed','candidate':'A','source_sha256':r['source']['sha256'],
             'sample_rate':str(r['source']['sample_rate']),
             **{k: str(c[k]) for k in ('loop_start','loop_end','loop_start_sample','loop_end_sample','safe_resume_start','safe_resume_end')},
             'crossfade_ms':str(c['suggested_crossfade_ms'])}
    with (args.output/'loops.proposed.ini').open('w',encoding='utf-8') as h:
        h.write('; AUDIO4 proposal only. A is a numerical rank, NOT an approved selection.\n'); ini.write(h)
    content=['<!doctype html><meta charset="utf-8"><title>AUDIO4 listening review</title>',
       '<style>body{font:17px system-ui;max-width:1100px;margin:40px auto;background:#111820;color:#eef;padding:20px}a{color:#8dd}article{background:#202c38;padding:20px;margin:15px 0;border-radius:8px}audio{width:100%;margin:10px 0}select,input,button{font:inherit;padding:7px}small{color:#bbc}</style>',
       '<h1>AUDIO4 · Loop listening review</h1><p>All candidates are unreviewed. A/B/C are numerical ranks, not approval. '
       'Listen for clicks, missing/doubled beats, tonal jumps and repetition. No Android validation yet.</p>',
       '<p>Primary: 6 seconds before → join → 6 seconds after, then a 1-second silent preview reset and the same join again. '
       'The reset is not a loop boundary. Comparison: hard, 25, 50, 100, 150, 250 ms in that order; each has 4 seconds either side and 1 second silence. '
       'Join centers at 4, 13, 22, 31, 40, 49 seconds. Equal preview gain within each reel; no loudness matching across tracks.</p>',
       '<p>To assess the full phrase, use the full-loop render command in the README after shortlisting. '
       'Download your review before closing; choices are not sent anywhere.</p><button onclick="downloadReview()">Download my review JSON</button>']
    review_seed=[]
    for r in results:
        content.append('<h2>'+html.escape(r['track'])+'</h2><small>'+html.escape(r['source']['filename'])+'</small>')
        for c in r['candidates']:
            key=r['track']+'_'+c['label']
            review_seed.append(dict(id=key,track=r['track'],candidate=c['label'],source_sha256=r['source']['sha256'],
                                    loop_start_sample=c['loop_start_sample'],hard_loop_end_sample=c['hard_loop_end_sample'],
                                    sample_rate=r['source']['sample_rate']))
            content.append(f'<article><h3>Candidate {c["label"]} · {c["loop_start"]:.3f}–{c["loop_end"]:.3f}s · {c["suggested_crossfade_ms"]}ms fade</h3>'
              f'<p>Effective loop: {c["effective_period"]:.1f}s. Provisional resume: {c["safe_resume_start"]:.3f}–{c["safe_resume_end"]:.3f}s. '
              f'Rank score: {c["overall_score"]:.1f}.</p>'
              f'<audio controls preload="none" src="{c["audition"]["file"]}"></audio>'
              f'<details><summary>Compare all six crossfades</summary><audio controls preload="none" src="{c["audition"]["comparison_file"]}"></audio></details>'
              f'<p>{html.escape(" ".join(c["notes"][2:]))}</p>'
              f'<label>Verdict <select id="{key}"><option>unreviewed</option><option>best</option><option>acceptable</option><option>bad</option><option>manual refinement</option></select></label> '
              f'<label>Fade <select id="{key}_fade">'+''.join(f'<option {"selected" if ms==c["suggested_crossfade_ms"] else ""}>{ms}</option>' for ms in FADES)+
              f'</select> ms</label> <input id="{key}_note" placeholder="Listening notes"></article>')
    content.append('<script>const seed='+json.dumps(review_seed)+'; function downloadReview(){const data=seed.map(x=>{const ms=Number(document.getElementById(x.id+"_fade").value);return {...x,status:document.getElementById(x.id).value,crossfade_ms:ms,loop_end_sample:x.hard_loop_end_sample+Math.round(ms*x.sample_rate/1000),note:document.getElementById(x.id+"_note").value}});const a=document.createElement("a");a.href=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:"application/json"}));a.download="audio4-human-review.json";a.click();}</script>')
    (args.output/'listen.html').write_text('\n'.join(content),encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('--output',type=Path,default=Path('dist/audio4'))
    p.add_argument('--ffmpeg',default=shutil.which('ffmpeg')); p.add_argument('--ffprobe',default=shutil.which('ffprobe'))
    p.add_argument('--track',action='append',help='Canonical ID, e.g. medical_explore; repeatable')
    p.add_argument('--inventory-only',action='store_true'); p.add_argument('--candidates',type=int,choices=range(1,6),default=3)
    for option in ('start-min','start-max','end-min','end-max','min-loop'):
        p.add_argument('--'+option,type=float)
    p.add_argument('--full-loop',choices=list('ABCDE'),help='Render an existing candidate 3 times; requires exactly one --track')
    args=p.parse_args()
    if not args.ffmpeg or not args.ffprobe:
        p.error('Specify --ffmpeg and --ffprobe or add both to PATH')
    args.source=args.source.resolve(); args.output=args.output.resolve()
    repo=Path(__file__).resolve().parents[2]
    # Fail closed: generated commercial media must be ignored, wherever output is.
    if args.output==args.source or args.source in args.output.parents or args.output in args.source.parents:
        p.error('Output and source must be separate directory trees')
    if repo in args.output.parents:
        check=subprocess.run(['git','check-ignore','-q',str(args.output/'audio4-ignore-probe.wav')],cwd=repo)
        if check.returncode:
            p.error('Output inside repository must be git-ignored first')
    if args.min_loop is not None and args.min_loop <= 0:
        p.error('--min-loop must be positive')
    args.output.mkdir(parents=True,exist_ok=True)
    tracks,missing,ignored=discover(args.source)
    if not tracks:
        p.error('No recognized Explore/Combat MP3 files')
    if args.track and any(t not in tracks for t in args.track):
        p.error('Track not found; available: '+', '.join(tracks))
    if args.full_loop:
        if not args.track or len(args.track)!=1:
            p.error('--full-loop requires exactly one --track')
        key=args.track[0]; result=json.loads((args.output/(key+'.json')).read_text(encoding='utf-8'))
        if sha(tracks[key]) != result['source']['sha256']:
            p.error('Source fingerprint changed')
        c=next(c for c in result['candidates'] if c['label']==args.full_loop)
        sr=result['source']['sample_rate']; pcm=decode(tracks[key],args.ffmpeg,channels=result['source']['channels'])
        s,e=c['loop_start_sample'],c['hard_loop_end_sample']; n=round(c['suggested_crossfade_ms']*sr/1000)
        # First body, then two actual end->start joins; no fabricated middle resets.
        pieces=[pcm[s:e]]
        for _ in range(2):
            if n:
                w=np.linspace(0,1,n,dtype=np.float32)[:,None]
                pieces += [pcm[e:e+n]*(1-w)+pcm[s:s+n]*w,pcm[s+n:e]]
            else:
                pieces += [pcm[s:e]]
        dest=args.output/(key+'_candidate_'+args.full_loop+'_full.wav')
        write_wav(dest,np.concatenate(pieces),sr); print(dest); return
    inventory=dict(source_directory=str(args.source),tracks={k:probe(v,args.ffprobe) for k,v in tracks.items()},
                   missing=missing,unmapped_files=ignored,
                   tools=dict(python=sys.version.split()[0],numpy=np.__version__,
                              ffmpeg=run([args.ffmpeg,'-version']).decode().splitlines()[0],
                              ffprobe=run([args.ffprobe,'-version']).decode().splitlines()[0],decoder='FFmpeg native MP3 -> float32 PCM'))
    save_json(args.output/'inventory.json',inventory)
    print('Found '+str(len(tracks))+' tracks. Missing: '+str(missing),flush=True)
    if args.inventory_only:
        return
    # Do not overwrite listener notes; generated blank CSV is explicitly a template.
    for key,path in tracks.items():
        if args.track and key not in args.track:
            continue
        analyze(key,path,inventory['tracks'][key],args)
    index_outputs(args,inventory)


if __name__=='__main__':
    main()
