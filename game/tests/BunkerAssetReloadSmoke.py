"""A model-only hot reload must produce a save that a new process can load."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

exe, source, root = map(Path, sys.argv[1:])
root.mkdir(parents=True, exist_ok=True)
assets = root / 'assets'
shutil.copytree(source, assets, dirs_exist_ok=True)
slot = root / 'reloaded.save.json'
base = [str(exe), '--assets', str(assets), '--headless', '--deterministic',
        '--width', '160', '--height', '90', '--threads', '2', '--fixed-dt', '0.05']
command = base + ['--frames', '300', '--save-at-exit', str(slot),
                  '--report', str(root / 'reload.json')]
process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
lines = []
changed = False
for line in process.stdout:
    lines.append(line)
    if 'Bunker loaded:' in line and not changed:
        model = assets / 'models/generator.glb'
        data = model.read_bytes()
        length = struct.unpack_from('<I', data, 12)[0]
        document = json.loads(data[20:20 + length])
        # Change valid asset bytes without changing geometry, scene order, or scene files.
        document['asset']['generator'] += ' (hot reload save regression)'
        chunk = json.dumps(document, separators=(',', ':')).encode()
        chunk += b' ' * (-len(chunk) % 4)
        binary = data[20 + length:]
        model.write_bytes(struct.pack('<III', 0x46546c67, 2, 20 + len(chunk) + len(binary))
                          + struct.pack('<II', len(chunk), 0x4e4f534a) + chunk + binary)
        changed = True
output = ''.join(lines)
(root / 'reload.log').write_text(output)
assert process.wait() == 0, output
assert changed and 'Hot reload: reloaded ' in output, output
assert 'Level reloaded (generation' not in output, output
assert json.loads((root / 'reload.json').read_text())['validation'] == {'errors': 0, 'warnings': 0}
result = subprocess.run(base + ['--frames', '1', '--load-save', str(slot),
                               '--report', str(root / 'restart.json')],
                        capture_output=True, text=True, timeout=120)
(root / 'restart.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
assert json.loads((root / 'restart.json').read_text())['validation'] == {'errors': 0, 'warnings': 0}
print('Model-only hot reload: exit save loads in a fresh process; zero Vulkan messages')
