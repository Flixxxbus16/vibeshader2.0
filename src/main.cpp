#include "Device.h"
#include "GpuResources.h"
#include "Scene.h"
#include "Gltf.h"
#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

using namespace fvk;

int main(int argc, char** argv) {
    std::string modelPath = argc > 1 ? argv[1] : "assets/scene.gltf";
    bool validation = argc > 2 && std::string(argv[2]) == "--validate";

    auto dev = Device::create(validation);
    if (!dev) return 1;

    auto gltf = loadGltf(modelPath);
    if (!gltf) { std::fprintf(stderr, "Failed to load %s\n", modelPath.c_str()); return 1; }
    Scene& scene = gltf->scene;
    std::fprintf(stderr, "Loaded %s: %zu nodes, %zu meshes, %zu materials, %zu vertices, %zu indices\n",
                  modelPath.c_str(), scene.nodeCount(), scene.meshFirstLod.size(),
                  scene.materials.size(), gltf->positions.size() / 3, gltf->indices.size());

    Renderer renderer;
    renderer.gpu.init(dev.get(), gltf->positions, gltf->normals, gltf->uvs,
                      gltf->indices, scene.nodeCount() + 16, scene.materials.size() + 16);
    renderer.init(dev.get(), {1280, 720});

    // --- runtime editing demo: modify material and node transform ---
    if (!scene.materials.empty())
        scene.setMaterialBaseColor(0, {0.9f, 0.4f, 0.2f, 1.0f});
    if (scene.nodeCount() > 0) {
        scene.nodeTranslation[0].y += 0.0f;
        scene.nodeRotation[0] = glm::angleAxis(glm::radians(35.0f), glm::vec3(0, 1, 0));
        scene.markDirty(0);
    }

    std::vector<glm::mat4> world;
    scene.updateMatrices(world);

    // camera: fit to model bounds
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (size_t i = 0; i + 2 < gltf->positions.size(); i += 3) {
        bmin = glm::min(bmin, {gltf->positions[i], gltf->positions[i+1], gltf->positions[i+2]});
        bmax = glm::max(bmax, {gltf->positions[i], gltf->positions[i+1], gltf->positions[i+2]});
    }
    glm::vec3 center = 0.5f * (bmin + bmax);
    float radius = glm::length(bmax - bmin) * 0.5f;
    if (radius <= 0) radius = 1;
    glm::vec3 eye = center + glm::vec3(0.6f, 0.5f, 1.4f) * radius * 1.6f;
    glm::mat4 view = glm::lookAt(eye, center, {0, 1, 0});
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 1280.0f / 720.0f, 0.05f, 1000.0f);
    glm::mat4 viewProj = proj * view;

    VkCommandBuffer cmd = dev->beginOneShot();
    renderer.render(scene, world, viewProj, eye, cmd);
    dev->endOneShot(cmd);

    renderer.saveColorPNG("output.png");
    std::fprintf(stderr, "Wrote output.png\n");

    renderer.destroy();
    renderer.gpu.destroy();
    return 0;
}
