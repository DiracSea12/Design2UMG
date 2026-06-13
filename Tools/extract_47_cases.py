#!/usr/bin/env python3
"""Extract all 47 case specs from formal gate for the full isolation experiment."""
import json
from pathlib import Path

run_root = Path(r'G:\UE5\UnrealEngine\Engine\Plugins\Marketplace\Design2UMG\Saved\Design2UMG\Automation\_formal_full_47_20260415_shared_semantics_refroot')

cases = []
for case_dir in sorted(run_root.iterdir()):
    if not case_dir.is_dir():
        continue
    spec_file = case_dir / 'automation_generate_spec.json'
    if not spec_file.is_file():
        continue
    try:
        spec = json.loads(spec_file.read_text(encoding='utf-8-sig'))
        mode = spec.get('mode', '')
        source = spec.get('source_path', '')
        node = spec.get('node_id', '')
        name = case_dir.name
        cases.append((name, mode, source, node))
    except Exception as e:
        print(f'ERROR {case_dir.name}: {e}', file=__import__('sys').stderr)

print(f'Found {len(cases)} cases:')
for c in cases:
    print(repr(c))