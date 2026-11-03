"""Real game record/replay: fixed actions, identical pixels, state checks and rejection paths."""
import copy
import json
from pathlib import Path
import subprocess
import sys

exe, root = Path(sys.argv[1]), Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)


def run(label, args, success=True):
    result = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=120)
    (root / (label + '.log')).write_text(result.stdout + result.stderr)
    if (result.returncode == 0) != success:
        raise AssertionError(f'{label}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result.stdout + result.stderr


def hash_bytes(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f'{value:016x}'


def write_record(path, rows):
    data = ''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows).encode()
    path.write_bytes(data + (json.dumps({'end': len(rows)-1, 'hash': hash_bytes(data)}) + '\n').encode())


for level in ['bunker', 'highlands']:
    base = ['--headless', '--deterministic', '--level', level, '--width', '160', '--height', '90', '--threads', '0']
    base += ['--explore'] if level == 'bunker' else ['--view-radius', '1']
    recording = root / (level + '.inputrec.jsonl')
    live_png = root / (level + '-live.png')
    run(level + '-record', base + ['--frames', '12', '--record-input', str(recording), '--screenshot', str(live_png)])
    for repeat in range(2):
        png, report = root / f'{level}-replay{repeat}.png', root / f'{level}-replay{repeat}.json'
        run(level + f'-replay{repeat}', base + ['--replay-input', str(recording), '--screenshot', str(png), '--report', str(report)])
        data = json.loads(report.read_text())
        assert data['input']['ticks'] == 12 and data['input']['mode'] == 'replay'
        assert data['validation'] == {'errors': 0, 'warnings': 0}
        assert png.read_bytes() == live_png.read_bytes(), 'record/replay pixels differ'
    rows = [json.loads(line) for line in recording.read_text().splitlines()][:-1]
    idle_state = json.loads((root / f'{level}-replay0.json').read_text())['input']['stateHash']
    changed = copy.deepcopy(rows)
    for tick in changed[1:]:
        tick['state'] = ''  # hand-authored recordings can omit checkpoints
        tick['actions']['move'] = [0, 1, False, False, False]
        tick['actions']['look'] = [2, 0, False, False, False]
        if level == 'highlands':
            tick['actions']['look_hold'] = [1, 0, True, tick['tick'] == 1, False]
            tick['actions']['dig'] = [1, 0, True, True, False] if tick['tick'] == 3 else [0, 0, False, False, tick['tick'] == 4]
            tick['actions']['place'] = [1, 0, True, True, False] if tick['tick'] == 7 else [0, 0, False, False, tick['tick'] == 8]
    moving = root / (level + '-moving.inputrec.jsonl')
    write_record(moving, changed)
    for repeat in range(2):
        png, report = root / f'{level}-moving{repeat}.png', root / f'{level}-moving{repeat}.json'
        run(level + f'-moving{repeat}', base + ['--replay-input', str(moving), '--screenshot', str(png), '--report', str(report)])
        data = json.loads(report.read_text())
        assert data['input']['stateHash'] != idle_state, 'injected movement did not change simulation'
        assert data['validation'] == {'errors': 0, 'warnings': 0}
    assert (root / f'{level}-moving0.png').read_bytes() == (root / f'{level}-moving1.png').read_bytes()
    assert json.loads((root / f'{level}-moving0.json').read_text())['input']['stateHash'] == json.loads((root / f'{level}-moving1.json').read_text())['input']['stateHash']
    broken = copy.deepcopy(rows)
    broken[1]['state'] = 'wrong-state'
    bad = root / (level + '-bad.inputrec.jsonl')
    write_record(bad, broken)
    assert 'state mismatch at tick 1' in run(level + '-bad-state', base + ['--replay-input', str(bad)], False)
    broken = copy.deepcopy(rows)
    broken[0]['inputMapHash'] = 'changed'
    write_record(bad, broken)
    assert 'incompatible recording' in run(level + '-bad-map', base + ['--replay-input', str(bad)], False)
    run(level + '-lenient-map', base + ['--replay-input', str(bad), '--replay-lenient'])
print('Bunker and Highlands: record, replay twice, moving replay twice, divergence and compatibility checks passed')

# Recording uses the fixed clock even when render frames advance at wall-clock speed.
variable = root / 'variable.inputrec.jsonl'
base = ['--headless', '--level', 'bunker', '--explore', '--width', '160', '--height', '90', '--threads', '0']
run('variable-record', base + ['--frames', '60', '--record-input', str(variable)])
run('variable-replay', base + ['--replay-input', str(variable)])
print('Variable-frame-rate recording replayed with verified fixed-tick state')
