/**
 * @file   PackageWorkCell.cpp
 * @brief  WorkCell/DWC XML 外供导出的实现（exportWorkCellXml）——数据源为
 *         runtime 快照只读视图（WorkCellConstView/DynamicWorkCellConstView），
 *         modeling 仅编排"取只读数据→确定性 XML 序列化→io AtomicWriter
 *         原子落盘"，不改造内容（卡 §6.8 第二条；ACC5）。
 *
 * 设计依据：units/modeling.md §6.8（Package.hpp 文件头注全文）；任务契约
 * tasks/foundation/WP-13-T13.json acceptance 5。
 *
 * 实现注：
 *   ① 本 TU 消费 runtime 编译产物的**非模板类**（WorkCell/Device/Joint/
 *      Body 基线符号与 WorkCellConstView 实现），仅集成模式编译（runtime
 *      CMake 同款 TARGET sdurw_kinematics gating——冒烟模式声明可用、无 TU
 *      消费，runtime §15.4 v0.10 同款分工）。
 *   ② XML 为 modeling 自有的外供查看表示（元素/属性命名镜像 RobWork
 *      WorkCell/DWC 概念，便于外部工具阅读），非 RobWork wc.xml 格式——
 *      框架写侧 sdurw_loaders 属"原则不使用、启用须登记"表行（DTB §4.6），
 *      本任务不引入（io/modeling 通道自行实现 XML 处理的同款设计哲学）。
 *   ③ "不改造内容"的落点：视图逐项只读取数（帧名/父子/变换/设备/关节限
 *      位/体物性——无增删改、无单位换算——基线 WC 内部已是 SI，§3.4 总约
 *      定 4），数值 std::to_chars 最短往返（locale 无关——NFR-COR-02）；
 *      同快照＋同目标→同 XML 字节。
 *   ④ 零修订、零失效：本函数只读快照视图＋写目标文件，不触碰 project
 *      修订与评估器依赖键（消费已应用修订的编译产物——不私设第二编译
 *      路径）。
 */

#include <sdurws/ird/modeling/Package.hpp>

#include <charconv>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <rw/kinematics/Frame.hpp>          // Frame 只读查询（名/父/变换/DOF）
#include <rw/kinematics/State.hpp>          // 默认状态（getTransform 输入）
#include <rw/models/Device.hpp>             // Device 基类（名/基座/末端/DOF）
#include <rw/models/Joint.hpp>              // Joint 限位（getBounds——rad/m）
#include <rw/models/JointDevice.hpp>        // SerialDevice 的关节表（getJoints）
#include <rw/models/SerialDevice.hpp>       // S6 产出的设备类型
#include <rw/models/WorkCell.hpp>           // WC 只读查询（名/帧/设备）
#include <rwsim/dynamics/Body.hpp>          // Body 信息（质量/质心/惯量/材料）
#include <rwsim/dynamics/DynamicDevice.hpp> // DWC 动力学子（名）
#include <rwsim/dynamics/DynamicWorkCell.hpp>   // DWC 只读查询（重力/体/设备）

#include <sdurws/ird/io/AtomicFile.hpp>     // IAtomicFileWriter/ReplacePolicy（IO-T06 落位面）
#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken——io 码 token 唯一来源
#include <sdurws/ird/modeling/DiagCodes.hpp>    // MDL-EXPORT-FAILED 常量（禁字符串拼码）
#include <sdurws/ird/runtime/Adapter.hpp>   // WorkCellConstView/DynamicWorkCellConstView（§8.3）
#include <sdurws/ird/runtime/Errors.hpp>    // runtime::Expected/RuntimeError（视图失败轨）
#include <sdurws/ird/runtime/Snapshot.hpp>  // RuntimeSnapshot（WC/DWC 视图持有者）

namespace sdurws::ird::modeling {
namespace {

// =====================================================================
// XML 序列化小工具（确定性纯函数）
// =====================================================================

/// double → 最短往返文本（std::to_chars——locale 无关；非有限值防御占位）。
std::string xmlDouble(double v)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
    if (result.ec != std::errc()) {
        return "nan";   // 建模对象/编译产物不含非有限值——防御性确定性占位
    }
    return std::string(buffer, result.ptr);
}

/// XML 属性/文本转义（&<>"' 五字符——名称经 runtime 名称映射合法化，防御
/// 性转义保证输出恒为良构 XML）。
std::string xmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

/// Vector3D → "x,y,z"（单位由属性名/注释承载——WC 内部 SI，§3.4 总约定 4）。
std::string xmlVector(const rw::math::Vector3D<double>& v)
{
    return xmlDouble(v[0]) + "," + xmlDouble(v[1]) + "," + xmlDouble(v[2]);
}

/// 位姿 → 平移＋旋转行主序 12 元组（T_parent_frame——Frame::getTransform
/// 的 "相对父帧" 契约，与 modeling JointPose 同一 T_ab 语义方向）。
std::string xmlTransform(const rw::math::Transform3D<double>& t)
{
    std::string out = xmlDouble(t.P()[0]) + "," + xmlDouble(t.P()[1]) + "," + xmlDouble(t.P()[2]);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            out += "," + xmlDouble(t.R()(row, col));
        }
    }
    return out;
}

/// 单文件原子落盘（io AtomicFile prepare→写入→commit——失败 abort，目标
/// 零接触；返回空串＝成功，非空＝io 稳定码 token 定位的环境失败描述）。
bool atomicWrite(const std::filesystem::path& targetFile, io::ReplacePolicy replace,
                 const std::string& bytes, std::string* failDetail)
{
    io::IAtomicFileWriterPtr writer = io::makeAtomicFileWriter();
    auto prepared = writer->prepare(targetFile, replace);
    if (!prepared) {
        *failDetail = std::move(prepared.error.detail);
        return false;
    }
    io::AtomicTarget atomicTarget = prepared.value;
    if (auto written = atomicTarget.write(bytes); !written) {
        (void)writer->abort(atomicTarget);   // 暂存清理——目标不变（V-29 同款）
        *failDetail = std::move(written.error.detail);
        return false;
    }
    if (auto committed = writer->commit(atomicTarget); !committed) {
        (void)writer->abort(atomicTarget);
        *failDetail = std::move(committed.error.detail);
        return false;
    }
    return true;
}

/// 导出失败错误值构造（Package.cpp 匿名命名空间的 makeError 不跨 TU 暴露
/// ——本 TU 自持同形构造：错误值的字面构造无共享状态，"唯一实现"纪律
/// 针对的是语义判定与哈希/序列化算法——与 Import.cpp 失败出口自持同款口径）。
ModelingError makeExportError(std::string detail)
{
    ModelingError error;
    error.code = ModelingErrorCode::ExportFailed;
    error.detail = std::move(detail);
    return error;
}

/// MDL-EXPORT-FAILED 失败出口（诊断＋错误值一体——导出族统一语义）。
PackageExportOutcome failExport(std::vector<core::DiagnosticRecord>& diags,
                                std::string stage, std::string detail)
{
    PackageExportOutcome outcome;
    diags.push_back(core::DiagnosticRecord::make(
        std::string(kMdlExportFailed), std::nullopt, std::nullopt, std::nullopt,
        "workcell-export@" + stage,
        detail,   // 诊断侧拷贝（cause 为值参——两处消费各自持有）
        "项目状态不变且旧输出文件完好；检查目标路径/预算后重试（MDL-20）"));
    outcome.error = makeExportError(std::move(detail));
    return outcome;
}

}  // namespace

PackageExportOutcome exportWorkCellXml(const runtime::RuntimeSnapshot& snapshot,
                                       const WorkCellExportTarget& target,
                                       std::vector<core::DiagnosticRecord>& diags)
{
    PackageExportOutcome outcome;

    // ---- WC 面（数据源＝快照只读视图——Compiled 态前置由调用方保证）----
    std::string wcXml;
    std::uint64_t writtenFiles = 0;
    std::uint64_t writtenBytes = 0;

    if (!target.wcTargetFile.empty()) {
        const runtime::WorkCellConstView& wcView = snapshot.workCell();
        const rw::models::WorkCell& workCell = wcView.workCell();
        const rw::kinematics::State defaultState = wcView.defaultState();

        std::string xml;
        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml += "<ird-workcell-export name=\"" + xmlEscape(workCell.getName())
            + "\" generator=\"ird-modeling\">\n";

        // ---- 帧表（getFrames 全量——名/父/变换/自由度；逐项只读）----
        xml += "  <frames>\n";
        for (const rw::kinematics::Frame* frame : workCell.getFrames()) {
            const rw::kinematics::Frame* parent = frame->getParent();
            xml += "    <frame name=\"" + xmlEscape(frame->getName())
                + "\" parent=\"" + xmlEscape(parent != nullptr ? parent->getName() : "")
                + "\" dof=\"" + std::to_string(frame->getDOF())
                + "\" t-parent-frame=\"" + xmlTransform(frame->getTransform(defaultState))
                + "\"/>\n";
        }
        xml += "  </frames>\n";

        // ---- 设备表（S6 产出为 SerialDevice——基座/末端/逐关节限位）----
        xml += "  <devices>\n";
        for (const rw::core::Ptr<rw::models::Device>& device : workCell.getDevices()) {
            xml += "    <device name=\"" + xmlEscape(device->getName())
                + "\" base=\"" + xmlEscape(device->getBase()->getName())
                + "\" end=\"" + xmlEscape(device->getEnd()->getName())
                + "\" dof=\"" + std::to_string(device->getDOF()) + "\">\n";
            if (const auto* serial = dynamic_cast<const rw::models::SerialDevice*>(device.get());
                serial != nullptr) {
                // 关节逐项（限位对 rad/m 随类型——Joint::getBounds 权威值）。
                const std::vector<rw::models::Joint*> joints = serial->getJoints();
                for (std::size_t i = 0; i < joints.size(); ++i) {
                    const rw::models::Joint* joint = joints[i];
                    const auto bounds = joint->getBounds();
                    xml += "      <joint name=\"" + xmlEscape(joint->getName())
                        + "\" index=\"" + std::to_string(i)
                        + "\" lower=\"" + xmlDouble(bounds.first[0])
                        + "\" upper=\"" + xmlDouble(bounds.second[0])
                        + "\"/>\n";
                }
            }
            xml += "    </device>\n";
        }
        xml += "  </devices>\n";
        xml += "</ird-workcell-export>\n";

        std::string failDetail;
        if (!atomicWrite(target.wcTargetFile, target.replace, xml, &failDetail)) {
            return failExport(diags, "wc-write", std::move(failDetail));
        }
        ++writtenFiles;
        writtenBytes += xml.size();
    }

    // ---- DWC 面（能力门控——快照无 DWC 而请求了 DWC 目标＝失败）----
    if (!target.dwcTargetFile.empty()) {
        auto dwcView = snapshot.tryDynamicWorkCell();
        if (!dwcView.ok()) {
            return failExport(diags, "dwc-view",
                              std::string(dwcView.error().what()));
        }
        const rwsim::dynamics::DynamicWorkCell& dwc = dwcView.get().dynamicWorkCell();

        std::string xml;
        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml += "<ird-dwc-export generator=\"ird-modeling\" gravity=\""
            + xmlVector(dwc.getGravity()) + "\">\n";

        // ---- 体表（getInfo 全量——质量/质心/惯量/材料；不判型、不改造）----
        xml += "  <bodies>\n";
        for (const rwsim::dynamics::Body::Ptr& body : dwc.getBodies()) {
            const rwsim::dynamics::BodyInfo& info = body->getInfo();
            xml += "    <body name=\"" + xmlEscape(body->getName())
                + "\" material=\"" + xmlEscape(info.material)
                + "\" mass=\"" + xmlDouble(info.mass)
                + "\" masscenter=\"" + xmlVector(info.masscenter);
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t col = 0; col < 3; ++col) {
                    xml += (row == 0 && col == 0 ? "\" inertia=\"" : ",");
                    xml += xmlDouble(info.inertia(row, col));
                }
            }
            xml += "\"/>\n";
        }
        xml += "  </bodies>\n";

        // ---- 动力设备名表（S7 产出 RigidDevice——名面即可定位回 WC 设备）----
        xml += "  <devices>\n";
        for (const rwsim::dynamics::DynamicDevice::Ptr& device : dwc.getDynamicDevices()) {
            xml += "    <dynamic-device name=\"" + xmlEscape(device->getName()) + "\"/>\n";
        }
        xml += "  </devices>\n";
        xml += "</ird-dwc-export>\n";

        std::string failDetail;
        if (!atomicWrite(target.dwcTargetFile, target.replace, xml, &failDetail)) {
            return failExport(diags, "dwc-write", std::move(failDetail));
        }
        ++writtenFiles;
        writtenBytes += xml.size();
    }

    outcome.ok = true;
    outcome.entryCount = writtenFiles;
    outcome.totalBytes = writtenBytes;
    return outcome;
}

}  // namespace sdurws::ird::modeling
