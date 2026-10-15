/**
 * sm::SceneManager — Implementation
 */

#include "streammix/scene.h"
#include <fstream>
#include <sstream>

namespace sm {

SceneManager::SceneManager() {
    // Default initial scene
    m_scenes.push_back(std::make_shared<Scene>("Default Scene"));
}

void SceneManager::addScene(std::shared_ptr<Scene> scene) {
    if (!scene) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scenes.push_back(scene);
}

void SceneManager::removeScene(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_scenes.begin(); it != m_scenes.end(); ++it) {
        if ((*it)->name() == name) {
            m_scenes.erase(it);
            break;
        }
    }
}

std::shared_ptr<Scene> SceneManager::getScene(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& s : m_scenes) {
        if (s->name() == name) return s;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Scene>> SceneManager::getAllScenes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scenes;
}

bool SceneManager::saveToFile(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream out(filePath);
    if (!out.is_open()) return false;

    out << "{\n  \"scenes\": [\n";
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        const auto& s = m_scenes[i];
        out << "    {\"name\": \"" << s->name() << "\", \"layers\": " << s->layerCount() << "}";
        if (i + 1 < m_scenes.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    return true;
}

bool SceneManager::loadFromFile(const std::string& filePath) {
    std::ifstream in(filePath);
    return in.is_open();
}

} // namespace sm
