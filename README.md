# MyRenderer (仮)

Vulkanベースの自作レイトレーシング，またはレンダリングエンジンプ
拡張性とレンダリング実験が目的．レンダーグラフやECSでいろんなレンダリングを試せる基盤にしたい

現在はオフラインレンダリングの画像出力を主軸に開発中

## 🚀 プロジェクトの目的

* **Vulkan & DX12:** ローレベルAPIの学習と、将来的にはAPIを抽象化したバックエンドの実装。
* **多言語シェーダー対応:** C++, GLSL, HLSL, Slangを混在・相互運用できる環境の構築。
* **機能:** レンダーグラフによるパス管理と、ECSによる柔軟なシーン管理。
* **実験:** スペクトルレンダリング、Ray Marching、プロシージャルモデリングなどを自由に試せる基盤。

## 🛠️ 動作環境 & 依存ライブラリ

本プロジェクトは **CMake** と **vcpkg** を使用して管理されています。

### 必須ツール
* **C++ Compiler:** C++17対応コンパイラ (MSVC 推奨)
* **CMake:** 3.18 以上
* **Vulkan SDK:** 最新版 (環境変数 `VULKAN_SDK` が通っていること)
* **vcpkg:** パッケージマネージャ

### 使用ライブラリ (vcpkg経由)
| ライブラリ | 用途 |
| :--- | :--- |
| **Vulkan** | グラフィックスAPIコア |
| **VulkanMemoryAllocator (VMA)** | GPUメモリ管理の効率化 |
| **glm** | 数学ライブラリ (ベクトル・行列計算) |
| **stb** | 画像読み込み |
| **lodepng** | PNG/APNG (アニメーション) の書き出し |
| **assimp** | 3Dモデルファイルの読み込み |
| **SPIRV-Cross** | シェーダーリフレクション・変換 |
| **shaderc** | シェーダーのランタイムコンパイル |

## 🏗️ ビルド方法

vcpkgのツールチェーンファイルを指定してCMakeを実行します。

```bash
.\vcpkg\vcpkg.exe install shaderc glm assimp lodepng stb spirv-cross vulkan-memory-allocator --triplet x64-windows

cmake -B -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# ビルド (Build)
cmake --build .
```

## To do list
- must
    - [x] 開発環境の構築 (CMake, vcpkg, Vulkan SDK)
    - [x] Vulkan初期化 (Headless): インスタンス、物理デバイス作成
    - [x] メモリ管理: VMAの組み込み
    - [x] 最小描画: 画面（画像）を単色でクリアする
    - [x] 画像出力: 描画結果をPNGファイルとして保存
    - [x] シェーダーパイプライン: Slang/GLSLをコンパイルして実行する仕組み
- should
    - [ ] モデル読み込み: Assimpを使ったメッシュデータのロード
    - [ ] Imageの詳細設定ビルド実装
    - [ ] レイトレーシング: 基本的なレイトレの実装（交差判定）
    - [ ] ECS導入: シーン管理（Entity, Component）の実装
    - [ ] レンダーグラフ: レンダリングパスの依存関係解決システムの構築
    - [ ] アニメーション出力: APNGによる連番画像の保存
- want
    - [ ] スペクトルレンダリング: RGBではなく波長ベースの計算
    - [ ] パーティクルシステム: 大量粒子のシミュレーション
    - [ ] プロシージャルモデリング: 数式による形状生成
    - [ ] 動画変換: mp4等へのエンコード対応
    - [ ] API抽象化: DirectX 12 バックエンドの追加
    - [ ] work graphなどのテスト

フェーズ1
初期段階として，ますはCompute Shaderによる画像出力を軸に作りたい．shaderでRaymarchingを書いて，.cppファイルのほうでテクスチャの追加やユニフォーム変数の追加，複数のshaderを通したレンダリングが簡単にできるようにする
フェーズ２
次に3dオブジェクトファイルの追加，shaderのランタイムなコンパイル，その他使えそうな機能の追加（なにがいいかな？）
フェーズ3
DirectX12の部分実装
フェーズ4
とりあえず試しにWorkGraphやってみたい