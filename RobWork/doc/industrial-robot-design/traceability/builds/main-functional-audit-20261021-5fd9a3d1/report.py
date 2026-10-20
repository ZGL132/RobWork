"""按本轮新证据生成167项复核表；未闭合分支不由关键词命中自动升级。"""
from pathlib import Path
from collections import Counter
import json
import re
import inventory

O = Path(__file__).resolve().parent
inventory.main()
d = json.loads((O/'inventory.json').read_text(encoding='utf-8'))

# 手工审阅的通过范围。其余整项保持 NOT RUN；这些值不能由用例数量推导。
req_pass = {
 'R01-01':'真实宿主新增、默认/选中/树状态，组件生命周期用例通过。',
 'R01-04':'引用删除保护真实GUI反例及域拒绝原子性通过。',
 'R01-05':'宿主原工位改名回写，文本/工艺定义真实控件原子编辑通过。',
 'R01-09':'宿主逐个切换六分量并核对权威掩码；真实求解器约束回归通过。',
 'R01-11':'真实宿主等级/启用控件及权威值对照通过。',
 'R01-12':'重复ID/名称拒绝、字节不变及四态组件回归通过。',
 'R02-01':'宿主与GUI正距离后启停、方向切换、权威字段及撤销回归通过。',
 'R02-03':'宿主撤离字段与接近独立，GUI原子编辑/撤销回归通过。',
 'R02-04':'真实顺序键控件合法链、环/重复/悬空及原子拒绝通过；不含轨迹联合终验。',
 'R03-02':'宿主4×3×2计划和面板计数24；实际覆盖结果分母24。',
 'R03-03':'间距模式真实控件、规范化计划及同义身份回归通过。',
 'R03-05':'方向采样真实控件计数及真实采样执行回归通过。',
 'R03-06':'滚转采样字段、数量/非法范围组件与真实采样执行回归通过。',
 'R03-07':'宿主85%显示/0~1权威值及边界回归通过。',
 'R03-08':'退化区域、零计划和溢出边界实际用例通过。',
 'R03-09':'真实窗口中性轮廓/网格截图已审阅；无结果时不标可达。',
 'R04-02':'工况名称及备注真实GUI编辑回归通过，无原改名异常。',
 'R04-04':'负载从空创建、质量四态与拒绝原子性GUI/域用例通过。',
 'R04-05':'负载质心/完整惯量结构表单与非法输入回归通过。',
 'R04-06':'三种事件、工位引用、时间及非法值结构表单/域回归通过。',
 'R04-08':'三种适用范围、结构化工位引用及悬空拒绝回归通过。',
 'R04-09':'真实宿主节拍与草稿数值对照、空值首次编辑回归通过。',
 'R04-10':'必验集合唯一派生及Must/enabled组合回归通过；全域漏验另列J-02。',
 'R05-02':'字段词典/别名/未知列的解析及映射窗口回归通过。',
 'R05-03':'生产字段映射窗口确认/拒绝/取消回归通过。',
 'R05-04':'生产映射窗口单位预览、mm/deg换算回归通过。',
 'R05-05':'生产错误明细窗口及行/列/原文回归通过。',
 'R05-06':'部分成功正确行保留、坏行跳过实际用例通过。',
 'R05-07':'缺列/重复/非法单位正反例通过。',
 'R05-08':'生产CSV公式/引号文本只按数据处理回归通过。',
 'R05-09':'方言标识、自带前缀/转义符可逆roundtrip回归通过。',
 'R05-10':'映射/导入取消和失败零写入回归通过。',
 'R05-11':'导入摘要/原始行号的域与序列化回归通过。',
 'R06-02':'宿主实际确认应用产生新修订；取消零修订。',
 'R06-03':'非法启用Must的就绪与正式入口双门控反例通过。',
 'R06-08':'R0~R9正反例及实际校验页回归通过。',
 'R06-09':'校验筛选、对象跳转与真实树定位回归通过；全文长字段可读性另见U12。',
 'R07-07':'生产模板窗口参数与实时预览回归通过。',
 'R07-08':'生成来源/批次溯源组件及编码回归通过。',
 'R07-09':'重生成/手改冲突及拒绝原子性生产窗口回归通过。',
 'R07-10':'解除关联保留对象ID/引用且一笔撤销回归通过。',
 'R08-01':'正式宿主真实视图事件、世界坐标预览、确认/取消及原子写回通过。',
 'R08-02':'正式宿主平面拖动/透视yaw/输入调整、确认与取消、临时层清理通过。',
 'R08-03':'实际确认窗口及确认拍重新核验/只读/取消零写入通过。',
 'R09-01':'Fixed解析及真实FK/IK消费正反例通过。',
 'R09-02':'AlignFrame解析及旋转参考系真实求解正反例通过。',
 'R09-04':'PointAtTarget解析、方向反例与真实求解消费通过。',
 'R09-05':'ToolRollFree解析及真实滚转求解正反例通过。',
 'R09-06':'规则来源、确定性与解析错误定位回归通过；真实资源法向另列。',
 'R10-01':'正式宿主真实中心ray-cast、名称反解及Frame引用确认通过。',
 'R10-03':'正式宿主真实会话TCP捕获/改变姿态后再捕获，确认及取消通过。',
 'R10-04':'确认拍过期/切项目/只读守卫与来源标签回归通过。',
 'R11-01':'生产镜像参考系/平面/法向及坐标数值、一次撤销回归通过。',
 'R11-02':'不可镜像规则与派生无环/拒绝原子性回归通过。',
 'R11-03':'生产线性阵列参数与生成坐标、整体撤销回归通过。',
 'R11-04':'生产行/列/层和独立间距、三维生成坐标回归通过。',
 'R11-05':'生产圆阵列半径/角步长/法向及坐标回归通过。',
 'R11-06':'生产三维折线控制点、非法输入/取消和弧长采样回归通过。',
 'R11-07':'局部批次整体撤销/重做及失败零修改回归通过。',
 'R11-08':'宿主项目撤销与需求局部历史分离、首次不可逆提示通过。',
 'R12-02':'生产JSON版本/未知键拒绝、安全及词典回归通过。',
 'R12-03':'生产副本导出窗口与IO写文件回归通过。',
 'R12-04':'副本零项目写入、失败保留旧输出回归通过。',
 'U01':'宿主空态/新增可达/选择后空态清理断言及截图通过。',
 'U05':'本轮真实剪贴板及筛选后TSV、非法值原子拒绝GUI回归通过。',
 'U06':'显示单位切换零草稿写入、数值/单位分列回归通过。',
 'U07':'四页树表/面包屑联动及宿主对象跳转回归通过。',
 'U09':'实际应用确认/取消以及显示操作零草稿/零修订通过。',
 'U10':'实际策略编辑/阈值消费/撤销重做/重开，显示掩码零策略写入通过。',
 'U14':'正式宿主顶/前/右/轴测动作、相机状态及截图对照通过。',
 'U15':'正式宿主zoom/fit/home动作与零写入检查通过（代理图元/区域场景）。',
 'U16':'正式宿主相机/投影动作与状态恢复通过；实际鼠标完整导航另待验。',
 'U17':'正式宿主显示动作/分组掩码/恢复及零写入通过；仅代理图元场景。',
 'U19':'正式宿主真实ray-cast和产品PNG导出可解码通过；不证明真实mesh法向。',
}
req_fail = {
 'R04-07':'MA-02：可编辑需求，但正式宿主碰撞设施缺位，要求碰撞时只有DataInsufficient。',
 'R10-02':'MA-01：实际资源三角面未加载，当前Frame/基本图元拾取不能证明真实曲面法向。',
 'U12':'MA-04：两个尺寸下滚动到底出现空白内容截图；第二轮独立复跑同样出现。',
 'U18':'MA-02：真实碰撞检测/高亮闭环未装配，显示掩码通过不能替代。',
}
req_notes = {
 'R02-02':'段定义/编码用例通过；作业段进入同一真实轨迹任务的联合终验仍缺。',
 'R03-10':'本轮真实覆盖1176条结果到列表/颜色通过；四态像素是显示夹具，未证明真实算法全部状态。',
 'R03-11':'真实结果选中键和独立夹具蓝圈像素通过；逐样本独立世界坐标/遮挡定位仍需补证。',
 'R06-05':'混合草稿有效区域几何预览通过；不等于只计算有效条目的完整计算Preview。',
 'R06-07':'未应用编辑与正式证据Currentness全链未在本轮完成。',
 'R09-03':'基本图元法向/规则数值可用；真实资源曲面及网格法向未证明，见MA-01。',
 'U08':'真实本轮覆盖与夹具调色板分开通过；全算法状态/图例一致仍未完整覆盖。',
 'U11':'布局恢复/空态尺寸断言通过；加载草稿底部存在MA-04，不提升整项。',
 'U13':'取消/进度有MA-03；七种真实业务状态未逐场景验齐。',
 'U20':'阶段门控组件回归通过；完整下游依赖失效与复算联合终验未完成。',
 'U24':'关闭清理及迟到纪元拒绝组件通过；覆盖计算实际早停未接通，见MA-03。',
 'J-01':'跨项目完整业务身份/切片隔离联合场景未完成；独立fixture不能替代。',
 'J-02':'冻结采样分母/缺失工况组件回归通过；同项目全域正式漏验阻断未完成。',
 'J-03':'轨迹/动力学各自套件通过；同一需求任务的完整联合执行证据仍缺。',
 'J-04':'选型/回填相关套件通过；同一项目工具回填后需求引用与失效全链仍缺。',
}
mdl_pass = {
 'M04':'真实关节字段控件、暂存确认/取消、类型与只读拒绝回归通过。',
 'M05':'速度/加速度编辑与拒绝、完整草稿/固定修订保存组合回归通过。',
 'M08':'真实导入完整报告窗口可滚动/选择/取消；URDF默认/非法轴解析反例通过。',
 'M10':'真实continuous范围确认/取消/只读、草稿历史及权威类型保留回归通过。',
 'M12':'不支持关节/链型拒绝与R2耦合能力门控回归通过。',
 'M16':'代理连杆图元、尺寸SI、FK随帧变化/确定性回归通过，非真实网格。',
 'M21':'三类截面及多段真实估算窗口与独立解析公式回归通过。',
 'M22':'真实质量/质心/完整张量两处理路径、取消/拒绝/只读及历史通过。',
 'M23':'物性/限位合法性域断言、对象定位及已提供/缺失分域回归通过。',
 'M24':'正式宿主真实策略编辑/行程实际值与阈值比较、取消及保存重开通过。',
 'M26':'真实编译名称映射往返/改名及正式拾取反解回归通过。',
 'M29':'正式宿主命名姿态恢复前后真实q/Frame及画面变化、模型零修订通过。',
 'M30':'规范包/四种预览/自有来源识别、完整字段roundtrip与失败恢复回归通过。',
 'M31':'R2非对角矩阵编辑/拒绝/移除/历史、完整Codec及真实虚功消费者回归通过。',
 'M33':'真实分组差异表前后值/单位、定位及不覆盖基线回归通过。',
 'H03':'真实参数表批量粘贴、单位分列/换算与非法输入原子拒绝回归通过。',
 'H05':'真实控件暂存/确认/取消/脏标记、同对象刷新与应用回填回归通过。',
 'H06':'真实字段/结构/部件本地撤销重做、分叉及零项目修订通过。',
 'H07':'正式宿主完整工作集应用、项目撤销/重做与固定修订回读对照通过。',
 'H08':'正式宿主完整工作集保存/关闭重开、同ID模型树/活动部件字节及继续编辑通过。',
 'H09':'真实冲突三选择、旧稿对照/取消/重建/手工重填及再次应用回归通过。',
 'H10':'真实只读动态门控及直接触发写信号仍被拒绝回归通过；不限制会话姿态。',
}
mdl_fail = {
 'M15':'MA-01：真实视觉/碰撞资源mesh未进入WorkCell，加载/解绑控件通过不能替代。',
 'M17':'MA-02/MA-06：配置入口存在；真实碰撞消费缺设施，逐对来源和重开候选恢复仍开放。',
 'M19':'工具CRUD/物性/安装/TCP和完整工作集持久回归通过；真实几何附着受MA-01阻断。',
 'M27':'固定帧/场景CRUD及世界变换/重开通过；真实障碍资源和环境碰撞受MA-01/02阻断。',
 'V01':'MA-01：当前代理图元不能验收为真实机型网格显示正确。',
 'V03':'MA-02：已证明Frame/图元拾取；真实碰撞覆盖及遮挡/多选高亮未闭合。',
}
mdl_notes = {
 'M01':'六轴模板及代理显示组件通过；三种安装预设的完整同场景视觉尚未逐一验齐。',
 'M02':'七轴generic模板受既有P-03工程参数未冻结门禁限制；自定义七轴不等于模板交付。',
 'M06':'双权威及派生只读组件通过；完整切换结果窗口逐状态终验未齐。',
 'M07':'五状态数学回归通过；五种真实模态结果页及人工可读性未逐一验齐。',
 'M09':'URDF/Xacro选链命令流回归通过；真实多分支文件完整窗口流程未在本轮独立跑齐。',
 'M11':'混合链/4~5轴门控相关全套通过；同一混合模型跨全部域AT-36尚无本轮联合实证。',
 'M13':'实际磁盘include/参数/错误与窗口回归通过；生产导入向导所有失败/取消路径终验仍缺。',
 'M14':'真实WorkCell文件选择到报告取消、命令目录及提取内核通过；多设备选择→接受草稿终验未齐。',
 'M18':'安装链与FK/TCP数据回归通过；默认TCP完整坐标架视觉矩阵未齐。',
 'M20':'多个TCP身份/FK/IK消费与UI增删改通过；全部TCP坐标架/安装姿态视觉未齐。',
 'M25':'真实WC/DWC相关套件/宿主正常应用通过；全部真实编译失败回滚窗口情形未逐一验齐。',
 'M28':'摩擦/力矩字段与动力学独立套件通过；同一建模表单输入→真实动力学消费未单独联合验齐。',
 'M32':'欧拉输入及任意角数值变换/重力回归通过；三预设/任意角/真实几何/碰撞完整联合视觉未齐。',
 'H01':'已审阅正式宿主空/已建/缺输入；全部状态及各分辨率长文未齐。',
 'H02':'正式ray-cast/单选/树和属性联动通过；多选/遮挡/清空的完整视觉未齐。',
 'H04':'错误翻译/比较值组件通过；全部窗口与长文本可读性未逐场景人工验齐。',
 'H11':'命令目录/面板模糊搜索与快捷键组件通过；全部建模高频命令键盘路径未逐条终验。',
 'V02':'需求tour通用视图动作及代理场景通过；真实机型鼠标导航/全部模式/重开矩阵仍缺。',
}

extra = {
 'R01':r'StationDefinition|StationText|StationDuplicate|StationReferenced|NamedStation|StationPosition',
 'R02':r'StationSegments|SequenceEditing|SequenceContract',
 'R03':r'RegionSampling|RegionOrientation|RegionSpacing|NamedRegion|OrientedBox|SamplingPlan',
 'R04':r'ConditionStructure|ConditionName|ConditionReferences|NamedCondition|PayloadEvents|RequiredCases',
 'R05':r'CsvProduction|CsvAllError|CsvMapping|CsvFormula|CsvPartial|CsvFormula|UnitPreview|Escape',
 'R06':r'MixedDraft|RequirementRevisionSync|ReqReadiness|ReqEditor|Currentness',
 'R07':r'ProductionTemplateKinds|GenerationProvenance|DetachAtomic|ReqTemplateArray',
 'R08':r'View3DTarget|CaptureConfirmation|CaptureReadOnly',
 'R09':r'StationDefinitionFiveRules|TaskPoseConstraint|ToolRoll|AlignFrame|PointAt',
 'R10':r'CaptureTcp|ScenePick|CaptureRotation|NamedGeometry',
 'R11':r'ArrayComplete|ArrayInvalid|MirrorFrame|DetachAtomic|ReqTemplateArray|UndoReruns',
 'R12':r'JsonImportVersion|ExportCopy|JsonAndDegrades|ReqImport.*Json',
 'U05':r'StationBatch|RegionBatch|ParameterTsv|BatchParameters',
 'U10':r'HostPolicy|HostEngineeringPolicy',
 'U12':r'DockWorkspace|NoVerticalClipping',
}
flat = [(s['suite'],c) for s in d['executions'] if not s['suite'].startswith('standalone_') for c in s['cases']]

def evidence(row, is_req):
    matched = []
    old = set(row.get('historical_tests', []))
    pattern = extra.get(row['id'][:3], extra.get(row['id'], '')) if is_req else '|'.join(re.escape(x.strip()) for x in row['keywords'].split('/') if x.strip())
    for suite,c in flat:
        if c['name'] in old or (pattern and re.search(pattern,c['name'],re.I)):
            matched.append(dict(suite=suite, **c))
    row['current_related_cases'] = matched
    # 全清单在JSON；表内只举定位例，且明确是相关实测而非覆盖率证明。
    return '<br>'.join(f"[{c['name']}]({c['suite']}.xml) {c['status']}" for c in matched[:3]) or '无具名匹配；不推断通过'

for kind,passes,fails,notes,filename in (
 ('requirements',req_pass,req_fail,req_notes,'REQUIREMENTS-MATRIX.md'),
 ('modeling',mdl_pass,mdl_fail,mdl_notes,'MODELING-MATRIX.md')):
    title = '需求120项' if kind=='requirements' else '建模47项'
    lines = [f'# {title}：合入后主线复核', '', f"基线 `{d['baseline']}`。每项原功能/方法保留。相关用例只是定位证据，不能按名字命中数量推导整行通过。PASS仅限本行明确的已实测范围；未齐的整项为NOT RUN。非空间字段不强加三维验收，空间功能的未齐处单列。",'',
             '|ID/需求|功能|如何验证|本轮相关自动化证据（节选）|整项判定|已验证范围与剩余条件|',
             '|---|---|---|---|---|---|']
    for row in d[kind]:
        id = row['id']
        row['status'] = 'PASS' if id in passes else 'FAIL' if id in fails else 'SKIP' if id=='M02' else 'NOT RUN'
        row['scope_and_limit'] = passes.get(id, fails.get(id, notes.get(id, '相关组件套件已执行；本行列出的全部交互/反例/完整场景未在本轮逐项证实，不沿用旧结论。')))
        e = evidence(row,kind=='requirements')
        esc = lambda s:str(s).replace('|','／').replace('\n',' ')
        lines.append('|'+ '|'.join(map(esc,[id+' '+row['source'],row['feature'],row['method'],e,row['status'],row['scope_and_limit']]))+'|')
    counts = dict(Counter(r['status'] for r in d[kind]))
    lines[2] += ' 本表行数分布：'+json.dumps(counts,ensure_ascii=False)+'；不是交付覆盖率。'
    (O/filename).write_text('\n'.join(lines)+'\n',encoding='utf-8')

tours = []
for name in ('requirements-tour','modeling-tour','auto','layout'):
    path = O/(name+'.log')
    log = path.read_text(encoding='utf-8-sig',errors='replace') if path.exists() else ''
    tours.append(dict(name=name,passed_assertions=len(re.findall(r'\] pass ',log)),
                      auto_ok_steps=len(re.findall(r'\] step\d+ .*?=ok',log)),
                      screenshots=len(list((O/name).glob('*.png'))),done=bool(re.search(r'\] DONE',log))))
d['host_runs']=tours
d['note']='重复专项与视觉复跑不累计主套件总数；套件通过不等于167行功能闭环。'
(O/'inventory.json').write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8')
summary=[]
for s in d['executions']:
    summary.append({k:v for k,v in s.items() if k!='cases'})
(O/'verification-summary.json').write_text(json.dumps(dict(baseline=d['baseline'],executions=summary,
              host_runs=tours,row_counts={k:dict(Counter(r['status'] for r in d[k])) for k in ('requirements','modeling')}),ensure_ascii=False,indent=2),encoding='utf-8')
print('reports generated')
