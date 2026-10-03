# SRT Receiver（開発開始版）

Windows用のSRT受信専用アプリを開発するリポジトリです。受信、デコード、同期、音声ルーティング、Spout映像出力を独立したエンジンにまとめ、Qt 6 Widgetsは操作画面だけを担当させます。将来の画面なしLinux版では、同じエンジンに別の入出力アダプターを接続する予定です。

## 現在できること

- C++17のエンジンで、タイムスタンプに沿って整列済みの複数音声トラックを、任意の出力チャンネルへ割り当て・複製・加算できます。
- Qt 6 Widgetsが見つかる環境では、現在の開発状態を表示するGUIシェルをビルドできます。
- PortAudio開発ファイルが見つかる環境では、デバイス列挙、WASAPI共有／排他の形式確認、無音出力時のタイムスタンプ確認ができます。

**SRT受信、TS等の分離、デコード、映像／音声同期、エンジンからPortAudioへの音声出力、Spout映像出力は未実装です。** GUIにも受信開始操作はありません。

## このWindows環境でのビルド

PowerShellで以下を実行します。`cmake`、`git`、`g++`、`mingw32-make`がPATHに必要です。

```powershell
./scripts/bootstrap-portaudio.ps1
cmake --preset windows-mingw-dev
cmake --build --preset windows-mingw-dev --parallel 4
ctest --preset windows-mingw-dev
./build/srt_rx_diag.exe
$env:PATH = "$(Resolve-Path ./build/deps/portaudio-install/bin);$env:PATH"
./build/srt_rx_pa_probe.exe --list
```

Qt 6がない場合、GUIターゲットだけをスキップします。Qt 6 Widgetsを導入した後、**そのQtバイナリと互換性のあるC++ツールチェーン**でCMakeを再構成し、`Qt6_DIR`または`CMAKE_PREFIX_PATH`を指定してください。GUIを必須にして不足時に構成を失敗させるには `-DSRT_RX_REQUIRE_QT=ON` を指定します。現在のプリセットはMinGW用です。

PortAudioの簡易確認例（出力デバイス番号は `--list` で確認）:

```powershell
./build/srt_rx_pa_probe.exe --format 12 shared 2
./build/srt_rx_pa_probe.exe --format 12 exclusive 2
./build/srt_rx_pa_probe.exe --timing 12 shared 10 2
```

`--timing` は指定デバイスに無音を出力します。報告する時刻はPortAudioが返す値で、実際のDAC出力時刻を外部測定したものではありません。

ASIOの検証用ビルドには、Steinberg公式ASIO SDK 2.3.4をローカルで使います。初回のスクリプト実行時に公式配布URLから取得し、SHA-256を照合します。SDKとビルド成果物は `build/` 以下に置き、Gitへ含めません。検証用PortAudioパッチは環境変数で1つのASIOドライバだけを開くためのもので、通常ビルドには適用しません。

```powershell
./scripts/bootstrap-portaudio-asio.ps1
cmake --preset windows-mingw-asio-test
cmake --build --preset windows-mingw-asio-test --parallel 4
$env:PATH = "$(Resolve-Path ./build/deps/portaudio-asio-test-install/bin);$env:PATH"
$env:SRT_RX_ASIO_ONLY = 'VB-Matrix VASIO-32'
./build/asio-test-app/srt_rx_pa_probe.exe --list
./build/asio-test-app/srt_rx_pa_probe.exe --timing 0 default 5 32 44100
```

ASIOドライバ名、デバイス番号、サンプルレートは実機に合わせて選びます。**検証用パッチを使わず全ASIOドライバを列挙すると、このPCでは処理が停止しました。**

## 開発メモ

- [アーキテクチャと次の実装順](docs/architecture.md)
- [ローカル環境の調査結果と不足項目](docs/environment-2026-10-04.md)
- [PortAudio実機検証計画](docs/portaudio-validation.md)
- [依存関係・公開ライセンスの確認事項](docs/dependencies-and-licensing.md)

自作コードのライセンスは未決定です。現時点でライセンスファイルを付けていません。
