# 探针脚本（不属于仓库交付物）：验证
# ① string(SUBSTRING) 长度 -1 语义；② 新线性 DFA 与被替换正则的语义等价性
#   （刁钻语料＋真实产品文件全集）；③ 旧正则悬崖阈值复现。
cmake_minimum_required(VERSION 3.16)

set(PROBE_ROOT "C:/Users/zgl18/AppData/Local/Temp/gov-gate-probe")

# ---------------------------------------------------------------------
# 旧管线（现引擎 555-556 行原样）：行注释正则＋块注释正则
# ---------------------------------------------------------------------
function(strip_old src out_var)
    string(REGEX REPLACE "//[^\n]*" "" _s "${src}")
    string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" " " _s "${_s}")
    set(${out_var} "${_s}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# 新管线：行注释正则保持不变；块注释替换为线性 DFA（星跳步进）。
# DFA 语义＝正则 /\*([^*]|\*[^/])*\*/ 的非重叠 leftmost 全部替换为 " "：
#   - 非 '*' 字符只能走 [^*]（吞 1 字符）
#   - '*' 若后继字符存在且非 '/' 只能走 \*[^/]（吞 2 字符）
#   - '*' 后紧跟 '/' 时两个内容分支皆死 → 该 */ 即闭合
#   - 扫到文本末尾仍无对齐闭合（含末尾孤星）→ 该 /* 起点失配，
#     原样保留 '/' 并自下一字符位重试（复刻正则引擎逐位推进）
# ---------------------------------------------------------------------
function(strip_block_dfa text out_var)
    set(_in "${text}")
    set(_out "")
    string(LENGTH "${_in}" _n)
    set(_i 0)
    while(TRUE)
        if(_i GREATER_EQUAL _n)
            break()
        endif()
        # 自 _i 起的尾串中找下一个 "/*" 候选起点
        string(SUBSTRING "${_in}" ${_i} -1 _tail)
        string(FIND "${_tail}" "/*" _rel)
        if(_rel EQUAL -1)
            # 无更多候选：剩余文本原样保留，扫描结束
            string(APPEND _out "${_tail}")
            break()
        endif()
        math(EXPR _open "${_i} + ${_rel}")     # 候选起点绝对位置
        # 保留候选起点之前的原样片段
        if(_rel GREATER 0)
            string(SUBSTRING "${_in}" ${_i} ${_rel} _head)
            string(APPEND _out "${_head}")
        endif()
        # ---- 内容 DFA：自 _open+2 起做星跳扫描 ----
        set(_j "${_open}")
        math(EXPR _j "${_j} + 2")
        set(_match_end -1)
        while(TRUE)
            if(_j GREATER_EQUAL _n)
                break()                            # 扫到末尾：无闭合
            endif()
            string(SUBSTRING "${_in}" ${_j} -1 _seg)
            string(FIND "${_seg}" "*" _srel)
            if(_srel EQUAL -1)
                break()                            # 无星号：无闭合
            endif()
            math(EXPR _sabs "${_j} + ${_srel}")    # 下一星号绝对位置
            # 星号后继字符判定（可能只有孤星到末尾）
            math(EXPR _one "${_sabs} + 1")
            if(_one GREATER_EQUAL _n)
                break()                            # 末尾孤星：无闭合
            endif()
            string(SUBSTRING "${_in}" ${_one} 1 _c2)
            if(_c2 STREQUAL "/")
                math(EXPR _match_end "${_sabs} + 1")  # 对齐 */：闭合
                break()
            endif()
            # 星号与非斜杠字符成对吞（\*[^/]；后继为另一星号亦同）
            math(EXPR _j "${_sabs} + 2")
        endwhile()
        if(_match_end GREATER_EQUAL 0)
            string(APPEND _out " ")                # 与 REGEX REPLACE 同替换串
            math(EXPR _i "${_match_end} + 1")      # 非重叠：自闭合后继续
        else()
            string(APPEND _out "/")                # 失配起点：保留 '/' 原样
            math(EXPR _i "${_open} + 1")           # 逐位推进重试
        endif()
    endwhile()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# ① SUBSTRING -1 语义确认
# ---------------------------------------------------------------------
string(SUBSTRING "abcdef" 2 -1 _rest)
if(_rest STREQUAL "cdef")
    message("PROBE-1 SUBSTRING(-1)=remainder: OK")
else()
    message(FATAL_ERROR "PROBE-1 SUBSTRING -1 语义不符: got '${_rest}'")
endif()

# ---------------------------------------------------------------------
# ② 等价性语料（每行一个用例；期望＝旧管线即基准）
# ---------------------------------------------------------------------
set(_cases
    ""
    "no comment at all"
    "/* simple */ int x;"
    "/**/"
    "/***/"
    "/*a**/ int x;"                              # 偶星收尾：原正则不匹配
    "/*a***/ int x;"                             # 奇星收尾：原正则整体匹配
    "/*a*/ x /*b**/ y"
    "/**/x/**/"
    "int x; /* never closed"
    "/* // */ int y;"
    "// line /* not block */"
    "/* c1 */ // c2 /* c3 */"
    "/*/"
    "/* * / */"
    "a**/b"
    "/**/\"RobWork\"/**/"
    "/*a*//*b*/"
    "/* /* */ x"
    "*/ alone"
    "/* alone"
    "a = \"/*\"; b = \"*/\"; c"                  # 引号不特殊：/* 在串内也开注释
    "/* **/ code(\"RobWork\"); /* end */"        # 贪心吞并中部代码
    "x = \"http://a\"; /* b */ y"                # 行注释截断后块语义
    "/* u */ int a; /* v */ int b; /* w */"      # 多重非重叠
    "/*** /*/ z"                                 # 星串吞斜杠后重找闭合
)

set(_fail 0)
set(_idx 0)
foreach(_c ${_cases})
    math(EXPR _idx "${_idx} + 1")
    strip_old("${_c}" _o)
    string(REGEX REPLACE "//[^\n]*" "" _l "${_c}")
    strip_block_dfa("${_l}" _n2)
    if(NOT _o STREQUAL _n2)
        message("CASE ${_idx} 不等价!")
        message("  输入: [[${_c}]]")
        message("  旧: [[${_o}]]")
        message("  新: [[${_n2}]]")
        math(EXPR _fail "${_fail} + 1")
    endif()
    # 双重核对：判定面（RobWork 引号字面量 MATCHALL）一致
    string(REGEX MATCHALL "\"[^\"]*RobWork[^\"]*\"" _mo "${_o}")
    string(REGEX MATCHALL "\"[^\"]*RobWork[^\"]*\"" _mn "${_n2}")
    if(NOT "${_mo}" STREQUAL "${_mn}")
        message("CASE ${_idx} 判定面不一致: 旧[${_mo}] 新[${_mn}]")
        math(EXPR _fail "${_fail} + 1")
    endif()
endforeach()
message("PROBE-2 语料等价: ${_idx} 例, 不一致 ${_fail} 例")

# ---------------------------------------------------------------------
# ②b 长注释语料（320 星行连续块注释——F-570 同形态）＋星串对抗
# ---------------------------------------------------------------------
string(REPEAT " * 回归锚星行 RobWork 框架名仅注释提及\n" 320 _longbody)
set(_long_comment "/**\n${_longbody} */\nint k = 1;\n")
strip_old("${_long_comment}" _o)
string(REGEX REPLACE "//[^\n]*" "" _l "${_long_comment}")
strip_block_dfa("${_l}" _n2)
if(_o STREQUAL _n2)
    message("PROBE-2b 320 星行长注释等价: OK (新输出长度 ${_n2})")
else()
    message("PROBE-2b 长注释不等价: 旧[[${_o}]] 新[[${_n2}]]")
    math(EXPR _fail "${_fail} + 1")
endif()

string(REPEAT "*****\n" 320 _starrun)
set(_unclosed "int q;\n/**\n${_starrun}")       # 未闭合星行块
strip_old("${_unclosed}" _o)
string(REGEX REPLACE "//[^\n]*" "" _l "${_unclosed}")
strip_block_dfa("${_l}" _n2)
if(_o STREQUAL _n2)
    message("PROBE-2c 未闭合星串块等价: OK")
else()
    message("PROBE-2c 不等价")
    math(EXPR _fail "${_fail} + 1")
endif()

# ---------------------------------------------------------------------
# ②c 真实产品文件全集等价（IRD_ROOT 由 -D 传入）
# ---------------------------------------------------------------------
if(DEFINED PROBE_TREE AND NOT PROBE_TREE STREQUAL "")
    file(GLOB_RECURSE _files
        "${PROBE_TREE}/*/include/*.hpp" "${PROBE_TREE}/*/include/*.h"
        "${PROBE_TREE}/*/include/*.cpp" "${PROBE_TREE}/*/include/*.ipp"
        "${PROBE_TREE}/*/src/*.hpp" "${PROBE_TREE}/*/src/*.h"
        "${PROBE_TREE}/*/src/*.cpp" "${PROBE_TREE}/*/src/*.ipp")
    list(LENGTH _files _nf)
    set(_treefail 0)
    set(_rwhit_old 0)
    foreach(_f ${_files})
        file(READ "${_f}" _src)
        strip_old("${_src}" _o)
        string(REGEX REPLACE "//[^\n]*" "" _l "${_src}")
        strip_block_dfa("${_l}" _n2)
        if(NOT _o STREQUAL _n2)
            message("真实文件不等价: ${_f}")
            math(EXPR _treefail "${_treefail} + 1")
        endif()
        string(REGEX MATCHALL "\"[^\"]*RobWork[^\"]*\"" _mo "${_o}")
        string(REGEX MATCHALL "\"[^\"]*RobWork[^\"]*\"" _mn "${_n2}")
        if(NOT "${_mo}" STREQUAL "${_mn}")
            message("真实文件判定面不一致: ${_f} 旧[${_mo}] 新[${_mn}]")
            math(EXPR _treefail "${_treefail} + 1")
        endif()
        if(_mo)
            math(EXPR _rwhit_old "${_rwhit_old} + 1")
        endif()
    endforeach()
    message("PROBE-2d 真实产品面 ${_nf} 文件: 剥离输出不一致 ${_treefail} 文件（其中命中 R4 词面的 ${_rwhit_old} 文件）")
    math(EXPR _fail "${_fail} + ${_treefail}")
endif()

if(_fail GREATER 0)
    message(FATAL_ERROR "PROBE 等价性失败 ${_fail} 处")
endif()
message("PROBE 全部通过")
