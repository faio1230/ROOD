# アーキテクチャと実装順

## 境界

```
SRT transport (libsrt)
    -> demux / decode (FFmpeg: stream ID と channel layout を維持)
    -> timestamped queues / jitter and output-delay control
    -> audio master clock / drift estimator / resampler
       |-> channel router -> PortAudio (ASIO / WASAPI)
       `-> video scheduler -> Spout2 (Windows)
                              Qt Widgets (設定・状態表示のみ)
```

音声デバイスの消費速度を同期の基準にし、SRT送信側とデバイスのクロック差を測って音声のリサンプル比をゆっくり調整する案を第一候補とします。映像は同じ時間軸の表示予定時刻に合わせます。PortAudioの `outputBufferDacTime` は観測値として使い、正確な物理出力時刻と決めつけません。異常値、欠落、デバイス再接続ではクロック推定をリセット／再確立する必要があります。

`ChannelRouter::render()` は**同期・サンプルレート変換後**の同じフレーム区間を入力に取ります。複数トラックはIDで区別し、各チャンネルを出力チャンネルへ複製・加算できます。未着トラックは無音です。ここにはSRTやQt、Windowsのヘッダーを含めません。

SRT受信遅延、受信キュー長、音声出力遅延、映像／音声オフセットは別の量として管理します。値の意味と実際の下限は実機計測後に決めます。

## 次の実装順

1. PortAudioのASIO多チャンネル、WASAPI共有／排他、時刻情報、長時間同期、切断復帰を実機で評価する。使用するASIO SDKと公開条件を確定する。
2. 固定したライブラリ版でSRT受信とFFmpegのdemux/decodeを接続し、音声トラックID・チャンネルレイアウト・PTSを保持する。
3. タイムライン、受信バッファ、遅延制御、ドリフト補正を実装し、音声をPortAudioへ出す。
4. 映像をSpout2へ出し、音声との表示時刻差を測る。
5. Qt Widgetsへ設定と診断値を接続し、デバイス切断／SRT再接続を扱う。

Linux小型ボード版ではSRT・FFmpeg・同期・ルーティングを再利用し、映像と音声の出力アダプターを差し替えます。SyphonとNDIは将来の検討項目です。
