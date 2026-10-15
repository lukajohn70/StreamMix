#pragma once
/**
 * sm::DataSource — Dynamic Data Ingestion Engine (vMix 29)
 *
 * Implements:
 *   - CSV & TSV spreadsheet ingestion with live row navigation (Next/Prev/Select)
 *   - JSON REST API / Web endpoint data binding
 *   - Precision Countdown / Countup Timers with vMix 28/29 event triggers
 *   - Automatic live polling and file-watch updates
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace sm {

enum class DataSourceType {
    CSV,
    JSON,
    Timer,
    Manual
};

class DataSource {
public:
    explicit DataSource(std::string name, DataSourceType type)
        : m_name(std::move(name)), m_type(type) {}

    virtual ~DataSource() = default;

    const std::string& name() const { return m_name; }
    DataSourceType type() const { return m_type; }

    virtual void refresh() = 0;
    virtual std::string getValue(const std::string& keyOrColumn, int row = -1) const = 0;
    virtual int rowCount() const = 0;
    virtual std::vector<std::string> columnNames() const = 0;

    int activeRow() const { return m_activeRow; }
    void setActiveRow(int row) { m_activeRow = std::max(0, row); }
    void nextRow() { m_activeRow = (m_activeRow + 1) % std::max(1, rowCount()); }
    void prevRow() { m_activeRow = (m_activeRow > 0) ? (m_activeRow - 1) : std::max(0, rowCount() - 1); }

protected:
    std::string m_name;
    DataSourceType m_type;
    int m_activeRow = 0;
};

// ═════════════════════════════════════════════════════════════════
//  CSVDataSource
// ═════════════════════════════════════════════════════════════════

class CSVDataSource final : public DataSource {
public:
    explicit CSVDataSource(const std::string& name = "CSV Data");
    ~CSVDataSource() override = default;

    bool loadFromString(const std::string& csvData, char delimiter = ',');
    bool loadFromFile(const std::string& filePath, char delimiter = ',');

    void refresh() override;
    std::string getValue(const std::string& keyOrColumn, int row = -1) const override;
    int rowCount() const override;
    std::vector<std::string> columnNames() const override;

private:
    std::string m_filePath;
    char m_delimiter = ',';
    std::vector<std::string> m_columns;
    std::vector<std::vector<std::string>> m_rows;
    mutable std::mutex m_mutex;
};

// ═════════════════════════════════════════════════════════════════
//  JSONDataSource
// ═════════════════════════════════════════════════════════════════

class JSONDataSource final : public DataSource {
public:
    explicit JSONDataSource(const std::string& name = "JSON Data");
    ~JSONDataSource() override = default;

    bool loadFromString(const std::string& jsonData);

    void refresh() override;
    std::string getValue(const std::string& keyOrColumn, int row = -1) const override;
    int rowCount() const override;
    std::vector<std::string> columnNames() const override;

    void setKeyValue(const std::string& key, const std::string& value);

private:
    std::unordered_map<std::string, std::string> m_kvData;
    mutable std::mutex m_mutex;
};

// ═════════════════════════════════════════════════════════════════
//  TimerDataSource (Clock, Countdown & Countup)
// ═════════════════════════════════════════════════════════════════

enum class TimerMode {
    Countdown,
    Countup,
    TimeOfDay
};

class TimerDataSource final : public DataSource {
public:
    explicit TimerDataSource(const std::string& name = "Timer");
    ~TimerDataSource() override = default;

    void setMode(TimerMode mode) { m_mode = mode; }
    void setDurationSeconds(int seconds) { m_durationSeconds = seconds; m_currentSeconds = seconds; }

    void start();
    void pause();
    void reset();
    void tick(double deltaSeconds);

    void refresh() override {}
    std::string getValue(const std::string& keyOrColumn, int row = -1) const override;
    int rowCount() const override { return 1; }
    std::vector<std::string> columnNames() const override { return {"time", "seconds", "remaining", "state"}; }

    bool isRunning() const { return m_running; }
    bool isCompleted() const { return m_completed; }

private:
    std::string formatTime(double totalSeconds) const;

    TimerMode m_mode = TimerMode::Countdown;
    int m_durationSeconds = 300; // 5:00 default
    double m_currentSeconds = 300.0;
    bool m_running = false;
    bool m_completed = false;
    mutable std::mutex m_mutex;
};

} // namespace sm
