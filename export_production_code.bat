@echo off
setlocal EnableExtensions
rem 统一到中文 Windows 控制台代码页，保证本文件 GBK 编码的中文正常显示
chcp 936 >nul

rem =====================================================================
rem export_production_code.bat —— 一键导出 industrialrobot 各单元正式产品代码
rem
rem 功能：
rem   1. 在目标根目录下为每个单元创建同名文件夹；
rem   2. 把各单元中“真正进入正式产品”的源码文件【扁平】拷入对应单元文件夹，
rem      不保留任何子文件夹——例如 export_production\modeling\ 下直接就是
rem      该模块全部产品代码文件，来源目录结构不在导出结果中体现：
rem        include   公共头——单元唯一对外接口面
rem        src       计算内核/库实现——产品 STATIC 库源码〔含其内部子目录〕
rem        plugin    插件与面板产品源码——静态装配面
rem        assembly  装配门面公共头——宿主装配层消费
rem        worker    隔离工作进程源码——execution 单元专属
rem        studio    产品主程序与宿主插件源码——ui 单元专属
rem      各来源目录按“存在才扫描”处理；业务域单元的典型形态如 modeling：
rem      include＋src＋plugin＋assembly 四目录的文件全部拍平放入。
rem   3. ui 单元的 app\ 目录特殊处理：一并拍平拷入，但剔除开发期 harness
rem      入口 HarnessMain.cpp——其中 PortAdapters 与 DiagnosticsAssembly
rem      是被正式插件目标编入的产品 TU，不能丢。
rem   4. 一律不拷贝测试与辅助面：test、contract_test、gui_test、perf_test、
rem      app〔除 ui 特批外〕、demo、tools 以及各 CMakeLists.txt。
rem   5. 同名保护：若不同来源目录出现同名文件〔当前 19 个单元实测为零〕，
rem      后到者加“来源子目录_”前缀保留并告警，绝不静默覆盖。
rem
rem 用法：
rem   export_production_code.bat [目标根目录]
rem   缺省目标根＝本脚本所在目录\export_production
rem   本脚本须放在仓库根目录，即与 start_studio.bat 同级。
rem
rem 实现注记：
rem   - 递归枚举放在 :copy_tree 子程序内做——for /R 的路径参数含 for 元
rem     变量〔如 %%S〕时枚举会静默落空，必须传入已解析的字面路径。
rem   - 本文件为 GBK 编码：中文批处理必须以控制台本地代码页保存，cmd 才能
rem     正确解析；行尾必须为 CRLF。
rem =====================================================================

set "SRC_ROOT=%~dp0RobWork\RobWorkStudio\src\rwslibs\industrialrobot"
if not exist "%SRC_ROOT%" (
    echo [错误] 源码根不存在：%SRC_ROOT%
    echo [错误] 请把本脚本放在仓库根目录，或修改本文件中的 SRC_ROOT。
    exit /b 1
)

if "%~1"=="" (
    set "DEST_ROOT=%~dp0export_production"
) else (
    set "DEST_ROOT=%~1"
)

echo 源码根：%SRC_ROOT%
echo 目标根：%DEST_ROOT%
echo.

set "UNIT_LIST=core diagnostics drivetrain dynamics evidence execution io kinematics modeling optimization policy project reporting requirements runtime selection trajectory ui workflow"
set "PRODUCT_DIRS=include src plugin assembly worker studio"

set /a UNIT_FAIL=0
for %%U in (%UNIT_LIST%) do call :copy_unit %%U

rem 生成导出说明，随目录一起交付，说明拷贝口径与已知排除项。
> "%DEST_ROOT%\导出说明.txt" (
    echo 本目录由仓库根的 export_production_code.bat 生成，仅含各单元进入正式产品的源码。
    echo 布局为扁平式：每个单元文件夹下直接放该模块全部产品代码文件，不含子文件夹。
    echo 拷贝来源：include＝公共头；src＝计算内核实现〔含其内部子目录〕；
    echo           plugin＝插件与面板；assembly＝装配门面头；
    echo           worker＝执行单元隔离工作进程；studio＝ui 产品主程序与宿主插件。
    echo 排除范围：test、contract_test、gui_test、perf_test、app〔ui 除外且剔除 HarnessMain.cpp〕、
    echo           demo、tools、CMakeLists.txt 与全部构建产物。
    echo testkit 单元未导出：其库按 T-1 红线仅被测试目标链接，不进入正式交付程序。
    echo 同名文件若在不同来源目录撞名，将加“来源子目录_”前缀保留，绝不覆盖。
    echo 生成时间：%date% %time%
)

if not "%UNIT_FAIL%"=="0" (
    echo.
    echo 完成，但有 %UNIT_FAIL% 个单元拷贝出错，请回看上方 [错误] 行。
    exit /b 1
)
echo.
echo 全部 19 个单元导出完成。目标根：%DEST_ROOT%
exit /b 0

:copy_unit
rem %1＝单元名。逐产品子目录派发递归枚举，汇总本单元文件数与成败。
set "UNIT=%~1"
if not exist "%SRC_ROOT%\%UNIT%\" (
    echo [警告] 单元目录不存在，跳过：%UNIT%
    set /a UNIT_FAIL+=1
    exit /b 0
)
md "%DEST_ROOT%\%UNIT%" >nul 2>&1
set /a UNIT_FILES=0
set UNIT_HAS_PRODUCT=
rem 第三实参为空串＝无剔除名单；ui 的 app 特批传 HarnessMain.cpp。
for %%S in (%PRODUCT_DIRS%) do (
    if exist "%SRC_ROOT%\%UNIT%\%%S" call :copy_tree %UNIT% %%S "" "%SRC_ROOT%\%UNIT%\%%S"
)
rem ui 特批：app\ 内 PortAdapters 与 DiagnosticsAssembly 属产品 TU，
rem 随正式插件目标编入——一并拍平拷入但剔除开发期 harness 入口。
if /i "%UNIT%"=="ui" if exist "%SRC_ROOT%\ui\app" call :copy_tree ui app HarnessMain.cpp "%SRC_ROOT%\ui\app"
if defined UNIT_HAS_PRODUCT (
    echo [完成] %UNIT%，共 %UNIT_FILES% 个文件
) else (
    echo [警告] %UNIT% 未发现任何产品文件
)
exit /b 0

:copy_tree
rem %1＝单元名  %2＝来源子目录名  %3＝需剔除的文件名〔可空〕  %4＝来源目录完整路径
for /R "%~4" %%F in (*) do call :flat_copy %1 %2 %3 "%%F"
exit /b 0

:flat_copy
rem %1＝单元名  %2＝来源子目录名  %3＝需剔除的文件名〔可空〕  %4＝源文件完整路径
if not "%~3"=="" if /i "%~nx4"=="%~3" exit /b 0
rem 同名保护：目标已存在同名文件时不覆盖，加“来源子目录_”前缀保留。
if exist "%DEST_ROOT%\%1\%~nx4" (
    copy /Y "%~4" "%DEST_ROOT%\%1\%2_%~nx4" >nul || (echo [错误] 拷贝失败：%~4 & set /a UNIT_FAIL+=1 & exit /b 1)
    echo [警告] 同名文件已加前缀保留：%1\%2_%~nx4
) else (
    copy /Y "%~4" "%DEST_ROOT%\%1\%~nx4" >nul || (echo [错误] 拷贝失败：%~4 & set /a UNIT_FAIL+=1 & exit /b 1)
)
set /a UNIT_FILES+=1
set UNIT_HAS_PRODUCT=1
exit /b 0
