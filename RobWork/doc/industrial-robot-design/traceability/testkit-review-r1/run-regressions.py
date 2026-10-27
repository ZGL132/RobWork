"""TK-T13：只运行稳定源码的受影响用例，独立核对命中/XML/JSON与退出码。"""
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--only', choices=['testkit-final', 'optimization', 'policy', 'project'])
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
binary = args.repo / 'build/RobWorkStudio/bin/Release'
selected = [
 ('testkit-final', 'sdurws_ird_testkit_test',
  'CheckClose.*:AllCloseWithin.*:CheckAtMost.*:CheckIdentical.*:AssertMacrosBehavior.*:'
  'Set*.*:StableOrder.*:ToleranceProfile*.*:Dataset*.*:TempDir*.*:ReproRecord*.*:'
  'DeterministicEnv*.*:DemoFixture.*:PureFixture.*:Json*.*:PerfBenchmark.*:'
  'ReproBenchmark.*:TestRecordJson.*:Report*.*:RecordStore.*:RptDemoFixture.*:'
  'ListenerOwnership.*:ProcessRunner*.*:TestdataLint.*'),
 ('optimization', 'sdurws_ird_optimization_test',
  'OptGoldenLhs.SeededBatchMatchesGoldenAndReplays_WP20T11_ACC1:'
  'OptGoldenPareto.OrchestratorRunMatchesGolden_WP20T11_ACC1'),
 ('policy', 'sdurws_ird_policy_contract_test',
  'PolicyContractDoubles.ThreeEntriesShareSingleEvaluatorInstanceAndEquivalentOutputs:'
  'PolicyContractDoubles.At19ThreeEntriesIdenticalIdPairsVerdictsAndReasonCodes'),
 ('project', 'sdurws_ird_project_contract_test',
  'Tx7ProcessLock.SecondInstanceChildDegradesReadOnly_ReportsHolderPid')]
results = []

def reject_constant(value):
    raise ValueError('非法 JSON 数值: ' + value)

for name, exe, filters in selected:
    if args.only and name != args.only:
        continue
    xml = args.out / (name + '.xml')
    native = args.out / (name + '.ird-test-report.json')
    assert not xml.exists() and not native.exists(), '禁止旧报告冒充本次结果'
    command = [str((binary / (exe + '.exe')).resolve()), '--gtest_filter=' + filters,
        '--gtest_output=xml:' + str(xml.resolve()), '--ird_report=' + str(native.resolve())]
    run = subprocess.run(command, cwd=args.repo, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, timeout=300)
    (args.out / (name + '.log')).write_bytes(run.stdout)
    result = dict(name=name, command=command, actualExit=run.returncode)
    try:
        root = ET.parse(xml).getroot()
        cases = root.findall('.//testcase')
        report = json.loads(native.read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        assert len(cases) == int(root.attrib['tests']) > 0, '过滤必须命中真实用例'
        assert len(cases) == len(report['tests']) == report['summary']['total']
        assert run.returncode == 0
        assert int(root.attrib['failures']) == 0 and not root.findall('.//testcase/failure')
        assert not root.findall('.//testcase/skipped')
        assert all(t['outcome'] == 'passed' for t in report['tests'])
        assert report['summary']['passed'] == len(cases)
        for field in ['failed', 'skipped', 'notRun', 'envUnavailable', 'datasetInvalid']:
            assert report['summary'][field] == 0
        result.update(status='PASS', passed=len(cases), failed=0, skipped=0)
    except Exception as error:
        result.update(status='FAIL', error=repr(error))
    results.append(result)
(args.out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('\n'.join(str(r) for r in results))
raise SystemExit(any(r['status'] != 'PASS' for r in results))
