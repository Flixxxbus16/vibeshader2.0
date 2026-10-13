#include "Gltf.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>

namespace fvk {

using json = nlohmann::json;

namespace {

struct BinChunk { const uint8_t* data; size_t size; };

struct Accessor {
    const uint8_t* data = nullptr;
    size_t count = 0;
    size_t byteStride = 0;
    int componentType = 0;
    int type = 0;   // 2..4 (VEC2..VEC4), 1 scalar
    std::vector<float> minValues, maxValues;

    float elem(size_t i, size_t comp) const {
        const uint8_t* p = data + i * byteStride;
        switch (componentType) {
        case 5126: { float f; std::memcpy(&f, p + comp * 4, 4); return f; }
        case 5123: { uint16_t u; std::memcpy(&u, p + comp * 2, 2); return float(u); }
        case 5125: { uint32_t u; std::memcpy(&u, p + comp * 4, 4); return float(u); }
        case 5122: { int16_t v; std::memcpy(&v, p + comp * 2, 2); return float(v); }
        case 5121: { uint8_t u; std::memcpy(&u, p + comp, 1); return float(u); }
        default: return 0;
        }
    }
};

size_t compsOf(int type) {
    switch (type) {
    case 0: return 1;   // SCALAR
    case 1: return 2;   // VEC2
    case 2: return 3;   // VEC3
    case 3: return 4;   // VEC4
    case 4: return 4;   // MAT2... actually 4
    case 5: return 16;  // MAT4
    default: return 1;
    }
}

int jsonTypeToSize(const json& t) {
    std::string s = t.get<std::string>();
    if (s == "SCALAR") return 0;
    if (s == "VEC2") return 1;
    if (s == "VEC3") return 2;
    if (s == "VEC4") return 3;
    if (s == "MAT2") return 4;
    if (s == "MAT3") return 5;
    if (s == "MAT4") return 6;
    return 0;
}

} // namespace

static bool loadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize(size_t(f.tellg()));
    f.seekg(0);
    f.read((char*)out.data(), std::streamsize(out.size()));
    return true;
}

static std::string baseDir(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string(".") : path.substr(0, p);
}

std::optional<GltfLoadResult> loadGltf(const std::string& path) {
    std::vector<uint8_t> raw;
    if (!loadFile(path, raw)) {
        std::fprintf(stderr, "Cannot open %s\n", path.c_str());
        return std::nullopt;
    }

    std::string dir = baseDir(path);
    std::vector<uint8_t> glbBody;

    // GLB detection: magic 'glTF'
    bool isGlb = raw.size() >= 12 && std::memcmp(raw.data(), "glTF", 4) == 0;
    json doc;
    std::vector<uint8_t> bin;

    if (isGlb) {
        uint32_t version, length;
        std::memcpy(&version, raw.data() + 4, 4);
        std::memcpy(&length, raw.data() + 8, 4);
        if (version != 2) { std::fprintf(stderr, "Unsupported GLB version\n"); return std::nullopt; }
        size_t off = 12;
        while (off + 8 <= raw.size()) {
            uint32_t chunkLen, chunkType;
            std::memcpy(&chunkLen, raw.data() + off, 4);
            std::memcpy(&chunkType, raw.data() + off + 4, 4);
            const uint8_t* cdata = raw.data() + off + 8;
            if (chunkType == 0x4E4F534A) { // JSON
                doc = json::parse(cdata, cdata + chunkLen);
            } else if (chunkType == 0x004E4942) { // BIN
                bin.assign(cdata, cdata + chunkLen);
            }
            off += 8 + chunkLen;
        }
    } else {
        doc = json::parse(raw.begin(), raw.end());
        // external .bin
        auto bufs = doc.value("buffers", json::array());
        if (!bufs.empty() && bufs[0].contains("uri")) {
            std::string uri = bufs[0]["uri"].get<std::string>();
            if (!loadFile(dir + "/" + uri, bin)) {
                std::fprintf(stderr, "Cannot open buffer %s\n", uri.c_str());
                return std::nullopt;
            }
        }
    }

    if (doc.empty()) { std::fprintf(stderr, "Invalid glTF JSON\n"); return std::nullopt; }

    // --- buffer views ---
    const json& views = doc["bufferViews"];
    const json& accessors = doc["accessors"];

    auto getAccessor = [&](size_t idx) -> Accessor {
        Accessor a;
        if (idx >= accessors.size()) return a;
        const json& acc = accessors[idx];
        const json& view = views[acc["bufferView"].get<size_t>()];
        size_t byteOffset = acc.value("byteOffset", 0);
        size_t viewOffset = view.value("byteOffset", 0);
        size_t compSize = 0;
        switch (acc["componentType"].get<int>()) {
            case 5120: compSize = 1; break;
            case 5121: compSize = 1; break;
            case 5122: compSize = 2; break;
            case 5123: compSize = 2; break;
            case 5125: compSize = 4; break;
            case 5126: compSize = 4; break;
        }
        size_t n = compsOf(jsonTypeToSize(acc["type"]));
        a.byteStride = view.contains("byteStride") ? view["byteStride"].get<size_t>() : compSize * n;
        a.count = acc["count"].get<size_t>();
        a.componentType = acc["componentType"].get<int>();
        a.type = jsonTypeToSize(acc["type"]);
        a.data = bin.data() + viewOffset + byteOffset;
        if (acc.contains("min")) for (auto& v : acc["min"]) a.minValues.push_back(v.get<float>());
        return a;
    };

    GltfLoadResult res;

    // --- materials ---
    if (doc.contains("materials")) {
        for (const auto& m : doc["materials"]) {
            Material mat;
            if (m.contains("pbrMetallicRoughness")) {
                const auto& pbr = m["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor"))
                    for (int i = 0; i < 4; i++) mat.baseColorFactor[i] = pbr["baseColorFactor"][i].get<float>();
                if (pbr.contains("metallicFactor")) mat.metallicFactor = pbr["metallicFactor"].get<float>();
                if (pbr.contains("roughnessFactor")) mat.roughnessFactor = pbr["roughnessFactor"].get<float>();
                if (pbr.contains("baseColorTexture")) {
                    mat.baseColorTex = pbr["baseColorTexture"]["index"].get<uint32_t>();
                    mat.flags |= 1;
                }
                if (pbr.contains("metallicRoughnessTexture")) {
                    mat.metallicRoughnessTex = pbr["metallicRoughnessTexture"]["index"].get<uint32_t>();
                    mat.flags |= 2;
                }
            }
            if (m.contains("alphaMode") && m["alphaMode"] == "BLEND") mat.flags |= 8;
            if (m.contains("doubleSided") && m["doubleSided"].get<bool>()) mat.flags |= 4;
            if (m.contains("name")) res.scene.materialNames.push_back(m["name"].get<std::string>());
            else res.scene.materialNames.push_back("material" + std::to_string(res.scene.materials.size()));
            res.scene.materials.push_back(mat);
        }
    }
    if (res.scene.materials.empty()) {
        res.scene.materials.push_back(Material{});
        res.scene.materialNames.push_back("default");
    }

    // --- meshes -> primitive ranges ---
    const json& meshes = doc.value("meshes", json::array());
    res.scene.meshFirstLod.resize(meshes.size(), 0);
    res.scene.lodCount.resize(meshes.size(), 0);

    for (size_t mi = 0; mi < meshes.size(); mi++) {
        res.scene.meshFirstLod[mi] = uint32_t(res.scene.lodRanges.size());
        for (const auto& prim : meshes[mi]["primitives"]) {
            Scene::LodRange r{};
            int matIdx = prim.value("material", -1);
            r.materialId = matIdx;
            if (prim.contains("indices")) {
                Accessor idx = getAccessor(prim["indices"].get<size_t>());
                r.firstIndex = uint32_t(res.indices.size());
                r.indexCount = uint32_t(idx.count);
                for (size_t i = 0; i < idx.count; i++) {
                    res.indices.push_back(uint32_t(idx.elem(i, 0)));
                }
            }
            // attributes: POSITION, NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0
            const json& attrs = prim["attributes"];
            Accessor pos = getAccessor(attrs.value("POSITION", size_t(0)));
            Accessor nrm = attrs.contains("NORMAL") ? getAccessor(attrs["NORMAL"].get<size_t>()) : Accessor{};
            Accessor uv = attrs.contains("TEXCOORD_0") ? getAccessor(attrs["TEXCOORD_0"].get<size_t>()) : Accessor{};
            Accessor joint = attrs.contains("JOINTS_0") ? getAccessor(attrs["JOINTS_0"].get<size_t>()) : Accessor{};
            Accessor weight = attrs.contains("WEIGHTS_0") ? getAccessor(attrs["WEIGHTS_0"].get<size_t>()) : Accessor{};

            r.firstVertex = uint32_t(res.positions.size() / 3);
            for (size_t i = 0; i < pos.count; i++) {
                for (int c = 0; c < 3; c++) res.positions.push_back(pos.elem(i, c));
                if (nrm.data) { for (int c = 0; c < 3; c++) res.normals.push_back(nrm.elem(i, c)); }
                else { res.normals.push_back(0); res.normals.push_back(0); res.normals.push_back(1); }
                if (uv.data) { res.uvs.push_back(uv.elem(i, 0)); res.uvs.push_back(uv.elem(i, 1)); }
                else { res.uvs.push_back(0); res.uvs.push_back(0); }
                for (int c = 0; c < 4; c++) {
                    if (joint.data) res.joints.push_back(joint.elem(i, c));
                    else res.joints.push_back(0);
                    if (weight.data) res.weights.push_back(weight.elem(i, c));
                    else res.weights.push_back(c == 0 ? 1.0f : 0.0f);
                }
            }
            if (!prim.contains("indices")) {
                // non-indexed: synthesize indices
                r.firstIndex = uint32_t(res.indices.size());
                r.indexCount = uint32_t(pos.count);
                for (size_t i = 0; i < pos.count; i++) res.indices.push_back(uint32_t(i));
            }
            res.scene.lodRanges.push_back(r);
        }
        res.scene.lodCount[mi] = uint32_t(res.scene.lodRanges.size()) - res.scene.meshFirstLod[mi];
    }

    // --- nodes ---
    const json& nodes = doc.value("nodes", json::array());
    for (const auto& n : nodes) {
        uint32_t mesh = n.contains("mesh") ? n["mesh"].get<uint32_t>() + 1 : 0;
        uint32_t id = res.scene.addNode(mesh, -1);  // parent fixed below
        glm::vec3 t{0}, s{1};
        glm::quat q{1, 0, 0, 0};
        if (n.contains("translation")) t = {n["translation"][0], n["translation"][1], n["translation"][2]};
        if (n.contains("scale")) s = {n["scale"][0], n["scale"][1], n["scale"][2]};
        if (n.contains("rotation")) q = {n["rotation"][3], n["rotation"][0], n["rotation"][1], n["rotation"][2]};
        res.scene.nodeTranslation[id] = t;
        res.scene.nodeRotation[id] = q;
        res.scene.nodeScale[id] = s;
        res.scene.nodeDirty[id] = true;
        if (n.contains("matrix")) {
            glm::mat4 m(1);
            for (int c = 0; c < 4; c++)
                for (int r2 = 0; r2 < 4; r2++)
                    m[c][r2] = n["matrix"][c * 4 + r2].get<float>();
            res.scene.nodeLocalMatrix[id] = m;
            res.scene.nodeDirty[id] = false;
        }
    }
    // node hierarchy: glTF scene arrays reference nodes; children listed per node
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].contains("children")) {
            for (auto c : nodes[i]["children"]) {
                uint32_t cid = c.get<uint32_t>();
                if (cid < res.scene.nodeParent.size()) res.scene.nodeParent[cid] = int32_t(i);
            }
        }
    }
    return res;
}

} // namespace fvk
