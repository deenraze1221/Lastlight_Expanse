"""Save a completed replay checkpoint, restore it, and compare continuation with a full run."""
import json
from pathlib import Path
import subprocess
import sys

exe, root = Path(sys.argv[1]), Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)

def run(name, args, success=True):
    result = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=180)
    (root / (name + '.log')).write_text(result.stdout + result.stderr)
    assert (result.returncode == 0) == success, f'{name}: {result.returncode}\n{result.stdout}\n{result.stderr}'
    return result.stdout + result.stderr

for level in ['bunker', 'highlands']:
    base = ['--headless', '--deterministic', '--level', level, '--width', '160', '--height', '90', '--threads', '0']
    base += ['--explore'] if level == 'bunker' else ['--view-radius', '1']
    recording = root / (level + '.inputrec.jsonl')
    run(level+'-record', base+['--frames', '12', '--record-input', str(recording)])
    # Use movement, edges and a voxel edit, rather than testing only idle replay.
    rows = [json.loads(line) for line in recording.read_text().splitlines()][:-1]
    for row in rows[1:]:
        row['state'] = ''
        row['actions']['move'] = [0, 1, False, False, False]
        row['actions']['look'] = [2, 0, False, False, False]
        if level == 'highlands':
            row['actions']['look_hold'] = [1, 0, True, row['tick'] == 1, False]
            row['actions']['dig'] = [1, 0, True, True, False] if row['tick'] == 3 else [0, 0, False, False, row['tick'] == 4]
    payload = ''.join(json.dumps(row, separators=(',', ':'))+'\n' for row in rows).encode()
    value = 14695981039346656037
    for byte in payload:
        value = ((value ^ byte)*1099511628211) & ((1 << 64)-1)
    recording.write_bytes(payload+(json.dumps({'end': 12, 'hash': f'{value:016x}'})+'\n').encode())
    replay = base+['--replay-input', str(recording)]
    full_png, full_report = root/(level+'-full.png'), root/(level+'-full.json')
    run(level+'-full', replay+['--screenshot', str(full_png), '--report', str(full_report)])
    slot = root/(level+'.save.json')
    run(level+'-prefix', replay+['--frames', '6', '--save-at-exit', str(slot), '--autosave-interval', '0.05'])
    saved = json.loads(slot.read_text())
    assert saved['tick'] == 6 and saved['runtime']['cursor'] == 6
    assert Path(str(slot)+'.bak1').exists()
    for suffix in ['resume', 'recover']:
        if suffix == 'recover':
            Path(str(slot)+'.bak1').write_bytes(slot.read_bytes())
            slot.write_text('corrupt')
        png, report = root/(level+'-'+suffix+'.png'), root/(level+'-'+suffix+'.json')
        log = run(level+'-'+suffix, replay+['--load-save', str(slot), '--screenshot', str(png), '--report', str(report)])
        if suffix == 'recover':
            assert 'Recovered save' in log
        data, expected = json.loads(report.read_text()), json.loads(full_report.read_text())
        assert data['input']['ticks'] == 12
        assert data['input']['stateHash'] == expected['input']['stateHash'], 'continuation state differs'
        assert data['validation'] == {'errors': 0, 'warnings': 0}
        assert png.read_bytes() == full_png.read_bytes(), 'continuation screenshot differs'
    slot.write_text('{}')
    for i in range(1,4):
        Path(str(slot)+f'.bak{i}').unlink(missing_ok=True)
    run(level+'-invalid', replay+['--load-save', str(slot)], False)
print('Bunker and Highlands replay/save/load/continue, autosave, recovery and rejection passed')
