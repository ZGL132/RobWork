"""读取真实XML/窗口图生成摘要，不以截图差异替代数值正确性。"""
from pathlib import Path
import hashlib,json,xml.etree.ElementTree as ET
from PIL import Image,ImageChops

out=Path(__file__).parent
cases=ET.parse(out/'core.xml').getroot().findall('.//testcase')
tests=[]
for c in cases:
    failed=c.find('failure')
    skipped=c.find('skipped')
    tests.append(dict(testId=c.attrib['classname']+'.'+c.attrib['name'],
        outcome='failed' if failed is not None else 'skipped' if skipped is not None else 'passed',
        durationMs=float(c.attrib.get('time',0))*1000,traceMetadata=None))
summary=dict(total=len(tests),passed=sum(t['outcome']=='passed' for t in tests),
    failed=sum(t['outcome']=='failed' for t in tests),skipped=sum(t['outcome']=='skipped' for t in tests),
    notRun=0,envUnavailable=0,datasetInvalid=0)
(out/'core-report.json').write_text(json.dumps(dict(schemaVersion='ird-gtest-xml-derived/1',
    reportOrigin='derived-from-gtest-xml',sourceXml='core.xml',
    sourceSha256=hashlib.sha256((out/'core.xml').read_bytes()).hexdigest(),
    note='core沿用gtest_main，本伴随报告由真实XML派生，不能恢复IRD追溯元数据。',
    summary=summary,tests=tests),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
before=Image.open(out/'modeling-tour/mtour-b2b-before-restore.png').convert('RGB')
after=Image.open(out/'modeling-tour/mtour-b2b-after-restore.png').convert('RGB')
assert before.size==after.size
# 正式宿主中部真实三维视口；排除左侧控件文字及右侧诊断文本，避免假变化。
roi=(511,98,1528,1074)
diff=ImageChops.difference(before.crop(roi),after.crop(roi))
changed=sum(max(p)>12 for p in diff.getdata())
assert changed>100,'恢复后的机器人画面未发生可辨识变化'
result=dict(schemaVersion='modeling-pose-window-evidence/1',viewportRoi=list(roi),
    changedPixelsAbove12=changed,totalViewportPixels=diff.width*diff.height,
    pixelEvidence='仅证明同相机下真实三维呈现变化；几何正确性另由真实Device q及独立FK测试核对',
    before='modeling-tour/mtour-b2b-before-restore.png',after='modeling-tour/mtour-b2b-after-restore.png')
(out/'visual-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
