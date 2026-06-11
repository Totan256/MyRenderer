今のところcompute shaderのみを動かす．compute shaderの実行基盤を固めてからvert+fragレンダリングパイプラインを実装したい

## memo


## 実装済み機能
- コンピュートパス
- コピーパス
- レンダーグラフ
    - push contents+バインドレス
    - バリア最適化
    - リソース作成時に不要になったリソースの再利用
    - マルチディスパッチ
    - 動的にも静的にも扱えるようにした
    - シェーダリフレクション
- リングバッファ
- 静的uniform
- リソース遅延破棄
- リソースインポートをステージングバッファで処理
- リソースインポートを即実行とコマンドバッファ送信後の場合分け
- 同期の非同期化frame in flght
    - サンプラー機構
    - ミニマップ自動生成
    - イメージインポート
    - モデルインポート
    - グラフィックパイプライン
    - ディスクリプタ更新をコマンドバッファ処理後にまとめて実行

## 今後実装したい
- must
    - デバッグ，デバッグ図と安全チェックの取り外し
    - Buffer Device Address (BDA) の有効化
- should
    - シェーダバリアント（マクロ）管理システム
    - レンダーターゲット（アタッチメント）の動的リサイズ管理
    - マルチディスパッチの競合チェック
    - ハッシュによるコンパイルスキップ
    - updateResource() の安全化
    - 中間テクスチャのミニマップ生成Compute Shader ダウンサンプリングのヘルパー
    - Instance Index / DrawID 駆動

- should / safety
    - [ ] UploadManager: 画像アップロード時の bufferOffset アライメントを `optimalBufferCopyOffsetAlignment` に準拠させる (ハードウェア互換性向上)
    - [ ] UploadManager: 転送専用キューでの `vkCmdBlitImage` 呼び出しを廃止し、ミップマップ生成処理を完全に RenderGraph に委譲する (関心の分離・バグ防止)
- want / optimization
    - [ ] UploadManager: 巨大アセットロード時、メモリピークを抑えるためのストリーミング（チャンク分割）転送のサポート
    - [ ] UploadManager: マルチスレッド環境からの enqueue に備えたスレッドセーフ化
    - [ ] ReadbackManager: GPUからCPUへ結果をノンブロッキングで取得する非同期ダウンロード機構の実装


セマフォクラスで統一してタイムラインセマフォの導入をしやすくすべきか検討

異なるキューは一括submitできない？
