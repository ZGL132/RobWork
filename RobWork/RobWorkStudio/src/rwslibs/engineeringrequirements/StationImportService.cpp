#include "StationImportService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace rws {
namespace {

constexpr int maximumImportedStations = 10000;

void addDiagnostic(StationImportResult& result, int recordNumber, const std::string& message)
{
    result.diagnostics.push_back({recordNumber, message});
}

bool parseDouble(const std::string& text, double& value)
{
    std::istringstream stream(text);
    stream >> value;
    // 除了数值本身，还必须确认没有残留字符；例如 "0.1mm" 不能静默被当作 0.1 m。
    return stream && stream.eof() && std::isfinite(value);
}

bool parseCsvLine(const std::string& line, std::vector<std::string>& values, std::string& error)
{
    values.clear();
    std::string value;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                value.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            values.push_back(value);
            value.clear();
        } else {
            value.push_back(character);
        }
    }
    if (quoted) {
        error = "CSV quotation is not closed.";
        return false;
    }
    values.push_back(value);
    return true;
}

int columnIndex(const std::vector<std::string>& columns, const char* name)
{
    const auto it = std::find(columns.begin(), columns.end(), std::string(name));
    return it == columns.end() ? -1 : static_cast<int>(std::distance(columns.begin(), it));
}

bool readCsvValue(const std::vector<std::string>& values, int index, const char* name,
                  int recordNumber, StationImportResult& result, std::string& output)
{
    if (index < 0 || index >= static_cast<int>(values.size())) {
        addDiagnostic(result, recordNumber, std::string("Missing CSV column: ") + name + ".");
        return false;
    }
    output = values[static_cast<std::size_t>(index)];
    return true;
}

bool validateStation(PoseTask& station, int recordNumber, StationImportResult& result)
{
    if (station.id.empty()) {
        addDiagnostic(result, recordNumber, "Station ID cannot be empty.");
        return false;
    }
    if (station.refFrame.empty() || station.tcpFrame.empty()) {
        addDiagnostic(result, recordNumber, "Reference frame and TCP frame are required.");
        return false;
    }
    if (station.name.empty())
        station.name = station.id;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(station.position[axis]) || !std::isfinite(station.rpyDeg[axis])) {
            addDiagnostic(result, recordNumber, "Position and RPY values must be finite.");
            return false;
        }
    }
    return true;
}

bool appendParsedStations(RequirementSet& requirements, std::vector<PoseTask>& stations,
                          StationImportResult& result, std::string* error)
{
    if (!result.diagnostics.empty()) {
        if (error != nullptr) *error = "The import contains invalid records.";
        return false;
    }
    if (stations.empty() || stations.size() > maximumImportedStations) {
        if (error != nullptr) *error = "The import must contain between 1 and 10000 stations.";
        return false;
    }
    std::set<std::string> ids;
    for (const PoseTask& station : requirements.poseTasks)
        ids.insert(station.id);
    for (const PoseTask& station : stations) {
        if (!ids.insert(station.id).second)
            addDiagnostic(result, station.importProvenance.recordNumber, "Station ID duplicates an existing or imported station: " + station.id);
    }
    if (!result.diagnostics.empty()) {
        if (error != nullptr) *error = "The import contains duplicate station IDs.";
        return false;
    }
    // 只有所有语法、枚举、数值和唯一性检查都通过，才修改调用方的需求集。
    requirements.poseTasks.insert(requirements.poseTasks.end(), stations.begin(), stations.end());
    result.importedCount = static_cast<int>(stations.size());
    if (error != nullptr) error->clear();
    return true;
}

bool parseCommonFields(const std::string& id, const std::string& name, const std::string& referenceFrame,
                       const std::string& tcpFrame, const std::array<double, 3>& position,
                       const std::array<double, 3>& rpyDeg, const std::string& level,
                       const std::string& processType, const std::string& sourcePath, int recordNumber,
                       PoseTask& station, StationImportResult& result)
{
    station.id = id;
    station.name = name;
    station.refFrame = referenceFrame;
    station.tcpFrame = tcpFrame;
    station.position = position;
    station.rpyDeg = rpyDeg;
    station.source = PoseTaskSource::Imported;
    station.importProvenance.sourcePath = sourcePath;
    station.importProvenance.recordNumber = recordNumber;
    if (!requirementLevelFromString(level.empty() ? "Must" : level, station.level)) {
        addDiagnostic(result, recordNumber, "Requirement level is invalid: " + level);
        return false;
    }
    if (!processTypeFromString(processType.empty() ? "Generic" : processType, station.processType)) {
        addDiagnostic(result, recordNumber, "Process type is invalid: " + processType);
        return false;
    }
    return validateStation(station, recordNumber, result);
}

bool readJsonArray3(const QJsonObject& object, const char* key, std::array<double, 3>& values,
                    int recordNumber, StationImportResult& result)
{
    const QJsonArray array = object.value(key).toArray();
    if (array.size() != 3) {
        addDiagnostic(result, recordNumber, std::string("JSON field ") + key + " must contain exactly three numbers.");
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!array[axis].isDouble() || !std::isfinite(array[axis].toDouble())) {
            addDiagnostic(result, recordNumber, std::string("JSON field ") + key + " contains a non-finite number.");
            return false;
        }
        values[axis] = array[axis].toDouble();
    }
    return true;
}

} // namespace

bool StationImportService::appendCsv(RequirementSet& requirements, const std::string& csv,
                                     const std::string& sourcePath, StationImportResult& result,
                                     std::string* error)
{
    result = StationImportResult();
    std::istringstream stream(csv);
    std::string line;
    if (!std::getline(stream, line)) {
        if (error != nullptr) *error = "CSV import is empty.";
        return false;
    }
    // std::getline 只丢弃 '\n'；Windows CSV 的表头会遗留 '\r'，必须先清理再匹配列名。
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
    std::vector<std::string> headers;
    std::string parseError;
    if (!parseCsvLine(line, headers, parseError)) {
        if (error != nullptr) *error = parseError;
        return false;
    }
    const std::vector<std::string> required = {"id", "name", "refFrame", "tcpFrame", "x", "y", "z", "roll", "pitch", "yaw", "level", "processType"};
    for (const std::string& column : required) {
        if (columnIndex(headers, column.c_str()) < 0) {
            if (error != nullptr) *error = "CSV import is missing required column: " + column;
            return false;
        }
    }
    std::vector<PoseTask> stations;
    int recordNumber = 1;
    while (std::getline(stream, line)) {
        ++recordNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> values;
        if (!parseCsvLine(line, values, parseError)) {
            addDiagnostic(result, recordNumber, parseError);
            continue;
        }
        std::string id, name, referenceFrame, tcpFrame, level, processType;
        bool rowValid = readCsvValue(values, columnIndex(headers, "id"), "id", recordNumber, result, id) &&
                        readCsvValue(values, columnIndex(headers, "name"), "name", recordNumber, result, name) &&
                        readCsvValue(values, columnIndex(headers, "refFrame"), "refFrame", recordNumber, result, referenceFrame) &&
                        readCsvValue(values, columnIndex(headers, "tcpFrame"), "tcpFrame", recordNumber, result, tcpFrame) &&
                        readCsvValue(values, columnIndex(headers, "level"), "level", recordNumber, result, level) &&
                        readCsvValue(values, columnIndex(headers, "processType"), "processType", recordNumber, result, processType);
        std::array<double, 3> position = {{0.0, 0.0, 0.0}};
        std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};
        const char* numericColumns[] = {"x", "y", "z", "roll", "pitch", "yaw"};
        for (int valueIndex = 0; valueIndex < 6; ++valueIndex) {
            std::string value;
            rowValid = readCsvValue(values, columnIndex(headers, numericColumns[valueIndex]), numericColumns[valueIndex], recordNumber, result, value) && rowValid;
            double number = 0.0;
            if (!parseDouble(value, number)) {
                addDiagnostic(result, recordNumber, std::string("Invalid numeric value for ") + numericColumns[valueIndex] + ".");
                rowValid = false;
            } else if (valueIndex < 3) {
                position[valueIndex] = number;
            } else {
                rpyDeg[valueIndex - 3] = number;
            }
        }
        PoseTask station;
        if (rowValid && parseCommonFields(id, name, referenceFrame, tcpFrame, position, rpyDeg, level, processType,
                                           sourcePath, recordNumber, station, result))
            stations.push_back(station);
    }
    return appendParsedStations(requirements, stations, result, error);
}

bool StationImportService::appendJson(RequirementSet& requirements, const std::string& json,
                                      const std::string& sourcePath, StationImportResult& result,
                                      std::string* error)
{
    result = StationImportResult();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) *error = "JSON parse error: " + parseError.errorString().toStdString();
        return false;
    }
    const QJsonArray records = document.isArray() ? document.array() : document.object().value("stations").toArray();
    if (records.isEmpty() && !(document.isObject() && document.object().contains("stations"))) {
        if (error != nullptr) *error = "JSON import must be an array or an object containing stations.";
        return false;
    }
    std::vector<PoseTask> stations;
    for (int index = 0; index < records.size(); ++index) {
        const int recordNumber = index + 1;
        if (!records[index].isObject()) {
            addDiagnostic(result, recordNumber, "Station record must be a JSON object.");
            continue;
        }
        const QJsonObject object = records[index].toObject();
        std::array<double, 3> position;
        std::array<double, 3> rpyDeg;
        const bool arraysValid = readJsonArray3(object, "position", position, recordNumber, result) &&
                                 readJsonArray3(object, "rpyDeg", rpyDeg, recordNumber, result);
        PoseTask station;
        if (arraysValid && parseCommonFields(object.value("id").toString().toStdString(), object.value("name").toString().toStdString(),
                                              object.value("refFrame").toString("WORLD").toStdString(), object.value("tcpFrame").toString().toStdString(),
                                              position, rpyDeg, object.value("level").toString("Must").toStdString(),
                                              object.value("processType").toString("Generic").toStdString(), sourcePath,
                                              recordNumber, station, result))
            stations.push_back(station);
    }
    return appendParsedStations(requirements, stations, result, error);
}

} // namespace rws
