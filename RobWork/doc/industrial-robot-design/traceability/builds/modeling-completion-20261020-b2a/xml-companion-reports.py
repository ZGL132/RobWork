"""为使用gtest_main的既有core/policy生成明确标注来源的XML派生逐例报告。

不模拟执行，不修改被测目标；无法从XML恢复的IRD_TEST_INFO/种子元数据不编造。
"""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

source = Path.cwd() / 'build/modeling-completion-20261020-b2a'
for name in ['core']:
    path = source / (name + '.xml')
    root = ET.parse(path).getroot()
    records = []
    for c in root.findall('.//testcase'):
        failure, skip = c.find('failure'), c.find('skipped')
        outcome = 'failed' if failure is not None else 'skipped' if skip is not None else 'notRun' if c.attrib.get('status') != 'run' else 'passed'
        records.append(dict(testId=c.attrib['classname'] + '.' + c.attrib['name'], outcome=outcome,
                            reason='' if failure is None and skip is None else (failure if failure is not None else skip).attrib.get('message', ''),
                            durationMs=float(c.attrib.get('time', '0')) * 1000,
                            sourceFile=c.attrib.get('file', ''), sourceLine=c.attrib.get('line', ''),
                            traceMetadata=None))
    summary = {key: sum(r['outcome'] == key for r in records) for key in ['passed', 'failed', 'skipped', 'notRun']}
    summary.update(total=len(records), envUnavailable=0, datasetInvalid=0,
                   decisive=not summary['skipped'] and not summary['notRun'])
    report = dict(schemaVersion='ird-gtest-xml-derived/1', reportOrigin='derived-from-gtest-xml',
                  note='目标沿用GTest::gtest_main，不原生输出ird-test-report；本伴随报告仅转写真实XML，traceMetadata不可恢复，不冒充原生listener报告。',
                  sourceXml=path.name, sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  summary=summary, tests=records)
    (source / (name + '-report.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(name, summary)
