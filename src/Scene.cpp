#include "Scene.h"
#include <algorithm>

namespace fvk {

void Scene::reserve(size_t nodes, size_t meshes, size_t materials) {
    nodeTranslation.reserve(nodes);
    nodeRotation.reserve(nodes);
    nodeScale.reserve(nodes);
    nodeLocalMatrix.reserve(nodes);
    nodeParent.reserve(nodes);
    nodeMesh.reserve(nodes);
    nodeDirty.reserve(nodes);
    lodRanges.reserve(meshes);
    nodeMesh.reserve(std::max(nodes, meshes));
    this->materials.reserve(materials);
}

uint32_t Scene::addNode(uint32_t mesh, int32_t parent) {
    uint32_t id = uint32_t(nodeTranslation.size());
    nodeTranslation.push_back({0, 0, 0});
    nodeRotation.push_back({1, 0, 0, 0});
    nodeScale.push_back({1, 1, 1});
    nodeLocalMatrix.emplace_back(1.0f);
    nodeParent.push_back(parent);
    nodeMesh.push_back(mesh);
    nodeDirty.push_back(true);
    return id;
}

void Scene::updateMatrices(std::vector<glm::mat4>& out) {
    for (size_t i = 0; i < nodeTranslation.size(); i++) {
        if (!nodeDirty[i]) continue;
        glm::mat4 t = glm::translate(glm::mat4(1), nodeTranslation[i]);
        glm::mat4 r = glm::mat4_cast(nodeRotation[i]);
        glm::mat4 s = glm::scale(glm::mat4(1), nodeScale[i]);
        nodeLocalMatrix[i] = t * r * s;
        nodeDirty[i] = false;
    }

    // resolve world matrices topologically (parents always precede children in glTF)
    out.clear();
    out.resize(nodeTranslation.size());
    for (size_t i = 0; i < nodeTranslation.size(); i++) {
        int32_t p = nodeParent[i];
        out[i] = (p >= 0) ? out[size_t(p)] * nodeLocalMatrix[i] : nodeLocalMatrix[i];
    }
}

}
