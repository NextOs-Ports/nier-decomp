"""Regressão: a rodada mais recente deve revogar qualquer reprovação anterior."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class OverrideGate(unittest.TestCase):
    def test_latest_failed_run_revokes_verified_function(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory = root / 'inventory.json'
            inventory.write_text(json.dumps({'functions': [
                {'name': 'sub_00000001', 'status': 'verified'},
                {'name': 'sub_8256BB90', 'status': 'verified'}]}))
            run = root / 'run.json'
            output = root / 'overrides.h'
            for reason in ('poucas tentativas executaram sem falha de acesso',
                           'mais lenta que a tradução', 'divergência'):
                with self.subTest(reason=reason):
                    run.write_text(json.dumps({'results': [
                        {'name': 'sub_00000001', 'pass': False, 'error': reason}]}))
                    subprocess.run([sys.executable,
                        str(Path(__file__).with_name('gen_overrides_header.py')),
                        str(inventory), str(output), '--run', str(run)],
                        check=True, capture_output=True)
                    self.assertEqual(output.with_suffix('.txt').read_text().split(), [])


if __name__ == '__main__':
    unittest.main()
