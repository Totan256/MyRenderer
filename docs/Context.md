# MyRenderer Context

> 生成元: `MyRenderer-source.zip` のソーススナップショット  
> 作成日: 2026-08-05  
> 目的: ChatGPTなどへMyRendererの前提、利用方法、実装状況を短時間で共有する

## 1. この文書の使い方

この文書は、MyRendererについて設計相談、バグ調査、機能追加、リファクタリングを行う際の共通コンテキストである。

詳細な所有関係、フレーム処理、RenderGraphのコンパイルと実行、同期、リソース管理については `Architecture.md` を参照する。

回答時は、次を区別すること。

- 現在のソースで実装済みの処理
- APIや型だけ存在し、実処理が未完成または未使用のもの
- READMEやコメントに書かれた将来構想
- Vulkan仕様上の要件
- 現行実装固有の制約や不具合候補


---

## 2. プロジェクト概要

MyRendererは、VulkanをバックエンドとするC++製の実験用レンダリング基盤である。

現在のSandboxアプリは、OBJモデルを読み込み、CPUでBVHを構築し、Compute ShaderでBVHを走査するリアルタイムレイトレーシングを実行する。結果はSwapchain ImageへStorage Imageとして直接書き込む。

中心となる設計要素は以下である。

- RHIインターフェースとVulkan実装の分離
- RenderGraphによるパス、リソース依存、バリア、キュー同期の管理
- Descriptor Indexingを使ったBindlessリソース参照
- Push Constantsを介したBindless Indexの受け渡し
- Uniformデータ用リングバッファ
- Timeline SemaphoreとSynchronization2
- Graphics、Compute、Transferという論理キュー分類
- Dynamic RenderingによるGraphics Pass
- Frames in Flight
- 一時リソースの寿命解析と同一フレーム内エイリアシング
- 非同期アップロード
- GLSLのランタイムコンパイルとSPIR-V Reflection
- CPU BVH構築とGPUバッファ転送
- Timestamp QueryによるGPUプロファイリング

将来構想としてDX12バックエンド、ECS、BDA、Work Graph系の実験などが記載されているが、このスナップショットで利用可能なバックエンドはVulkanのみである。

---

## 3. 現在の実行例

エントリポイントは `apps/sandbox/main.cpp` である。

実行フローは次のとおり。

```text
SandboxAppを生成
  ↓
ApplicationがWindow、VulkanDevice、Swapchainを生成
  ↓
Assimpでassets/teapot.objをCPUメモリへ読み込む
  ↓
GPU用Position / Attribute / Index Bufferを生成し、アップロードを予約
  ↓
CpuBVHBuilderでSpatial Median BVHを構築
  ↓
BVH Node / Reordered Index Bufferを生成し、アップロードを予約
  ↓
UploadManagerで転送をSubmitして完了待機
  ↓
RenderGraphを生成し、Applicationへ登録
  ↓
Swapchain、モデル、BVHをRenderGraphへImport
  ↓
raytrace.compを使用するCompute Passを追加
  ↓
RenderGraph::compile()
  ↓
毎フレーム beginFrame → パラメータ更新 → graph.execute → endFrame
```

現在のSandboxでは4秒周期で、通常の法線表示とBVH走査回数ヒートマップを切り替える。

---

## 4. 最小利用例

### 4.1 Applicationの派生

```cpp
class SandboxApp : public core::Application {
public:
    SandboxApp()
        : Application({"MyRenderer Realtime Raytracing", 1280, 720}) {}

    void run();
};
```

`Application`の生成時に以下が作成される。

1. GLFW Window
2. RHI DeviceのVulkan実装
3. Vulkan Swapchain

### 4.2 GPUリソースの準備

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
```

`buildAndEnqueue`はGPUバッファを生成してUploadManagerへ転送要求を登録するが、その時点では転送を完了しない。

明示的に即時転送する場合は次を呼ぶ。

```cpp
auto* uploads = getDevice().getUploadManager();
uploads->submitUploadsAsync();
uploads->waitUploads();
```

RenderGraphの`compile()`時に未処理アップロードをAuto Upload Passとして取り込む経路も存在する。

### 4.3 RenderGraphの構築

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
    [this](uint32_t& w, uint32_t& h, uint32_t& d) {
        w = getWidth();
        h = getHeight();
        d = 1;
    }
);

dispatch.write(hSwapchain)
        .read(model->hPosition)
        .read(model->hAttribute)
        .read(bvh->hReorderedIndices)
        .read(bvh->hNodes);

graph->compile();
```

`dispatchThreads`へ渡した値はThread数であり、Shader Reflectionで取得したLocal Sizeに基づいてWorkgroup数へ変換される。

### 4.4 毎フレームの更新

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

`beginFrame()`は`std::optional<FrameInfo>`を返す。最小化中、またはAcquire後にSwapchain再作成が必要な場合は`std::nullopt`となる。

---

## 5. ShaderとC++の名前対応規則

MyRendererでは、RenderGraphへ登録したリソース名とShaderのPush Constantメンバ名を一致させる。

例:

```cpp
auto handle = graph->importResource(buffer, "ModelPos"_hash);
dispatch.read(handle);
```

```glsl
layout(push_constant) uniform PushConstants {
    uint ModelPos;
} pc;
```

処理の流れは次のとおり。

1. GLSLをshadercでSPIR-Vへコンパイルする
2. SPIRV-CrossでPush Constantメンバ名とOffsetを取得する
3. C++側の32bit FNV-1a `StringHash`で名前を照合する
4. 実行時に物理リソースのBindless IndexをPush Constantの該当Offsetへ書き込む

### Uniformデータ

`setUniform(nameHash, value)`もShaderのPush Constantメンバ名を使用する。ただし値そのものをPush Constantsへ入れるのではない。

1. Uniform値をConstantBufferManagerのリングバッファへ書く
2. Bindless UBO IndexとByte Offsetを取得する
3. Push Constantへ`uvec2`相当の8byteを書き込む
4. Shader側でBindless UBO配列をIndex指定して読み取る

Shader側は次のような形になる。

```glsl
layout(push_constant) uniform PushConstants {
    uvec2 sceneGlobalsPtr;
} pc;
```

`setUniform<T>`の`T`は16byte単位でなければならない。通常の`setPushConstant<T>`は4byte単位でなければならない。

---

## 6. Bindless Descriptorの固定構成

Global Descriptor Setは`set = 0`で、現在のBindingは固定である。

| Binding | Descriptor Type | 主な用途 |
|---:|---|---|
| 0 | Storage Buffer | 頂点、Index、BVH、一般SSBO |
| 1 | Storage Image | Compute出力、Swapchain直接書き込み |
| 2 | Uniform Buffer | ConstantBufferManager |
| 3 | Sampled Image | テクスチャ読み取り |
| 4 | Sampler | サンプラー配列 |

Storage Buffer、Storage Image、Uniform Buffer、Sampled Imageの最大数はコード上100,000、Samplerは32である。

DescriptorはUpdate After Bind、Partially Bound、Runtime Descriptor Array、Non-uniform Indexingを前提とする。

---

## 7. ディレクトリ構成

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
│     │  ├─ Device.hpp
│     │  ├─ Resource.hpp
│     │  ├─ CommandList.hpp
│     │  ├─ Swapchain.hpp
│     │  └─ UploadManager.hpp
│     ├─ vulkan/
│     │  ├─ VulkanDevice.*
│     │  ├─ VulkanRenderGraph.*
│     │  ├─ VulkanResourceAllocator.*
│     │  ├─ VulkanBuffer.*
│     │  ├─ VulkanImage.*
│     │  ├─ VulkanSwapchain.*
│     │  ├─ VulkanUploadManager.*
│     │  ├─ VulkanCommandList.*
│     │  ├─ Vulkan*Pipeline.*
│     │  ├─ VulkanConstantBufferManager.*
│     │  ├─ VulkanGPUProfiler.*
│     │  └─ ShaderReflection.*
│     └─ utils/
│        ├─ ShaderCompiler.*
│        ├─ ModelImporter.*
│        ├─ ImageImporter.*
│        ├─ ImageExporter.*
│        └─ bvh/
└─ shaders/
   ├─ raytrace.comp
   ├─ raytrace_types.glsl
   ├─ raytrace_math.glsl
   ├─ model.vert
   └─ model.frag
```

---

## 8. ビルド前提

ルートCMakeの実際の設定は以下である。

- CMake 3.18以上
- C++20
- Vulkan
- Vulkan Memory Allocator
- GLM
- stb
- SPIRV-Cross Core / GLSL
- Assimp
- lodepng
- shaderc
- GLFW3

`RendererCore`はOBJECT Libraryで、`SandboxApp`からリンクされる。

### リポジトリ上の注意

- READMEには「C++17対応」とあるが、CMakeはC++20を要求する
- 依存マニフェストのファイル名が`vckg.json`であり、通常の`vcpkg.json`ではない
- `vckg.json`にはGLFWが記載されていないが、CMakeでは`find_package(glfw3 CONFIG REQUIRED)`を使用する
- `assets/*`と`workspace/*`はGit管理から除外されているため、実行には別途アセットとキャッシュ用ディレクトリが必要になる
- Pipeline Cacheのパスは`workspace/shader_cache.bin`

---

## 9. 実装済みと判断できる機能

### Application / Window

- GLFW Window生成
- Vulkan Surface連携
- Resizeイベント
- Real-timeフレームループの基盤
- Delta Time計測
- 最小化時の描画スキップ
- Swapchain再作成
- 登録済みRenderGraphへのResize通知

### RHI / Vulkan Device

- Vulkan 1.3 Instance / Device
- Validation LayerとDebug Messenger
- VMA
- Graphics QueueとDedicated Compute Queueの取得
- Timeline Semaphore
- Synchronization2
- Dynamic Rendering
- Extended Dynamic State 1/2/3
- Descriptor Indexing
- Bindless Descriptor Set
- Frames in Flight用Fence
- 遅延破棄キュー
- Vulkan Pipeline Cacheの保存と読み込み

### RenderGraph

- Compute Pass
- Graphics Pass
- Copy Pass
- Resource Import
- Swapchain Import
- 一時Image / Buffer生成
- Shader Reflectionに基づく名前Bind
- 動的Dispatch Size
- トポロジカルソート
- リソース寿命計算
- 一時リソース再利用
- Graphics Scopeのマージ
- Synchronization2 Barrier生成
- Queue間Timeline Semaphore依存
- Swapchain Acquire / Present Semaphore接続
- GPU Profiler統合
- Swapchain相対リソースのResize

### Asset / Rendering

- stbによる8bit、16bit、HDR画像読み込み
- PNG出力
- Assimpによるモデル読み込み
- Position / Attributeストリーム分離
- CPU Spatial Median BVH
- BVH Nodeと並べ替え済みIndexのGPU転送
- Compute ShaderによるBVHレイトレーシング
- BVH Traversal Heatmap
- Programmable Vertex Pulling形式のGraphics Shader例

---

## 10. 未完成、未使用、または要検証の事項

以下はソーススナップショット上の状態である。

### APIはあるが現在使われていないもの

- `AppMode::OnDemand`
- `requestRedraw()`と`m_needsRedraw`
- 独立したTransfer Queue
- `findTransferQueueFamilyIndex()`
- DX12バックエンド
- `GraphicsState`の一部を超える高度なGraphics設定
- ECS
- BDA

### 現在の制約

- 物理デバイス選択はDedicated Compute Queueを持つGPUを要求する
- Surface Present対応Queueの明示的な検査は行っていない
- `QueueType::Transfer`は現在Compute Queueへマップされる
- Push Constant Rangeは常に128byte
- Shader名の対応は32bit FNV-1a Hashで、衝突検出はない
- Shaderのホットリロードはない
- Shader Cacheはパス単位で、ファイル更新時の無効化はない
- CPU BVHの列挙値にはSAHとBinnedSAHがあるが、実装はSpatial Medianのみ
- モデルのMaterialとTexture読み込みは未実装
- SwapchainへのCompute直接書き込みを前提とするSandbox構成

### 重要な実装注意事項

- `MAX_FRAMES_IN_FLIGHT`は3だが、ConstantBufferManagerは2フレーム分で生成されている
- `Application::beginFrame()`と`processEvents()`の両方でイベントをPollするため、現行Sandboxでは通常フレーム中に2回Pollする
- RenderGraph再`compile()`時にPass Requirementをクリアする処理が見当たらない
- RenderGraphの依存ソートは主にWriter→Readerを構築し、Writer→Writerなどの順序を明示的には扱わない
- Resourceの現在StateはMipmap生成以外では永続的に更新されていない
- GPUProfilerは平均値を蓄積するが、`Current`表示用配列への代入が見当たらない
- Queue Family Ownership Transferはコード上存在するが、Cross-familyのRelease/Acquire配置は実機Validationで再確認すべき
- SwapchainのStorage対応可否を判定してUsageへ反映する一方、`isEnableForCompute()`は要求設定値をそのまま返す

これらは文書作成時に発見したソース上の注意点であり、すべてが実行時不具合として再現済みという意味ではない。

---

## 11. ChatGPTへの回答方針

このレンダラについて回答する際は、以下の形式を優先する。

1. 現行コードで何が起きるか
2. Vulkan仕様上どうあるべきか
3. 最小修正案
4. 現在の設計を維持する改善案
5. 将来拡張を考慮する案

特に同期問題では、次を分けて判断する。

- 同一Queue内のMemory Dependency
- 異なるQueue間のExecution Dependency
- Queue Family Ownership Transfer
- Acquire / Present用Binary Semaphore
- RenderGraph Batch間のTimeline Semaphore
- CPU再利用防止用Frame Fence
- UploadManagerの非同期転送完了

コード断片だけが提示された場合、提示されていないBarrier、Semaphore、Descriptor更新、Frame Fence処理を実装済みと仮定しない。

---

## 12. 質問時の追加テンプレート

```text
目的:

現在の現象:

期待する結果:

関係するファイル / クラス / 関数:

使用Queue:

対象ResourceHandleとResourceState:

Shader側のPush Constant宣言:

Validation Layer出力:

すでに確認したこと:

変更可能な範囲:
```

同期やRenderGraphの質問では、対象Passの追加順、`read()` / `write()`、QueueType、`forceBatchBreak()`の有無も示す。
