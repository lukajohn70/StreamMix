/**
 * sm::DataSource — Implementation
 */

#include "streammix/data_source.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  CSVDataSource
// ═════════════════════════════════════════════════════════════════

CSVDataSource::CSVDataSource(const std::string& name)
    : DataSource(name, DataSourceType::CSV)
{
}

bool CSVDataSource::loadFromFile(const std::string& filePath, char delimiter) {
    m_filePath = filePath;
    m_delimiter = delimiter;

    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    return loadFromString(ss.str(), delimiter);
}

bool CSVDataSource::loadFromString(const std::string& csvData, char delimiter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delimiter = delimiter;
    m_columns.clear();
    m_rows.clear();

    std::istringstream stream(csvData);
    std::string line;

    // First line = Column Headers
    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream lineStream(line);
        std::string cell;
        while (std::getline(lineStream, cell, delimiter)) {
            // Trim whitespace
            auto start = cell.find_first_not_of(" \t\"");
            auto end = cell.find_last_not_of(" \t\r\n\"");
            m_columns.push_back((start != std::string::npos) ? cell.substr(start, end - start + 1) : "");
        }
    }

    // Subsequent lines = Rows
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        std::istringstream lineStream(line);
        std::string cell;
        std::vector<std::string> rowCells;

        while (std::getline(lineStream, cell, delimiter)) {
            auto start = cell.find_first_not_of(" \t\"");
            auto end = cell.find_last_not_of(" \t\r\n\"");
            rowCells.push_back((start != std::string::npos) ? cell.substr(start, end - start + 1) : "");
        }

        while (rowCells.size() < m_columns.size()) rowCells.emplace_back("");
        m_rows.push_back(std::move(rowCells));
    }

    m_activeRow = 0;
    return !m_columns.empty();
}

void CSVDataSource::refresh() {
    if (!m_filePath.empty()) {
        loadFromFile(m_filePath, m_delimiter);
    }
}

std::string CSVDataSource::getValue(const std::string& keyOrColumn, int row) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_rows.empty() || m_columns.empty()) return "";

    int targetRow = (row >= 0) ? row : m_activeRow;
    if (targetRow >= static_cast<int>(m_rows.size())) {
        targetRow = static_cast<int>(m_rows.size()) - 1;
    }

    for (size_t col = 0; col < m_columns.size(); ++col) {
        if (m_columns[col] == keyOrColumn) {
            return (col < m_rows[targetRow].size()) ? m_rows[targetRow][col] : "";
        }
    }
    return "";
}

int CSVDataSource::rowCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_rows.size());
}

std::vector<std::string> CSVDataSource::columnNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_columns;
}

// ═════════════════════════════════════════════════════════════════
//  JSONDataSource
// ═════════════════════════════════════════════════════════════════

JSONDataSource::JSONDataSource(const std::string& name)
    : DataSource(name, DataSourceType::JSON)
{
}

bool JSONDataSource::loadFromString(const std::string& jsonData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_kvData.clear();

    // Lightweight key-value extractor: extracts `"key": "value"` or `"key": number`
    size_t pos = 0;
    while ((pos = jsonData.find('"', pos)) != std::string::npos) {
        size_t keyEnd = jsonData.find('"', pos + 1);
        if (keyEnd == std::string::npos) break;

        std::string key = jsonData.substr(pos + 1, keyEnd - pos - 1);
        pos = keyEnd + 1;

        // Find colon
        size_t colon = jsonData.find(':', pos);
        if (colon == std::string::npos) break;
        pos = colon + 1;

        // Skip whitespace
        while (pos < jsonData.size() && (jsonData[pos] == ' ' || jsonData[pos] == '\t' || jsonData[pos] == '\r' || jsonData[pos] == '\n')) {
            pos++;
        }

        std::string val;
        if (pos < jsonData.size() && jsonData[pos] == '"') {
            size_t valEnd = jsonData.find('"', pos + 1);
            if (valEnd != std::string::npos) {
                val = jsonData.substr(pos + 1, valEnd - pos - 1);
                pos = valEnd + 1;
            }
        } else {
            size_t valEnd = jsonData.find_first_of(",}\r\n", pos);
            if (valEnd != std::string::npos) {
                val = jsonData.substr(pos, valEnd - pos);
                pos = valEnd;
            }
        }

        m_kvData[key] = val;
    }

    return !m_kvData.empty();
}

void JSONDataSource::refresh() {
}

std::string JSONDataSource::getValue(const std::string& keyOrColumn, int /*row*/) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_kvData.find(keyOrColumn);
    return (it != m_kvData.end()) ? it->second : "";
}

int JSONDataSource::rowCount() const {
    return 1;
}

std::vector<std::string> JSONDataSource::columnNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> keys;
    for (const auto& pair : m_kvData) {
        keys.push_back(pair.first);
    }
    return keys;
}

void JSONDataSource::setKeyValue(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_kvData[key] = value;
}

// ═════════════════════════════════════════════════════════════════
//  TimerDataSource
// ═════════════════════════════════════════════════════════════════

TimerDataSource::TimerDataSource(const std::string& name)
    : DataSource(name, DataSourceType::Timer)
{
}

void TimerDataSource::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = true;
    m_completed = false;
}

void TimerDataSource::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
}

void TimerDataSource::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
    m_completed = false;
    m_currentSeconds = (m_mode == TimerMode::Countdown) ? m_durationSeconds : 0.0;
}

void TimerDataSource::tick(double deltaSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running) return;

    if (m_mode == TimerMode::Countdown) {
        m_currentSeconds -= deltaSeconds;
        if (m_currentSeconds <= 0.0) {
            m_currentSeconds = 0.0;
            m_running = false;
            m_completed = true;
        }
    } else if (m_mode == TimerMode::Countup) {
        m_currentSeconds += deltaSeconds;
    }
}

std::string TimerDataSource::formatTime(double totalSeconds) const {
    int total = static_cast<int>(std::round(totalSeconds));
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;

    std::ostringstream ss;
    if (h > 0) {
        ss << std::setfill('0') << std::setw(2) << h << ":";
    }
    ss << std::setfill('0') << std::setw(2) << m << ":"
       << std::setfill('0') << std::setw(2) << s;
    return ss.str();
}

std::string TimerDataSource::getValue(const std::string& keyOrColumn, int /*row*/) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (keyOrColumn == "time" || keyOrColumn == "remaining") {
        return formatTime(m_currentSeconds);
    }
    if (keyOrColumn == "seconds") {
        return std::to_string(static_cast<int>(m_currentSeconds));
    }
    if (keyOrColumn == "state") {
        if (m_completed) return "Completed";
        return m_running ? "Running" : "Paused";
    }
    return formatTime(m_currentSeconds);
}

} // namespace sm
