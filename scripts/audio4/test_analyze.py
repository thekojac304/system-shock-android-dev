"""Focused tests of timing, source mapping, and multi-signal ranking."""
import argparse
from pathlib import Path
import tempfile
import unittest

import numpy as np
import analyze as a


class AnalyzerTest(unittest.TestCase):
    def test_discovery_precedence_and_album_aliases(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name in ['medical.mp3','medical_explore.mp3',
                         '02 The Ruined Infirmary (Medical [Explore]).mp3',
                         '19 Terminal Interference (Systems Engineering [Combat]).mp3',
                         '24 Domain Linked (Cyberspace).mp3']:
                (root/name).touch()
            tracks,missing,ignored=a.discover(root)
            self.assertEqual(tracks['medical_explore'].name,'medical_explore.mp3')
            self.assertIn('engineering_combat',tracks)
            self.assertEqual(len(missing),20)
            self.assertEqual(len(ignored),1)

    def test_overlap_has_constant_period_and_correct_head_consumption(self):
        pcm=np.column_stack((np.arange(1000),np.arange(1000))).astype(np.float32)
        for n in [0,2,25,50]:
            rendered=a.fade_audio(pcm,100,600,n,80)
            self.assertEqual(len(rendered),160)
            np.testing.assert_array_equal(rendered[:80],pcm[520:600])
            np.testing.assert_array_equal(rendered[80+n:],pcm[100+n:180])
            if n:
                np.testing.assert_array_equal(rendered[80],pcm[600])
                np.testing.assert_array_equal(rendered[80+n-1],pcm[100+n-1])
                # Metadata end includes overlap; effective interval does not.
                self.assertEqual((600+n)-100-n,500)

    def test_tonal_mismatch_not_hidden_by_equal_level(self):
        t=np.arange(a.RATE*24)/a.RATE
        pcm=np.sin(2*np.pi*220*t)*.3
        pcm[12*a.RATE:]=np.sin(2*np.pi*311.13*t[12*a.RATE:])*.3
        f=a.features(np.column_stack((pcm,pcm)).astype(np.float32))
        beat=dict(period=.5,confidence=0)
        matching=a.metrics(f,4,8,beat)
        wrong=a.metrics(f,4,18,beat)
        self.assertLess(matching['feature_cost'],wrong['feature_cost'])
        self.assertGreater(matching['harmonic_similarity'],wrong['harmonic_similarity'])

    def test_search_recovers_periodic_body_and_respects_narrow_bounds(self):
        sr=4000
        t=np.arange(40*sr)/sr
        # Periodic amplitude pattern and harmonics, no copyrighted input.
        mono=(.2*np.sin(2*np.pi*80*t)+.08*np.sin(2*np.pi*113*t))*(.6+.4*np.cos(2*np.pi*t/2))
        pcm=np.column_stack((mono,mono)).astype(np.float32)
        low_t=np.arange(40*a.RATE)/a.RATE
        low=np.interp(low_t,t,mono)
        f=a.features(np.column_stack((low,low)).astype(np.float32))
        args=argparse.Namespace(start_min=5.8,start_max=6.2,end_min=21.8,end_max=22.2,candidates=1)
        rows=a.search(pcm,sr,f,dict(period=.5,confidence=.9,phase=0),4,36,10,args)
        self.assertEqual(len(rows),1)
        _,s,e,m=rows[0]
        self.assertTrue(5.8<=s<=6.2 and 21.8<=e<=22.2)
        self.assertAlmostEqual(e-s,16,delta=.02)
        self.assertGreater(m['waveform_similarity'],.95)

    def test_audition_markers_and_wav_dimensions(self):
        with tempfile.TemporaryDirectory() as d:
            sr=1000
            pcm=np.zeros((30000,2),np.float32)
            pcm[20000:20250]=.5
            result=a.auditions(pcm,sr,7000,20000,100,Path(d)/'test')
            import wave
            with wave.open(str(Path(d)/result['file'])) as w:
                self.assertEqual(w.getnframes(),25000)
                self.assertEqual(w.getnchannels(),2)
            self.assertEqual(result['boundary_seconds'],[6,19])
            self.assertEqual(result['reset_silence_seconds'],[12,13])
            self.assertEqual([c['join_seconds'] for c in result['comparison_chapters']],[4,13,22,31,40,49])


if __name__=='__main__':
    unittest.main()
