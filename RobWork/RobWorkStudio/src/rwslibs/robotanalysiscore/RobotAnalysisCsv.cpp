#include "RobotAnalysisCsv.hpp"

#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace {
const char* Header =
    "id,name,type,refFrame,tcpFrame,x,y,z,rollDeg,pitchDeg,yawDeg,positionToleranceMeters,"
    "orientationToleranceDeg,allowToolRollFree,weight,enabled,note";

const char* toString (rws::TaskPointType type)
{
    switch (type) {
        case rws::TaskPointType::Pick:    return "Pick";
        case rws::TaskPointType::Place:   return "Place";
        case rws::TaskPointType::Weld:    return "Weld";
        case rws::TaskPointType::Glue:    return "Glue";
        case rws::TaskPointType::Inspect: return "Inspect";
        case rws::TaskPointType::Screw:   return "Screw";
        case rws::TaskPointType::Custom:  return "Custom";
        case rws::TaskPointType::Generic:
        default:                          return "Generic";
    }
}

rws::TaskPointType taskPointTypeFromString (const std::string& type)
{
    if (type == "Pick")
        return rws::TaskPointType::Pick;
    if (type == "Place")
        return rws::TaskPointType::Place;
    if (type == "Weld")
        return rws::TaskPointType::Weld;
    if (type == "Glue")
        return rws::TaskPointType::Glue;
    if (type == "Inspect")
        return rws::TaskPointType::Inspect;
    if (type == "Screw")
        return rws::TaskPointType::Screw;
    if (type == "Custom")
        return rws::TaskPointType::Custom;
    return rws::TaskPointType::Generic;
}

std::string trimRightCr (std::string value)
{
    if (!value.empty () && value.back () == '\r')
        value.pop_back ();
    return value;
}

std::string escape (const std::string& value)
{
    bool quote = value.find_first_of (",\"\r\n") != std::string::npos;
    if (!quote)
        return value;

    std::string escaped;
    escaped.reserve (value.size () + 2);
    escaped.push_back ('"');
    for (const char ch : value) {
        if (ch == '"')
            escaped.push_back ('"');
        escaped.push_back (ch);
    }
    escaped.push_back ('"');
    return escaped;
}

void appendField (std::ostringstream& out, const std::string& value, bool& first)
{
    if (!first)
        out << ',';
    out << escape (value);
    first = false;
}

void appendField (std::ostringstream& out, const char* value, bool& first)
{
    appendField (out, std::string (value), first);
}

void appendField (std::ostringstream& out, const double value, bool& first)
{
    std::ostringstream number;
    number << std::setprecision (17) << value;
    appendField (out, number.str (), first);
}

void appendField (std::ostringstream& out, const bool value, bool& first)
{
    appendField (out, std::string (value ? "true" : "false"), first);
}

std::vector< std::string > parseCsvLine (const std::string& line, bool& ok)
{
    std::vector< std::string > fields;
    std::string field;
    bool inQuotes = false;
    ok            = true;

    for (std::size_t i = 0; i < line.size (); ++i) {
        const char ch = line[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size () && line[i + 1] == '"') {
                    field.push_back ('"');
                    ++i;
                }
                else {
                    inQuotes = false;
                }
            }
            else {
                field.push_back (ch);
            }
        }
        else if (ch == '"') {
            if (!field.empty ())
                ok = false;
            inQuotes = true;
        }
        else if (ch == ',') {
            fields.push_back (field);
            field.clear ();
        }
        else {
            field.push_back (ch);
        }
    }

    if (inQuotes)
        ok = false;
    fields.push_back (field);
    return fields;
}

bool parseDouble (const std::string& value, double& result)
{
    char* end = nullptr;
    result    = std::strtod (value.c_str (), &end);
    return end != value.c_str () && *end == '\0';
}

bool parseBool (const std::string& value, bool& result)
{
    if (value == "true" || value == "1") {
        result = true;
        return true;
    }
    if (value == "false" || value == "0") {
        result = false;
        return true;
    }
    return false;
}

bool readDoubleField (const std::vector< std::string >& fields, const std::size_t index,
                      double& result, std::string* error)
{
    if (!parseDouble (fields[index], result)) {
        if (error)
            *error = "Invalid numeric value in CSV field " + std::to_string (index) + ".";
        return false;
    }
    return true;
}
}    // namespace

namespace rws {

std::string RobotAnalysisCsv::taskPointsToCsv (const std::vector< TaskPoint >& points)
{
    std::ostringstream out;
    out << Header << '\n';
    for (const TaskPoint& point : points) {
        bool first = true;
        appendField (out, point.id, first);
        appendField (out, point.name, first);
        appendField (out, toString (point.type), first);
        appendField (out, point.refFrame, first);
        appendField (out, point.tcpFrame, first);
        appendField (out, point.position[0], first);
        appendField (out, point.position[1], first);
        appendField (out, point.position[2], first);
        appendField (out, point.rpyDeg[0], first);
        appendField (out, point.rpyDeg[1], first);
        appendField (out, point.rpyDeg[2], first);
        appendField (out, point.tolerance.positionMeters, first);
        appendField (out, point.tolerance.orientationDeg, first);
        appendField (out, point.tolerance.allowToolRollFree, first);
        appendField (out, point.weight, first);
        appendField (out, point.enabled, first);
        appendField (out, point.note, first);
        out << '\n';
    }
    return out.str ();
}

bool RobotAnalysisCsv::taskPointsFromCsv (const std::string& csv, std::vector< TaskPoint >& points,
                                          std::string* error)
{
    std::istringstream input (csv);
    std::string line;
    if (!std::getline (input, line)) {
        if (error)
            *error = "CSV is empty.";
        return false;
    }

    if (trimRightCr (line) != Header) {
        if (error)
            *error = "CSV header does not match RobotAnalysisCore TaskPoint format.";
        return false;
    }

    std::vector< TaskPoint > parsed;
    std::size_t lineNumber = 1;
    while (std::getline (input, line)) {
        ++lineNumber;
        line = trimRightCr (line);
        if (line.empty ())
            continue;

        bool ok = true;
        const std::vector< std::string > fields = parseCsvLine (line, ok);
        if (!ok || fields.size () != 17) {
            if (error)
                *error = "Invalid CSV row at line " + std::to_string (lineNumber) + ".";
            return false;
        }

        TaskPoint point;
        point.id       = fields[0];
        point.name     = fields[1];
        point.type     = taskPointTypeFromString (fields[2]);
        point.refFrame = fields[3];
        point.tcpFrame = fields[4];
        if (!readDoubleField (fields, 5, point.position[0], error) ||
            !readDoubleField (fields, 6, point.position[1], error) ||
            !readDoubleField (fields, 7, point.position[2], error) ||
            !readDoubleField (fields, 8, point.rpyDeg[0], error) ||
            !readDoubleField (fields, 9, point.rpyDeg[1], error) ||
            !readDoubleField (fields, 10, point.rpyDeg[2], error) ||
            !readDoubleField (fields, 11, point.tolerance.positionMeters, error) ||
            !readDoubleField (fields, 12, point.tolerance.orientationDeg, error) ||
            !readDoubleField (fields, 14, point.weight, error))
            return false;
        if (!parseBool (fields[13], point.tolerance.allowToolRollFree)) {
            if (error)
                *error = "Invalid boolean value in CSV field 13.";
            return false;
        }
        if (!parseBool (fields[15], point.enabled)) {
            if (error)
                *error = "Invalid boolean value in CSV field 15.";
            return false;
        }
        point.note = fields[16];
        parsed.push_back (point);
    }

    points.swap (parsed);
    if (error)
        error->clear ();
    return true;
}

}    // namespace rws
