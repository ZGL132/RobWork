# UI-T81 返工批运行证据（2026-10-15，实施者自测留痕）

## 冒烟面（build-smoke，QT_QPA_PLATFORM=windows）
- sdurws_ird_modeling_gui_test：39/39 PASSED（连续三轮稳定——含 UI-T47×2/T48/T71 四既有用例复绿与 UI-T81 两新用例）
- sdurws_ird_ui_test：255/255 PASSED
- sdurws_ird_ui_gui_test：72/72 PASSED
- ird_gates：GATES_EXIT=0 零命中（主线已含 WP-01-T05 门禁修复）

## 集成面（build-int 全新配置 RWS_BUILD_INDUSTRIALROBOT=ON）
- sdurws_ird_modeling_gui_test：44/44 PASSED
- sdurws_ird_ui_test：281/281 PASSED（需 PATH 前置：RobWork/bin/Release＋vcpkg bin＋python313.dll 目录——DTB §5.1 fresh 树口径）
- sdurws_ird_ui_gui_test：82/82 PASSED
- ird_gates：GATES_EXIT=0 零命中

## 红面（attempt 1 验收记录已证，本批复验）
- attempt 1 验收者实证：还原产品码后 UI-T81 新用例逐字段翻红（首次回填不得显示未设）；本批修复后全绿。

## 回归根因（探针实证）
- findChildren<QTreeWidget*>() 序＝[ird_modeling_readiness_items, ird_modeling_struct_tree]——QSplitter 使结构树降一级父层后无名首树查找误中就绪表；修复＝七处查找点改按 objectName=ird_modeling_struct_tree 具名。
