#include "RobotAnalysisValidation.hpp"

#include <cmath>
#include <string>

namespace {
bool blank (const std::string& value)
{
    return value.find_first_not_of (" \t\r\n") == std::string::npos;
}

bool finite3 (const std::array< double, 3 >& values)
{
    return std::isfinite (values[0]) && std::isfinite (values[1]) && std::isfinite (values[2]);
}

bool finite6 (const std::array< double, 6 >& values)
{
    for (const double value : values) {
        if (!std::isfinite (value))
            return false;
    }
    return true;
}

rws::AnalysisWarning error (const std::string& code, const std::string& message,
                            const std::string& source)
{
    rws::AnalysisWarning warning;
    warning.code     = code;
    warning.message  = message;
    warning.source   = source;
    warning.severity = rws::AnalysisStatus::Fail;
    return warning;
}

void append (std::vector< rws::AnalysisWarning >& destination,
             const std::vector< rws::AnalysisWarning >& source)
{
    destination.insert (destination.end (), source.begin (), source.end ());
}
}    // namespace

namespace rws {

std::vector< AnalysisWarning > RobotAnalysisValidation::validateTaskPoint (const TaskPoint& point)
{
    std::vector< AnalysisWarning > warnings;
    if (blank (point.id))
        warnings.push_back (error ("TaskPoint.Id.Empty", "Task point id must not be empty.",
                                   "TaskPoint"));
    if (blank (point.name))
        warnings.push_back (error ("TaskPoint.Name.Empty", "Task point name must not be empty.",
                                   "TaskPoint"));
    if (blank (point.refFrame))
        warnings.push_back (error ("TaskPoint.RefFrame.Empty",
                                   "Task point reference frame must not be empty.", "TaskPoint"));
    if (blank (point.tcpFrame))
        warnings.push_back (error ("TaskPoint.TcpFrame.Empty",
                                   "Task point TCP frame must not be empty.", "TaskPoint"));
    if (!finite3 (point.position))
        warnings.push_back (error ("TaskPoint.Position.NonFinite",
                                   "Task point position must contain finite values.",
                                   "TaskPoint"));
    if (!finite3 (point.rpyDeg))
        warnings.push_back (error ("TaskPoint.Rpy.NonFinite",
                                   "Task point RPY must contain finite values.", "TaskPoint"));
    if (!std::isfinite (point.tolerance.positionMeters) || point.tolerance.positionMeters < 0.0)
        warnings.push_back (error ("TaskPoint.Tolerance.PositionNegative",
                                   "Task point position tolerance must be finite and non-negative.",
                                   "TaskPoint"));
    if (!std::isfinite (point.tolerance.orientationDeg) || point.tolerance.orientationDeg < 0.0)
        warnings.push_back (error ("TaskPoint.Tolerance.OrientationNegative",
                                   "Task point orientation tolerance must be finite and non-negative.",
                                   "TaskPoint"));
    if (!std::isfinite (point.weight) || point.weight < 0.0)
        warnings.push_back (
            error ("TaskPoint.Weight.Negative", "Task point weight must be finite and non-negative.",
                   "TaskPoint"));
    return warnings;
}

std::vector< AnalysisWarning > RobotAnalysisValidation::validatePayload (const PayloadSpec& payload)
{
    std::vector< AnalysisWarning > warnings;
    if (blank (payload.name))
        warnings.push_back (
            error ("Payload.Name.Empty", "Payload name must not be empty.", "Payload"));
    if (!std::isfinite (payload.mass) || payload.mass < 0.0)
        warnings.push_back (
            error ("Payload.Mass.Negative", "Payload mass must be finite and non-negative.",
                   "Payload"));
    if (!finite3 (payload.cog))
        warnings.push_back (
            error ("Payload.Cog.NonFinite", "Payload center of gravity must contain finite values.",
                   "Payload"));
    if (!finite6 (payload.inertia))
        warnings.push_back (
            error ("Payload.Inertia.NonFinite", "Payload inertia must contain finite values.",
                   "Payload"));
    return warnings;
}

std::vector< AnalysisWarning >
RobotAnalysisValidation::validateAnalysisResult (const AnalysisResult& result)
{
    std::vector< AnalysisWarning > warnings;
    if (!std::isfinite (result.score) || result.score < 0.0 || result.score > 100.0)
        warnings.push_back (error ("AnalysisResult.Score.OutOfRange",
                                   "Analysis result score must be finite and in [0, 100].",
                                   "AnalysisResult"));
    return warnings;
}

std::vector< AnalysisWarning >
RobotAnalysisValidation::validateRobotDesignContext (const RobotDesignContext& context)
{
    std::vector< AnalysisWarning > warnings;
    if (blank (context.baseFrame))
        warnings.push_back (
            error ("RobotDesignContext.BaseFrame.Empty", "Base frame must not be empty.",
                   "RobotDesignContext"));
    if (blank (context.tcpFrame))
        warnings.push_back (
            error ("RobotDesignContext.TcpFrame.Empty", "TCP frame must not be empty.",
                   "RobotDesignContext"));
    if (blank (context.refFrame))
        warnings.push_back (
            error ("RobotDesignContext.RefFrame.Empty", "Reference frame must not be empty.",
                   "RobotDesignContext"));

    if (context.modelSpec.robotName.empty () || context.modelSpec.transformJoints.empty ()) {
        warnings.push_back (error ("RobotDesignContext.ModelSpec.Incomplete",
                                   "Robot design context must contain a complete RobotModelSpec for optimization.",
                                   "RobotDesignContext"));
    }

    append (warnings, validatePayload (context.payload));
    for (const TaskPoint& point : context.taskPoints) {
        append (warnings, validateTaskPoint (point));
    }
    return warnings;
}

bool RobotAnalysisValidation::hasErrors (const std::vector< AnalysisWarning >& warnings)
{
    for (const AnalysisWarning& warning : warnings) {
        if (warning.severity == AnalysisStatus::Fail)
            return true;
    }
    return false;
}

}    // namespace rws
