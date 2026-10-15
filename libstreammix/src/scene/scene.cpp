/**
 * sm::Scene — Implementation
 */

#include "streammix/scene.h"

namespace sm {

Scene::Scene(std::string name)
    : m_name(std::move(name))
{
}

void Scene::addLayer(const SceneLayer& layer) {
    m_layers.push_back(layer);
}

void Scene::removeLayer(size_t index) {
    if (index < m_layers.size()) {
        m_layers.erase(m_layers.begin() + static_cast<long>(index));
    }
}

void Scene::clearLayers() {
    m_layers.clear();
}

} // namespace sm
