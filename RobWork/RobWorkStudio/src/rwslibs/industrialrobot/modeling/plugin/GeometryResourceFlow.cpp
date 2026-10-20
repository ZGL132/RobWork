/**
 * @file   GeometryResourceFlow.cpp
 * @brief  几何资源选择流实现（契约头 GeometryResourceFlow.hpp——io 端口
 *         真装＋对话框编排；B3h追加工具/场景私有候选资源登记，不写项目修订。
 */

#include "GeometryResourceFlow.hpp"
#include <algorithm>
#include <cctype>

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
        // Qt/领域字符串契约为UTF-8；Windows窄路径构造受系统代码页影响，
        // 中文网格会在转换/回填时抛异常。显式UTF-8↔原生路径，不改变io安全检查。
        const std::filesystem::path path =
            std::filesystem::u8path(absPath).make_preferred();

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
        result.absPath = snap.value.finalPath.u8string();
        result.isMeshFamily = isMeshFamilyKind(identified.value);
        return result;
    };
}


// B3h/MDL-13/15：场景和工具共用同一几何分类及Recorded登记，避免两套来源规则漂移。
/**
 * @brief 从已有几何引用或Recorded网格文件确定类别，排除来源XML等非几何资源。
 * @param ws [in] 本次只读基线；不读取文件/不产生新摘要。
 * @param key [in] 清单中的唯一资源键。
 * @return 几何类别；非几何返回nullopt。纯输入分类，不替代io格式/预算检查。
 */
std::optional<GeometryKind> existingGeometryKind(const ModelingWorkingSet& ws,const std::string& key)
{
    std::optional<GeometryKind> kind;
    const auto take=[&](const std::optional<GeometryRef>& ref){if(ref&&ref->resourceRefId==key){kind=ref->kind;}};
    for(const auto& link:ws.design.links){take(link.visual);take(link.collision);}
    for(const auto& tool:ws.toolObjects){take(tool.geometry);}
    for(const auto& scene:ws.sceneObjects){take(scene.geometry);}
    if(kind){return kind;}
    for(const auto& resource:ws.design.resourceManifest){
        if(resource.resourceId!=key||!resource.externalRecord){continue;}
        auto extension=std::filesystem::u8path(resource.externalRecord->absPath).extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".stl"||extension==".obj"||extension==".dae"){return GeometryKind::Mesh;}
    }
    return std::nullopt;
}

std::optional<ModelingWorkingSet> evaluateManagedGeometry(const ModelingWorkingSet& before,
    std::optional<GeometryRef>& geometry,ManagedGeometrySource source,const std::string& resourceOrPath,
    const ResourceProbeFn& probe,std::string& error)
{
    error.clear();
    const auto fail=[&](const std::string& why)->std::optional<ModelingWorkingSet>{error=why;return std::nullopt;};
    auto candidate=before;
    if(source==ManagedGeometrySource::None){geometry.reset();}
    else {
        std::string resourceId=resourceOrPath;
        if(source==ManagedGeometrySource::External){
            if(!probe){return fail("几何读取服务尚未连接，请重新打开建模页。");}
            std::string why;const auto result=probe(resourceOrPath,why);
            if(!result){return fail("几何读取被拒绝："+why);}
            if(!result->isMeshFamily){return fail("请选择受支持的STL、OBJ或DAE几何文件。");}
            // 同一冻结内容及实体路径复用已有Recorded条目，防重复确认制造伪变更。
            const auto existing=std::find_if(candidate.design.resourceManifest.begin(),candidate.design.resourceManifest.end(),
                [&](const ResourceRef& ref){return ref.state==ResourceState::Recorded&&ref.externalRecord
                    &&ref.contentDigest==result->contentDigest&&ref.externalRecord->absPath==result->absPath
                    &&ref.externalRecord->recordedDigest==result->contentDigest;});
            if(existing!=candidate.design.resourceManifest.end()){resourceId=existing->resourceId;}
            else {
                for(std::size_t n=1;;++n){
                    resourceId="res-"+std::to_string(n);
                    if(std::none_of(candidate.design.resourceManifest.begin(),candidate.design.resourceManifest.end(),
                        [&](const ResourceRef& ref){return ref.resourceId==resourceId;})){break;}
                }
                ResourceRef ref;ref.resourceId=resourceId;ref.contentDigest=result->contentDigest;
                ref.externalRecord=ExternalResourceRecord{result->absPath,result->contentDigest};
                candidate.design.resourceManifest.push_back(std::move(ref));
            }
        }
        if(std::count_if(candidate.design.resourceManifest.begin(),candidate.design.resourceManifest.end(),
            [&](const ResourceRef& ref){return ref.resourceId==resourceId;})!=1){
            return fail("选中的几何资源已不存在或不唯一，请重新选择。");
        }
        const auto kind=source==ManagedGeometrySource::External
            ?std::optional<GeometryKind>(GeometryKind::Mesh):existingGeometryKind(candidate,resourceId);
        if(!kind){return fail("所选资源不是几何；请选择网格资源，不能使用来源XML等文件。");}
        // 替换来源不改变几何局部姿态；世界位姿始终保留，不能叠加基座安装变换。
        auto nextGeometry=geometry.value_or(GeometryRef{});
        nextGeometry.resourceRefId=resourceId;
        nextGeometry.kind=*kind;
        geometry=std::move(nextGeometry);
    }
    return candidate;
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
