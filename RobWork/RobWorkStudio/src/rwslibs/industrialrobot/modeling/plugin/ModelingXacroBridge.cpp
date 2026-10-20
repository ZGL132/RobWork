/**
 * @file ModelingXacroBridge.cpp
 * @brief WP-13-T24/MDL-19：生产Xacro冻结读取与展开装配，io护栏/领域语义不重写。
 *
 * io通用XML读取负责解析安全和单文件预算。本层只走查已解析include节点，
 * 组装既有ValidatedXacroSource：include为硬依赖，mesh留给URDF映射按缺失警告处理。
 * 整轮另用io预算累计文件数/总字节/深度，避免逐文件预算重置形成漏洞。
 * UI线程调用，所有状态局部拥有；不调用shell，不写项目，不读环境变量。
 */
#include "ModelingXacroBridge.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <functional>
#include <set>

#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/modeling/XacroExpand.hpp>

namespace sdurws::ird::modeling {
namespace {
/** @brief 折叠导入根相对键；[in] path已经规范化，返回正斜杠ASCII小写确定性键。 */
std::string relativeKey(const std::filesystem::path& path)
{
    std::string text=path.generic_u8string();
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
/** @brief 透传io稳定码并附相对文件定位；[in] error为失败值，key不得使用绝对路径。 */
std::string describeIo(const io::IoError& error, const std::string& key)
{
    std::string text=std::string(io::errorCodeToken(error.code))+"："+key;
    // 比较型预算诊断保留实际值/上限/单位；不把底层绝对路径原文直接抛向界面。
    for(const auto& item:error.params){
        if(item.first=="actual"||item.first=="limit"||item.first=="unit"){
            text+=" "+item.first+"="+item.second;
        }
    }
    return text;
}
/** @brief 读取XML属性；[in] node借用io值树、key为原属性名，缺席返回空串。 */
std::string attribute(const io::GenericXmlNode& node, const char* key)
{
    for(const auto& a:node.attrs){if(a.first==key){return a.second;}}
    return {};
}
} // namespace

/**
 * @brief 冻结输入内部实现；[in] expected=nullptr代表直接安全读盘，否则逐字节比对入口。
 * [in] path为用户选择路径；返回失败值不用于展开。预算/句柄由本轮局部对象所有。
 */
static ModelXacroInput loadXacroInputImpl(const std::vector<std::uint8_t>* expected, const QString& path)
{
    ModelXacroInput result;
    const auto reader=io::makeResourceReader();
    const auto resolver=io::makeSafePathResolver();
    const auto normalized=resolver->normalize(io::PathRole::UserSource,path.toStdWString());
    if(!normalized){result.errorDetail=describeIo(normalized.error,"入口文件");return result;}
    const std::filesystem::path rootPath=std::filesystem::u8path(normalized.value.display);
    const auto rootDir=rootPath.parent_path();
    result.dependencyTree.rootRel=relativeKey(rootPath.filename());
    const auto budget=io::makeBudgetGuard();
    const auto opened=budget->openScope(io::BudgetSpec::productDefault());
    if(!opened){result.errorDetail=describeIo(opened.error,"预算开户");return result;}
    const auto scope=opened.value;
    // 本scope无父级，关闭不触发父账合并；仍检查返回值，不能吞掉预算管理错误。
    auto closeScope=[&]{
        const auto closed=budget->closeScope(scope);
        if(!closed){result.ok=false;result.errorDetail=describeIo(closed.error,"预算关闭");}
    };
    auto charge=[&](io::BudgetDimension dim,std::uint64_t amount,const std::string& key){
        const auto r=budget->charge(scope,dim,amount);
        if(!r){result.errorDetail=describeIo(r.error,key);return false;}
        return true;
    };
    std::set<std::string> active,visited,parameterNames;
    std::vector<std::string> includeStack;
    std::uint32_t maxSeenDepth=0;
    std::function<bool(const std::filesystem::path&,std::uint32_t)> visit;
    visit=[&](const std::filesystem::path& file,std::uint32_t depth){
        const auto resolved=resolver->normalize(io::PathRole::UserSource,file.wstring());
        if(!resolved){result.errorDetail=describeIo(resolved.error,relativeKey(file.lexically_relative(rootDir)));return false;}
        const auto rel=std::filesystem::u8path(resolved.value.display).lexically_relative(rootDir);
        // io的UserSource仅负责用户文件准入；include额外受入口目录管辖。
        // 比较最终实体路径，符号链接不能把include带出导入根；不依赖字符串前缀。
        if(rel.empty()||rel.is_absolute()||*rel.begin()==".."){
            result.errorDetail=std::string(io::errorCodeToken(io::IoErrorCode::SecPathEscape))+"：include越过导入根";return false;
        }
        const std::string key=relativeKey(file.lexically_relative(rootDir));
        const std::string physicalKey=relativeKey(rel);
        if(active.count(physicalKey)){
            result.errorDetail=std::string(io::errorCodeToken(io::IoErrorCode::FormatXmlCycle))+"：";
            for(const auto& part:includeStack){result.errorDetail+=part+" -> ";}
            result.errorDetail+=key;return false;
        }
        // io charge为累计量；深度账只记新增最大值，不能把宽树各分支深度相加。
        if(depth>maxSeenDepth){
            if(!charge(io::BudgetDimension::IncludeDepth,depth-maxSeenDepth,key)){return false;}
            maxSeenDepth=depth;
        }
        if(visited.count(key)){return true;}
        if(!charge(io::BudgetDimension::FileCount,1,key)){return false;}
        const auto parsed=reader->openGenericXml(file,{},nullptr,nullptr);
        if(!parsed){result.errorDetail=describeIo(parsed.error,key);return false;}
        if(!charge(io::BudgetDimension::TotalBytes,parsed.value.totalBytes,key)){return false;}
        // 读取原文时仍经io句柄，逐块累计预算并复算摘要，与安全解析的摘要相对照。
        // 不使用普通fstream绕过单文件/句柄检查，也不把变化窗口读出的字节冒充原快照。
        auto stream=reader->open(file,{},nullptr,nullptr);
        if(!stream){result.errorDetail=describeIo(stream.error,key);return false;}
        std::vector<std::uint8_t> content;
        core::ContentDigester digester;
        // include每层保留局部状态直到子文件返回；64 KiB缓冲必须在堆上，
        // 否则默认16层就耗尽Windows默认1 MiB栈，来不及返回深度预算诊断。
        std::vector<std::uint8_t> block(io::kResourceReadBlockBytes);
        for(;;){
            const auto chunk=stream.value.read(block.data(),block.size());
            if(!chunk){result.errorDetail=describeIo(chunk.error,key);return false;}
            if(chunk.value==0){break;}
            if(!charge(io::BudgetDimension::TotalBytes,chunk.value,key)){return false;}
            content.insert(content.end(),block.begin(),block.begin()+chunk.value);
            digester.update(block.data(),chunk.value);
        }
        const auto digest=digester.finalize();
        if(digest!=parsed.value.digest||content.size()!=parsed.value.totalBytes){
            result.errorDetail=std::string(io::errorCodeToken(io::IoErrorCode::ResChanged))+"："+key;return false;
        }
        if(key==result.dependencyTree.rootRel&&expected&&content!=*expected){
            result.errorDetail=std::string(io::errorCodeToken(io::IoErrorCode::ResChanged))+"：入口文件在选择后发生变化";return false;
        }
        io::ResourceSnapshot snapshot;
        snapshot.finalPath=resolved.value.native;snapshot.sizeBytes=content.size();
        snapshot.contentDigest=digest; // mtime=0：身份只按实际读取字节，不取时钟。
        io::ResourceNode resource;resource.relPath=key;resource.exists=true;resource.snapshot=snapshot;
        result.dependencyTree.nodes.push_back(std::move(resource));
        if(key==result.dependencyTree.rootRel){
            result.entryBytes=std::move(content);result.entrySnapshot=snapshot;
        }else{result.includeBytes.emplace(key,std::move(content));}
        active.insert(physicalKey);includeStack.push_back(key);
        std::function<bool(const io::GenericXmlNode&)> walk;
        walk=[&](const io::GenericXmlNode& node){
            // 元素仍由io解析；这里只作语义路由，不用正则重写XML解析器。
            if(node.name=="xacro:property"){
                const auto name=attribute(node,"name");
                if(!name.empty()&&parameterNames.insert(name).second){
                    result.parameterDefaults.emplace_back(name,attribute(node,"value"));
                }
            }
            if(node.name=="xacro:include"){
                const auto target=attribute(node,"filename");
                // 动态include路径不猜测、不读取任意位置；受控引擎本来只允许离线依赖。
                if(target.empty()||target.find('$')!=std::string::npos){
                    result.errorDetail="MDL-IMPORT-XACRO-UNRESOLVED："+key+" include须为离线相对路径";return false;
                }
                const auto candidate=(rel.parent_path()/std::filesystem::u8path(target)).lexically_normal();
                io::ResourceEdge edge;edge.fromRel=key;edge.toRel=relativeKey(candidate);
                edge.kind=io::ResourceEdgeKind::Include;result.dependencyTree.edges.push_back(std::move(edge));
                if(!visit(rootDir/candidate,depth+1)){return false;}
            }
            for(const auto& child:node.children){if(!walk(child)){return false;}}
            return true;
        };
        if(!walk(parsed.value.root)){return false;}
        active.erase(physicalKey);includeStack.pop_back();visited.insert(key);return true;
    };
    result.ok=visit(rootPath,1);
    closeScope();
    if(result.ok){
        std::sort(result.dependencyTree.nodes.begin(),result.dependencyTree.nodes.end(),
            [](const auto& a,const auto& b){return a.relPath<b.relPath;});
    }
    return result;
}

ModelXacroInput loadXacroInput(const std::vector<std::uint8_t>& bytes,const QString& path)
{
    return loadXacroInputImpl(&bytes,path);
}

ModelXacroInput loadXacroInput(const QString& path)
{
    return loadXacroInputImpl(nullptr,path);
}

ModelXacroExpandResult expandXacroInput(const ModelXacroInput& input,
    const std::vector<std::pair<std::string,std::string>>& substitutions)
{
    ModelXacroExpandResult result;
    result.entrySnapshot=input.entrySnapshot;result.sourceDigest=input.entrySnapshot.contentDigest;
    if(!input.ok){result.errorDetail=input.errorDetail;return result;}
    ValidatedXacroSource source;
    source.entryBytes=input.entryBytes;source.entrySnapshot=input.entrySnapshot;
    source.dependencyTree=input.dependencyTree;source.includeBytes=input.includeBytes;
    std::vector<core::DiagnosticRecord> diags;
    const auto expanded=XacroExpandService{}.expand(source,substitutions,diags);
    if(!expanded.expandedBytes){
        result.errorDetail=expanded.error?expanded.error->detail:"未知展开错误";
        // 错误值可能只概述失败；相对文件/行/列由已有诊断参数补齐，不显示实体绝对路径。
        for(const auto& diag:diags){
            result.errorDetail+="\n"+diag.context+"："+diag.cause;
        }
        return result;
    }
    result.ok=true;result.expandedBytes=*expanded.expandedBytes;
    result.substitutions=substitutions;return result;
}

ModelXacroExpandResult expandXacroBytes(const std::vector<std::uint8_t>& bytes,const QString& path)
{
    return expandXacroInput(loadXacroInput(bytes,path),{});
}
} // namespace sdurws::ird::modeling
