# プラグイン API の追加候補

**状態（2026-10-03）: 推奨順 1（ダウンロード・範囲読み・アップロード・フォルダ作成）は §F の仕様で実装済み
（87a90de）。続いて権限（dd0aea1、同意ダイアログは保留）、`fields` と `ui.reveal`（3a038b2）、フォルダ背景と
左ペインのメニュー（954c038・b940881、§C）、`initialize` の `app.colorScheme`（ba24d50、§E）も実装済み。
推奨順 3（`items.copy` / `items.move` / `items.moveToRubbish`）も §G の仕様で実装済み。
残りの候補は未決定で、必要になった時点で決める。仕様の正は `PLUGINS.md`。**
現行 API（`items.get` / `children` / `descendants` / `update` / `fetchPreview`、`ui.confirm`、
`ui.progress`）は WD Tagger プラグインの必要分しか無いため、汎用プラグインに要りそうなものを
`STUDY_PLUGIN_V1_DESIGN.md` §6-3 の予定分と `IMegaClient` の既存機能から拾った。
「下地」はアプリ側に既に実装があり、公開するだけで済むかどうか。
次の手順: なし（残りの候補は必要になった時点で）。

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
| ~~context に `folder` / `view`、フォルダ背景の右クリック対応~~ **済（954c038・b940881）** | 「このフォルダ全部」をフォルダ選択なしで | `folder` 欄は作らず、`context.site: "folder"` と `items` にそのフォルダ 1 件を入れる形にした（`when.targets: "folders"` がそのまま効く）。出す場所はフォルダ背景と左ペインのツリー行・Quick access の行。Cloud Drive ビューの最上位とツリーの「Cloud Drive」行は実ルート（`IMegaClient::getRootSnapshot`）を渡す。Favourites などの最上位はフォルダが無いので灰色。`view` は入れていない |

## D. 共有リンク・アカウント

| 候補 | 下地 |
| --- | --- |
| `items.exportLink {handle, expiry?, password?}` → `{url}` / `items.disableLink` | `exportNode` / `setLinkExpiry` / `encryptLinkWithPassword` |
| `account.info` → ストレージ・転送量の上限と使用量 | `getAccountInfo`。転送量上限が近ければ止める判断に使える（WD Tagger SPEC §5 の懸念） |

## E. UI・その他

| 候補 | メモ |
| --- | --- |
| `ui.toast` / `ui.input` / `ui.choose` | 設計書 §6-3 で予定済み。`ui.choose` は出力先の選択などに |
| `ui.reveal {handle}` | **済（3a038b2）**。アプリのウィンドウは前に出さない |
| `initialize` で `app.colorScheme` | **済（ba24d50）**。自前のウィンドウを開くプラグイン（MegaDirStat）が、OS ではなくアプリのテーマ設定に合わせるため。値は実際に描いている配色で `"light"` / `"dark"`、不明なら省略 |
| コンテキストメニュー以外からの起動 | 今のコマンドは右クリックメニュー（選択・フォルダの空き領域・左ペイン）からしか起動できない。選択と関係の無いコマンド（例: WD Tag Query Builder のクエリ作成画面）のために、将来はほかの場所（候補は More メニュー）にも出せるようにする予定。当面はコンテキストメニューのまま。context に項目が無い起動になるので、`site` の値を足すことになる |
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
2. context の `folder` とフォルダ背景の右クリック（**済**、§C）、`hasPreview` フィールド（未） — 小さく、
   WD Tagger もすぐ恩恵を受ける。
3. `copy` / `move` / `trash` と権限表示 — 一緒に。（**済**、§G。`trash` は `items.moveToRubbish` になった）
4. 残り（`search`、リンク、`account.info`、`ui.*`）は必要になった時点で。

## F. 決定: 推奨順 1 の仕様（2026-10-03）

§A の `items.download` は用途で2つに分けた。**処理用**（プラグインが読んで使う素材。`fetchPreview` と同じ
位置づけで使い捨て）と、**ユーザー向け**（手元に残す成果物。プラグインがユーザーの代わりに
「ダウンロード」を押す）では、寿命・見せ方・衝突の扱いがまるで違うため。`readRange` は「大きい範囲」
（zip のエントリ本体など、GB 級）を base64 で返せないので、大きい範囲は `fetchFile` の範囲指定で受ける。

### F-1. `items.fetchFile {handle, offset?, length?}` → `{path}`（処理用）

- 実行ごとの一時フォルダ（`fetchPreview` と同じ置き場）へ保存。ファイルはプラグインのもので、移動・削除自由。
  実行終了時にアプリが残りを消す。
- サイズ上限なし（Item の `size` を見て判断するのはプラグイン）。`offset` / `length` 指定でその範囲だけを
  一時ファイルにする。
- **アプリは何も表示しない**。バイト単位の進捗も無し。長い処理ではプラグインが進捗ダイアログ
  （`"progress": true` ＋ `ui.progress`）を出す前提。
- 1 本ずつ順番に処理（複数送られてもアプリのキューで直列化。MEGA に負荷を掛けない方針）。
- 中断は `$/cancel` のみ: アプリがその実行の進行中・待ちの `fetchFile` を全部中断し `-32800` を返す。
  書きかけはアプリが消す。個別の `$/cancelRequest` は必要になるまで作らない。

### F-2. `items.readRange {handle, offset, length}` → `{data, length}`

- `data` は base64。1 回の上限 1 MiB、超えたら `-32602`（大きい範囲は F-1 の範囲指定へ）。
- ファイル末尾を超える分は切り詰める。`$/cancel` で中断。

### F-3. `transfers.download {items: [{handle, subPath?}], onConflict?}`（ユーザー向け）

- 保存先は **OS の「ダウンロード」フォルダ固定**（アプリのメニューの「ダウンロード」と同じ）。`subPath` は
  そこからの相対パスで項目ごとに指定（省略で直下）。`..`・絶対パスは `-32602`。無いフォルダはアプリが作る
  （作るのはダウンロードフォルダの中だけ）。
- ファイルのみ。アプリのダウンロードキューに入れ、**キュー投入で応答**。転送一覧に出て、実行が終わっても
  続く。完了はプラグインに知らせない（完了を前提に処理するなら F-1）。
- `onConflict`: `"rename"`（既定、「(1)」）/ `"skip"`（既にあれば飛ばす）/ `"overwrite"`。

### F-4. `items.upload {parent, localPath, name?, onConflict?}` → `{item}`

- ファイルのみ（フォルダは F-5 と組み合わせる。SDK のフォルダアップロードは既存フォルダへの強制マージと
  中身のバージョン化があり、`onConflict` と整合しないため）。
- 完了まで待って作られた Item を返す。転送一覧には出さない。1 本ずつ順番、`$/cancel` で中断（F-1 と同じ）。
- `onConflict`: `"rename"`（既定）/ `"fail"`（`-32004` Conflict）/ `"version"`（既存の新バージョンにする）。
  **バージョン管理がオフのアカウントで `"version"` はエラー**: 旧ファイルがゴミ箱にも行かず消え
  （`SPEC_NAME_CONFLICT_UPLOAD.md` §1-4）、アプリ自身のアップロードならダイアログで示すところを、
  プラグイン経由だとユーザーの目を通らないため。
- アプリは `localPath` のファイルを消さない。

### F-5. `items.createFolder {parent, name, onConflict?}` → `{item, created}`

- 1 階層のみ。階層はプラグイン（またはヘルパ）が `"existing"` で順に呼ぶ。
- `onConflict`: `"existing"`（既定、あればそれを返し `created: false`）/ `"fail"`（`-32004`）/ `"rename"`。

### F-6. 権限・同意

今回は入れない。F の 5 つは既存のものを消さない追加系が中心（消えうるのはローカルの `overwrite` だけ）。
**`move` / `trash` を出す前に必ず入れる。**
→ その後、権限（manifest の `permissions`、未宣言の呼び出しは `-32001`）は実装した（dd0aea1）。
同意ダイアログは「必要になったら」で保留。

### F-7. 実装メモ（決定に付随する作業）

- `IMegaClient::createFolder` は作ったフォルダのハンドルを返さない → 返すよう変える。
- `IMegaClient::download` は同名に黙って「(1)」を付ける（`COLLISION_CHECK_ASSUMEDIFFERENT`）→ `skip` /
  `overwrite` のため切り替え可能にする。
- バージョン管理の有無は既存の `getFileVersioningEnabled` で判定。
- エラーコード `-32004` Conflict を使い始める。F-4 の版管理オフ時の拒否も `-32004` にし、`data.reason` で
  区別する（`"exists"` = `onConflict: "fail"`、`"versioningDisabled"` = 版管理オフで `"version"`）。
  `-32010` にしないのは、MEGA に何も送らないアプリ側の拒否で「MEGA の失敗」ではなく、ヘルパでは
  `MegaError` になり「切り替える」でなく「リトライ／打ち切り」に誘導するため。新設しないのは、
  プラグインから見て `"fail"` と同じ状況（同名あり・何も変わっていない）で、取る行動
  （`"rename"` でやり直すか `ui.confirm`）も同じだから。
- upload / createFolder で変更があれば既存の `changed` で実行後に表示を更新。
- `PLUGINS.md` のリファレンスとヘルパ（`megaexplorer_plugin.py`）を同時に更新する。

## G. 決定: copy / move / moveToRubbish の仕様（2026-10-03）

正は `PLUGINS.md`。ここには決めた理由だけを残す。

```
items.move          {handle, to, name?, onConflict?}  → {item, moved}
items.copy          {handle, to, name?, onConflict?}  → {item}
items.moveToRubbish {handle}                          → {}
```

- **名前**: `items.trash` ではなく `items.moveToRubbish`。MEGA・SDK・`IMegaClient` の用語に合わせた。
- **権限**: `copy` / `move` は既存の `items.write`（説明を「追加・コピー・移動」に広げた）。
  権限を細かく増やしたくないので既存に寄せたが、ゴミ箱だけは戻せない削除に繋がりうるので
  **`items.rubbish`** を新設した。これは「ゴミ箱に関わる操作の権限」で、復元や完全削除を将来足すなら
  ここに入れるかをその時に決める（完全削除は必要になるまで検討しない）。
- **`onConflict`**: `"rename"`（既定）/ `"fail"` / `"version"`（コピー×ファイルのみ）。衝突は同名かつ同種別だけ
  （`SPEC_NAME_CONFLICT_COPY_MOVE.md` §3-6、`createFolder` と同じ）。同名の兄弟を作る選択肢（アプリの
  「このまま実行」）は出さない。プラグインが意図して欲しがる場面がない。フォルダのマージは SDK に無い。
- **今いるフォルダへの move**: 何もせず成功（`moved: false`）。アプリでも同じフォルダへの移動はエラーに
  しないのと揃え、振り分けの再実行を安全にする。`name` 付きは `-32602`（名前変更は `items.update`）。
- **今いるフォルダへの copy**: 自分自身と衝突する通常の衝突として扱い、`"rename"` なら `name (2)`。
  アプリの貼り付けの「- Copy」は使わない。
- **確認と通知**: アプリは出さない。同意ダイアログも引き続き保留。確認（`ui.confirm`）と結果の通知は
  プラグイン開発者に任せる。
- **ゴミ箱の中の項目**: 3 つとも `-32002`。`move` でゴミ箱から出す（復元）は将来の `items.rubbish` 側の話。

実装メモ: `IMegaClient::copyNode` はコピーのハンドルを返すようにした（F-7 の `createFolder` と同じ変更）。
ゴミ箱内の判定のため `NodeSnapshot::inRubbish` を足した（削除済みのノードもハンドルで引けてしまうため）。
転送キューには入れない単発リクエストで、Cancel では止まらない（`items.update` と同じ）。
