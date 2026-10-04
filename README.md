# ROOD（Studio Sandix 開発コード）

ROODは、SRTの受信・分配・接続状況の監視に特化したWindowsアプリです。受信した映像・音声を同期し、映像はSpout2とOMT、音声はASIO／WASAPIとOMTへ出力します。エンジンはQt 6 Widgetsの画面から独立させ、将来の画面なしLinux版でも再利用します。現段階ではSRT配信機能を開発範囲に含めません。

このリポジトリは開発中のソースコードを公開するものです。Windows実行バイナリとSteinberg ASIO SDKは含めません。実行バイナリの配布は第三者ライセンスと実機検証の残項目を完了してから判断します。

ROOD自作部分のライセンスは[MIT](LICENSE)です。外部ライブラリにはそれぞれのライセンスが適用されます。

GitHubへのpush前には `./scripts/audit-publication.ps1` で全コミットのnoreplyメール、追跡履歴のローカルパス・認証情報の典型的な形式、ビルド成果物の混入を検査します。この作業リポジトリでは `git config core.hooksPath .githooks` を設定すると、同じ検査をpush前に実行できます。

## 現在できること

- C++17のエンジンで、タイムスタンプに沿って整列済みの複数音声トラックを、任意の出力チャンネルへ割り当て・複製・加算できます。
- Qt 6 Widgetsの画面から、SRT待受と受信バッファ容量、音声デバイスとチャンネル経路、Spout2／OMT出力を設定して受信開始・停止できます。接続状態と直近50件の履歴、検出トラック、SRT統計、出力統計を表示します。
- GUIの設定は受信開始時と終了時にユーザー別のINIファイルへ保存します。次回起動時にチャンネル経路とデバイス識別子を復元し、デバイスが見つからない間は選択を待機状態で保持します。受信は自動開始しません。
- PortAudio開発ファイルが見つかる環境では、デバイス列挙、WASAPI共有／排他の形式確認、無音出力時のタイムスタンプ確認ができます。
- 開発用CLI `rood_ingest` がSRT listenerでMPEG-TSを受信し、FFmpegで映像と複数音声トラックを分離・デコードします。ストリームID、チャンネル構成、PTS、受信統計を表示し、切断後は再び待ち受けます。
- `rood_ingest` に音声デバイスを指定すると、各トラックのチャンネルを任意の出力チャンネルへ割り当て、必要なサンプルレート変換を行ってPortAudioのWASAPI／ASIOデバイスへ出力できます。出力遅延をミリ秒で指定できます。デバイスが使えなくなった場合は識別子、または名前とホストAPIで再探索し、開設に失敗したときは1秒間隔で再試行します。受信統計の更新時にも出力状態とコールバックの停止を確認するため、SRT接続中に音声フレームが途切れても復帰試行を続けます。
- 稼働中に音声ストリームが止まった場合は停止回数を記録し、直ちに再開設を試みます。再開設に失敗した場合は1秒間隔で再試行します。停止回数と復帰回数をGUIとCLIに表示します。
- `rood_ingest` にSpout名を指定すると、デコード映像をSpout2へ出力できます。音声デバイスを同時指定した場合は、音声コールバックの推定メディア時刻に映像を合わせます。
- `rood_ingest` にOMT名を指定すると、映像と、複数トラックから最大32チャンネルへルーティングした音声をOMTへ出力できます。双方の元PTSをOMTタイムスタンプへ渡します。

**クロック差補正は初期実装で、1時間のローカルWASAPIループバック試験を通しました。** 別クロックの実機での長時間試験は未完了です。受信エンジンは現時点でMPEG-TSとlistenerモードに限定されます。映像と音声の同期時刻はPortAudioコールバックから推定しており、物理出力時刻の測定値ではありません。

SRT受信遅延20 ms・出力遅延40 msを指定した同一PC内の短時間試験では、仮想WASAPIの共有・排他、Spout、32チャンネルOMTの同時出力が通りました。実ネットワークで安定する最小値や物理的な出力遅延を示す結果ではありません。数値と試験条件は[PortAudio検証記録](docs/portaudio-validation.md)に記載しています。

## Windows MSVC + Qt 6での開発

このPCではVisual Studio Build Tools 2026のMSVC、Qt 6.10.3の公式MSVC 2022向け動的ライブラリ、PortAudio v19.7.0を使います。Microsoftは両MSVCのバイナリ互換性を案内しています。Qtが公式に列挙するQt 6.10のWindows構成はMSVC 2022なので、この組み合わせの動作はROODのビルドと起動でも確認します。

PowerShellでリポジトリ直下から実行します。Qt取得にはPython 3.11、Git、ネット接続が必要です。依存と出力はすべてGit対象外の `build/` に置きます。

```powershell
./scripts/bootstrap-qt.ps1
./scripts/bootstrap-portaudio-msvc.cmd
./scripts/build-msvc.cmd
$env:PATH = "$(Resolve-Path ./build/deps/qt/6.10.3/msvc2022_64/bin);$(Resolve-Path ./build/deps/portaudio-msvc-install/bin);$env:PATH"
./build/msvc/rood_gui.exe
```

`build-msvc.cmd` はGUI、診断ツール、WASAPI版PortAudioプローブをビルドして `ctest` を実行します。`./build/msvc/rood_pa_probe.exe --list` で音声デバイスを確認できます。Qtは `aqtinstall==3.3.0` で公式アーカイブから取得します。

受信・デコード・映像分配の実装に使う開発ライブラリは、次のスクリプトで `build/deps` に用意します。FFmpegの初回ビルドには時間がかかります。

```powershell
./scripts/bootstrap-media-deps.cmd
./scripts/bootstrap-spout2.cmd
./scripts/bootstrap-omt.ps1
./scripts/build-media-msvc.cmd
./scripts/run-gui-msvc.cmd
```

最適化したWASAPI版の確認には `./scripts/build-media-release-msvc.cmd` を使います。Debug版とは別の `build/msvc-media-release` に出力し、同じ固定済み依存でビルドとCTestを実行します。起動は `./scripts/run-gui-release-msvc.cmd` です。ASIO SDKはこの構成に入りません。

既存のCMakeキャッシュを使わずにソースからRelease版を確認するには `./scripts/verify-clean-build-msvc.cmd` を実行します。`build/repro-verify-*` の新規ディレクトリに全ターゲットを構築し、CTest、実際にリンクした依存DLLの診断、PortAudioにASIOデバイスが現れないことの確認を実行します。このPCで38工程のビルド、CTest 5件、FFmpegのLGPL構成確認が通りました。ローカルに導入済みの固定依存を再利用する検証であり、別のWindows機で依存の取得から再現した結果ではありません。

GitHub Actionsの[`Windows source build`](https://github.com/faio1230/ROOD/actions/runs/37186234401)は、クリーンなWindowsランナーで固定版のQt 6とOMTのバイナリを取得し、PortAudio、Spout2、FFmpeg、libsrtをソースから構築しました。ROODのReleaseビルド、CTest 5件、依存診断まで合格し、FFmpeg DLLの実行時ライセンス表示は `LGPL version 2.1 or later` でした。さらにQtソースとライセンス本文を照合し、102ファイルの確認用フォルダーを作成して、依存診断・SRT待受・GUI起動のスモークテストを通しました。非圧縮ファイルの監査でランナーのユーザープロファイルパスと認証情報パターンは0件でした。これはバイナリ公開の承認ではなく、ASIO実機、物理出力時刻、開発ツールを入れていないWindows機でのVCランタイムは未検証です。

続く[CI実行](https://github.com/faio1230/ROOD/actions/runs/37188689534)では、SHA-256で固定した検証専用FFmpeg CLIからSRTを送信し、映像・ステレオ・5.1音声のデコード、切断後の再接続、OMTの0番と31番の受信信号までクリーンなランナーで確認しました。

[最新のソースビルドCI](https://github.com/faio1230/ROOD/actions/runs/37190724342)は、チャンネル構成が変わる再接続の修正も含むReleaseビルドとCTest 5件に合格しました。SRTからOMTへの32チャンネル出力で0番と31番の信号を受け、別の試験ではステレオ＋5.1音声の8チャンネルを異なるトーンで0～7番へ割り当て、受信側で全8本を照合しました。これらはソフトウェア経路の検証で、ASIO機器の物理端子の出力や独立クロックでの同期は示しません。

公開前の依存ファイル確認には `./scripts/bootstrap-qt-source.ps1` の後で `./scripts/stage-windows-release.ps1` を使います。[Qt公式のqtbase 6.10.3ソース](https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz.mirrorlist)をSHA-256で照合し、ライセンス本文を取り出します。確認用フォルダーにはRelease版GUI・CLI、必要なDLL、ROODと第三者のライセンス文書、QtのSBOM・公式バイナリアーカイブ・ソースアーカイブ、FFmpeg 8.1.2とlibsrt 1.5.6のソース・vcpkgパッチ・Releaseビルド設定、ファイルのSHA-256一覧を集めます。QtのSBOMから関連する第三者パッケージ一覧も生成します。依存診断・PortAudioのASIO非列挙・SRT待受・GUI起動を最小限のPATHで確認します。**この確認用フォルダーは配布物ではありません。** Qtの第三者通知、クリーンなWindows機でのVCランタイム確認、依存DLLとビルド資料に残るローカルパスなどを `STAGING-STATUS.txt` に示します。`PRIVACY-AUDIT.json` には該当ファイル名だけを保存します。Qtの4つのDLLとSBOMは公式バイナリアーカイブ内のファイルと一致します。DLLの生SHA-1はSBOMと異なりますが、PE署名領域を除き署名位置とチェックサムをゼロに戻すと4つともSBOMと一致します。比較結果は `licenses/Qt-SBOM-CHECKSUM-AUDIT.json` に保存します。

ローカルユーザーのビルドパスを含まない依存DLLを使う場合は `scripts/build-privacy-deps-msvc.cmd` で依存を構築し、`scripts/build-media-privacy-release-msvc.cmd` でROOD本体をビルドしてから `./scripts/stage-windows-release.ps1 -PrivacyBuild` を実行します。この経路もローカル確認用であり、公開可否はステージの `STAGING-STATUS.txt` で判断します。

`vcpkg.json` はlibsrt 1.5.6とFFmpeg 8.1.2の共有ライブラリ構成を固定します。FFmpegは `avcodec`、`avformat`、`swresample`、`swscale` のみを指定し、GPL／nonfreeの追加機能を選びません。Spout2は2.007.017のソースを固定してMSVCで構築し、OMTはv1.0.0.16の公式Windows x64配布物をSHA-256で確認します。Spout2とOMTの出力は開発用CLIから利用できます。

`build-media-msvc.cmd` は全ライブラリをリンクする `rood_deps_probe` を起動します。このPCではFFmpeg DLLが `LGPL version 2.1 or later` と報告し、libsrt・Spout2・OMTのシンボルも解決できました。

`run-gui-msvc.cmd` はメディア対応GUIを起動します。音声・Spout・OMTはそれぞれ個別に有効化できます。GUIで指定した設定はユーザー別のINIファイルへ保存され、次回起動時に復元されます。

### SRT受信の確認

受信側を起動するとUDPポート9000で待ち受けます。このコマンドでは映像・音声フレームを診断用コールバックへ渡し、音声デバイスには出しません。`--latency` はSRTの受信遅延、`--srt-buffer-kib` はSRT受信バッファの容量です。容量だけを増やしても意図した出力遅延は増えません。後者を省略するとlibsrtの既定値を使います。統計の `receiveBufferBytes` / `receiveBufferMs` は現在のキュー使用量、`receiveBufferCapacityBytes` はlibsrtから読み返した実効容量です。[libsrtの設定資料](https://github.com/Haivision/srt/blob/v1.5.6/docs/API/configuration-guidelines.md)に従い、容量は接続前に設定し、内部でパケット数へ丸められます。256 KiBを指定した短時間試験では実効容量262,016 bytesを確認しました。

```powershell
./scripts/run-ingest-msvc.cmd --port 9000 --latency 120 --srt-buffer-kib 1024
```

音声デバイスへの出力例。デバイス番号は `./scripts/list-audio-devices-msvc.cmd`、再接続時にも使うIDは `./scripts/run-ingest-msvc.cmd --list-audio-devices` で確認します。`--audio-device-id` を指定すると、番号が変わっても同じWASAPIエンドポイントを探します。出力先が一時的に見えない場合もSRT受信を続け、1秒間隔で再試行します。GUIの「更新」は選択済みの出力先を保持します。以下のトラックIDはループバック用MPEG-TSの例です。`--route` は `トラックID:入力チャンネル:出力チャンネル[:ゲイン]` で、チャンネル番号は0始まりです。`--wasapi-exclusive` を加えるとWASAPI排他モードになります。

```powershell
./scripts/run-ingest-msvc.cmd --port 9000 --audio-device 12 --audio-channels 2 --audio-rate 48000 --audio-delay 250 --route 257:0:0 --route 257:1:1 --route 258:5:1:0.5
./scripts/run-ingest-msvc.cmd --list-audio-devices
./scripts/run-ingest-msvc.cmd --port 9000 --audio-device-id '<列挙結果のID>' --route 257:0:0 --route 257:1:1
./scripts/test-srt-loopback.ps1 -AudioDevice 12
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -WasapiExclusive
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -SpoutName ROOD-Loopback
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -SpoutName ROOD-Loopback -OmtName ROOD-Loopback
./scripts/test-wasapi-cable-capture.ps1
./scripts/test-wasapi-cable-capture.ps1 -WasapiExclusive
./scripts/test-srt-clock-drift.ps1 -AudioDevice 12 -SenderReadRate 1.0003 -LogName srt-rate-plus300
./scripts/test-srt-clock-drift.ps1 -AudioDevice 12 -SenderReadRate 0.9997 -LogName srt-rate-minus300
./scripts/test-audio-recovery.ps1
./scripts/test-audio-active-failure.ps1 -AudioDevice 12 -WithVideoOutputs
```

仮想ケーブル録音試験にはVB-Audio Virtual Cable、`C:\Program Files\ffmpeg\bin\ffmpeg.exe`、Pythonが必要です。WASAPIの共有・排他それぞれで、ROODが出した左右の識別信号を対になる録音端から取得して判定します。出力先のPortAudio番号は名前から探します。録音端までの確認結果と限界は[検証記録](docs/portaudio-validation.md)に記載しています。

送出速度差試験は、FFmpegで最初のSRT接続を3分間だけ公称速度から±300 ppmずらします。終盤の補正量、音声キューの誤差、Spoutとの推定時差と破棄数を自動判定します。同一PCの模擬試験なので、独立した送信機と音声デバイスによる長時間同期の検証は引き続き必要です。

デコードスレッドはチャンネルを時刻付きの有界リングバッファに配置し、PortAudioコールバックは用意済みのfloat32 PCMを読むだけです。キュー競合、入力不足、未着トラックと、再接続後に入力から消えたチャンネルの経路は無音になります。診断出力の `renderedFrames` はコールバックがメディアの入ったフレーム位置を読んだ数で、実際の物理出力を測定した値ではありません。

長時間のクロック差補正では、音声キューの先行量を開始後5秒で基準化し、その後の変化から最大±500 ppmの緩やかなリサンプル補正を行います。GUIとCLIに補正量を表示します。1時間の同一PC内ループバックでは内部時差を維持しました。別の送信機・音声機器を使う試験と物理出力の時差測定は未実施です。

```powershell
./scripts/test-srt-hour.ps1 -AudioDevice 11
./scripts/test-srt-hour.ps1 -AnalyzeOnly
```

1時間試験の最初のSRT接続では、音声172,339,052フレーム、Spout映像89,968フレームを出力しました。デバイスunderflowとSpout送信失敗は0、補正器は安定状態でした。一時的なPC負荷上昇時に音声14,825フレーム（約0.31秒）と映像1フレームが破棄され、SRT受信キューは最大575 msになりました。ログ上の補正誤差は最大2.44 ms、音声・Spout推定時差は最大17.97 msでした。これらはアプリ内部の観測値です。試験後、SRT切断・再接続も通りました。詳細は[検証記録](docs/portaudio-validation.md)を参照してください。

`test-audio-recovery.ps1` は仮想WASAPIデバイスを8秒間排他占有し、ROODが初期の開設失敗から同じSRT接続中に復帰して音声を再生するか確認します。稼働中の機器を物理的に切断する試験は別途必要です。

`./scripts/test-srt-abrupt-disconnect.ps1` は受信中のFFmpeg送信プロセスを強制終了し、ROODの再待受後に別のSRT接続で映像・音声を再受信できるか確認します。同一PC上の試験では2回の接続・切断と再受信に合格しました。ログは `build/tests/srt-abrupt-disconnect` に保存します。

`test-audio-active-failure.ps1` はDebug版の最初のPortAudioストリームを再生中に停止させ、同じSRT接続内での再開設と音声・Spout・OMTの継続を確認します。これはアプリ内部から停止させる試験で、Windows上の実機切断は再現しません。結果と出力の欠落は[PortAudio検証記録](docs/portaudio-validation.md)に記載しています。

Spout出力のみを試す場合は `./scripts/run-ingest-msvc.cmd --port 9000 --spout ROOD` を使います。`--video-delay`、`--video-offset`、`--video-late-drop` はミリ秒単位です。`--video-delay` は音声デバイスを使わないときの遅延です。音声デバイスを同時指定すると、その出力遅延とコールバックの推定メディア時刻を映像の基準に使い、映像との時差は `--video-offset` で調整します。デコードスレッドがRGBAに変換して有界キューへ入れ、別スレッドが表示時刻に合わせて送信します。`-SpoutName` 付きループバックでは別プロセスのSpout受信器が画像画素を取得したことまで確認します。

OMT出力の例は `./scripts/run-ingest-msvc.cmd --port 9000 --omt ROOD --omt-channels 2 --omt-rate 48000 --omt-delay 250 --omt-route 257:0:0 --omt-route 258:5:1` です。OMT出力は映像BGRAと最大32チャンネルの平面float32音声を出します。別プロセスの受信プローブで映像画素と32チャンネル音声を受信し、ステレオトラックを0番、別の5.1トラックの6番目を31番に割り当てた信号を確認済みです。音声デバイスと同時出力する場合、OMTの映像・音声はPortAudioの推定メディア時計に追従します。デバイスが使えない間はホスト時計で継続します。これは送出時刻の制御であり、受信画面と物理DACの時差は未測定です。

WASAPI・Spout・32チャンネルOMTの同時出力を2分間受信した試験では、OMT受信器の映像・音声タイムスタンプに逆行はなく、最大間隔は映像40 ms、音声20 msでした。両系列のタイムスタンプ幅も118.6秒で一致しました。これはOMTのメディア時刻の連続性であり、受信画面やDACの物理出力時差を測った値ではありません。

```powershell
./scripts/test-srt-loopback.ps1 -OmtName ROOD-32ch -OmtChannels 32 -OmtRoutes @('257:0:0','258:5:31') -OmtSignalChannels '0,31' -LogName srt-omt-32ch
./scripts/test-srt-loopback.ps1 -AudioDevice 11 -SpoutName ROOD-OMT-120 -OmtName ROOD-OMT-120 -OmtChannels 32 -OmtRoutes @('257:0:0','258:5:31') -OmtSignalChannels '0,31' -OmtProbeSeconds 120 -FirstSeconds 150 -ReceiverSeconds 165 -ReceiverExe (Resolve-Path ./build/msvc-media-release/rood_ingest.exe).Path -LogName srt-omt-120s
```

別の端末からSRT callerでMPEG-TSを送ります。`--seconds 20` で自動終了、Ctrl+Cでも停止できます。任意のポートを使うループバック検証は、FFmpeg CLIがある環境で次を実行します。

```powershell
./scripts/test-srt-loopback.ps1
```

検証は映像1本、ステレオ音声1本、5.1音声1本を生成し、ストリームID、チャンネル数、PTS、デコード済みフレーム、切断後の再接続を確認します。`-FfmpegPath` でFFmpeg CLIの場所を指定できます。FFmpeg CLIは検証用で、アプリの実行時依存ではありません。

8つの入力チャンネルを個別に照合するには `./scripts/test-srt-eight-channel-map.ps1` を使います。ステレオと5.1の各チャンネルへ異なる周波数を入れ、OMTの0～7番へ割り当てます。別プロセスの受信器が各チャンネルの周波数を照合します。`-AudioDevice <番号> -AsioOnly '<ドライバ名>' -PortAudioBin <ASIO検証用DLLフォルダー> -ReceiverExe <ASIO検証用rood_ingest.exe>` を指定すると、同じ経路を8チャンネルのPortAudio出力にも渡します。この試験のOMT受信結果だけでは、ASIO機器の物理端子への配線は証明できません。

CIと同じ送信器を使う場合は `./scripts/bootstrap-ffmpeg-test-cli.ps1` を実行し、`-FfmpegPath ./build/deps/ffmpeg-test-cli/ffmpeg.exe` を指定します。[gyan.devのFFmpeg 8.1.2 Essentials](https://www.gyan.dev/ffmpeg/builds/)をSHA-256で照合して `build/` へ展開します。このGPLv3のCLIはSRTループバック試験だけに使い、ROODの実行ファイルにはリンクせず、配布候補にも含めません。

ASIO検証用PortAudioでは、ドライバを絞ったうえで別プリセットを使います。次の例はVB-Matrix VASIO-32の32出力チャンネル・44.1 kHzへ、48 kHz入力を変換する検証です。検証用ビルドはSteinberg SDKを含む可能性があるため、配布物には使いません。

```powershell
./scripts/build-media-asio-msvc.cmd
./scripts/test-srt-loopback.ps1 -AudioDevice 0 -AudioChannels 32 -AudioRate 44100 -Routes @('257:0:0','258:5:31') -ReceiverExe "$(Resolve-Path ./build/msvc-media-asio/rood_ingest.exe)" -PortAudioBin "$(Resolve-Path ./build/deps/portaudio-asio-msvc-test-install/bin)" -AsioOnly 'VB-Matrix VASIO-32'
```

ASIOの検証用ビルドにはSteinberg公式ASIO SDK 2.3.4をローカルで使います。初回のPowerShellスクリプトは公式配布URLから取得し、SHA-256を照合します。SDKとビルド成果物は `build/` 以下に置き、Gitへ含めません。検証用PortAudioパッチは環境変数で1つのASIOドライバだけを開くためのもので、通常ビルドには適用しません。ASIO対応版の公開にはSDKのGPLv3またはSteinberg独自ライセンスの経路を選ぶ必要があり、条件は[依存関係と公開前の確認](docs/dependencies-and-licensing.md)に整理しています。

**このPCのVB-MatrixとVoicemeeterの仮想ASIOでは、44.1 kHzを申告しながら実際のコールバック消費速度は約24.5～24.8 kframes/sでした。** 短時間の開設成功は連続出力の合格を意味しません。VB-Matrixを使った20～30秒のSRT受信では音声とSpout映像が大量に破棄されたため、この環境でのASIO採用は保留です。`rood_pa_probe --timing` は申告レートと実効速度が5%以上ずれると失敗します。詳細は[PortAudio検証記録](docs/portaudio-validation.md)を参照してください。

ASIO実機をつないだら、検証用ビルドを作り、そのドライバだけを読み込んで無音出力を試せます。診断スクリプトは列挙と出力に時間制限を設け、ログを `build/tests/asio-manual` に保存します。ドライバ名、チャンネル数、サンプルレートは接続機器に合わせます。

```powershell
./scripts/build-media-asio-msvc.cmd
./scripts/probe-asio-msvc.ps1 -DriverName '接続したASIOドライバ名' -ListOnly
./scripts/probe-asio-msvc.ps1 -DriverName '接続したASIOドライバ名' -Channels 8 -SampleRate 48000 -Seconds 10
```

ASIO、Spout、OMTを同時に使う長時間のクロック差試験は、Release版の検証ビルドで実行します。`-AudioDevice` は、指定したドライバだけを列挙したときの番号に合わせてください。以下の例は送信速度を公称値から+300 ppmずらし、1時間の受信中に補正量、音声とSpoutの推定時差、OMTの音声時計への追従、内部で無音にしたフレームが2秒以内かを判定します。実機の物理出力時差と、別の送信機との同期は別途測定します。

```powershell
./scripts/build-media-asio-release-msvc.cmd
./scripts/test-srt-clock-drift.ps1 -ReceiverExe ./build/msvc-media-asio-release/rood_ingest.exe -PortAudioBin ./build/deps/portaudio-asio-msvc-test-install/bin -AsioOnly '接続したASIOドライバ名' -AudioDevice 0 -AudioRate 44100 -AudioChannels 2 -FirstSeconds 3600 -OmtName ROOD-ASIO-Drift -OmtProbeSeconds 600 -RequireOmtClockSync -LogName asio-clock-drift
```

```powershell
./scripts/prepare-portaudio-asio.ps1
./scripts/bootstrap-portaudio-asio-msvc.cmd
cmd /c "call scripts\msvc-env.cmd && cmake --preset windows-msvc-asio-test && cmake --build --preset windows-msvc-asio-test --parallel 4 && ctest --preset windows-msvc-asio-test"
$env:PATH = "$(Resolve-Path ./build/deps/portaudio-asio-msvc-test-install/bin);$env:PATH"
$env:ROOD_ASIO_ONLY = 'VB-Matrix VASIO-32'
./build/msvc-asio-test/rood_pa_probe.exe --list
./build/msvc-asio-test/rood_pa_probe.exe --timing 0 default 3 32 44100
```

ASIOドライバ名、デバイス番号、サンプルレートは実機に合わせて選びます。`--timing` は無音を出力します。報告する時刻はPortAudioが返す値で、実際のDAC出力時刻を外部測定したものではありません。

## 既存のMinGW検証ビルド

PowerShellで以下を実行します。`cmake`、`git`、`g++`、`mingw32-make`がPATHに必要です。

```powershell
./scripts/bootstrap-portaudio.ps1
cmake --preset windows-mingw-dev
cmake --build --preset windows-mingw-dev --parallel 4
ctest --preset windows-mingw-dev
./build/rood_diag.exe
$env:PATH = "$(Resolve-Path ./build/deps/portaudio-install/bin);$env:PATH"
./build/rood_pa_probe.exe --list
```

MinGWプリセットはGUIをスキップし、コアとPortAudioの旧検証環境を使います。GUI開発には上のMSVCプリセットを使います。

PortAudioの簡易確認例（出力デバイス番号は `--list` で確認）:

```powershell
./build/rood_pa_probe.exe --format 12 shared 2
./build/rood_pa_probe.exe --format 12 exclusive 2
./build/rood_pa_probe.exe --timing 12 shared 10 2
```

`--timing` は指定デバイスに無音を出力します。報告する時刻はPortAudioが返す値で、実際のDAC出力時刻を外部測定したものではありません。

MinGWでもASIO検証版を使う場合:

```powershell
./scripts/bootstrap-portaudio-asio.ps1
cmake --preset windows-mingw-asio-test
cmake --build --preset windows-mingw-asio-test --parallel 4
$env:PATH = "$(Resolve-Path ./build/deps/portaudio-asio-test-install/bin);$env:PATH"
$env:ROOD_ASIO_ONLY = 'VB-Matrix VASIO-32'
./build/asio-test-app/rood_pa_probe.exe --list
./build/asio-test-app/rood_pa_probe.exe --timing 0 default 5 32 44100
```

ASIOドライバ名、デバイス番号、サンプルレートは実機に合わせて選びます。**検証用パッチを使わず全ASIOドライバを列挙すると、このPCでは処理が停止しました。**

## 開発メモ

- [アーキテクチャと次の実装順](docs/architecture.md)
- [PortAudio実機検証計画](docs/portaudio-validation.md)
- [依存関係・公開ライセンスの確認事項](docs/dependencies-and-licensing.md)

ROOD自作部分はMITライセンスで公開します。実行バイナリの配布条件は[依存関係の確認事項](docs/dependencies-and-licensing.md)に記載しています。
