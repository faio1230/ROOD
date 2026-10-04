# 依存関係と公開前の確認

## 固定済み／未確定

| 依存 | 状態 | 版・設定 |
| --- | --- | --- |
| PortAudio | ローカル検証用に固定 | v19.7.0、commit `147dd722548358763a8b649b3e4b41dfffbcfbb6`。WASAPI版はASIO OFF、ASIO検証版はASIO/WASAPI ON＋ドライバ限定パッチ |
| Qt 6 Widgets | ローカル導入済み | 公式MSVC 2022向けQt 6.10.3の `qtbase`。`aqtinstall==3.3.0` で取得、動的リンク。MSVC 2026でビルド・起動確認済み。公式ソースアーカイブのSHA-256は `383dc907816338f0cba72088a524c07458dfc69ce684ca9132fcc4fe91c24b0b`、公式バイナリアーカイブのSHA-256は `4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7` |
| FFmpeg | vcpkgマニフェストで固定・構築済み | 8.1.2、`avcodec` / `avformat` / `swresample` / `swscale` の共有DLL。実行時ライセンス文字列は `LGPL version 2.1 or later`、GPL／nonfree機能なし |
| libsrt | vcpkgマニフェストで固定・構築済み | 1.5.6、x64-windows共有ライブラリ。暗号処理はOpenSSL 3.6.3に依存 |
| Spout2 | ローカル構築済み | 2.007.017、commit `c2bcc12147711d12ace7d5f08e869d774d840f8a`、MSVC `/MD`、BSD-2-Clause |
| OMT (`libomt` / `libvmx`) | ローカル取得済み | 公式Windows x64配布v1.0.0.16、ZIP SHA-256 `C70E67F7E2A7ED5B4C389D99AF62796A8C9C7BE23C8DEBFAE3FD8020C1DC66B9`、同梱MITライセンス |
| Steinberg ASIO SDK | ローカル検証用に取得 | 公式配布2.3.4、ZIP SHA-256 `D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA`。SDKをこのリポジトリへコピーしない |

OMTの[公式プロジェクト](https://github.com/openmediatransport)はMITライセンスを掲げています。C/C++向けの[`libomt`](https://github.com/openmediatransport/libomt)はCエクスポートを持つ共有ライブラリで、映像圧縮に[`libvmx`](https://github.com/openmediatransport/libvmx)を使います。[公式のバイナリ配布](https://github.com/openmediatransport/libomtnet/releases/tag/v1.0.0.16)を取得し、ROODから送信した映像と2チャンネル音声を別プロセスで受信確認しました。

FFmpeg公式の[法務・ライセンス案内](https://ffmpeg.org/legal.html)は、LGPL構成ではGPL・nonfreeオプションを使わず、WindowsではDLLでリンクする方法を示しています。このPCの既存FFmpeg CLIは `--enable-gpl` 付きで、しかも開発ヘッダーがありません。プロジェクトの依存には採用しません。

`vcpkg.json` はvcpkg `2026.07.29` のcommit `9e593bb18ea69cc5095e012465dcd675a822ed0d` をbaselineに固定します。[vcpkgのFFmpegポート](https://github.com/microsoft/vcpkg/blob/9e593bb18ea69cc5095e012465dcd675a822ed0d/ports/ffmpeg/portfile.cmake)はGPLとnonfreeのオプションを各機能選択に応じて追加します。ROODではそれらを選ばず、構築時の設定と配布物を公開前に再検査します。

`rood_deps_probe` は実際のFFmpeg DLLからライセンス文字列と構成を読み取り、GPL／nonfreeの有効化を検出した場合に失敗します。libsrt・Spout2・OMTのシンボルも同じMSVCプログラムでリンク確認しました。公開前には各依存のライセンス文書・DLL・ソース提供要件を配布物に合わせて整理します。

`scripts/verify-clean-build-msvc.cmd` は既存のCMakeビルドディレクトリを再利用せず、固定済みのローカル依存からWASAPI専用Release構成を新規生成します。全ターゲットのビルド、CTest、`rood_deps_probe`、PortAudioにASIOデバイスが現れないことの確認を実行して合格しました。この確認はソースからの再構成を示しますが、依存ライブラリを未導入のWindows機での取得・構築やVCランタイムの配布条件を検証したものではありません。

2026-10-04には公開済みGitHubリポジトリを同じWindows PCの別ディレクトリへ新規クローンし、固定済みスクリプトでQt、PortAudio、Spout2、OMT、vcpkg依存を取得・構築しました。Qtはハッシュ検証済みの公式バイナリ、OMTはハッシュ検証済みの公式配布物を使用し、PortAudioとSpout2はソースから構築しました。通常のvcpkg実行では10パッケージがバイナリキャッシュから復元されたため、次のコマンドをMSVC環境で追加実行し、vcpkgの10パッケージを別のインストール先へキャッシュなしで構築しました。

```cmd
call scripts\msvc-env.cmd
build\deps\vcpkg-src\vcpkg.exe install --triplet x64-windows --x-install-root build\deps\vcpkg-source-only --binarysource=clear --clean-after-build --no-print-usage
```

FFmpeg 8.1.2、OpenSSL 3.6.3、libsrt 1.5.6を含む全10パッケージのインストールが成功し、FFmpegの構築ログと実行時表示はいずれも `LGPL version 2.1 or later` でした。このインストール先を指定したROODの新規ReleaseビルドでCTest 5件がすべて合格しました。ソースビルドしたDLLを実行ファイルのディレクトリへ置き、各DLLのSHA-256がインストール先と一致することを確認してSRTループバックを実施しました。映像、2chと5.1ch音声、WASAPI出力、Spout／OMT受信、送信側の切断後の再接続が合格し、OMT受信側の映像・音声タイムスタンプ逆行は0件でした。これは同一PCでの再現確認であり、別のWindows機への移植性や配布用DLLのパス情報除去は未確認です。

Qtはモジュールごとにライセンスが異なります。Qt Widgetsの[ライセンス案内](https://doc.qt.io/qt-6/qtwidgets-index.html)と[Qt全体の案内](https://doc.qt.io/qt-6/licensing.html)を、実際に選ぶ版の配布物に対して再確認します。LGPL構成では動的リンクを第一候補にします。

Qt 6.10の[Windows対応表](https://doc.qt.io/qt-6.10/windows.html)はMSVC 2022を列挙しています。ここではMSVC 2026のBuild Toolsを使います。[Microsoftのバイナリ互換性の説明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)を根拠にこの組み合わせを試し、ローカルでGUIのビルドと起動を確認しました。

SteinbergはASIO SDKについて[オープンソース版とプロプライエタリ版](https://www.steinberg.net/developers/)を案内し、[公式発表](https://ocl-steinberg-live.steinberg.net/_storage/asset/808575/storage/master/Press%20Release%20-%202025-10-15%20-%20OBS%20Partnership-%20EN.pdf)ではオープンソース側をGPLv3としています。`scripts/prepare-portaudio-asio.ps1` は[公式配布のSDK 2.3.4](https://www.steinberg.net/asiosdk)をSHA-256で照合します。取得したSDKの `LICENSE.txt` はGPLv3またはSteinberg独自ライセンスを選べると記載し、ファイルごとのライセンス表示も確認するよう求めています。実際のPortAudio ASIO検証ビルドは、SDKの `common/asio.cpp`、`host/asiodrivers.cpp`、`host/pc/asiolist.cpp` をDLLへコンパイルします。前者はSDKの二重ライセンスを参照し、後二者には別の再配布条件が記されています。ROOD自作部分をMITにしても、ASIO対応DLLやアプリの配布条件がMITだけになるとは扱いません。

ROOD自作部分は[MITライセンス](../LICENSE)で公開します。GitHubリポジトリにはソースコードだけを置き、Steinberg ASIO SDKや依存DLL、Windows実行バイナリは含めません。

## 配布構成の候補

現状の `windows-msvc-media-dev` はWASAPI専用のPortAudio DLLを使い、Steinberg ASIO SDKを含みません。この構成を将来のWindows実行バイナリ配布の候補にします。ただし、MITはROOD自作コードの条件であり、同梱DLLの条件を置き換えるものではありません。実際に配布するファイルを確定して、各ライセンスの義務を満たしてからバイナリを公開します。

`windows-msvc-media-asio-test` はSDK 2.3.4を使うローカル検証用です。SDK同梱の独自ライセンス契約書は2.0.5版で、SDKを開発キットとして再配布しないことや、製品公開時の表示条件などを定めています。SDKの `LICENSE.txt` は独自ライセンスでの製品公開前にSteinbergが署名した契約書を求めています。ASIO対応バイナリの公開経路は次のとおりで、どちらも現時点では未選択です。

| 経路 | 公開前に必要な確認 |
| --- | --- |
| GPLv3側 | ASIO対応PortAudio DLL、ROODアプリ、Qt・FFmpeg等を合わせた実際の配布構成について、GPLv3との整合性、対応ソース・ライセンス表示・ビルド手順を確認する。ROOD自作ソースのMIT表記は維持できるが、ASIO対応バイナリを「全体がMIT」と表示しない。 |
| Steinberg独自ライセンス側 | Steinberg署名済み契約を取得し、契約書とSDK内の個別条件に従って製品表示と配布物を確認する。SDK本体をGitHubや配布パッケージへ含めない。 |

ASIOの多チャンネル実機試験も未完了です。経路を決めてライセンスと実機の両方を確認するまで、ASIO検証版を配布物に転用しません。GitHub上のMITソース公開とWASAPI専用の確認用ステージは、このASIO対応バイナリの判断から分けて管理します。

`scripts/bootstrap-qt-source.ps1` は[Qt公式のqtbase 6.10.3ソース](https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz.mirrorlist)をSHA-256 `383dc907816338f0cba72088a524c07458dfc69ce684ca9132fcc4fe91c24b0b` で確認し、ライセンス本文38件を取り出します。アーカイブの `.tag` と導入済みバイナリのSBOMには同じcommit `7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4` が記録されています。`scripts/bootstrap-qt.ps1` はQt公式バイナリアーカイブを保存し、SHA-256 `4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7` を検証します。ステージング時には公式アーカイブからQtの4つのDLLとSBOMを取り出し、同梱ファイルとバイト単位で照合します。5ファイルとも一致します。4つのDLLの生SHA-1はSBOM記載値と異なりますが、[PE形式の証明書テーブル](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)を末尾から除き、証明書ディレクトリとチェックサムをゼロに戻したバイト列は4つともSBOM記載SHA-1と一致します。`scripts/measure-pe-without-certificate.ps1` がこの比較用ハッシュを求めます。これは署名前のバイト列を再構成した通常のSHA-1であり、Authenticodeのダイジェストではありません。同梱DLLのQt Company署名も有効です。

`scripts/stage-windows-release.ps1` はWASAPI専用ReleaseビルドからGUI・CLI、依存DLL、[第三者ソフトウェアの通知](third-party-notices.md)、Qtライセンス本文、2つのSPDX文書、公式バイナリアーカイブ、検証済みソースアーカイブとソース参照情報をローカル確認用フォルダーへ集めます。配布用フォルダーで起動したGUIの「ROODについて」には通知・ライセンス・ソースの場所を表示します。`scripts/audit-qt-third-party.ps1` はQtのCore・Gui・Widgets・qwindowsについて、SBOMの `DEPENDS_ON` 関係をたどり、第三者パッケージ43件を一覧にします。これはSBOM上の依存関係であり、個々のコードが実際にDLLへ組み込まれた証拠ではありません。1件はSBOM上のライセンス結論が `NOASSERTION` です。FFmpegはvcpkgのポートが指定するSHA-512と一致する8.1.2のソースアーカイブ、14件のパッチ、ポート定義、Releaseビルド時に生成した設定ファイルを保存します。libsrtも同様に1.5.6のソースアーカイブ、3件のパッチ、ポート定義、CMakeCacheを保存します。各ファイルのSHA-256、Qtの公式アーカイブとのバイト照合、Qtの4つのDLLとSBOMの生SHA-1と署名領域を除いた再構成SHA-1、Git作業ツリーの状態を記録し、依存診断、PortAudioのASIO非列挙、SRT待受、GUI起動を開発用DLLパスなしで確認します。Qtの第三者通知の最終照合、公開時のソース提供方法、WindowsのVCランタイムがない機械での起動は未確認です。フォルダー内の `STAGING-STATUS.txt` はこれらを公開前の不足として明記します。このローカル確認は公開可否の承認ではありません。

`scripts/audit-stage-privacy.ps1` はステージの非圧縮ファイルを走査し、ユーザーフォルダーの絶対パスと資格情報に似た文字列を含むファイル名だけを `PRIVACY-AUDIT.json` に保存します。FFmpegとlibsrtのビルド設定4ファイルは、ステージへコピーするときにユーザーフォルダーの絶対パスを `<LOCAL_USER_PROFILE>` へ置換します。再現時は実際のパスを代入します。置換後のステージでは、このPCのユーザーフォルダーのパスを依存DLLの8ファイルから検出し、資格情報パターンは0件でした。残るDLLにはPDB参照、vcpkgビルドツリー、FFmpeg構成に由来する文字列があり、PortAudio DLLにもソースパスが1件あります。圧縮アーカイブ内はこの走査の対象外です。ローカルパスを含む確認用フォルダーをそのまま公開せず、実際に配布するファイルを確定した後で再構築または除去し、再監査します。

このローカルパス問題に対し、`scripts/build-privacy-deps-msvc.cmd` はDocuments内のROOD作業フォルダーを一時的に `R:` へ割り当て、PortAudioとvcpkg依存を固定版のソースから再構築します。vcpkgのバイナリキャッシュを無効にし、終了時に割り当てを解除します。実行後、`scripts/check-privacy-deps.ps1` が生成DLLだけを走査して、このPCのユーザーフォルダーのパスと資格情報パターンがあれば失敗します。2026-10-04の初回実行ではvcpkgの10パッケージが約33分で成功し、PortAudioを含む11 DLLは両パターンとも0件でした。DLLには `R:\build` という汎用のビルドパスが残ります。PDBの一部には元のローカルパスが残るため、この確認をPDBの公開許可には使いません。

この依存セットにリンクした別のROOD ReleaseビルドではCTest 5件、FFmpegのLGPL診断、WASAPI・Spout・OMTを使うSRTループバックが合格しました。必要な実行ファイルとDLLだけを集めた24ファイルのローカル実行候補では、非圧縮ファイルの監査でこのPCのユーザーフォルダーのパスと資格情報パターンがともに0件でした。GUIも起動を確認しました。Qt公式配布の4ファイルにはビルド元のユーザーフォルダー形式のパスが残るため、一般的なパス検出結果は0件ではありません。この候補には第三者通知や対応ソースの公開用構成をまだ付けていません。バイナリ配布には完成したフォルダー全体の再監査とライセンス確認が必要です。

`scripts/build-media-privacy-release-msvc.cmd` はその依存セットでROODを新規Releaseビルドし、CTestと依存診断を行います。`scripts/stage-windows-release.ps1 -PrivacyBuild` は同じ依存セットとROOD Releaseビルドを選び、ライセンス文書・検証済みソースアーカイブ・vcpkgパッチ・FFmpegのconfigure記録・libsrtのCMake設定ログを含むローカル確認用フォルダーを作ります。クリーンアップ付きビルドでは生成された `config.h` 等が消えるため、プライバシー用経路は残存する構築ログを設定記録として使います。2026-10-04の確認では95ファイルを走査し、このPCのユーザーフォルダーのパスと資格情報パターンは0件でした。MSYS形式の `/Users/` と `/home/` も構築記録から置換しました。Qt公式DLL 4件とSBOMには配布元のユーザーフォルダー形式のパスが残ります。公式アーカイブ4件はハッシュを確認済みですが、圧縮ファイル内部の文字列走査は未実施です。ステージの起動試験は合格し、通常経路のステージ作成も再確認しました。バイナリ公開の条件は `STAGING-STATUS.txt` に残し、manifestの `publishable` は引き続き `false` です。

SBOMでライセンス結論がない1件は `WrapAtomic` です。SBOMでは `WrapAtomic::WrapAtomic` というCMakeターゲットで `FilesAnalyzed: false` と記録されています。対応するqtbase 6.10.3ソースの `cmake/FindWrapAtomic.cmake` はC++標準の `<atomic>` を検査して `INTERFACE IMPORTED` ターゲットを作り、必要な環境だけ `-latomic` をリンクします。Windows配布フォルダーに独立した `WrapAtomic` のファイルはありません。このため、SBOM上の未結論1件をそのまま「未特定の同梱DLL」とは扱いません。Qtに取り込まれた他の第三者コードの通知確認は残っています。

| 対象 | 現在の確認結果 | 公開前に実施すること |
| --- | --- | --- |
| ROOD自作コード | MITライセンス、`LICENSE` にStudio Sandixの著作権表示を記載 | ソース公開に適用。外部ライブラリの条件は各ライセンスで別途確認する |
| Qt 6.10.3 `Core` / `Gui` / `Widgets` | 動的ライブラリでリンク。Qt 6.10はLGPLv3でのアプリ開発を案内する | 同梱するQt DLL・プラグインを列挙し、各モジュールとQt内の第三者コードの通知、利用者が互換DLLへ差し替えられる構成、対応ソースを確認する |
| FFmpeg 8.1.2 | 実行時ライセンス表示は `LGPL version 2.1 or later`。GPL／nonfree機能なし | 配布DLLと一致するソース、vcpkgパッチ、configure/build設定を保存して提供する。ダウンロードページとアプリのAbout表示にFFmpegとソース入手先を明記する |
| libsrt 1.5.6 | [MPL-2.0](https://github.com/Haivision/srt/blob/v1.5.6/LICENSE)、共有DLL | ライセンス・著作権表示、配布DLLと一致するソース・パッチ・ビルド設定を確認し、ソース入手先を利用者へ知らせる。確認用フォルダーには該当ファイルを保存済み |
| OpenSSL 3.6.3 | libsrtが使用。vcpkgの共有DLL | ライセンス・通知文を同梱し、実際のDLLと依存関係を検査する |
| PortAudio v19.7.0 | WASAPI版はASIO SDKなし、共有DLL | MITライセンス・著作権表示、公開ビルドと検証用ASIOビルドの混入防止を確認する |
| Spout2 2.007.017 | BSD-2-Clause、MSVC共有DLL | ライセンス・著作権表示を同梱する |
| OMT v1.0.0.16 | 配布ZIPにMITライセンス。`libomt` と `libvmx` を使用 | 両DLLの配布元・版・同梱ライセンスを記録し、通知文を同梱する |
| Steinberg ASIO SDK 2.3.4 | ローカル検証用のみ | ASIO対応バイナリの公開経路を選び、SDKの各ファイルの条項と配布物を照合する |

[Qt 6.10のライセンス案内](https://doc.qt.io/qt-6.10/licensing.html)はモジュールによって条件が異なること、第三者コードの通知とSBOMを確認できることを示しています。[FFmpeg公式のチェックリスト](https://ffmpeg.org/legal.html)はDLLリンクに加え、一致するソース、ビルド方法、配布ページとAbout表示への記載を求めています。[MozillaのMPL FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/)は、実行形式を配布する場合に対応するソースの入手方法を利用者へ知らせるよう案内しています。公開用パッケージを作る際は、この表を実ファイル一覧と照合して更新します。
