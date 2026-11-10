"""Run with: python -m unittest discover -s scripts/audio4 -p test_select_review.py"""
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from select_review import select, verify_sources


class SelectionTest(unittest.TestCase):
    def setUp(self):
        self.row = dict(track='medical_explore', candidate='A', status='acceptable',
                        source_sha256=hashlib.sha256(b'abc').hexdigest(), sample_rate=44100,
                        loop_start_sample=100, hard_loop_end_sample=1000,
                        loop_end_sample=1000, crossfade_ms=0, note='')

    def test_best_wins_and_acceptable_fallback(self):
        best = dict(self.row, candidate='B', status='best')
        bad = dict(self.row, candidate='C', status='bad')
        self.assertEqual(select([self.row, bad, best]), [best])
        self.assertEqual(select([self.row, bad]), [self.row])

    def test_ambiguity_and_timing_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            select([self.row, dict(self.row, candidate='B')])
        with self.assertRaisesRegex(ValueError, 'Invalid timing'):
            select([dict(self.row, loop_end_sample=900)])

    def test_hash_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'medical.mp3').write_bytes(b'abc')
            catalog = root / 'candidates.csv'
            catalog.write_text('track,candidate,source_filename,source_sha256,loop_start_sample\n'
                               f'medical_explore,A,medical.mp3,{self.row["source_sha256"]},100\n')
            verify_sources([self.row], root, catalog)
            (root / 'medical.mp3').write_bytes(b'different')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                verify_sources([self.row], root, catalog)


if __name__ == '__main__':
    unittest.main()
