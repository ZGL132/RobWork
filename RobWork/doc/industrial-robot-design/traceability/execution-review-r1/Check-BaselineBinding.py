"""只读核实全量复用阻断已存在于起点；不刷新历史生产证据，不改门禁。"""
import hashlib
import json
from pathlib import Path
import subprocess

BASE = '6a49e128c00f8cca3943083819ea59aea16a7be9'
SOURCE = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt'
REPORT = ('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T13/'
          'production/runtime.compileWorkCell.json')

def baseline(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path]).decode('utf-8-sig')

def normalized(text):
    return text.replace('\r\n', '\n').rstrip('\n') + '\n'

old_source = normalized(baseline(SOURCE))
new_source = normalized(Path(SOURCE).read_text(encoding='utf-8-sig'))
old_report = normalized(baseline(REPORT))
new_report = normalized(Path(REPORT).read_text(encoding='utf-8-sig'))
actual = hashlib.sha256(old_source.encode()).hexdigest()
binding = next(b for b in json.loads(old_report)['bindings'] if b['path'] == SOURCE)
print('baseline=' + BASE)
print('source=' + SOURCE)
print('historical report=' + REPORT)
print('baseline and current source identical=' + str(old_source == new_source))
print('baseline and current historical report identical=' + str(old_report == new_report))
print('actual source digest=' + actual)
print('historical bound digest=' + binding['sha256'])
print('baseline mismatch=' + str(actual != binding['sha256']))
assert old_source == new_source and old_report == new_report and actual != binding['sha256']
