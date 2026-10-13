# forward-vulkan

Forward-Renderer in C++20 mit **Vulkan 1.3** (Core Features: Dynamic Rendering,
Synchronization2, Buffer Device Address) und **glTF 2.0**-Loading (.gltf + .bin sowie GLB).

## Architektur (Data-Oriented)

Daten liegen durchgehend in flachen Arrays (Structure of Arrays), nicht in
Objektgraphen:

- **Vertex-Daten** (`src/GpuResources.cpp`): getrennte Device-Buffer pro Attribut
  (Positionen als vec4-Stream, Normalen als vec4-Stream, UVs als vec2-Stream,
  Indizes als u32-Stream). Der Vertex-Shader fetcht per Index aus den Storage-Buffern —
  kein klassisches Vertex-Input-Binding, alles std430.
- **Knoten** (`src/Scene.h`): TRS getrennt als `nodeTranslation[]`, `nodeRotation[]`,
  `nodeScale[]` plus Parent-Index und Dirty-Flag pro Knoten. Weltmatrizen werden
  jedes Frame aus den SoA-Arrays propagiert (Eltern vor Kindern).
- **Materialien** (`src/Common.h`): flaches `Material[]`-Array (64-Byte-Stride, gemirrored
  im Storage-Buffer), per Push-Constant-Index im Shader adressiert.
- **Draw-Ranges**: pro Mesh ein Bereich in `lodRanges[]` (`firstIndex`, `indexCount`,
  `firstVertex`, `materialId`).

## Laufzeit-Anpassung

- Knoten: `scene.nodeTranslation[i] = ...; scene.markDirty(i)` — lokale Matrizen werden
  nur für dirty Knoten neu berechnet, dann Welt-Matrizen propagiert.
- Material: `scene.material(id).baseColorFactor = ...` (u.a. metallic/roughness,
  Emissive, Alpha-Cutoff); Upload zum GPU-Spiegel per `uploadMaterials`.
- Beides wird pro Frame vor dem Rendern hochgeladen.

## Rendering (Vulkan 1.3)

- `vkCmdBeginRendering`/`vkCmdEndRendering` (Dynamic Rendering, keine Renderpasses)
- Barriers über `vkCmdPipelineBarrier2` (Synchronization2)
- Offscreen-Render-Ziel (RGBA8 + D32), Readback + PNG-Export via stb_image_write
- PBR-Beleuchtung: GGX-Specular, Metallic/Roughness aus glTF

## Build

Abhängigkeiten sind als Quellen im `vendor/`-Verzeichnis (Symlinks auf Vulkan-Headers,
volk, GLM, nlohmann/json, stb). Shader werden per `glslangValidator` zu SPIR-V
kompiliert (CMake-Target `shaders`, Pfad via `-DGLSLANG=` überschreibbar).

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/forward_vulkan assets/scene.gltf   # rendert nach output.png
```

Ohne GPU (CI/Sandbox) läuft der Renderer z. B. mit SwiftShader:

```bash
export LD_LIBRARY_PATH=<swiftshader-lib>
export VK_ICD_FILENAMES=<swiftshader-icd.json>
./build/forward_vulkan assets/scene.gltf
```

## glTF-Support

- JSON- und GLB-Container
- Punctual-Meshes: POSITION, NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0
- Indizes (u32/u16), Non-Indexed-Primitives werden indiziert
- Materialien: PBR-MetallicRoughness-Faktoren, Alpha-Mode, DoubleSided
- Node-Hierarchie (TR-Transformationen, Rotation als Quaternion, Scale, Matrix-Nodes)
