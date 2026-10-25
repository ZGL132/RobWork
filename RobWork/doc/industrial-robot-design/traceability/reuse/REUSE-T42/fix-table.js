const fs = require('fs');
const p = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T42/gen-matrix.js';
let t = fs.readFileSync(p, 'utf8');
const startMarker = 'const R = [';
const endMarker = '\n];\n\nconst SEARCHES';
const si = t.indexOf(startMarker), ei = t.indexOf(endMarker);
if (si < 0 || ei < 0) { console.error('markers not found'); process.exit(1); }
const newTable = `const R = [
  [51, 'TemplateArray.hpp', 'requirements.TemplateArray.fileHead', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '文件头/命名空间/确定性约定（IEEE754、%.17g 无损往返、strtod 全消费、冲突升序去重）——产品数据身份契约，上游无对应物（S1/S2 零命中）。'],
  [60, 'formatNumber', 'requirements.formatNumber', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '数学数值文本化 %.17g（IEEE754 double 无损往返格式——provenance 快照协议面），上游 Vector3D 无产品文本格式。'],
  [75, 'parseNumber', 'requirements.parseNumber', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', 'strtod 全消费解析＋确定性文本约定，属需求参数文本协议；上游 StringUtil 无全消费数值协议同义物。'],
  [81, 'formatVec3', 'requirements.formatVec3', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '%.17g 三分量文本化（provenance 快照协议）；上游 Vector3D 无产品文本格式。'],
  [105, 'parseVec3', 'requirements.parseVec3', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '三分量全消费解析（协议面），非数学核心。'],
  [118, 'paramValue', 'requirements.paramValue', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', 'GenerationProvenance 参数读取（产品 provenance 模型字段），上游无 provenance 概念（S2 零命中）。'],
  [135, 'sourceIdsOf', 'requirements.sourceIdsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品来源 ObjectId 列表读取；上游无 ObjectId/provenance 模型。'],
  [148, 'refFrameToText', 'requirements.refFrameToText', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '需求参考系文本编码（产品引用协议）。'],
  [185, 'refFrameFromText', 'requirements.refFrameFromText', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '需求参考系文本解码（产品引用协议）。'],
  [199, 'makeDiag', 'requirements.makeDiag', 'NONE', 'business-specific', 'owner', ['NFR-COR-02'], 'S1', '产品诊断记录装配（diagnostics 契约承载，本单元恒不写诊断）。'],
  [210, 'errorBatch', 'requirements.errorBatch', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 EditBatch 错误面（I-REQ 契约），非上游能力。'],
  [221, 'finiteVec3', 'requirements.finiteVec3', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品参数有限性拒绝面（NaN/Inf 拒绝归业务入口原子拒绝——非 REUSE-T37 通用数值工具面，业务拒绝分支承载）。'],
  [232, 'reflectPosition', 'requirements.reflectPosition', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', '镜像反射 p\\'=p−2(n·p)n 是 §7.2 业务反射规则（等距变换、det=−1）；上游无反射原语（S4 零命中），Vector3D 值运算仅作承载，不能为复用把反射当 proper rotation。'],
  [251, 'rotationFromRpy', 'requirements.rotationFromRpy', 'MATH-ROT', 'direct', 'adapter', ['REQ-11', 'AT-24'], 'ROT', '已换上游：rw::math::RPY<double>(yaw,pitch,roll).toRotation3D()（字段换序边界单点承载，modeling/rpymath::rpyToRotation 同型、REUSE-T11 已闭）。逐元素解析式与原手写 Rz·Ry·Rx 展开恒等且乘加序一致——REUSE-T42 探针 12 用例（含 ±π/2 与近万向锁）IEEE754 位模式逐位相等；黄金镜像/数据集摘要零扰动（normal 221 项全过、oracle 2 项黄金过、断开 2 项真实红）。'],
  [261, 'reflectionMatrix', 'requirements.reflectionMatrix', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', 'M=I−2nnᵀ 为 det=−1 反射矩阵——Rotation3D 类型系统不能表达（非 proper rotation），上游无通用 3×3 反射构造（S4 零命中）；MATH-ROT 登记面为旋转值类型，不覆盖。'],
  [294, 'matMul', 'requirements.matMul', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', '固定 3 阶乘法承载 M·R·M 反射共轭（M 非旋转，Rotation3D operator* 类型前提不成立）；乘加序固定为确定性契约（黄金元素规则 σiσj 依据），上游无 det=−1 共轭能力。'],
  [344, 'reflectRpy', 'requirements.reflectRpy', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'ROTPROBE', 'MRM 共轭＋Z-Y-X 反解为 §7.2 业务镜像规则＋F-582 修复契约（roll\\'=0、yaw 按符号分支）；上游 RPY(R,eps) 差异实证：①万向锁带宽 eps=1e-5 远宽于产品 1e-12（|cp|∈[1.41e-6,1e-5] 区间上游吸附退化表示、产品保留一般表示——REQ-10 的 1e-8rad 回归禁止吸附）；②退化轴约定不同（上游置绕Z=0、产品置 roll=0）——同矩阵表示不同，条目字段/摘要随表示变化；③上游退化提取元素 atan2(r12,r22) 与产品 atan2(R[1][2],R[1][1]) 不同。probe-r6（REUSE-T01-api-r6）实测确认。反解保留产品唯一职责。'],
  [355, 'methodTagOfGenerator', 'requirements.methodTagOfGenerator', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance methodTag 语法（[a-z0-9./_-]）承载，上游无。'],
  [370, 'generatedSource', 'requirements.generatedSource', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 D-REQ-4 来源标记规则（UserProvided＋methodTag），上游无。'],
  [386, 'disambiguateName', 'requirements.disambiguateName', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '批次内确定性试缀消歧（产品 I-REQ-3 边界），上游无同名语义（S1 零命中）。'],
  [397, 'appendCommonParams', 'requirements.appendCommonParams', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance 参数快照协议。'],
  [409, 'contentEqualIgnoringIdentity', 'requirements.contentEqualIgnoringIdentity', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 TaskPoint 身份无关等值（批次冲突清单语义）。'],
  [422, 'appendRefFrameParam', 'requirements.appendRefFrameParam', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance 参考系参数协议。'],
  [431, 'operator==', 'requirements.EditBatchEquality', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 EditBatch 值等值（契约面）。'],
  [442, 'operator==', 'requirements.RegenerateOutcomeEquality', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 RegenerateOutcome 值等值（契约面）。'],
  [488, 'defaultTemplateParams', 'requirements.defaultTemplateParams', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '三类模板默认参数（§7.2 产品业务默认值契约）。'],
  [508, 'defaultProcessTag', 'requirements.defaultProcessTag', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品工艺标记默认值（业务契约）。'],
  [525, 'lowerHyphenToken', 'requirements.lowerHyphenToken', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品标识文本化约定（methodTag 连字符形态）。'],
  [531, 'templateGeneratorId', 'requirements.templateGeneratorId', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品生成器标识语法（冒号形态）。'],
  [542, 'arrayGeneratorId', 'requirements.arrayGeneratorId', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品阵列生成器标识语法。'],
  [654, 'applyTemplate', 'requirements.applyTemplate', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '模板装配业务（三类生成器公共面——I-REQ-10 单点执行）；上游无需求模板概念（S1 零命中）。'],
  [731, 'mirrorPoint', 'requirements.mirrorPoint', 'NONE', 'business-specific', 'owner', ['REQ-11', 'AT-24'], 'S4', '镜像点几何业务规则（§7.2 点/姿态反射＋rule 保留判定）；反射数学面归 reflectPosition/reflectRpy 行，业务编排为产品唯一职责。'],
  [817, 'applyMirror', 'requirements.applyMirror', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '批量镜像业务（来源选择、消歧、provenance 快照）；上游无。'],
  [1137, 'applyArray', 'requirements.applyArray', 'NONE', 'business-specific', 'owner', ['REQ-11', 'AT-24'], 'S1', '线性/矩形/圆形阵列生成业务（产品网格语义）；上游无网格模板生成（S1 零命中）。'],
  [1207, 'templateParamsOf', 'requirements.templateParamsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', 'provenance 参数反解（产品会话协议 regenerate 入参）。'],
  [1228, 'mirrorPlaneOf', 'requirements.mirrorPlaneOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '镜像面参数反解（产品协议）。'],
  [1309, 'arrayParamsOf', 'requirements.arrayParamsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '阵列参数反解（产品协议）。'],
  [1476, 'regenerate', 'requirements.regenerate', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '再生业务（AC4 无活传播、unlink 后再生——产品编辑器契约）。'],
  [taLines.length, 'unlinkGenerator', 'requirements.unlinkGenerator', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '生成器解链业务（D-REQ-4 普通条目可手改的承载面）。'],
]`;
t = t.slice(0, si) + newTable + t.slice(ei + 1);
fs.writeFileSync(p, t);
console.log('table replaced ok');
