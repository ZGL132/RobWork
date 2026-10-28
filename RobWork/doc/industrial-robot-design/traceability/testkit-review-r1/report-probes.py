"""TK-T13：真实 listener 的独立 JSON/XML/退出码交叉核对，不运行 legacy UB 样本。"""
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
results = []

def reject_constant(value):
    raise ValueError('非法 JSON 数值常量: ' + value)

for name, expected in [('Passes', 'passed'), ('Fails', 'failed'), ('Skips', 'skipped'),
        ('NonFiniteDetails_ReviewR03', 'failed'), ('MissingActual_ReviewR03', 'failed'),
        ('MissingExpected_ReviewR03', 'failed'), ('ExactSeed_ReviewR06', 'passed')]:
    base = args.out / name
    xml = base.with_suffix('.xml')
    native = base.with_suffix('.json')
    assert not xml.exists() and not native.exists(), '不可复用旧结果文件'
    command = [str(args.exe.resolve()), '--gtest_filter=LsnChild.' + name,
               '--gtest_output=xml:' + str(xml.resolve()), '--ird_report=' + str(native.resolve())]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    base.with_suffix('.log').write_bytes(run.stdout)
    result = dict(name=name, command=command, actualExit=run.returncode, expected=expected)
    try:
        report = json.loads(native.read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        root = ET.parse(xml).getroot()
        cases = root.findall('.//testcase')
        assert len(cases) == 1 and int(root.attrib['tests']) == 1, '过滤必须实际命中一次'
        assert run.returncode == (1 if expected == 'failed' else 0)
        assert len(report['tests']) == 1
        record = report['tests'][0]
        assert record['testId'] == 'LsnChild.' + name and record['outcome'] == expected
        for outcome in ['passed', 'failed', 'skipped', 'notRun', 'envUnavailable', 'datasetInvalid']:
            assert report['summary'][outcome] == int(outcome == expected)
        assert bool(cases[0].find('failure') is not None) == (expected == 'failed')
        assert bool(cases[0].find('skipped') is not None) == (expected == 'skipped')
        if name == 'NonFiniteDetails_ReviewR03':
            details = {c['fieldPath']: c for c in record['comparisons']}
            assert len(details) == 4
            for field, token in [('positive', '+Inf'), ('negative', '-Inf'), ('nan', 'NaN')]:
                assert details[field]['actual'] == token
                assert details[field]['expected'] == 0
            assert details['overflow']['actual'] == float.fromhex('0x1.fffffffffffffp+1023')
            assert details['overflow']['expected'] == -float.fromhex('0x1.fffffffffffffp+1023')
            assert details['overflow']['diff'] == '+Inf'
        elif name.startswith('Missing'):
            assert len(record['comparisons']) == 1
            c = record['comparisons'][0]
            missing = 'actual' if name.startswith('MissingActual') else 'expected'
            present = 'expected' if missing == 'actual' else 'actual'
            assert c[missing] is None and c[present] == 1 and c['diff'] is None
        elif name == 'ExactSeed_ReviewR06':
            seed = record['repro']['seed']
            assert type(seed) is int and seed == 18446744073709551615
        result['status'] = 'PASS'
        result['summary'] = report['summary']
    except Exception as error:
        result.update(status='FAIL', error=repr(error))
    results.append(result)
(args.out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(results, ensure_ascii=False, indent=2))
raise SystemExit(any(r['status'] != 'PASS' for r in results))
