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

## 配布構成の候補

現状の `windows-msvc-media-dev` はWASAPI専用のPortAudio DLLを使い、Steinberg ASIO SDKを含みません。自作コードをMITにする場合、この構成を最初の公開候補にします。ただし、MITはROOD自作コードの条件であり、同梱DLLの条件を置き換えるものではありません。実際に配布するファイルを確定して、各ライセンスの義務を満たしてから公開します。

`windows-msvc-media-asio-test` はSDK 2.3.4を使うローカル検証用です。取得したSDKの `LICENSE.txt` はGPLv3とSteinberg独自ライセンスを選択肢として記載し、独自ライセンスでの公開にはSteinbergが署名した契約書を求めています。ASIO対応バイナリを出す場合は、GPLv3に沿う配布構成を整えるか、Steinbergとの契約を取得するかを先に決めます。どちらの場合もSDK内の各ファイルにある個別のライセンス表示を確認します。現時点でASIO検証版を配布物に転用しません。

| 対象 | 現在の確認結果 | 公開前に実施すること |
| --- | --- | --- |
| ROOD自作コード | ライセンス未決定、MITが候補 | 権利者とライセンスを決め、`LICENSE` と著作権表示を追加する |
| Qt 6.10.3 `Core` / `Gui` / `Widgets` | 動的ライブラリでリンク。Qt 6.10はLGPLv3でのアプリ開発を案内する | 同梱するQt DLL・プラグインを列挙し、各モジュールとQt内の第三者コードの通知、利用者が互換DLLへ差し替えられる構成、対応ソースを確認する |
| FFmpeg 8.1.2 | 実行時ライセンス表示は `LGPL version 2.1 or later`。GPL／nonfree機能なし | 配布DLLと一致するソース、vcpkgパッチ、configure/build設定を保存して提供する。ダウンロードページとアプリのAbout表示にFFmpegとソース入手先を明記する |
| libsrt 1.5.6 | [MPL-2.0](https://github.com/Haivision/srt/blob/v1.5.6/LICENSE)、共有DLL | ライセンス・著作権表示、配布DLLと一致するライブラリソースと変更有無を整理する |
| OpenSSL 3.6.3 | libsrtが使用。vcpkgの共有DLL | ライセンス・通知文を同梱し、実際のDLLと依存関係を検査する |
| PortAudio v19.7.0 | WASAPI版はASIO SDKなし、共有DLL | MITライセンス・著作権表示、公開ビルドと検証用ASIOビルドの混入防止を確認する |
| Spout2 2.007.017 | BSD-2-Clause、MSVC共有DLL | ライセンス・著作権表示を同梱する |
| OMT v1.0.0.16 | 配布ZIPにMITライセンス。`libomt` と `libvmx` を使用 | 両DLLの配布元・版・同梱ライセンスを記録し、通知文を同梱する |
| Steinberg ASIO SDK 2.3.4 | ローカル検証用のみ | ASIO対応バイナリの公開経路を選び、SDKの各ファイルの条項と配布物を照合する |

[Qt 6.10のライセンス案内](https://doc.qt.io/qt-6.10/licensing.html)はモジュールによって条件が異なること、第三者コードの通知とSBOMを確認できることを示しています。[FFmpeg公式のチェックリスト](https://ffmpeg.org/legal.html)はDLLリンクに加え、一致するソース、ビルド方法、配布ページとAbout表示への記載を求めています。公開用パッケージを作る際は、この表を実ファイル一覧と照合して更新します。
