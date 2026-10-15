#pragma once
/**
 * sm::Plugin & sm::PluginRegistry — Extensible Plugin System
 *
 * Allows loading dynamic capture sources, effects filters, and outputs
 * (e.g. plugins/capture-ffmpeg, plugins/output-rtmp, NDI plugins, Zoom plugins).
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace sm {

enum class PluginType {
    Capture,
    Output,
    Filter,
    AudioEffect,
    ControlSurface
};

struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    PluginType  type = PluginType::Capture;
};

class Plugin {
public:
    virtual ~Plugin() = default;

    virtual const PluginInfo& info() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
};

class PluginRegistry {
public:
    PluginRegistry() = default;
    ~PluginRegistry();

    bool registerPlugin(std::shared_ptr<Plugin> plugin);
    bool unregisterPlugin(const std::string& pluginId);

    std::shared_ptr<Plugin> getPlugin(const std::string& pluginId) const;
    std::vector<PluginInfo> getAvailablePlugins() const;

    // Scans directory for .dylib (macOS), .so (Linux), or .dll (Windows)
    int loadPluginsFromDirectory(const std::string& directoryPath);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Plugin>> m_plugins;
};

} // namespace sm
