# PortAudio実機検証

対象はPortAudio v19.7.0です。`scripts/bootstrap-portaudio.ps1` はWASAPIだけを有効にします。`scripts/bootstrap-portaudio-asio.ps1` はSteinberg公式ASIO SDK 2.3.4をローカルで使い、ASIOとWASAPIを有効にした**検証用**ビルドを作ります。この検証版には、`ROOD_ASIO_ONLY` で単一ドライバに絞る[小さなパッチ](../patches/portaudio-v19.7.0-asio-allowlist.patch)を当てています。

## 初回の観測

- デバイス列挙: Windows WASAPI出力14件、各デバイスのPortAudio上限は2チャンネル。
- VB-Audio Virtual Cable（列挙時のID 12）、48 kHz / float32 / 2 ch: `Pa_IsFormatSupported` は共有・排他とも対応。
- 3秒間の無音出力: 共有303 callback、underflow 0、DAC時刻の後退0、報告された出力遅延22 ms。
- 3秒間の無音出力: 排他302 callback、underflow 0、DAC時刻の後退1、報告された出力遅延12 ms。
- 同じWASAPI排他での5秒再試験: 502 callback、underflow 0、DAC時刻の後退1。最初の後退はcallback 2で約1.6 msでした。
- ASIO全ドライバを読み込む通常ビルドはこのPCでデバイス列挙が停止しました。登録されたどのドライバで止まるかは未特定です。検証用パッチで1ドライバに限定すると列挙できました。
- VB-Matrix VASIO-8: 8チャンネル、44.1 kHzで5秒の無音出力が成功（243 callback、underflow 0、時刻後退0）。報告遅延は約23.22 ms。
- VB-Matrix VASIO-32: 16チャンネルと32チャンネル、44.1 kHzでそれぞれ3秒の無音出力が成功（各146 callback、underflow 0、時刻後退0）。報告遅延は約23.22 ms。
- VASIO-8の48 kHz / 8チャンネルは形式照会が対応と返しましたが、`Pa_OpenStream` は `Invalid sample rate` で失敗しました。

WASAPI排他モードで時刻が後退したため、`outputBufferDacTime` を無条件に連続時刻として使う設計にはしません。ASIOについても `Pa_IsFormatSupported` の結果だけでは開設可否を決められません。これらの数値はドライバ・機器・同時利用状況に依存します。無音出力が成功しても、各チャンネルの実際の行き先、物理DAC時刻、長時間ドリフトは未検証です。

## 採用を確定するための検証

| 項目 | 方法 | 合格判断に必要な記録 |
| --- | --- | --- |
| ASIO多チャンネル | 実際に使用するASIOドライバで4/8/16chを列挙・形式確認し、全チャンネルに識別信号を出す | デバイス名、ドライバ版、利用可能数、割り当ての実出力 |
| WASAPI共有／排他 | 同一機器で希望サンプルレート・チャンネル数・バッファ長を確認 | 開閉成否、報告遅延、連続出力時のunderflow |
| 出力時刻精度 | コールバック時刻と `outputBufferDacTime` の連続性を記録し、可能ならループバックで物理出力と照合 | ずれ・ジッター・後退・欠落の分布 |
| 長時間同期 | 送信側と別クロックで1時間以上のSRT入力を再生し、音声・映像の時差を継続記録 | ドリフト量、補正量、最終的な時差 |
| デバイス切断復帰 | 再生中の切断、既定デバイス変更、同名再接続を試す | エラー遷移、無音化、再列挙と復帰の所要時間 |

上記が通るまで、PortAudioを最終採用と宣言しません。タイムスタンプはPortAudio APIが示す「先頭サンプルのDAC出力予定時刻」で、ここで実測した物理時刻ではありません。[PortAudioの時刻情報](https://portaudio.com/docs/v19-doxydocs/structPaStreamCallbackTimeInfo.html)。
