# プラグイン API の追加候補

**状態（2026-10-03）: 候補の洗い出しのみ。どれを入れるかは未決定、実装なし。**
現行 API（`items.get` / `children` / `descendants` / `update` / `fetchPreview`、`ui.confirm`、
`ui.progress`）は WD Tagger プラグインの必要分しか無いため、汎用プラグインに要りそうなものを
`STUDY_PLUGIN_V1_DESIGN.md` §6-3 の予定分と `IMegaClient` の既存機能から拾った。
「下地」はアプリ側に既に実装があり、公開するだけで済むかどうか。
次の手順: 採用するものを選び、API ごとに決めることを推奨案つきで詰める。

## A. ファイルの中身（転送）

| 候補 | 用途の例 | 下地 | 決めること |
| --- | --- | --- | --- |
| **`items.download {handle}` → `{path}`** | 原寸画像の解析、ハッシュ、変換 | `IMegaClient::download` / DownloadService | サイズ上限、転送一覧に出すか、バイト進捗、キャンセル、一時フォルダの後始末（`fetchPreview` と同方式か） |
| **`items.readRange {handle, offset, length}`** | EXIF・ヘッダ・zip 目次だけ読む | `readFileRange` / `readFileRangeStreamed` | 返し方（base64 か一時ファイルか）、1回の上限。全体 DL を避けられ転送量の節約になる |
| `items.fetchThumbnail {handle}` → `{path}` | 軽い分類・重複チェック（約200px） | `getThumbnail` | `fetchPreview` と同形ならほぼ無し |
| `items.streamUrl {handle}` → `{url}` | ffprobe / ffmpeg に動画を DL せず渡す | `streamingUrl`（ローカル HTTP） | URL の寿命（実行終了まで） |
| **`items.upload {parent, localPath, name?, onConflict?}`** | 変換結果・サイドカー（`.txt` / `.xmp`）を上げる | UploadService / UploadController | 同名時（fail / rename / replace＝新バージョン）、転送一覧に出すか、完了まで応答を待たせるか |

## B. 作成・整理

| 候補 | 下地 | メモ |
| --- | --- | --- |
| `items.createFolder {parent, name, onConflict?}` | `createFolder` | upload とセットで要る |
| `items.copy` / `items.move {handle, to, onConflict?}` | `copyNode` / `moveNode` | 振り分け系（タグ・日付でフォルダ分け） |
| `items.trash {handle}` | `moveToRubbish` | 戻せるゴミ箱移動のみ。完全削除（`removeNode`）は出さない方針 |

## C. 読み取りの追加

| 候補 | 用途 | 下地 |
| --- | --- | --- |
| `items.ancestors {handle, limit?}` | 入っているフォルダ名を使う処理 | `getParentLocation`（Item の `path` で足りる場面も多い） |
| `items.search {handle, query}` | `tag:xxx` で対象を絞る | `search`（アプリの `tag:` 検索） |
| `items.byPath {path}` → Item | 設定に書いた出力先「/Tagged」などを解決 | `getNodeSnapshot` の組み合わせ |
| Item にフィールド追加（`hasPreview` / `hasThumbnail`、公開リンク有無など） | `fetchPreview` の空振り（`-32002`）を避ける | NodeSnapshot の拡張 |
| context に `folder` / `view`、フォルダ背景の右クリック対応 | 「このフォルダ全部」をフォルダ選択なしで | メニュー側の対応が要る |

## D. 共有リンク・アカウント

| 候補 | 下地 |
| --- | --- |
| `items.exportLink {handle, expiry?, password?}` → `{url}` / `items.disableLink` | `exportNode` / `setLinkExpiry` / `encryptLinkWithPassword` |
| `account.info` → ストレージ・転送量の上限と使用量 | `getAccountInfo`。転送量上限が近ければ止める判断に使える（WD Tagger SPEC §5 の懸念） |

## E. UI・その他

| 候補 | メモ |
| --- | --- |
| `ui.toast` / `ui.input` / `ui.choose` | 設計書 §6-3 で予定済み。`ui.choose` は出力先の選択などに |
| `ui.reveal {handle}` | 結果のフォルダ・項目をタブで開いて選択する |
| `log {level, message}` | stderr より構造化できる。優先度低 |
| `plugin.dataDir` | 置き場所は開発者任せと決定済みなので、便利機能どまり |

## 基盤として先に考えるもの

- **権限と同意ダイアログ**: 現状は見つかったプラグインが全部有効。読み取りとタグなら許容範囲だったが、
  `move` / `trash` / `upload` を出すとユーザーのファイルを動かせる・消せるようになるので、少なくとも
  「このプラグインは何をするか」の表示はこれらと同時か先に入れる。
- **`invocationId` の検査**（実行中でない要求を `-32003`）も同じ理由。

## 推奨順（案）

1. `download` / `readRange` / `upload` / `createFolder` — 要望の中心。下地があり、`fetchPreview` の
   一時フォルダの仕組みもそのまま使える。
2. context の `folder` とフォルダ背景の右クリック、`hasPreview` フィールド — 小さく、WD Tagger も
   すぐ恩恵を受ける。
3. `copy` / `move` / `trash` と権限表示 — 一緒に。
4. 残り（`search`、リンク、`account.info`、`ui.*`）は必要になった時点で。
