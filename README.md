# ROOD（Studio Sandix 開発コード）

ROODは、SRTの受信・分配・接続状況の監視に特化したWindowsアプリです。受信した映像・音声を同期し、映像はSpout2とOMT、音声はASIO／WASAPIとOMTへ出力します。エンジンはQt 6 Widgetsの画面から独立させ、将来の画面なしLinux版でも再利用します。現段階ではSRT配信機能を開発範囲に含めません。

## 現在できること

- C++17のエンジンで、タイムスタンプに沿って整列済みの複数音声トラックを、任意の出力チャンネルへ割り当て・複製・加算できます。
- Qt 6 Widgetsの画面から、SRT待受、音声デバイスとチャンネル経路、Spout2／OMT出力を設定して受信開始・停止できます。接続状態、検出トラック、SRT統計、出力統計を表示します。
- PortAudio開発ファイルが見つかる環境では、デバイス列挙、WASAPI共有／排他の形式確認、無音出力時のタイムスタンプ確認ができます。
- 開発用CLI `rood_ingest` がSRT listenerでMPEG-TSを受信し、FFmpegで映像と複数音声トラックを分離・デコードします。ストリームID、チャンネル構成、PTS、受信統計を表示し、切断後は再び待ち受けます。
- `rood_ingest` に音声デバイスを指定すると、各トラックのチャンネルを任意の出力チャンネルへ割り当て、必要なサンプルレート変換を行ってPortAudioのWASAPI／ASIOデバイスへ出力できます。出力遅延をミリ秒で指定できます。
- `rood_ingest` にSpout名を指定すると、デコード映像をSpout2へ出力できます。音声デバイスを同時指定した場合は、音声コールバックの推定メディア時刻に映像を合わせます。
- `rood_ingest` にOMT名を指定すると、映像と、複数トラックから最大32チャンネルへルーティングした音声をOMTへ出力できます。双方の元PTSをOMTタイムスタンプへ渡します。

**クロック差補正は初期実装で、長時間の実機検証が未完了です。** 受信エンジンは現時点でMPEG-TSとlistenerモードに限定されます。映像と音声の同期時刻はPortAudioコールバックから推定しており、物理出力時刻の測定値ではありません。

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

`vcpkg.json` はlibsrt 1.5.6とFFmpeg 8.1.2の共有ライブラリ構成を固定します。FFmpegは `avcodec`、`avformat`、`swresample`、`swscale` のみを指定し、GPL／nonfreeの追加機能を選びません。Spout2は2.007.017のソースを固定してMSVCで構築し、OMTはv1.0.0.16の公式Windows x64配布物をSHA-256で確認します。Spout2とOMTの出力は開発用CLIから利用できます。

`build-media-msvc.cmd` は全ライブラリをリンクする `rood_deps_probe` を起動します。このPCではFFmpeg DLLが `LGPL version 2.1 or later` と報告し、libsrt・Spout2・OMTのシンボルも解決できました。

`run-gui-msvc.cmd` はメディア対応GUIを起動します。音声・Spout・OMTはそれぞれ個別に有効化できます。GUIで指定した設定は現在の実行中だけ有効で、永続保存は今後追加します。

### SRT受信の確認

受信側を起動するとUDPポート9000で待ち受けます。このコマンドでは映像・音声フレームを診断用コールバックへ渡し、音声デバイスには出しません。

```powershell
./scripts/run-ingest-msvc.cmd --port 9000 --latency 120
```

音声デバイスへの出力例。デバイス番号は `./scripts/list-audio-devices-msvc.cmd` で確認します。以下のトラックIDはループバック用MPEG-TSの例です。`--route` は `トラックID:入力チャンネル:出力チャンネル[:ゲイン]` で、チャンネル番号は0始まりです。`--wasapi-exclusive` を加えるとWASAPI排他モードになります。

```powershell
./scripts/run-ingest-msvc.cmd --port 9000 --audio-device 12 --audio-channels 2 --audio-rate 48000 --audio-delay 250 --route 257:0:0 --route 257:1:1 --route 258:5:1:0.5
./scripts/test-srt-loopback.ps1 -AudioDevice 12
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -WasapiExclusive
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -SpoutName ROOD-Loopback
./scripts/test-srt-loopback.ps1 -AudioDevice 12 -SpoutName ROOD-Loopback -OmtName ROOD-Loopback
```

デコードスレッドはチャンネルを時刻付きの有界リングバッファに配置し、PortAudioコールバックは用意済みのfloat32 PCMを読むだけです。キュー競合、入力不足、未着トラックは無音になります。診断出力の `renderedFrames` はコールバックがメディアの入ったフレーム位置を読んだ数で、実際の物理出力を測定した値ではありません。

長時間のクロック差補正では、音声キューの先行量を開始後5秒で基準化し、その後の変化から最大±500 ppmの緩やかなリサンプル補正を行います。GUIとCLIに補正量を表示します。1時間以上の実機試験と物理出力の時差測定は未実施です。

Spout出力のみを試す場合は `./scripts/run-ingest-msvc.cmd --port 9000 --spout ROOD` を使います。`--video-delay`、`--video-offset`、`--video-late-drop` はミリ秒単位です。音声デバイスを同時指定すると音声コールバックの推定メディア時刻を映像の基準に使います。デコードスレッドがRGBAに変換して有界キューへ入れ、別スレッドが表示時刻に合わせて送信します。`-SpoutName` 付きループバックでは別プロセスのSpout受信器が画像画素を取得したことまで確認します。

OMT出力の例は `./scripts/run-ingest-msvc.cmd --port 9000 --omt ROOD --omt-channels 2 --omt-rate 48000 --omt-delay 250 --omt-route 257:0:0 --omt-route 258:5:1` です。OMT出力は映像BGRAと最大32チャンネルの平面float32音声を出します。別プロセスの受信プローブで映像画素と両音声チャンネルの非無音サンプルを確認済みです。音声デバイスとOMTの時刻基準は現時点では別なので、同時出力の長時間同期は未検証です。

別の端末からSRT callerでMPEG-TSを送ります。`--seconds 20` で自動終了、Ctrl+Cでも停止できます。任意のポートを使うループバック検証は、FFmpeg CLIがある環境で次を実行します。

```powershell
./scripts/test-srt-loopback.ps1
```

検証は映像1本、ステレオ音声1本、5.1音声1本を生成し、ストリームID、チャンネル数、PTS、デコード済みフレーム、切断後の再接続を確認します。`-FfmpegPath` でFFmpeg CLIの場所を指定できます。FFmpeg CLIは検証用で、アプリの実行時依存ではありません。

ASIO検証用PortAudioでは、ドライバを絞ったうえで別プリセットを使います。次の例はVB-Matrix VASIO-32の32出力チャンネル・44.1 kHzへ、48 kHz入力を変換する検証です。検証用ビルドはSteinberg SDKを含む可能性があるため、配布物には使いません。

```powershell
./scripts/build-media-asio-msvc.cmd
./scripts/test-srt-loopback.ps1 -AudioDevice 0 -AudioChannels 32 -AudioRate 44100 -Routes @('257:0:0','258:5:31') -ReceiverExe "$(Resolve-Path ./build/msvc-media-asio/rood_ingest.exe)" -PortAudioBin "$(Resolve-Path ./build/deps/portaudio-asio-msvc-test-install/bin)" -AsioOnly 'VB-Matrix VASIO-32'
```

ASIOの検証用ビルドにはSteinberg公式ASIO SDK 2.3.4をローカルで使います。初回のPowerShellスクリプトは公式配布URLから取得し、SHA-256を照合します。SDKとビルド成果物は `build/` 以下に置き、Gitへ含めません。検証用PortAudioパッチは環境変数で1つのASIOドライバだけを開くためのもので、通常ビルドには適用しません。

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
- [ローカル環境の調査結果と不足項目](docs/environment-2026-10-04.md)
- [PortAudio実機検証計画](docs/portaudio-validation.md)
- [依存関係・公開ライセンスの確認事項](docs/dependencies-and-licensing.md)

自作コードのライセンスは未決定です。現時点でライセンスファイルを付けていません。
