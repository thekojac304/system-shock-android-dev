"""Validate human AUDIO4 reviews and write a compact selected-loop manifest.

No audio is copied or changed. A source directory is required so stale metadata
cannot be generated from a review of different MP3 bytes.
"""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

FIELDS = ('track', 'candidate', 'status', 'source_sha256', 'sample_rate',
          'loop_start_sample', 'hard_loop_end_sample', 'loop_end_sample',
          'crossfade_ms', 'note')


def select(rows):
    groups = defaultdict(list)
    for row in rows:
        if not isinstance(row, dict) or not all(k in row for k in FIELDS):
            raise ValueError('Malformed review row')
        if row['status'] not in ('best', 'acceptable', 'bad', 'unreviewed'):
            raise ValueError('Unknown status: ' + str(row['status']))
        groups[row['track']].append(row)
    selected = []
    for track, choices in sorted(groups.items()):
        if len({r['candidate'] for r in choices}) != len(choices):
            raise ValueError('Duplicate candidate: ' + track)
        best = [r for r in choices if r['status'] == 'best']
        approved = best or [r for r in choices if r['status'] == 'acceptable']
        if len(approved) != 1:
            raise ValueError('Ambiguous or unapproved track: ' + track)
        row = approved[0]
        if any(r['source_sha256'] != row['source_sha256'] or
               r['sample_rate'] != row['sample_rate'] for r in choices):
            raise ValueError('Inconsistent source identity: ' + track)
        rate = row['sample_rate']
        start = row['loop_start_sample']
        hard_end = row['hard_loop_end_sample']
        end = row['loop_end_sample']
        fade = row['crossfade_ms']
        if not all(type(x) is int for x in (rate, start, hard_end, end, fade)) or \
           rate <= 0 or start < 0 or hard_end <= start or end < hard_end or \
           fade < 0 or abs((end - hard_end) - round(fade * rate / 1000)) > 1:
            raise ValueError('Invalid timing: ' + track)
        if len(row['source_sha256']) != 64 or any(c not in '0123456789abcdef' for c in row['source_sha256']):
            raise ValueError('Invalid SHA-256: ' + track)
        selected.append(row)
    return selected


def verify_sources(selected, source_dir, candidates_csv):
    with candidates_csv.open(encoding='utf-8', newline='') as f:
        catalog = {(r['track'], r['candidate']): r for r in csv.DictReader(f)}
    for row in selected:
        track = row['track']
        analysis = catalog.get((track, row['candidate']))
        if not analysis or analysis['source_sha256'] != row['source_sha256'] or \
           int(analysis['loop_start_sample']) != row['loop_start_sample']:
            raise ValueError('Review disagrees with analysis: ' + track)
        path = source_dir / analysis['source_filename']
        if not path.is_file():
            raise ValueError('Source missing: ' + str(path))
        with path.open('rb') as f:
            digest = hashlib.file_digest(f, 'sha256').hexdigest()
        if digest != row['source_sha256']:
            raise ValueError('Source hash mismatch: ' + track)


def write_manifest(selected, path):
    with path.open('w', encoding='utf-8', newline='') as f:
        f.write('# AUDIO4 selected loops v1; decoded PCM samples; validated source SHA-256\n')
        writer = csv.DictWriter(f, fieldnames=FIELDS, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows({k: r[k] for k in FIELDS} for r in selected)


def write_header(selected, path):
    lines = ['/* Generated from AUDIO4 human review by select_review.py. */',
             '#ifndef SHOCK_AUDIO4_LOOPS_H', '#define SHOCK_AUDIO4_LOOPS_H',
             '#include <stdint.h>',
             'typedef struct { const char *track, *sha256; uint32_t rate; uint64_t start, hard_end, end; unsigned fade_ms; } Audio4Loop;',
             'static const Audio4Loop audio4_loops[] = {']
    for r in selected:
        lines.append('    {"%s", "%s", %d, %dULL, %dULL, %dULL, %d},' %
                     (r['track'], r['source_sha256'], r['sample_rate'],
                      r['loop_start_sample'], r['hard_loop_end_sample'],
                      r['loop_end_sample'], r['crossfade_ms']))
    lines += ['};', '#define AUDIO4_LOOP_COUNT (sizeof(audio4_loops)/sizeof(audio4_loops[0]))', '#endif']
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('review', type=Path)
    parser.add_argument('source_dir', type=Path)
    parser.add_argument('--candidates', type=Path, default=Path('docs/AUDIO4_CANDIDATES.csv'))
    parser.add_argument('--output', type=Path, default=Path('docs/AUDIO4_SELECTED_LOOPS.tsv'))
    parser.add_argument('--header', type=Path, default=Path('SourceSnapshot/src/MacSrc/Audio4Loops.h'))
    args = parser.parse_args()
    selected = select(json.loads(args.review.read_text(encoding='utf-8')))
    verify_sources(selected, args.source_dir, args.candidates)
    write_manifest(selected, args.output)
    write_header(selected, args.header)
    print(f'{len(selected)} selected tracks; all source SHA-256 hashes match; wrote {args.output} and {args.header}')


if __name__ == '__main__':
    main()
