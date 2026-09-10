# 「Open」「Open as...」コンテキストメニュー — 実現可能性調査

> **状態: 机上調査のみ。実装未着手、実測値ゼロ。** 2026-09-11、`develop` の `123d16a` 時点のコードに対する調査。
> 2026-09-11 に採用、`docs/REQUESTS.md` へ要望として投入済み。§5 の未決事項は同日決定済み。
> **結論:** 開いた段階での検証は可能で、各ビューアは既に中身ベースで失敗を検出・表示している。
> 本当の課題は (1) 各ビューアの `open()` にある拡張子ガード、(2) PDF/画像を形式違いで開くと
> 失敗が分かる前にファイル全体を取得してしまうこと、(3) FFmpeg が寛容で「開けてしまう」こと。
> (2)(3) は **開く前に先頭 64 バイトを範囲読みしてマジックナンバーを見る** ことでほぼ防げる。

## 1. 現状 — 開く・開かないの判定

- 判定は**拡張子のみ**。`previewKindForName()`（`src/core/PreviewKind.cpp:111`）→
  `ViewerController::viewerKind()`（`src/qml/ViewerController.cpp:19`）→ `Main.qml:403` の `openViewer()`
  がビューアを選ぶ。該当しないファイルはダブルクリックしても何も起きない（`TabContentPane.qml:86`）。
- **各ビューアの `open()` が拡張子を再チェックしている。** 例: `ImageViewer.qml:74`
  `if (!root.controller || root.controller.viewerKind(name) !== "image") return;`
  （`PdfViewer.qml:45` / `VideoViewer.qml:58` / `AudioViewer.qml:61` / `ArchiveViewer.qml:53` も同形）。
  Open as ではここを外し、kind を引数で受ける形にする必要がある。
- 実際のデコードは中身ベース（Qt Image / PDFium / FFmpeg / 自前 zip パーサ）で、拡張子は使っていない。
  各ビューアに失敗表示が既にある:
  - `ImageViewer.qml:286` — "This image could not be displayed."（URL が取れないときは "This file could not be opened."）
  - `PdfViewer.qml:128`、`VideoViewer.qml:171`、`AudioViewer.qml:191` — 同様の `failed` 表示
  - `ArchiveViewer.qml:305` — `ArchiveBrowser.Reason` 別に "could not be read" / "could not be loaded" / "is empty"
- 画像・PDF・動画・音声のバイト列は SDK のローカル HTTP サーバ経由（`ViewerController::sourceUrl` →
  `IMegaClient::streamingUrl`）。zip だけは `readFileRange` による範囲読み。

## 2. 形式ごとの検証の効き具合

| Open as | 失敗の検出 | 問題点 |
|---|---|---|
| ZIP | 末尾 64KB を範囲読みし EOCD 署名を探す。無ければ `Unreadable`（`ViewerController.cpp:99`） | **最も良好。** 1〜2 往復で確実。docx / xlsx / jar / apk なども zip なので Open as ZIP がそのまま効く |
| PDF | `PdfPageItem` が `Error` | **危険。** `reply->readAll()`（`src/qml/PdfPageItem.cpp:75`）で全体をメモリに載せてから判定する。4GB の動画を PDF として開くとピーク約 8GB（`STUDY_PDF_LAZY_LOADING.md` §8）。サイズガードも無い |
| 画像 | `Image.Error` | 全体を取得してからデコードするはず（**未実測**）。拡張子違いでも中身が本物の画像なら Qt が内容から判別して表示できる見込み（`QImageReader` はデバイス渡しなら内容で判定、**未実測**） |
| 動画 / 音声 | FFmpeg が先頭（probesize 既定 ~5MB）を読んで判定、`player.error` | **寛容すぎる恐れ。** JPEG を動画として開くと 1 フレーム動画として成功しそう、動画を音声として開けば音声トラックが鳴る、ランダムデータが mp3 と誤検出される可能性（**すべて推測、要実機確認**） |
| テキスト | — | 専用ビューアが無い（プレビューペインのみ）。Open as の対象外、入れるなら別項目 |

## 3. 提案 — 開く前の先頭バイト検査（sniff）

`IMegaClient::readFileRange(handle, 0, 64, ...)` を 1 回だけ投げ、マジックナンバーを確認する。

| 形式 | 先頭シグネチャ |
|---|---|
| PDF | `%PDF-`（仕様上は先頭 1024 バイト以内のどこか。念のため 1KB 読むのも可） |
| PNG | `89 50 4E 47 0D 0A 1A 0A` |
| JPEG | `FF D8 FF` |
| GIF | `GIF87a` / `GIF89a` |
| WebP | `RIFF` + 4 バイト + `WEBP` |
| BMP | `BM` |
| TIFF / 多くの RAW | `II*\0` / `MM\0*` |
| MP4 / MOV / M4A / 3GP | オフセット 4 に `ftyp` |
| MKV / WebM | `1A 45 DF A3` |
| AVI / WAV | `RIFF` + 4 バイト + `AVI ` / `WAVE` |
| Ogg | `OggS` |
| FLAC | `fLaC` |
| MP3 | `ID3`、またはフレーム同期 `FF Ex/Fx`（弱い） |
| ZIP | `PK\x03\x04`（ただし zip は末尾判定が既にあるので sniff 不要） |

- **明らかに別形式** → ウィンドウを開かず、トーストで「この形式としては開けません」。
  `ToastStack.qml:84` の `push()`（エラー用なら `showError()` `:440`）が使える。
- **通過したがデコード失敗** → ビューア内表示（既存）。文言を「形式が違うか、ファイルが破損しています」に揃える。
- 効果: PDF / 画像の巨大ファイル誤指定によるメモリ問題と、FFmpeg の「開けてしまう」問題をほぼ封じられる。
- コスト: 開くたびに 1 往復（MEGA の範囲読み。数百 ms 程度と推測、**未実測**）。
- 判定関数は `std::vector<char>` → 形式 の純粋関数として `src/core` に置けるので、GoogleTest で固めやすい。
- **注意:** TGA、一部 RAW（CR2 等は TIFF 系だが例外あり）、MPEG-TS（`0x47` 同期のみ）、生の MP3 は
  シグネチャが無い/弱い。判定方針は「既知シグネチャに不一致なら拒否」ではなく
  **「別形式だと確定できたら拒否（PDF・ZIP・動画コンテナ・画像の相互取り違え）」** とし、
  判定不能は通して実デコードに任せるのが安全。PDF / 画像にはサイズ上限ガードの併用も検討余地あり。

## 4. 実装で触る範囲

1. **ビューアの kind 引数化。** `Main.qml` `openViewer()` に kind を渡せるようにし、各ビューアの `open()`
   のガードを「渡された kind を信じる」形に変える。Open as の画像は `imageSequence()`（拡張子で
   フィルタしている、`Main.qml:396`）に入らないので、前後送りなしの単発表示にする。
2. **メニュー。** `ActionCatalog.qml` に `open` と、`group: "openAs"` の各項目（画像 / 動画 / 音声 / PDF / ZIP）を追加。
   サブメニューは既に対応済み（`ActionMenu.qml:87` の `addMenu()`、`"share"` グループが前例、
   `ActionCatalog.qml:299` の `groups`）。項目の出し分けは `FileListModel::availableActions` 側。
3. **sniff + トースト**（§3）。
4. **失敗文言の統一。**

影響しないもの: サムネイル、プレビューペイン、ファイルアイコンは引き続き拡張子ベース。

## 5. 決定事項（2026-09-11、人間の判断）

- **「Open」（既定・ダブルクリック）は拡張子判定のまま。** sniff による自動判定はしない。
  形式違いのファイルを開く手段は Open as だけ。
- **Open as サブメニューは単一ファイル選択時のみ、全形式（画像 / 動画 / 音声 / PDF / ZIP）を常に並べる。**
  拡張子で既に開ける形式も除外しない。フォルダ・複数選択時は出さない。
- **sniff は判定後にウィンドウを出す。** 別形式と確定したらウィンドウは開かずトーストのみ。
  待機中にスピナー付きウィンドウを先に出す案は採らない。

## 6. 実機で確認すべきこと

- 拡張子違いの本物の画像（例: `photo.dat` 中身 JPEG）が Image で表示できるか。
- JPEG を動画 / 音声として開いたときの FFmpeg の挙動。
- 数 GB のファイルを PDF / 画像として開いたときのメモリ使用量（sniff 無しの場合）。
- sniff 1 往復の実測レイテンシ。
