#ifndef RWS_STRUCTUREOPTIMIZATION_GEOMETRYADAPTERSUPPORT_HPP
#define RWS_STRUCTUREOPTIMIZATION_GEOMETRYADAPTERSUPPORT_HPP

#include "ModelParameterAdapter.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

namespace rws { namespace geometry_adapter_detail {

inline bool isGeometrySemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::GeometryRadius || semantic == SemanticKind::GeometryLength ||
           semantic == SemanticKind::GeometryWidth || semantic == SemanticKind::GeometryHeight ||
           semantic == SemanticKind::GeometryDepth || semantic == SemanticKind::GeometryWallThickness;
}

inline TargetPropertyId propertyFor(const SemanticKind semantic)
{
    switch (semantic) {
    case SemanticKind::GeometryRadius: return TargetPropertyId::GeometryRadius;
    case SemanticKind::GeometryLength: return TargetPropertyId::GeometryLength;
    case SemanticKind::GeometryWidth: return TargetPropertyId::GeometryWidth;
    case SemanticKind::GeometryHeight: return TargetPropertyId::GeometryHeight;
    case SemanticKind::GeometryDepth: return TargetPropertyId::GeometryDepth;
    case SemanticKind::GeometryWallThickness: return TargetPropertyId::GeometryWallThickness;
    default: return TargetPropertyId::Unknown;
    }
}

inline bool supportsProperty(const CanonicalGeometryKind kind, const TargetPropertyId property)
{
    if (kind == CanonicalGeometryKind::Cylinder)
        return property == TargetPropertyId::GeometryRadius || property == TargetPropertyId::GeometryLength;
    if (kind == CanonicalGeometryKind::Box)
        return property == TargetPropertyId::GeometryWidth || property == TargetPropertyId::GeometryHeight ||
               property == TargetPropertyId::GeometryDepth;
    if (kind == CanonicalGeometryKind::Tube)
        return property == TargetPropertyId::GeometryRadius || property == TargetPropertyId::GeometryLength ||
               property == TargetPropertyId::GeometryWallThickness;
    return false;
}

inline void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
                     const char* adapter, const std::string& bindingId, const std::string& objectId,
                     const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic(adapter, bindingId, objectId, field, code, message));
}

inline std::string fingerprint(const std::string& scope, const std::string& id,
                               const CanonicalGeometryKind kind, const double radius,
                               const double length, const double width, const double height,
                               const double depth, const double wall,
                               const rw::math::Transform3D<>* transform = nullptr)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << scope << '|' << id << '|' << static_cast< int >(kind) << '|'
           << std::hexfloat << radius << '|' << length << '|' << width << '|' << height << '|'
           << depth << '|' << wall;
    if (transform != nullptr) {
        stream << '|' << transform->P()(0) << '|' << transform->P()(1) << '|' << transform->P()(2);
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                stream << '|' << transform->R()(row, column);
    }
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char c : stream.str()) { hash ^= c; hash *= UINT64_C(1099511628211); }
    std::ostringstream value;
    value.imbue(std::locale::classic());
    value << scope << "-artifact-v1:" << id << ':' << std::hex << std::setw(16)
          << std::setfill('0') << hash;
    return value.str();
}

inline bool finitePositive(const double value) { return std::isfinite(value) && value > 0.0; }

inline bool validDimensions(const CanonicalGeometryKind kind, const double radius,
                            const double length, const double width, const double height,
                            const double depth, const double wall)
{
    if (kind == CanonicalGeometryKind::Cylinder)
        return finitePositive(radius) && finitePositive(length);
    if (kind == CanonicalGeometryKind::Box)
        return finitePositive(width) && finitePositive(height) && finitePositive(depth);
    if (kind == CanonicalGeometryKind::Tube)
        return finitePositive(radius) && finitePositive(length) && finitePositive(wall) && wall < radius;
    return false;
}

inline void overrideDimension(const SemanticKind semantic, const double value, double& radius,
                              double& length, double& width, double& height, double& depth,
                              double& wall)
{
    if (semantic == SemanticKind::GeometryRadius) radius = value;
    else if (semantic == SemanticKind::GeometryLength) length = value;
    else if (semantic == SemanticKind::GeometryWidth) width = value;
    else if (semantic == SemanticKind::GeometryHeight) height = value;
    else if (semantic == SemanticKind::GeometryDepth) depth = value;
    else if (semantic == SemanticKind::GeometryWallThickness) wall = value;
}

} } // namespace rws::geometry_adapter_detail

#endif
