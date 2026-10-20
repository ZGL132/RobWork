from pathlib import Path
import json,xml.etree.ElementTree as E,re
out=Path('build/modeling-completion-20261020-b4c3d')
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
assert passed==361 and failed==0
s=dict(schemaVersion='modeling-b4c3d-checkpoint/1',taskId='WP-13-T24',baseCommit='7fcd854549be52d96709411586e377a47acfc086',completed=False,totals={k:sum(r[k] for r in rows) for k in ['total','passed','failed','skipped']},suites=rows,formalHost=dict(status='PASS',checksPassed=passed,checksFailed=failed,scope='外部场景创建及修改后即时完整编辑器刷新；原冲突/重建/应用/两次重开/继续编辑及FK/需求三维交互'),notRun=['47项全部功能最终验收','真实网格及全部安装姿态/TCP/导航/透明线框/遮挡高亮','F01/F14剩余入口及正式R2完整装配','全新上下文独立验收'],notes=['套件与正式宿主检查分开计数，重复专项不累计','Qt真实窗口已查看，代理图元不冒称真实mesh','正式同步revision事件在事务writer锁内重入查询，改为UI队列及来源adapter/关闭/project身份守卫，两个即时完整字节断言真实红后绿','冲突未决时宿主保留旧完整磁盘稿；显式重建不自动提交','无子代理、不推送、不合入；F06/F07留另一智能体','clean-refresh-red为模块红证；previous-modeling-tour目录归档原正式红轮及诊断红轮，原日志PNG未覆盖'])
assert s['totals']==dict(total=1520,passed=1520,failed=0,skipped=0)
(out/'checkpoint-summary.json').write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(s['totals'],s['formalHost'])
