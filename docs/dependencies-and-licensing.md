# 依存関係と公開前の確認

## 固定済み／未確定

| 依存 | 状態 | 版・設定 |
| --- | --- | --- |
| PortAudio | ローカル検証用に固定 | v19.7.0、commit `147dd722548358763a8b649b3e4b41dfffbcfbb6`。WASAPI版はASIO OFF、ASIO検証版はASIO/WASAPI ON＋ドライバ限定パッチ |
| Qt 6 Widgets | ローカル導入済み | 公式MSVC 2022向けQt 6.10.3の `qtbase`。`aqtinstall==3.3.0` で取得、動的リンク。MSVC 2026でビルド・起動確認済み |
| FFmpeg | vcpkgマニフェストで固定・構築済み | 8.1.2、`avcodec` / `avformat` / `swresample` / `swscale` の共有DLL。実行時ライセンス文字列は `LGPL version 2.1 or later`、GPL／nonfree機能なし |
| libsrt | vcpkgマニフェストで固定・構築済み | 1.5.6、x64-windows共有ライブラリ。暗号処理はOpenSSL 3.6.3に依存 |
| Spout2 | ローカル構築済み | 2.007.017、commit `c2bcc12147711d12ace7d5f08e869d774d840f8a`、MSVC `/MD`、BSD-2-Clause |
| OMT (`libomt` / `libvmx`) | ローカル取得済み | 公式Windows x64配布v1.0.0.16、ZIP SHA-256 `C70E67F7E2A7ED5B4C389D99AF62796A8C9C7BE23C8DEBFAE3FD8020C1DC66B9`、同梱MITライセンス |
| Steinberg ASIO SDK | ローカル検証用に取得 | 公式配布2.3.4、ZIP SHA-256 `D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA`。SDKをこのリポジトリへコピーしない |

OMTの[公式プロジェクト](https://github.com/openmediatransport)はMITライセンスを掲げています。C/C++向けの[`libomt`](https://github.com/openmediatransport/libomt)はCエクスポートを持つ共有ライブラリで、映像圧縮に[`libvmx`](https://github.com/openmediatransport/libvmx)を使います。[公式のバイナリ配布](https://github.com/openmediatransport/libomtnet/releases/tag/v1.0.0.16)を取得し、ROODから送信した映像と2チャンネル音声を別プロセスで受信確認しました。

FFmpeg公式の[法務・ライセンス案内](https://ffmpeg.org/legal.html)は、LGPL構成ではGPL・nonfreeオプションを使わず、WindowsではDLLでリンクする方法を示しています。このPCの既存FFmpeg CLIは `--enable-gpl` 付きで、しかも開発ヘッダーがありません。プロジェクトの依存には採用しません。

`vcpkg.json` はvcpkg `2026.07.29` のcommit `9e593bb18ea69cc5095e012465dcd675a822ed0d` をbaselineに固定します。[vcpkgのFFmpegポート](https://github.com/microsoft/vcpkg/blob/9e593bb18ea69cc5095e012465dcd675a822ed0d/ports/ffmpeg/portfile.cmake)はGPLとnonfreeのオプションを各機能選択に応じて追加します。ROODではそれらを選ばず、構築時の設定と配布物を公開前に再検査します。

`rood_deps_probe` は実際のFFmpeg DLLからライセンス文字列と構成を読み取り、GPL／nonfreeの有効化を検出した場合に失敗します。libsrt・Spout2・OMTのシンボルも同じMSVCプログラムでリンク確認しました。公開前には各依存のライセンス文書・DLL・ソース提供要件を配布物に合わせて整理します。

Qtはモジュールごとにライセンスが異なります。Qt Widgetsの[ライセンス案内](https://doc.qt.io/qt-6/qtwidgets-index.html)と[Qt全体の案内](https://doc.qt.io/qt-6/licensing.html)を、実際に選ぶ版の配布物に対して再確認します。LGPL構成では動的リンクを第一候補にします。

Qt 6.10の[Windows対応表](https://doc.qt.io/qt-6.10/windows.html)はMSVC 2022を列挙しています。ここではMSVC 2026のBuild Toolsを使います。[Microsoftのバイナリ互換性の説明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)を根拠にこの組み合わせを試し、ローカルでGUIのビルドと起動を確認しました。

SteinbergはASIO SDKについて[オープンソース版とプロプライエタリ版](https://www.steinberg.net/developers/)を案内しています。取得した2.3.4の `LICENSE.txt` にはGPLv3またはSteinberg独自ライセンスの選択肢が記されています。自作部分をMITにしても、ASIO対応バイナリ全体をMITだけの条件で配布できると想定しません。SDK現物の条項とPortAudioの結合形態を確認してから、公開ライセンスと配布方式を決めます。

自作コードのMITライセンスは候補のままで、まだLICENSEファイルやGitHub公開設定を作っていません。
