"""Play the authored D7 action route, inspect saved gameplay, and resume a moving door."""
import json
from pathlib import Path
import subprocess
import sys

exe, recording, root = map(Path, sys.argv[1:])
root.mkdir(parents=True, exist_ok=True)
base = [str(exe), '--headless', '--deterministic', '--width', '160', '--height', '90',
        '--threads', '2', '--fixed-dt', '0.05', '--replay-input', str(recording)]


def run(name, args):
    report, shot = root / (name + '.json'), root / (name + '.png')
    result = subprocess.run(base + args + ['--report', str(report), '--screenshot', str(shot)],
                            capture_output=True, text=True, timeout=360)
    (root / (name + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    data = json.loads(report.read_text())
    assert data['validation'] == {'errors': 0, 'warnings': 0}
    assert shot.stat().st_size > 0
    return data, shot.read_bytes()


slot = root / 'complete.save.json'
full, pixels = run('full', ['--save-at-exit', str(slot)])
saved = json.loads(slot.read_text())
player = saved['game']['bunker']['walker']
assert abs(player['feet'][0] + 4) < 0.35 and abs(player['feet'][2]) < 0.35, player
assert player['grounded']
entities = {e['id']: e for e in saved['world']['entities']}
assert entities['dormitory/power_panel']['electricalPanel']['repaired']
assert entities['corridor/electrical_panel_1']['electricalPanel']['repaired']
assert entities['generator_room/generator_1']['powerSwitch']['enabled']
door = entities['dormitory/door_leaf_1/leaf']['door']
assert door['open'] and door['fraction'] == 1 and not door['obstructed'], door
for entity in entities.values():
    light = entity.get('emergencyLight')
    if light and light['room'] in ('dormitory', 'corridor'):
        intensity = entity['pointLight']['intensity']
        assert intensity == (0 if light['emergency'] else light['intensity']), entity

# The first door interaction closes it. Save while it moves, with a persistent door timer.
rows = [json.loads(line) for line in recording.read_text().splitlines()]
uses = [r['tick'] for r in rows if r.get('actions', {}).get('interact', [0, 0, False, False])[3]]
assert len(uses) == 5
mid = root / 'moving.save.json'
run('prefix', ['--frames', str(uses[3] + 8), '--save-at-exit', str(mid)])
moving = json.loads(mid.read_text())
moving_door = next(e['door'] for e in moving['world']['entities'] if e['id'] == 'dormitory/door_leaf_1/leaf')
assert 0 < moving_door['fraction'] < 1, moving_door
resumed, resumed_pixels = run('resumed', ['--load-save', str(mid)])
assert resumed['input']['stateHash'] == full['input']['stateHash']
assert resumed_pixels == pixels
print('D7 route: dormitory, panels, generator, sliding door, airlock; moving-door save continuation matches state and pixels')
