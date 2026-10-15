#pragma once
/**
 * sm::Scene & sm::SceneManager — Production Presets & Multi-View Layouts
 *
 * Capabilities:
 *   - Preset saving & loading (JSON serialization)
 *   - Virtual Inputs & Multi-Layer compositions (e.g. 2-box, 3-box, 4-box news/sports layouts)
 *   - Input configuration, positioning, and bus mapping serialization
 */

#include "streammix/frame.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace sm {

struct SceneLayer {
    uint64_t sourceId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float cropLeft = 0.0f;
    float cropRight = 0.0f;
    float cropTop = 0.0f;
    float cropBottom = 0.0f;
    float opacity = 1.0f;
    int zIndex = 0;
};

class Scene {
public:
    explicit Scene(std::string name = "Scene 1");
    ~Scene() = default;

    const std::string& name() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    void addLayer(const SceneLayer& layer);
    void removeLayer(size_t index);
    void clearLayers();

    const std::vector<SceneLayer>& layers() const { return m_layers; }
    size_t layerCount() const { return m_layers.size(); }

private:
    std::string m_name;
    std::vector<SceneLayer> m_layers;
};

class SceneManager {
public:
    SceneManager();
    ~SceneManager() = default;

    void addScene(std::shared_ptr<Scene> scene);
    void removeScene(const std::string& name);
    std::shared_ptr<Scene> getScene(const std::string& name) const;
    std::vector<std::shared_ptr<Scene>> getAllScenes() const;

    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<Scene>> m_scenes;
};

} // namespace sm
