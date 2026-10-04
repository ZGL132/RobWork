/**
 * @file   GeometryResourceFlow.cpp
 * @brief  几何资源选择流实现（契约头 GeometryResourceFlow.hpp——io 端口
 *         真装＋对话框编排；本文件零模型语义——登记/挂接全部在域原语）。
 */

#include "GeometryResourceFlow.hpp"

#include <memory>
#include <utility>

#include <QFileDialog>

#include <sdurws/ird/io/Budget.hpp>       // makeBudgetGuard（预算守卫装配线——PM-09）
#include <sdurws/ird/io/IoDiagnostics.hpp>  // errorCodeToken（io 错误 token——诚实呈现素材）
#include <sdurws/ird/io/IoFwd.hpp>        // IoResult/IoError（错误轨道值面）
#include <sdurws/ird/io/ResourceIo.hpp>   // IResourceReader/makeResourceReader/ResourceKind（受管读取端口）
#include "PanelEditFlow.hpp"              // IPanelEditSink/EditRejection（编辑回流面）

namespace sdurws {
namespace ird {
namespace modeling {

namespace {

/// 几何承载族判定（§2.5 行 6——stl/obj/dae；io 登记族中的几何子集）。
bool isMeshFamilyKind(io::ResourceKind kind)
{
    switch (kind) {
    case io::ResourceKind::BinaryStl:
    case io::ResourceKind::AsciiStl:
    case io::ResourceKind::WavefrontObj:
    case io::ResourceKind::ColladaDae:
        return true;
    default:
        return false;  // URDF/Xacro/纹理/材质库等——登记族但非几何承载
    }
}

/// io 错误 → 域侧拒绝文本（token＋预算三要素/路径定位直投——脱敏由
/// diagnostics 呈现层既有链承载，本层不二次加工）。
std::string ioErrorText(const io::IoError& error)
{
    std::string text = std::string(io::errorCodeToken(error.code));
    for (const auto& [key, value] : error.params) {
        text += " " + key + "=" + value;
    }
    if (!error.detail.empty()) {
        text += "：" + error.detail;
    }
    return text;
}

}  // namespace

ResourceProbeFn makeIoResourceProbe()
{
    // io 产品装配线（端口实例无状态可复用；P-1 UserSource 角色——用户
    // 对话框自选路径的 io 合法通道，NFR-SEC-01 路径只在此层出现）。
    const io::IResourceReaderPtr reader = io::makeResourceReader();
    const io::IBudgetGuardPtr budget = io::makeBudgetGuard();
    return [reader, budget](const std::string& absPath,
                                        std::string& errText)
        -> std::optional<ResourceProbeResult> {
        const std::filesystem::path path =
            std::filesystem::path(absPath).make_preferred();

        // ①格式识别（魔数/扩展名——读前轻探；失败＝IO-FORMAT-MESH-UNKNOWN
        // 族，detail 携带首 16 字节摘要——io 侧脱敏语义）。
        const io::IoResult<io::ResourceKind> identified = reader->identify(path);
        if (!identified) {
            errText = ioErrorText(identified.error);
            return std::nullopt;
        }
        // ②受管读取（SafePath 逃逸检查＋预算 SingleFileBytes 预检＋
        // SHA-256 摘要——全部在 snapshot 端口内；P-1 角色基点为空）。
        const io::ResourceOpenSpec spec;
        const io::IoResult<io::ResourceSnapshot> snap =
            reader->snapshot(path, spec, budget.get(), nullptr);
        if (!snap) {
            errText = ioErrorText(snap.error);
            return std::nullopt;
        }

        ResourceProbeResult result;
        result.contentDigest = snap.value.contentDigest;
        result.absPath = snap.value.finalPath.string();
        result.isMeshFamily = isMeshFamilyKind(identified.value);
        return result;
    };
}

bool runGeometryResourceSelection(QWidget& parent, ModelingWorkingSet& ws,
                                  std::size_t linkIndex, GeometrySlot slot,
                                  IPanelEditSink& sink)
{
    // 文件对话框（§2.5 行 6 格式族过滤器；用户取消＝静默返回——非错误
    // 不出诊断不落摘要）。
    const QString path = QFileDialog::getOpenFileName(
        &parent, QStringLiteral("选择几何文件"), QString(),
        QStringLiteral("几何文件（stl obj dae）"
                       ";;STL（*.stl）;;OBJ（*.obj）;;COLLADA（*.dae）;;全部文件（*.*）"));
    if (path.isEmpty()) {
        return false;  // 用户取消——工作集未动
    }

    // 域原语（唯一判定点——io 探测/格式族校验/登记/挂接全在域侧；拒绝
    // 路径工作集字节不变）。
    const std::optional<GeometryLinkError> err = attachExternalGeometry(
        ws, linkIndex, slot, path.toStdString(), makeIoResourceProbe());
    if (!err.has_value()) {
        return true;  // 挂接落草稿（刷新编排归调用方）
    }

    // 拒绝呈现（比较型原因直投——UX-03 非模态；io detail 经错误码 token
    // 化，脱敏由 diagnostics 呈现链既有承载）。
    EditRejection rejection;
    rejection.codeToken = std::string(geometryLinkErrorCodeToken(err->code));
    rejection.detail = err->detail;
    sink.onEditRejected(rejection);
    return false;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws