# 契约 verify 数组回填（F-300 家族，2026-10-18）

| 字段 | 值 |
| --- | --- |
| 批次 | F-300 家族整族回填（契约 verify 缺测试目标构建行） |
| 规则 | CCP §2 v1.4——含 _test/_contract_test 的任务必须显式登记构建行且先于 ctest 行 |
| 数量 | 23 份契约 |
| 验证 | 23 份 validate-task 全 PASS；validate-docs -DeepEvidence PASS |

## 回填清单

| finding | 契约 | 增补内容（摘要） |
| --- | --- | --- |
| F-300 | WP-13-T04 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-304 | WP-13-T05 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-310 | WP-13-T06 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-314 | WP-13-T07 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-338 | WP-13-T09 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-344 | WP-13-T10 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-345 | WP-13-T11 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-350 | WP-13-T13 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test；cmake --build build --config Release --target sdurws_ird_io_test |
| F-351 | WP-13-T14 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-352 | WP-13-T15 | cmake --build build --config Release --target sdurws_ird_modeling_contract_test |
| F-358 | WP-14-T02 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test；ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$" |
| F-367 | WP-14-T04 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test |
| F-368 | WP-14-T05 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test |
| F-371 | WP-14-T06 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test |
| F-373 | WP-14-T07 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test |
| F-375 | WP-14-T08 | cmake --build build --config Release --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test |
| F-381 | WP-15-T02 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test；ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/kinematics -C Release -L "^ird$" |
| F-397 | WP-15-T06 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |
| F-399 | WP-15-T07 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |
| F-401 | WP-15-T08 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |
| F-404 | WP-15-T09 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |
| F-409 | WP-15-T11 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |
| F-411 | WP-15-T12 | cmake --build build --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test |

> 全部为契约 JSON 的 verify 数组增行（构建行先于 ctest 行），零产品代码/测试改动；历史验收记录不改写。
