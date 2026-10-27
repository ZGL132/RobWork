"""PRJ-T19: independent log/JSON evidence summaries; never edits product files."""
import json
import math
import re
from pathlib import Path

E = Path('RobWork/doc/industrial-robot-design/traceability/project-review-r1')

def save(name, data):
    (E/name).write_bytes((json.dumps(data, ensure_ascii=False, indent=2)+'\n').encode())

groups = {}
for label, name in [('beforeDedupAfter13Fixes', 'perf-project.log'), ('afterDedup', 'perf-project-after.log')]:
    text = (E/name).read_text(encoding='utf-8-sig')
    samples = [{'excluded': flag == '1', 'durationMs': float(ms)} for flag, ms in re.findall(
        r'\[b04-hot\] sample \(excluded=([01]) valid=1\): ([\d.]+) ms', text)]
    values = sorted(x['durationMs'] for x in samples if not x['excluded'])
    assert len(samples) == 16 and len(values) == 15, (label, len(samples))
    h = (len(values)-1)*0.95
    i = math.floor(h)
    p95 = values[i] + (h-i)*(values[min(i+1, len(values)-1)]-values[i])
    groups[label] = {'source': name, 'warmupCount': 1, 'validCount': len(values),
        'p95MsFromRoundedLog': p95, 'thresholdMs': 3000, 'pass': p95 <= 3000, 'samples': samples}
save('perf-hot-oracle.json', {'method': 'Python standard library; independently parse 16 printed samples and interpolate sorted 15 valid values; log rounding is 0.001ms', 'groups': groups})

reports = {}
for name in ['full-unit.ird-test-report.json', 'full-contract.ird-test-report.json',
             'consumer-execution.ird-test-report.json', 'smoke-project.ird-test-report.json',
             'dedup-unit.ird-test-report.json', 'final-process.ird-test-report.json',
             'smoke-final.ird-test-report.json', 'perf-project-before.ird-test-report.json',
             'perf-project-after.ird-test-report.json', 'perf-cold-details.ird-test-report.json']:
    data = json.loads((E/name).read_text(encoding='utf-8-sig'))
    reports[name] = data['summary']
save('verification-summary.json', {'reports': reports,
    'note': 'Runs overlap. Do not sum them as distinct tests. Full unit 304 precedes final recovery dedup/new test; final affected 58 include 15 review cases. No independent acceptance is claimed.'})
print(json.dumps({key: value['p95MsFromRoundedLog'] for key, value in groups.items()}, ensure_ascii=False))
