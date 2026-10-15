/**
 * sm::PluginRegistry — Implementation
 */

#include "streammix/plugin.h"

namespace sm {

PluginRegistry::~PluginRegistry() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_plugins) {
        if (pair.second) {
            pair.second->shutdown();
        }
    }
    m_plugins.clear();
}

bool PluginRegistry::registerPlugin(std::shared_ptr<Plugin> plugin) {
    if (!plugin) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& id = plugin->info().id;
    if (m_plugins.find(id) != m_plugins.end()) return false;

    if (plugin->initialize()) {
        m_plugins[id] = plugin;
        return true;
    }
    return false;
}

bool PluginRegistry::unregisterPlugin(const std::string& pluginId) {
    std::shared_ptr<Plugin> p;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(pluginId);
        if (it != m_plugins.end()) {
            p = it->second;
            m_plugins.erase(it);
        }
    }
    if (p) {
        p->shutdown();
        return true;
    }
    return false;
}

std::shared_ptr<Plugin> PluginRegistry::getPlugin(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(pluginId);
    return (it != m_plugins.end()) ? it->second : nullptr;
}

std::vector<PluginInfo> PluginRegistry::getAvailablePlugins() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PluginInfo> list;
    list.reserve(m_plugins.size());
    for (const auto& pair : m_plugins) {
        if (pair.second) {
            list.push_back(pair.second->info());
        }
    }
    return list;
}

int PluginRegistry::loadPluginsFromDirectory(const std::string& directoryPath) {
    // Scaffold directory loader: ready for dlopen (macOS/Linux) and LoadLibrary (Windows)
    return 0;
}

} // namespace sm
