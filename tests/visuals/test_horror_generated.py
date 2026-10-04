"""Focused accepted-input checks for the QA generated-light asset compiler."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('horror_generated', ROOT / 'scripts/compile-horror-generated.py')
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class GeneratedAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'dist/audio2/owned-ARCHIVE.DAT'
        if not path.exists():
            raise unittest.SkipTest('Owned archive is required for accepted-profile compilation')
        cls.archive = path.read_bytes()
        cls.rules = json.loads((ROOT / 'profiles/horror-author1/rules.json').read_text())

    def test_deterministic_bounded_profiles_and_masks(self):
        for level in (1, 6):
            first, summary = compiler.encode_level(level, self.archive, self.rules)
            second, _ = compiler.encode_level(level, self.archive, self.rules)
            self.assertEqual(first, second)
            header = struct.unpack_from('<4s6I32s32s', first)
            self.assertEqual(header[:3], (b'HG1P', 1, level))
            self.assertEqual(zlib.crc32(first[92:]), header[6])
            self.assertEqual(header[3], summary['lights'])
            self.assertEqual(header[4], summary['surfaces'])
            self.assertLess(len(first), 300000)
            mask_bytes = first[-2560:]
            for row, material in enumerate(compiler.MASK_IDS):
                actual = {i for i, bit in enumerate(mask_bytes[row*256:(row+1)*256]) if bit}
                self.assertEqual(actual, set(self.rules['reviewed_emissive_indices'][str(material)]))

    def test_stale_rules_and_archive_rejected(self):
        changed = dict(self.rules)
        changed['version'] = 999
        with self.assertRaisesRegex(ValueError, 'signature'):
            compiler.encode_level(1, self.archive, changed)
        resources = compiler.author.ARCHIVE(self.archive, 4100, 4200)
        changed_resources = dict(resources)
        changed_map = bytearray(resources[4105])
        changed_map[1000] ^= 1
        changed_resources[4105] = bytes(changed_map)
        with patch.object(compiler.author, 'ARCHIVE', return_value=changed_resources):
            with self.assertRaisesRegex(ValueError, 'archive|baseline|Incompatible'):
                compiler.encode_level(1, self.archive, self.rules)


if __name__ == '__main__':
    unittest.main()
