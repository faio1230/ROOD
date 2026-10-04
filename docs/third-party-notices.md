# ROODに同梱する第三者ソフトウェア

この一覧はWindows x64のWASAPI版ローカル確認用フォルダに対応します。各ライブラリのライセンスはROOD自作コードのライセンスとは別です。ライセンス本文は `licenses/`、照合したソースとビルド情報は `source/` に保存しています。実際に配布する前に、同梱ファイルと通知を再照合してください。

| コンポーネント | 版 | 同梱ファイル | ライセンス・ソースの参照先 |
| --- | --- | --- | --- |
| Qt Core / Gui / Widgets / Windows platform plugin | 6.10.3 | `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`、`platforms/qwindows.dll` | `licenses/Qt-LICENSES/`、`licenses/Qt-qtbase-6.10.3.spdx`、`licenses/Qt-THIRD-PARTY-SBOM-INVENTORY.json`、`source/Qt/` |
| FFmpeg | 8.1.2 | `avcodec-62.dll`、`avformat-62.dll`、`avutil-60.dll`、`swresample-6.dll`、`swscale-9.dll` | `licenses/FFmpeg-copyright.txt`、`source/FFmpeg/` |
| libsrt | 1.5.6 | `srt.dll` | `licenses/libsrt-copyright.txt`、`source/libsrt/` |
| OpenSSL | 3.6.3 | `libcrypto-3-x64.dll` | `licenses/OpenSSL-copyright.txt` |
| PortAudio | v19.7.0、WASAPI版 | `portaudio_x64.dll` | `licenses/PortAudio-LICENSE.txt` |
| Spout2 | 2.007.017 | `SpoutLibrary.dll`、`Spout.dll` | `licenses/Spout2-LICENSE.txt` |
| OMT | v1.0.0.16 | `libomt.dll`、`libomtnet.dll`、`libvmx.dll` | `licenses/OMT-LICENSE.txt` |

FFmpegは実行時に「LGPL version 2.1 or later」と報告する構成で、GPL・nonfreeのオプションを使わずにDLLでリンクしています。対応するソースアーカイブ、vcpkgパッチ、ポート定義、ビルド時の設定は `source/FFmpeg/` にあります。Qtのソースアーカイブと公式バイナリアーカイブは `source/Qt/`、libsrtの対応ソースとビルド情報は `source/libsrt/` にあります。

ASIO対応版はこのフォルダに含めません。ROOD自作部分には同梱のMITライセンスが適用されます。現時点の確認用フォルダは公開用配布物ではありません。
