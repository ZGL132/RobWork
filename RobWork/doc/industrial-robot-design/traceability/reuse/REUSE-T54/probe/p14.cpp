// 最小消费探针：直接链接构建出的 sdurws_ird_project.lib，观测白空格文档
// 的真实拒绝路径。
#include "Codec.hpp"
#include <sdurws/ird/project/StoreTypes.hpp>
#include <cstdio>
#include <string>

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string doc =
        "{ \"formatId\":\"rwdesign\", \"schemaVersion\":10000"
        ",\"projectId\":\"prj-0123456789abcdef0123456789abcdef\""
        ",\"createdAtUtc\":\"2026-09-15T08:30:45.123Z\""
        ",\"createdWithToolVersion\":\"RobWorkStudio-1.0\" }";
    try {
        const auto v = sdurws::ird::project::codec::parseStaticIdentity(doc);
        std::printf("PARSE OK projectId=%s\n", v.projectId.toCanonical().c_str());
    } catch (const sdurws::ird::project::StoreError& e) {
        std::printf("STOREERR code=%d detail=%s\n", static_cast<int>(e.code()),
                    e.what());
    }
    return 0;
}
