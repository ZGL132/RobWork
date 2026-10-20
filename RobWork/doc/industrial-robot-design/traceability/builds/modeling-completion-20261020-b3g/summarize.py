"""B3g计数只取本轮真实XML；gtest_main的伴随JSON明确标为派生。"""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root=Path(__file__).resolve().parent
names=['runtime','modeling','modeling_contract','modeling_gui','kinematics','kinematics_contract',
       'kinematics_gui','ui','ui_contract','ui_gui','standalone_runtime','standalone_modeling',
       'standalone_modeling_contract','standalone_modeling_gui','standalone_kinematics',
       'standalone_kinematics_contract','standalone_kinematics_gui','core']
suites=[]
skips=[]
for name in names:
    xml=ET.parse(root/(name+'.xml')).getroot();cases=list(xml.iter('testcase'))
    failed=sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
    skipped=sum(c.find('skipped') is not None for c in cases)
    assert int(xml.attrib['tests'])==len(cases)
    row=dict(name=name,total=len(cases),passed=len(cases)-failed-skipped,failed=failed,skipped=skipped)
    suites.append(row)
    for c in cases:
        s=c.find('skipped')
        if s is not None:skips.append(dict(suite=name,test=c.attrib['classname']+'.'+c.attrib['name'],reason=s.attrib.get('message',s.text or '')))
    if name in ['runtime','standalone_runtime','core']:
        data=dict(source=name+'.xml',note='GTest::gtest_main不消费ird_report；本报告仅派生自真实XML',**row)
        (root/(name+'-report.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
tour=(root/'modeling-tour.log').read_text(encoding='utf-8-sig')
summary=dict(schemaVersion='modeling-completion-b3g/1',taskId='WP-13-T24',baseCommit='69d753e95502',
    scope='固定帧编译/名称/身份/碰撞事实；真实网格/完整47项尚未封闭',
    totals={k:sum(s[k] for s in suites) for k in ['total','passed','failed','skipped']},suites=suites,skips=skips,
    tour=dict(passed=len(re.findall(r'^\[ird-ui-smoke-mtour\] pass ',tour,re.M)),failed=len(re.findall(r'^\[ird-ui-smoke-mtour\] FAIL ',tour,re.M)),scope='既有正式宿主综合流程，含其他域；未新增本批场景三维验收'),
    environment=dict(toolchain='VS2022 x64',qtPlatform='windows',guiExecution='单调度器串行、绝对exe'),
    limitations=['runtime/standalone_runtime/core的JSON派生自真实XML','F06归另一工作树，未实施/集成',
        '无全新上下文独立验收','无几何固定帧新入口至保存重开完整宿主路径尚待验齐',
        '真实mesh挂接、碰撞与完整47项仍开放','未推送、未自行合入'])
(root/'verification-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(summary['totals']);print(summary['tour']['passed'],summary['tour']['failed'])
