# ROODのアーキテクチャと実装順

## 境界

```
SRT receive (libsrt) -> connection monitor / statistics
    -> demux / decode (FFmpeg: stream ID と channel layout を維持)
    -> timestamped queues / jitter and output-delay control
    -> common media timeline / drift estimator
       |-> audio router -> PortAudio (ASIO / WASAPI)
       |               `-> OMT audio channels
       `-> video scheduler -> Spout2 (Windows)
                            `-> OMT sender (libomt + libvmx)

Qt Widgets: 設定と接続・出力状態の表示のみ
```

音声デバイスを使う構成では、その消費速度を同期の基準にし、SRT送信側とのクロック差を測って音声のリサンプル比をゆっくり調整する案を第一候補とします。OMTだけの出力ではホストの単調時計を基準にする設計を検証します。映像は共通時間軸の表示予定時刻に合わせます。PortAudioの `outputBufferDacTime` は観測値として使い、正確な物理出力時刻と決めつけません。異常値、欠落、デバイス再接続ではクロック推定をリセット／再確立する必要があります。

`ChannelRouter::render()` は**同期・サンプルレート変換後**の同じフレーム区間を入力に取ります。複数トラックはIDで区別し、各チャンネルを出力チャンネルへ複製・加算できます。未着トラックは無音です。ここにはSRTやQt、Windowsのヘッダーを含めません。

SRT受信遅延、受信キュー長、音声出力遅延、映像／音声オフセットは別の量として管理します。値の意味と実際の下限は実機計測後に決めます。

接続監視では状態遷移、切断・再接続、受信ビットレート、RTT、損失・再送、受信キューを記録します。libsrtの統計API `srt_bstats` を監視入力に使い、GUIと将来のヘッドレス版へ同じ診断値を公開します。[libsrt API](https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md)

OMTは**デコード後の映像・音声をネットワークに分配する出力**として加えます。複数のSRT音声トラックは、OMT出力ごとのルーティング設定で最大32チャンネルの音声バスへ写像します。映像のピクセル形式、フレーム時刻、音声との同期、接続先ごとの状態を扱います。[OMT公式の開発ガイド](https://github.com/openmediatransport)ではC/C++向け `libomt` と映像用 `libvmx` が案内されています。

Spout、音声デバイス、OMTの各出力に独立したキューと異常時の破棄方針を持たせます。ネットワーク側の遅い受信者が、ローカル音声と映像の出力スレッドを止めない構成にします。

SRT再送信を加える場合は独立した出力アダプターにします。入力の符号化済みストリームをそのまま中継する方式と、加工後の映像・音声を再エンコードして送る方式は別機能として評価します。現時点では採用を決めていません。

## 次の実装順

1. PortAudioのASIO多チャンネル、WASAPI共有／排他、時刻情報、長時間同期、切断復帰を実機で評価する。使用するASIO SDKと公開条件を確定する。
2. 固定したライブラリ版でSRT受信とFFmpegのdemux/decodeを接続し、音声トラックID・チャンネルレイアウト・PTSを保持する。
3. タイムライン、受信バッファ、遅延制御、ドリフト補正を実装し、音声をPortAudioへ出す。
4. 接続監視と再接続を実装し、入力ごとの統計と遅延を公開する。
5. 映像をSpout2へ出し、音声との表示時刻差を測る。
6. OMTへ映像と最大32チャンネルの音声を出し、受信側で同期とチャンネル対応を検証する。
7. Qt Widgetsへ設定と診断値を接続し、デバイス切断／SRT再接続を扱う。

Linux小型ボード版ではSRT・FFmpeg・同期・ルーティングを再利用し、映像と音声の出力アダプターを差し替えます。SyphonとNDIは将来の検討項目です。
