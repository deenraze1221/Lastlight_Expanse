"""The replay acceptance check must reject artifacts left by an earlier run."""
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


class ArtifactTests(unittest.TestCase):
    def test_missing_fresh_report_or_save_cannot_use_old_outputs(self):
        script = Path(__file__).with_name('BunkerGameplaySmoke.py')
        for fresh_render in (False, True):
            with self.subTest(fresh_render=fresh_render), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                report, shot, slot = root / 'full.json', root / 'full.png', root / 'complete.save.json'
                for output in (report, shot, slot):
                    output.write_text('stale')

                def omit_save(command, **_kwargs):
                    if fresh_render:
                        report.write_text(json.dumps({'validation': {'errors': 0, 'warnings': 0}}))
                        shot.write_bytes(b'fresh screenshot')
                    return subprocess.CompletedProcess(command, 0, '', '')

                arguments = [str(script), 'unused-executable', 'unused-recording', str(root)]
                with patch.object(sys, 'argv', arguments), patch('subprocess.run', side_effect=omit_save):
                    with self.assertRaises(FileNotFoundError) as error:
                        runpy.run_path(str(script))
                self.assertEqual(Path(error.exception.filename), slot if fresh_render else report)


if __name__ == '__main__':
    unittest.main()
