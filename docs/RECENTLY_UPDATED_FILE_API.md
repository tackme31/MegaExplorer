# 「最近更新されたファイル一覧」を取得する SDK API の調査

対象: `third_party/sdk`（pinned `v10.17.0`）。行番号はすべてこのバージョンの
`third_party/sdk/include/megaapi.h` のもの。

## 結論

**存在する。用途によって3系統から選ぶ。**

| 用途 | API |
| --- | --- |
| MEGA Web の "Recents" 相当（誰が・どのフォルダで・追加か更新か、まで欲しい） | `getRecentActionsAsync` |
| フラットな「更新日時降順のファイル一覧」／期間指定の絞り込み | `search` + `MegaSearchFilter::byModificationTime` |
| 日付セクション付きの写真アプリ的タイムライン（高速スクロール前提） | `groupAllNodesByDate` + `listAllNodesByPage` |

いずれも **ローカルのノードツリーに対するクエリ**であり、`fetchnodes` 完了が前提。
「リモートの変更を今すぐ拾う」用途（Phase 16）はこれらのポーリングではなく
`MegaGlobalListener::onNodesUpdate` が本筋で、上記はその補助（起動直後の「最近の更新」表示など）
という位置づけになる。

## 1. `getRecentActionsAsync` — 「最近の操作」そのもの

`megaapi.h:20956`

```cpp
void getRecentActionsAsync(unsigned days, unsigned maxnodes,
                           bool excludeSensitives,
                           MegaRequestListener* listener = nullptr);
```

- リクエスト種別は `MegaRequest::TYPE_GET_RECENT_ACTIONS`。結果は `onRequestFinish` で
  `MegaRequest::getRecentActions()` から `MegaRecentActionBucketList` として取り出す
  （`megaapi.h:6276`）。
- 直近 `days` 日に追加・更新されたノードを、**UTC の固定6時間窓（0-6 / 6-12 / 12-18 / 18-24）
  × 実行ユーザー × 親フォルダ**でバケットにまとめて返す（`megaapi.h:20898`）。
- 推奨値は 30 日 / 最大 500 ノード（`megaapi.h:20947`）。
- `bool excludeSensitives` なしの3引数版（`megaapi.h:20927`）は **deprecated**。旧版はセンシティブ
  ノードを常に含む挙動なので、新規コードでは4引数版を使う。
- 同期版 `MegaApi::getRecentActions()` はこのバージョンには**存在しない**（`megaapi.h:6276` の
  同名メソッドは `MegaRequest` 側のゲッターであって `MegaApi` のものではない）。ドキュメントコメント中に
  `MegaApi::getRecentActions` への言及が残っているが、宣言は非同期版のみ。
- 個別バケットの再取得用に `getRecentActionById(const char* id, ...)`（`megaapi.h:20992`）と、
  センシティブ指定を上書きできるオーバーロード（`megaapi.h:21031` 付近）がある。

### `MegaRecentActionBucket`（`megaapi.h:4646`）

| メソッド | 内容 |
| --- | --- |
| `getTimestamp()` | 変更が起きた時刻（epoch 秒） |
| `getUserEmail()` | 変更したユーザーの email |
| `getParentHandle()` | 変更が起きた親フォルダの handle |
| `isUpdate()` | **true = 既存ファイルの更新 / false = 新規追加** |
| `isMedia()` | バケット内が写真・動画か |
| `getId()` | バケット識別子。`getRecentActionById` に渡す |
| `getNodes()` | バケットに含まれるファイルの `MegaNodeList` |

所有権: `MegaRecentActionBucketList` がバケットを所有し、バケットが `getNodes()` の結果を所有する。
リストを跨いで保持したい場合は `copy()`（`megaapi.h:4663` / `:4762`）。

`getId()` の書式は
`dayStartTs|windowStartHour|windowEndHour|userHandle|parentHandle|isMedia|isUpdate|excludeSensitives`
（`megaapi.h:4707`）。

## 2. `search` + `MegaSearchFilter::byModificationTime`

```cpp
auto filter = std::unique_ptr<MegaSearchFilter>(MegaSearchFilter::createInstance());
filter->byModificationTime(lowerLimit, upperLimit);   // 0 を渡した側は無視される
api->search(filter.get(), MegaApi::ORDER_MODIFICATION_DESC, cancelToken, page);
```

- `byModificationTime` は `megaapi.h:10710`、作成日時版の `byCreationTime` は `megaapi.h:10697`。
  どちらも片方に 0 を渡せばその側の境界は無効化される。
- **注意**: `byModificationTime` に非0を渡すと、mtime を持つノード＝**ファイルのみ**が結果に含まれる
  （フォルダは落ちる、`megaapi.h:10703`）。
- ソート順は `ORDER_MODIFICATION_DESC = 8` / `ORDER_MODIFICATION_ASC = 7`、作成日時は
  `ORDER_CREATION_DESC = 6`（`megaapi.h:19317-19319`）。
- `search` の宣言は `megaapi.h:20681`（再帰）、非再帰は `getChildren(filter, ...)`
  （`megaapi.h:19527`）。第4引数の `MegaSearchPage` でページングできる。
- スコープは `MegaSearchFilter::byLocation` / `byLocationHandle` で指定（`megaapi.h:10687`、
  `SEARCH_TARGET_ROOTNODE` / `SEARCH_TARGET_ALL` など）。

3系統のうち、MegaExplorer の既存の一覧 UI（`FileEntry` のフラットなリスト）に一番素直に載るのはこれ。

## 3. `groupAllNodesByDate` / `listAllNodesByPage`

v10.17.0 で入っているフラット・カーソルページング前提の一覧 API。

- `groupAllNodesByDate(filter, order, cancelToken)`（`megaapi.h:20889`）が日付セクション
  （`MegaDateSectionList`、各セクションが件数を持つ）を返す。`order` は
  `ORDER_MODIFICATION_ASC/DESC` のみ受け付け、それ以外は空リスト＋警告。
  mtime <= 0 のノードは "1970-01-01" バケットを作らないよう除外される。
- `listAllNodesByPage` / `listAllNodesByPageAtOffset`（`megaapi.h:20856`）で、そのセクションを起点に
  ページングする。`MegaListAllNodesFilter::byTimestampAnchor(startDate, endDate, sectionOrder)`
  （`megaapi.h:11180`）でアンカーを指定するが、**片側境界しか効かない**（ASC なら下限、DESC なら
  上限）ので、1バケットだけ取りたいなら `MegaDateSection::getCount()` 件で自分で止める必要がある。
- `MegaListAllNodesFilter`（`megaapi.h:10973`）は `MegaSearchFilter` より意図的に機能が狭く、
  `byName` / `byTag` / `byDescription` / `byFavourite` / 時間窓 / `byNodeType` / テキスト検索演算子は
  **無い**。スコープ（MIME カテゴリ、ロケーション、祖先 handle 最大3件、センシティブ）だけ。
  それらが要るなら `search` / `getChildren` を使えとヘッダ自身が書いている（`megaapi.h:10978`）。

「日付見出し + 高速スクローラ」型の UI を作るとき以外は、複雑さに見合わない。

## MegaExplorer 側への示唆（未決定・メモ）

- 1 と 2 は取れる情報の粒度が違う。「誰が」「追加か更新か」が UI に要るなら 1、単に更新日時降順の
  ファイル一覧でいいなら 2。両方を `IMegaClient` に生やす必要は今のところ無さそう。
- どの系統を採っても `fetchnodes` 完了が前提なので、Phase 18 の fetchnodes 進捗まわりの制約が
  そのまま効く（`docs/FETCHNODES_PROGRESS_INVESTIGATION.md`）。
- Phase 16（リモート変更のリアルタイム反映）とは目的が別。あちらは `onNodesUpdate` 起点で、
  ここで挙げた API はスナップショット取得用。
