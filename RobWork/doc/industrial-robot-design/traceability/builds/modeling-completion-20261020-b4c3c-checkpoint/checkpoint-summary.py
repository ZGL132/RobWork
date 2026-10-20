from pathlib import Path
import json,xml.etree.ElementTree as E,re
out=Path('build/modeling-completion-20261020-b4c3c')
names=['modeling','modeling_contract','ui','ui_contract','core','standalone_modeling','standalone_modeling_contract','modeling_gui','ui_gui','standalone_modeling_gui']
rows=[]
for name in names:
 cases=list(E.parse(out/(name+'.xml')).getroot().iter('testcase'))
 failed=sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
 skipped=sum(c.find('skipped') is not None for c in cases)
 row=dict(name=name,total=len(cases),passed=len(cases)-failed-skipped,failed=failed,skipped=skipped)
 assert cases and failed==0,row
 if name=='core':
  (out/'core-report.json').write_text(json.dumps(dict(source='core.xml',note='gtest_main不消费ird_report；此报告从真实XML派生',**row),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 else:
  actual=json.loads((out/(name+'-report.json')).read_text(encoding='utf-8-sig'))['summary']
  assert all(actual[k]==row[k] for k in ['total','passed','failed']),(row,actual)
  assert sum(actual.get(k,0) for k in ['skipped','notRun','envUnavailable','datasetInvalid'])==skipped
 rows.append(row)
tour=(out/'modeling-tour.log').read_text(encoding='utf-8-sig')
passed=len(re.findall(r'\[ird-ui-smoke-mtour\] pass ',tour));failed=len(re.findall(r'\[ird-ui-smoke-mtour\] FAIL ',tour))
assert passed==359 and failed==0
s=dict(schemaVersion='modeling-b4c3c-checkpoint/1',taskId='WP-13-T24',baseCommit='37ddc7ded99cea94ae876a44bff7db8864accefe',completed=False,totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']},suites=rows,formalHost=dict(status='PASS',checksPassed=passed,checksFailed=failed,scope='正式draft.apply冲突/原稿落盘/取消/完整重建/手工重填/再次应用/两次重开/继续编辑；原FK、需求三维交互和投影检查'),notRun=['47项全部功能最终验收','真实网格及全部安装姿态/TCP/导航/透明线框/遮挡高亮','干净会话外部修订完整实时刷新专项','F01/F14剩余入口及正式R2完整装配','全新上下文独立验收'],notes=['套件与正式宿主检查分开计数，重复专项不累计','Qt真实窗口已查看，代理图元不冒称真实mesh','旧无条件脏通知导致undo后的无编辑旧稿落盘并覆盖后续场景；成功消费和clean结算共同修复','冲突未决时宿主保留旧完整磁盘稿；显式重建不自动提交','无子代理、不推送、不合入；F06/F07留另一智能体','早期红证与夹具错误单独保留；中间一次冲突结算失败原日志被同名重跑覆盖，仅在计划如实登记，不冒称冻结原日志'])
assert s['totals']==dict(total=1516,passed=1516,failed=0,skipped=0)
(out/'checkpoint-summary.json').write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(s['totals'],s['formalHost'])
