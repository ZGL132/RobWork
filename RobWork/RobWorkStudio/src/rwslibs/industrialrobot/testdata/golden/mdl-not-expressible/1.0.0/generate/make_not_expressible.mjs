/**
 * mdl-not-expressible 黄金数据集生成脚本（testkit.md §4.4——脚本与产物
 * 同置、可独立重跑；contract-fixture——参考结果为手工构造，合法性由
 * modeling 单元契约定义）。
 *
 * 期望面＝状态级断言（MDL-10 五状态的判定结论/收敛态/诊断码/参数产出
 * 纪律），非数值几何——三类样本的判定均为解析/合同结论：
 *   - not-expressible：第一阶结构检查终判（不进入求解——无参数产出）；
 *     诊断 subject＝首个违规关节（index 由样本结构手工推定）；
 *   - approximate：收敛但任一逐项偏差超附录 D 第 5 项上界（1e-9）→
 *     Approximate；附 E 度量（>0）与收敛态；近似参考参数存在但无权威
 *     资格（C-4）；诊断带比较型三要素；
 *   - analysis-failed：数值溢出→AnalysisFailed（无参数/无偏差表/
 *     收敛态 Diverged——不构成语义结论）。
 *
 * 重跑：node make_not_expressible.mjs（无时间戳/无随机源——产物与入库
 * 版本逐字节一致，NFR-COR-02）。
 */
'use strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const thisDir = path.dirname(fileURLToPath(import.meta.url));

const expected = {
  schemaVersion: 'mdl-not-expressible-expected/1',
  note: '状态级期望（contract-fixture 手工构造——MDL-10 五状态判定/收敛/诊断纪律；firstViolatingJointIndex 为第一阶结构检查"首个违规关节"的样本结构推定值，消费测试据此对账诊断 subject）',
  samples: [
    {
      id: 'prismatic-chain',
      expected: {
        determination: 'NotExpressible',
        convergence: 'Converged',
        diagnosticCode: 'MDL-DH-NOT-EXPRESSIBLE',
        parametersEmpty: true,
        firstViolatingJointIndex: 1,
      },
    },
    {
      id: 'zero-axis',
      expected: {
        determination: 'NotExpressible',
        convergence: 'Converged',
        diagnosticCode: 'MDL-DH-NOT-EXPRESSIBLE',
        parametersEmpty: true,
        firstViolatingJointIndex: 0,
      },
    },
    {
      id: 'fixed-joint',
      expected: {
        determination: 'NotExpressible',
        convergence: 'Converged',
        diagnosticCode: 'MDL-DH-NOT-EXPRESSIBLE',
        parametersEmpty: true,
        firstViolatingJointIndex: 1,
      },
    },
    {
      id: 'approximate-perturbed',
      expected: {
        determination: 'Approximate',
        convergence: 'Converged',
        diagnosticCode: 'MDL-DH-APPROXIMATE',
        diagnosticHasComparison: true,
        errorMetricEPositive: true,
        anyDeviationOverTolerance: true,
        tolerance: 1e-9,
        parameterCount: 2,
      },
    },
    {
      id: 'overflow-origin',
      expected: {
        determination: 'AnalysisFailed',
        convergence: 'Diverged',
        diagnosticCode: 'MDL-DH-ANALYSIS-FAILED',
        parametersEmpty: true,
        deviationsEmpty: true,
        solutionSetEmpty: true,
      },
    },
  ],
};

fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'samples-expected.json'),
  JSON.stringify(expected, null, 2) + '\n', 'utf8');
console.log('mdl-not-expressible: expected/samples-expected.json 已生成');
