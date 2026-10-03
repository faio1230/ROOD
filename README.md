# ROOD（Studio Sandix 開発コード）

ROODは、SRTの受信・分配・接続状況の監視に特化したWindowsアプリです。受信した映像・音声を同期し、映像はSpout2とOMT、音声はASIO／WASAPIとOMTへ出力する構想です。エンジンはQt 6 Widgetsの画面から独立させ、将来の画面なしLinux版でも再利用します。現段階ではSRT配信機能を開発範囲に含めません。

## 現在できること

- C++17のエンジンで、タイムスタンプに沿って整列済みの複数音声トラックを、任意の出力チャンネルへ割り当て・複製・加算できます。
- Qt 6 Widgetsが見つかる環境では、現在の開発状態を表示するGUIシェルをビルドできます。
- PortAudio開発ファイルが見つかる環境では、デバイス列挙、WASAPI共有／排他の形式確認、無音出力時のタイムスタンプ確認ができます。

**SRT受信、接続監視、TS等の分離、デコード、映像／音声同期、エンジンからPortAudioへの音声出力、Spout／OMT出力は未実装です。** GUIにも受信開始操作はありません。

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
```

`vcpkg.json` はlibsrt 1.5.6とFFmpeg 8.1.2の共有ライブラリ構成を固定します。FFmpegは `avcodec`、`avformat`、`swresample`、`swscale` のみを指定し、GPL／nonfreeの追加機能を選びません。Spout2は2.007.017のソースを固定してMSVCで構築し、OMTはv1.0.0.16の公式Windows x64配布物をSHA-256で確認します。これらをROODのエンジンに接続する実装は今後行います。

`build-media-msvc.cmd` は全ライブラリをリンクする `rood_deps_probe` を起動します。このPCではFFmpeg DLLが `LGPL version 2.1 or later` と報告し、libsrt・Spout2・OMTのシンボルも解決できました。

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
