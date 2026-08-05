# MyRenderer

Vulkan 1.3をバックエンドとする、C++製の実験用レンダリング基盤です。

RHIによるAPI抽象化、RenderGraphによるリソース依存・同期管理、Bindless Descriptor、Compute Shaderを中心に、低レイヤのレンダリング機能とリアルタイム描画手法を検証することを目的としています。

> このREADMEは現行ソースを基準にしています。内部構造の詳細は[Architecture.md](Architecture.md)、ChatGPTなどへ共有するための前提資料は[Context.md](Context.md)を参照してください。

---

## 現在のSandbox

`apps/sandbox/main.cpp`では、次の処理を行います。

1. GLFW Window、Vulkan Device、Swapchainを生成
2. Assimpで`assets/teapot.obj`を読み込み
3. Position、Attribute、IndexをGPU Bufferへ転送
4. CPU上でSpatial Median BVHを構築
5. BVH Nodeと並べ替え済みIndexをGPUへ転送
6. RenderGraphへSwapchain、モデル、BVHをImport
7. `shaders/raytrace.comp`を使うCompute Passを登録
8. Compute ShaderでBVHを走査し、Swapchainへ直接描画
9. 通常の法線表示とBVH走査回数ヒートマップを4秒周期で切り替え
10. Timestamp QueryでGPU実行時間を計測

```text
OBJ Model
  ↓ Assimp
CpuModelData
  ├─ Position Buffer ─────────────┐
  ├─ Attribute Buffer ────────────┤
  └─ Index Data                   │
         ↓ CPU BVH Build          │
     BVH Nodes + Reordered Index  │
         ↓ Upload                 │
RenderGraph Compute Pass          │
         ↓                        │
BVH Ray Tracing Shader ◀──────────┘
         ↓
Swapchain Storage Image
```

---

## 主な実装機能

### Core / Application

- GLFW WindowとVulkan Surface
- Frames in Flight
- Swapchain Acquire / Present
- Window ResizeとSwapchain再作成
- Delta Time計測
- 最小化中の描画スキップ
- 登録済みRenderGraphへのResize通知

### RHI / Vulkan Backend

- RHIインターフェースとVulkan実装の分離
- Vulkan 1.3
- Vulkan Memory Allocator
- Timeline Semaphore
- Synchronization2
- Dynamic Rendering
- Descriptor Indexing
- Update After Bind / Partially Bound
- Non-uniform Bindless Resource Access
- Deferred Resource Destruction
- Vulkan Pipeline Cacheの保存と読み込み

### RenderGraph

- Compute Pass
- Graphics Pass
- Copy Pass
- 外部ResourceとSwapchainのImport
- Graph内一時Image / Buffer生成
- Resource Read / Write依存の収集
- トポロジカルソート
- Resource Lifetime解析
- 同一フレーム内の一時Resource再利用
- Synchronization2 Barrier生成
- Queue間Timeline Semaphore依存
- Swapchain Acquire / Present Semaphore接続
- Dynamic Rendering Scopeの構築
- GPU Profiler統合
- Swapchain相対サイズResourceのResize

### Shader / Resource Binding

- shadercによるGLSLのランタイムSPIR-Vコンパイル
- SPIRV-CrossによるReflection
- Global Bindless Descriptor Set
- Push ConstantsによるResource Index受け渡し
- Uniform Ring BufferとBindless UBO参照
- Shader内メンバ名とC++側`StringHash`による名前Binding

### Asset / Rendering

- Assimpによるモデル読み込み
- Position / Attributeストリーム分離
- stbによる8bit、16bit、HDR画像読み込み
- PNG画像出力
- CPU Spatial Median BVH
- BVH Node / Reordered IndexのGPU転送
- Compute ShaderによるBVHレイトレーシング
- Programmable Vertex Pulling形式のGraphics Shader例

---

## 必要環境

現在の実装とCMake設定は、主に次の環境を前提としています。

- Windows 11
- MSVCを含むVisual Studio 2022 Build Tools
- CMake 3.18以上
- C++20対応コンパイラ
- Vulkan SDK
- vcpkg
- Vulkan 1.3対応GPUとドライバ

現在のPhysical Device選択処理は、**Graphics Queueとは別のDedicated Compute Queueを持つGPUを要求します**。一般的なVulkan 1.3対応GPUであっても、この条件を満たさない場合は選択されません。

Compute ShaderからSwapchainへ直接書き込むSandboxでは、SurfaceがStorage Image用途をサポートする必要があります。

---

## 依存ライブラリ

| ライブラリ | 用途 |
|---|---|
| Vulkan | Graphics / Compute API |
| GLFW | Window、入力、Surface生成 |
| Vulkan Memory Allocator | Buffer / Imageメモリ管理 |
| GLM | ベクトル・行列・CPU/GPU共有データ型 |
| shaderc | GLSLからSPIR-Vへのコンパイル |
| SPIRV-Cross | Shader Reflection |
| Assimp | 3Dモデル読み込み |
| stb | 画像読み込み・PNG出力 |
| lodepng | ビルド依存として登録済み |

---

## ビルド

### 1. リポジトリを取得

```powershell
git clone <repository-url>
cd MyRenderer
```

### 2. 依存ライブラリを用意

前節のvcpkgコマンドを実行します。

### 3. CMakeをConfigure

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Visual Studio Generatorを明示する例:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### 4. ビルド

```powershell
cmake --build build --config Debug
```

---

## 実行準備

Sandboxは実行時に、リポジトリルートからの相対パスを使用します。

```text
assets/teapot.obj
shaders/raytrace.comp
workspace/shader_cache.bin
```

`assets/*`と`workspace/*`は`.gitignore`の対象なので、初回実行前に用意してください。

```powershell
New-Item -ItemType Directory -Force assets
New-Item -ItemType Directory -Force workspace
```

`assets/teapot.obj`へ三角形メッシュのOBJファイルを配置します。

実行時のCurrent Working Directoryは**リポジトリルート**にしてください。`build`ディレクトリを作業ディレクトリにすると、ShaderとAssetの相対パス解決に失敗します。

Visual Studio Generatorの一般的な出力先から起動する例:

```powershell
.\build\apps\sandbox\Debug\SandboxApp.exe
```

出力先が異なる場合も、実行ファイルをリポジトリルートから起動してください。

---

## 基本的な利用例

### Applicationを派生する

```cpp
class SandboxApp : public core::Application {
public:
    SandboxApp()
        : Application({"MyRenderer Realtime Raytracing", 1280, 720}) {}

    void run();
};
```

### モデルとBVHを準備する

```cpp
CpuModelData cpuData = ModelImporter::loadFromFile("assets/teapot.obj");

auto model = ModelBuilder::buildAndEnqueue(
    getDevice(),
    getDevice().getUploadManager(),
    cpuData
);

BVHConfig config;
config.maxDepth = 24;
config.minPrimitivesPerLeaf = 4;

auto builder = std::make_unique<CpuBVHBuilder>(
    BVHSplitMethod::SpatialMedian,
    config
);

auto bvh = BVHManager::buildAndEnqueue(
    getDevice(),
    getDevice().getUploadManager(),
    cpuData,
    std::move(builder)
);

getDevice().getUploadManager()->submitUploadsAsync();
getDevice().getUploadManager()->waitUploads();
```

### RenderGraphを構築する

```cpp
auto graph = getDevice().createRenderGraph();
registerRenderGraph(graph.get());

auto hSwapchain = graph->importSwapchain(
    getSwapchain(),
    "swapchainImage"_hash
);

model->importToGraph(*graph);
bvh->importToGraph(*graph);

auto& pass = graph->addComputePass(
    "RaytracePass",
    "shaders/raytrace.comp"
);

auto& dispatch = pass.dispatchThreads(
    [this](uint32_t& width, uint32_t& height, uint32_t& depth) {
        width = getWidth();
        height = getHeight();
        depth = 1;
    }
);

dispatch.write(hSwapchain)
        .read(model->hPosition)
        .read(model->hAttribute)
        .read(bvh->hReorderedIndices)
        .read(bvh->hNodes);

graph->compile();
```

`dispatchThreads()`へ渡す値はWorkgroup数ではなくThread数です。Shader Reflectionで取得したLocal Sizeを使い、内部でWorkgroup数へ変換されます。

### 毎フレーム更新する

```cpp
while (isRunning()) {
    if (!beginFrame()) {
        continue;
    }

    processEvents();

    dispatch.setUniform("sceneGlobalsPtr"_hash, sceneGlobals);
    dispatch.setPushConstant("renderMode"_hash, renderMode);

    graph->execute();
    endFrame();
}
```

---

## Shader Resource Binding

RenderGraphへ登録したResource名と、ShaderのPush Constantメンバ名を一致させます。

```cpp
auto handle = graph->importResource(buffer, "ModelPos"_hash);
dispatch.read(handle);
```

```glsl
layout(push_constant) uniform PushConstants {
    uint ModelPos;
} pc;
```

コンパイル時にSPIRV-CrossでPush ConstantメンバのOffsetを取得し、実行時に対象ResourceのBindless Indexを書き込みます。

Dynamic Uniformは値そのものをPush Constantsへ格納せず、Uniform Ring Buffer上の位置を`uvec2`として渡します。

```cpp
dispatch.setUniform("sceneGlobalsPtr"_hash, sceneGlobals);
```

```glsl
layout(push_constant) uniform PushConstants {
    uvec2 sceneGlobalsPtr;
} pc;
```

Global Descriptor Setは`set = 0`を使用し、Bindingは次の固定構成です。

| Binding | Descriptor Type |
|---:|---|
| 0 | Storage Buffer |
| 1 | Storage Image |
| 2 | Uniform Buffer |
| 3 | Sampled Image |
| 4 | Sampler |

---

## ディレクトリ構成

```text
MyRenderer/
├─ apps/
│  └─ sandbox/
│     └─ main.cpp
├─ core/
│  ├─ include/core/
│  │  ├─ Application.hpp
│  │  ├─ RenderGraph.hpp
│  │  └─ Window.hpp
│  └─ src/
│     ├─ Application.cpp
│     ├─ RenderGraph.cpp
│     ├─ Window.cpp
│     ├─ rhi/
│     ├─ vulkan/
│     └─ utils/
├─ shaders/
├─ docs/
├─ CMakeLists.txt
├─ Context.md
├─ Architecture.md
└─ README.md
```

### 主要ドキュメント

- Context.md: AIや新しい会話へ共有するための短い技術コンテキスト
- Architecture.md: 所有関係、フレーム処理、RenderGraph、同期、Upload、BVHなどの内部構造

---

## 現在の制約と要検証点

- バックエンドはVulkanのみで、DX12は未実装
- `QueueType::Transfer`は現在Dedicated Transfer QueueではなくCompute Queueへ割り当てられる
- Physical Device選択はDedicated Compute Queueを持つGPUを要求する
- CPU BVHはSpatial Medianのみ実装済みで、SAHとBinned SAHは列挙値のみ
- モデルのMaterialとTextureのGPU構築は未実装
- ShaderのHot Reloadとファイル更新によるShader Cache無効化は未実装
- Push Constant Rangeは128byte固定
- Resource名は32bit FNV-1a Hashで照合し、Hash Collision検出はない
- `MAX_FRAMES_IN_FLIGHT`は3だが、Constant Buffer Managerは2フレーム分で生成されている
- RenderGraphを同じインスタンスで再`compile()`する処理は追加検証が必要
- Queue Family Ownership Transferは実機Validationでの継続確認が必要
- `AppMode::OnDemand`に関するAPIは存在するが、現在の実行経路では使用されていない

より詳細な分析はArchitecture.mdの「現行スナップショットの制約と要検証点」参照

---

## Roadmap

### 安定化

- Frames in FlightとUniform Ring構成の統一
- Physical Device / Queue / Present Support選択の厳密化
- Dedicated Transfer Queueの実装
- RenderGraph再Compile時の状態初期化
- Queue Family Ownership TransferのValidation
- Resource State追跡の整理

### Rendering / Asset

- SAH / Binned SAH BVH
- MaterialとTexture読み込み
- Sampled Image / Samplerを使う描画例
- Graphics Passの実用例拡充
- Ray Marchingと高度なRay Tracing実験
- スペクトルレンダリング
- パーティクル、プロシージャル生成

### Tooling / Architecture

- Shader Hot Reload
- Shader Cacheの更新検出
- ECSによるScene管理
- Buffer Device Address
- Descriptor Buffer
- マルチスレッドCommand記録
- DX12バックエンド
- Work Graph系機能の検証

---

