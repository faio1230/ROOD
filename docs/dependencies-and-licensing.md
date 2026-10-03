# 依存関係と公開前の確認

## 固定済み／未確定

| 依存 | 状態 | 版・設定 |
| --- | --- | --- |
| PortAudio | ローカル検証用に固定 | v19.7.0、commit `147dd722548358763a8b649b3e4b41dfffbcfbb6`。WASAPI版はASIO OFF、ASIO検証版はASIO/WASAPI ON＋ドライバ限定パッチ |
| Qt 6 Widgets | 未導入 | ABI互換のツールチェーンと版を選んで固定する |
| FFmpeg | 開発版未導入 | LGPLに収まる設定を選び、`--enable-gpl` / `--enable-nonfree` を避けてDLLで利用する |
| libsrt | 未導入 | 版とビルド設定を固定する |
| Spout2 | 未導入 | 版、API、ライセンス、配布物を確認する |
| Steinberg ASIO SDK | ローカル検証用に取得 | 公式配布2.3.4、ZIP SHA-256 `D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA`。SDKをこのリポジトリへコピーしない |

FFmpeg公式の[法務・ライセンス案内](https://ffmpeg.org/legal.html)は、LGPL構成ではGPL・nonfreeオプションを使わず、WindowsではDLLでリンクする方法を示しています。このPCの既存FFmpeg CLIは `--enable-gpl` 付きで、しかも開発ヘッダーがありません。プロジェクトの依存には採用しません。

Qtはモジュールごとにライセンスが異なります。Qt Widgetsの[ライセンス案内](https://doc.qt.io/qt-6/qtwidgets-index.html)と[Qt全体の案内](https://doc.qt.io/qt-6/licensing.html)を、実際に選ぶ版の配布物に対して再確認します。LGPL構成では動的リンクを第一候補にします。

SteinbergはASIO SDKについて[オープンソース版とプロプライエタリ版](https://www.steinberg.net/developers/)を案内しています。取得した2.3.4の `LICENSE.txt` にはGPLv3またはSteinberg独自ライセンスの選択肢が記されています。自作部分をMITにしても、ASIO対応バイナリ全体をMITだけの条件で配布できると想定しません。SDK現物の条項とPortAudioの結合形態を確認してから、公開ライセンスと配布方式を決めます。

自作コードのMITライセンスは候補のままで、まだLICENSEファイルやGitHub公開設定を作っていません。
