# REUSE-T01 首批返工独立验收 r2：PASS

冻结提交 `2b26f034c4ce24772239ff26697ac8ce38bb011e`；首轮 `8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae` 的FAIL原样保留；批次基线 `212226cfd2e2ca2fd12619dc57c458888f42534b`。

本轮仅验收首批6单元24条pending关系（23个不同functionId）、3个新增candidate能力、10项公共API探测与相应docs/task；不代表T01全20单元审计、生产复用或T02准入。

- 三项返工均通过：rotationFromCustomEaa命名空间准确；半周目标改为单Z关节无法达到的Rx(pi)；Euler反例固定yaw/pitch且仅改变roll。
- 独立数学核对：yawDelta=0，pitchDelta=0，rollDelta≈0.6；空间EAA=[0.4226797832,0.3560182700,-0.2336510054]，与0.6*Rz(yaw)*Ry(pitch)*X解析值误差<3e-16，Y/Z范数≈0.4258424602。
- 不可达性：任意Rz(q)保持+Z，而目标Rx(pi)将Z映为-Z；原成功标记仍为true，独立EAA残差π。预期限制测试通过不代表求解功能正常。
- 独立clone重跑validate-task/docs/reuse均exit=0；仍functions=24、closedFiles=0、compileWorkCell未授权pending提示。
- VS2022 x64重新configure/build/run：原10项passed=10、failed=0、skipped=0、notRun=0；真实gtest.xml和既有testkit/main生成的ird-test-report.json一致。附加语义探针单列，不并入10项计数。
- 24条区段/inputs摘要及catalog资源摘要/版本/符号均匹配。全部关系pending、能力candidate；无新增grant/closure，无产品、上游、vcpkg变动。
- README明确首轮RPY解释失效并保留原probe/；新probe-r2/独立存放。独立clone干净，未提交/推送/合入；未读取实施树未提交batch2。

完整上游集、全部20单元审计、生产迁移/断开、完整数值性能、140项治理自测、DeepEvidence及远程CI未执行。复现日志、原生报告、独立探针源码与详细绑定见本目录acceptance.json及semantic-*。
