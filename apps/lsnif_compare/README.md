# LSNIF / BVH 比較アプリ

左は Phase 6 LSNIF、右は BVH。両方とも同じカメラ・半画面解像度・一次レイでティーポットを法線色表示します。床、影、反射は比較に含めません。GPU 時間は `LSNIF primary rays` と `BVH primary rays` の二つの Compute Pass ごとにコンソールへ約 120 フレーム間隔で出力します。モデル読込・GPU 転送は計測値に含めません。

## 配置と起動

実行時の作業ディレクトリはレンダラのリポジトリルートにします。エクスポートは ZIP を展開したディレクトリを指定します。

```text
assets/teapot.obj
assets/renderer_export/manifest.json
assets/renderer_export/weights.bin
assets/renderer_export/occupancy.bin
assets/renderer_export/parity_cases.json
assets/renderer_export/RENDERER_FORMAT.md
assets/renderer_export/reference/...  （manifest の files にある全ファイル）
```

既存の CMake 手順で `LSNIFCompare` をビルドし、リポジトリルートから実行します。実験用設定は `main.cpp` 冒頭の `kMesh`、`kExport`、`kObjectPosition`、`kObjectScale`、`kObjectYawDegrees`、`kCamera`、`kTarget`、`kFovDegrees` を変更します。左右のモデルには同じ平行移動・一様スケール・Y 軸回転が適用されます。特に学習モデルの `occupancy.aabb_min/max` はオブジェクトローカル座標なので、OBJ の頂点座標とその局所座標が揃うようにしてください。

実装は `manifest.json` の `lsnif.renderer.phase06.v1` と配列形状を検証し、`files` に列挙されたファイルのサイズと SHA-256 を照合します。推論は FP32 で行います。DDA は GPU の FP32 で計算するため、CPU 参照と境界付近で微差が出る可能性があります。付属の `parity_cases.json` との GPU 中間値の自動照合は実装していません。

### GPUProfiler の既知の一行修正（この差分には含めない）

`core/src/vulkan/VulkanGPUProfiler.cpp` の `VulkanGPUProfiler::resolveResults` で、`durationMs` を計算した直後の `for` ループ内に `m_latestResults[i] = durationMs;` を加えてください。現状の `Current` は確保後に値が書き込まれません。`Average` の計算は既存のまま動作します。

画面幅が奇数なら左側に一列多く割り当てます。同じ画素の比較には偶数幅を推奨します。計測値は各半画面の時間であり、全画面描画の時間ではありません。
