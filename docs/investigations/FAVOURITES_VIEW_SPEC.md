# お気に入り一覧画面（Phase 24b）— 仕様書

2026-08-09。`docs/investigations/SPECIAL_VIEWS_INVESTIGATION.md`（特殊画面の枠組み調査）を
前提に、**お気に入り一覧という 1 画面ぶん**の仕様に落としたもの。コードは読んだが実装・計測は
していない。フレームワーク側の一般論はあちらに、ここは「この画面をこう作る」だけを書く。

依頼時点の要件（原文の趣旨）:

- クイックアクセスの上に導線（♡ Favourites）を置く / タブとして開ける
- UI は通常のグリッド・リストとほぼ同じ、プレビュー可
- フォルダをダブルクリックすると同じタブで通常のフォルダへ遷移、パンくずは通常どおり。
  その後「戻る」でお気に入り一覧へ帰る
- 削除・移動（Drop）・貼り付けは不可。リネーム・コピーは可
- 移動はお気に入り一覧⇄別フォルダのどちらも Drop 不可。ただし **Ctrl+ドロップのコピーは可**
- 別タブからお気に入り一覧へ持ってきてのドロップも不可。可能ならゴースト側で表現する

---

## 0. 結論（先に要約）

1. **「戻る」でお気に入り一覧に帰れる、という 1 行が設計を決めている。** 戻り先を持つのは
   `FolderNavigationService::mBackStack`（`Location{isRoot, handle}` のスタック）だけなので、
   お気に入り一覧は**同じスタックに載る 1 つのロケーション**でなければならない。したがって
   SPECIAL_VIEWS の案C（画面ごとに独自コントローラ）は要件と両立しない。**案A + §6-1 の
   ロケーション値型**（`Location` に `ViewKind` を足す）が事実上の一択。
   この挙動は MEGA 公式アプリに合わせるという明示の決定であり、`src/core` に踏み込むコストは
   承知のうえで受け入れる（一度検討して落としかけたが、再度採用と確定した）。
   代わりに、この 1 つの値型が**ゴミ箱・アルバム・最近の更新でもそのまま効く**共通投資になる。
2. **画面の中身は既存のまま丸ごと使える。** 行はノード、`FileListModel` はそのまま、
   テーブル/グリッド/プレビュー/サムネイル/選択/バンド選択はすべて無改修。新しいビューは要らない。
   → `TabContentPane` の `Loader` 化（案D の自由層）は**この phase では不要**。アルバム/最近の
   更新まで先送りしてよい。
3. **最大の実装コストは D&D ではなくアクション可否の配線。** SPECIAL_VIEWS §2.3 が言うとおり、
   Delete / Ctrl+X / Ctrl+V は resolver を見ていない。ここを直さないと「メニューには出ないのに
   Delete キーで消せる」画面が出来上がる。**resolver に `ViewKind` 軸を足すのは安いが、キー
   ハンドラをそこへ繋ぎ直す作業のほうが本体**。
4. **「移動不可・Ctrl+コピー可」は、ドラッグ開始を止めてはいけない**ことを意味する。move か copy
   かは**ドロップ時の修飾キー**で決まる（`DragProxy.sampleCopyMode()` は `finish()` でも
   再サンプルする）ので、開始時点では判定できない。拒否は `DragProxy.canDropOn()` の 1 箇所に
   置く。ここは既に全 6 ドロップ先が通る唯一の関門なので、**追加は 1 メソッド + 1 フィールド**。
5. **一覧の取得は同期・GUI スレッド・全ドライブ再帰。** `MegaApi::search()` は同期 API で
   （`IMegaClient.h` の規約 2、`AddressToolBar.qml` の `live: false` の理由）、
   お気に入り一覧はそれをドライブ全体に対して撃つ。60 万ノード級のアカウントでの所要は
   未計測で、**この phase で唯一の性能リスク**（§5.3）。
6. **フラットな横断一覧なのに「場所」列が無い。** 別フォルダの同名ファイルが 2 行並んでも
   区別できない。Explorer の検索結果はパス列を持つ。テーブルは 3 列固定・幅はアプリ全体で
   永続化という作りなので、列追加は見た目より配線が重い（§7.6）。要検討事項の筆頭。

---

## 1. 用語

| 語 | 意味 |
| --- | --- |
| **ビュー種別 / `ViewKind`** | `CloudDrive`（通常のフォルダ画面）/ `Favourites`。今回導入する enum |
| **お気に入り一覧** | `ViewKind::Favourites` を現在ロケーションに持つタブの状態 |
| **通常タブ** | `ViewKind::CloudDrive`。今あるもの全部 |

タブは「お気に入り専用タブ」にはならない。**1 つのタブが履歴の途中でお気に入り一覧になったり
通常フォルダになったりする。** これが要件「ダブルクリックで移動 → 戻るで帰る」の直訳。

---

## 2. 画面仕様

### 2.1 導線（サイドパネル）

`SidePanel.qml` の `ColumnLayout` 先頭、`QuickAccessSection` の**上**に新セクション
（`SpecialViewsSection.qml`）を置く。行は 1 つだけ:

- ラベル `Favourites`、先頭グリフはハートの**アウトライン**（`EB51`。塗りの `EB52` は行マーカー
  専用 — Phase 24a のログ参照。メニュー行と同じくここも状態でなく場所を指すのでアウトライン）
- 行の寸法・パディング・ピル・`focusPolicy: Qt.NoFocus` はクイックアクセスの `ItemDelegate` に揃える
  （`Theme.rowHeight.compact` / `Theme.tree.contentIndent`）
- 左クリック → 現在タブをお気に入り一覧へ（`navController.openFavourites()`）
- 中クリック → 新規タブで開く（`tabsController.addFavouritesTab()`）。バックグラウンドタブ、
  フォーカスは現タブに残す（既存の中クリック規約）
- 右クリック → メニュー無し（v1）
- 現在地強調は `navController.viewKind === Favourites`。クイックアクセス/ツリー側の
  `currentHandle` 比較とは別軸なので、**お気に入り一覧を開いている間はツリーもピンも
  どこも光らない**のが正（現状の式 `!atRoot && handle === currentHandle` は `currentHandle == 0`
  で自然に外れる。§7.2 参照）
- ドロップ先には**しない**（要件どおり）。`NodeDropArea` を置かない

`SidePanel.qml` の区切り線は現在 `visible: quickAccessModel.count > 0`。上に常設セクションが
来るので、**「ピンが 0 件でもお気に入り行と ツリーの間には線が要る」**という条件に変わる。

### 2.2 タブ

- `TabsController::addFavouritesTab()` を追加（`addTab` / `addTabAt` と同列）
- タブのタイトルは `TabsController::data()` の `TitleRole` ＝ `navigation->currentFolderName()`。
  これは `mBreadcrumb.last().name` を読むので、**パンくずに合成セグメントを 1 つ入れておけば
  タイトルは無改修で "Favourites" になる**（§3.3）
- `AtRootRole` は false。`TabStrip.qml` は `atRoot ? "Cloud Drive" : title` なのでそのまま通る
- タブへのドロップ（`TabStrip` の `NodeDropArea`）は `targetHandle: navigation.currentHandle`
  ＝ 0 なので現状でも拒否されるが、**偶然に頼らず明示する**（§4.3）。
  スプリングロード（ホバーでタブ切替、Phase 22b）は拒否ターゲットでも作動する設計なので、
  「お気に入りタブにホバーして切り替え、そこから更に別タブへ」は今までどおり効く

**F6 実装時の訂正（2026-08-11）**: この節は 3 点が古い。①タイトルは合成セグメントの `name` からは
出ない。§3.3 の訂正で `name` は空になったので `TitleRole` は `""` で、ラベルは F5 が足した
`ViewKindRole` 経由の `ViewLabels.label()` が出す。②`TabStrip.qml` の `atRoot ? "Cloud Drive" : title`
という三項演算子はもう無く、同じく `ViewLabels.label()` を呼ぶ（結論「F6 に作業なし」は別の理由で
成り立つ）。③タブへのドロップの明示化は **F4 で完了済み**（`targetKind: navigation.viewKind`）。
結果として F6 に残ったのは `addFavouritesTab()` とサイドパネルの導線だけだった。

### 2.3 ビュー本体

`TabContentPane` は**無改修**。`FileTableView` / `FileGridView` / `PreviewPane` / `StatusBar` /
サムネイル / 選択 / バンド選択 / インラインリネーム / カット時のゴースト表示、すべて
`FileListModel` に乗っているだけなので、行がノードである限り動く。

- 表示形式（リスト/グリッド）とソート順は通常タブと同じ last-write-wins の初期値を引く（決定 7）
- **ハートの行マーカーは出さない**（決定 6）。一覧の全員がお気に入りなので情報量がゼロ。
  両デリゲートのマーカーに `navController.viewKind !== ViewKind.Favourites` を足すだけ。
  「解除したのに行が残っている」という誤読の心配は、解除で行が消える（§4.4）ので生じない
- **空状態（0 件）の表示が要る。** 現在どのビューにも無く、お気に入り一覧は初期状態が必ず
  0 件なので、ここで初めて必要になる:
  - **文言のみ**。アイコン（ハート）は置かない（決定 9）
  - 左右中央・**上寄せ**。`horizontalCenter` + 上マージン。垂直中央にはしない
  - 置き場所は `TabContentPane` の `StackLayout` の上に重ねるのが良い。両ビューに 1 つずつ
    書くと二重管理になるし、ここに置けば将来「空のフォルダ」にも同じ仕組みを流用できる
    （v1 はお気に入り一覧のときだけ出す）

### 2.4 パンくず・ツールバー

| 部品 | お気に入り一覧での挙動 |
| --- | --- |
| パンくず | `Favourites` の 1 セグメントのみ。**クリック不可・ドロップ不可**（§3.2） |
| 戻る | 有効。一覧を開く前のフォルダへ帰る（`canGoBack` は `mBackStack` 依存なので自然に動く。§3.2） |
| 上へ | 無効（`canGoUp` は `breadcrumb.size() >= 2`。1 セグメントなので自然に false） |
| 更新 | 有効。`syncWithServer()` → お気に入りクエリ再実行 |
| 検索 | **お気に入りの中を名前で絞り込む**（決定 2）。同じ filter に `byName` を足すだけ（§3.5） |
| ステータスバー | 件数・選択件数・表示切替・プレビュー切替、すべて無改修で動く |

フォルダを開いた後（＝通常タブに戻った後）のパンくずは**従来どおりルートから**。
`resolveCurrentPath()` が実ノードの祖先チェーンを返すので、これは何もしなくてよい。

---

## 3. C++ 設計

### 3.1 ロケーション値型に種別を足す（SPECIAL_VIEWS §6-1 の前倒し分）

```cpp
// src/core/ViewKind.h（新規）
enum class ViewKind { CloudDrive, Favourites };
```

`FolderNavigationService::Location` / `CurrentLocation` に `ViewKind kind = CloudDrive` を追加。
`Location` は `{kind, isRoot, handle}` の 3 値になり、`kind == Favourites` のとき残り 2 つは
無意味になる —— `isRoot == true` が `handle` を無意味にするのと同型のセンチネル
（`IMegaClient.h` 冒頭の規約 1）。これだけで**バックスタックが種別を運ぶ**ようになり、
要件の中核が満たされる。

サービスに増えるメソッドは 1 つ:

```cpp
// 現在地を back-stack に積んでお気に入り一覧へ。既にお気に入り一覧なら何もしない（§3.2）。
void openFavourites(SortOrder, std::function<void(Result<std::vector<FileEntry>>)> onDone);
```

分岐が要る既存メソッドは 3 つだけ:

| メソッド | `kind == Favourites` のとき |
| --- | --- |
| `goBack` | **pop した Location の** `kind` で fetch 先を選ぶ（`listFavourites` / `getChildren` / `getRootChildren`） |
| `refreshCurrent` | `listFavourites` |
| `resolveCurrentPath` | SDK を呼ばず、合成セグメント 1 つを返す（§3.3） |

`runAndCommit()` の「network を撃つ → 成功時だけ commit → onDone」構造はそのまま使えるので、
どれも network 側の lambda を差し替えるだけで済む。`canGoBack` / `resetToRoot` /
`currentLocation` / `listChildrenOf` / `syncWithServer` / `openRoot` は無改修。

**なぜ `FolderNavigationController` 側でなくサービス側か**: バックスタックがサービスにあるから。
コントローラに `mViewKind` を持たせると、goBack でスタックから戻ってきたロケーションの種別を
コントローラが知る術がなく、結局同じ情報を二重管理することになる。逆に言えば、
**「戻るで帰る」を要件から外せばこの節はまるごと消え、種別はコントローラ内で完結する。**
そうしないと決めたので、以下はその前提。

**F1 実装時に確定した 3 点（2026-08-09）**:

- QML 側の名前は `src/qml/ViewKindEnum.h`（`QML_NAMED_ELEMENT(ViewKind)` + `QML_UNCREATABLE` +
  unscoped `enum Kind` + `Q_ENUM`）。`src/core` は Qt 非依存なので `Q_ENUM` を enum 本体の隣に
  置けない。値の一致は `static_assert` 2 本で担保。QML からは `ViewKind.Favourites` と読む
- `FolderNavigationController::viewKind` は**サービスではなく `mBreadcrumb.last()` から導出**し、
  NOTIFY は既存 4 プロパティ（`canGoUp`/`currentFolderName`/`atRoot`/`currentHandle`）と同じ
  `breadcrumbChanged` を共有する。種別と一覧内容が必ず同じタイミングで切り替わり、emit 箇所を
  手で管理せずに済む。**代償として `PathSegment::kind` は §3.3 のクリック抑止用ではなく F1 時点で
  必須**になり、`refreshBreadcrumb()` の `QVariantMap` にも `"kind"` キーが 1 つ増える
- `kind` の位置は `Location` / `CurrentLocation` では**先頭**、`PathSegment` では**末尾**。前者は
  `enum class` が `bool` から暗黙変換されないので既存の位置指定初期化 2 箇所が必ずコンパイル
  エラーになる（silent に壊れない）。後者を末尾にしたのは、テストの集約初期化 `{"", 0, true}` を
  無改修で通すため

### 3.2 履歴の意味論

タブは「お気に入り専用」にはならず、履歴の途中で種別が切り替わる。トレース:

```
Cloud Drive/写真                       stack=[]                          current={CloudDrive,/写真}
  ↓ サイドパネルの Favourites をクリック
お気に入り一覧                          stack=[{CloudDrive,/写真}]         current={Favourites}
  ↓ 一覧の中の 旅行/ をダブルクリック
Cloud Drive/旅行                       stack=[{CloudDrive,/写真},         current={CloudDrive,/旅行}
                                              {Favourites}]
  ↓ 戻る                                                                  ← 要件の中核
お気に入り一覧                          stack=[{CloudDrive,/写真}]         current={Favourites}
  ↓ 戻る
Cloud Drive/写真                       stack=[]                          current={CloudDrive,/写真}
```

決めておくべき端の挙動が 4 つ:

- **既にお気に入り一覧のときに Favourites をもう一度クリック**したら、積まずに何もしない
  （または更新扱い）。素通しにすると `[…, {Favourites}]` が積み上がり、戻るを連打すると
  同じ画面が何度も出る。`openFavourites()` の先頭 1 行のガード。
  **F5 で「更新扱い」に確定（2026-08-11）**: push せず `listFavourites` を撃って `onDone` を
  呼ぶ。完全な no-op にすると fire-and-forget の `onDone` が握り潰される経路が 1 つ増える
- **「上へ」は戻らない。** 一覧から `旅行/` を開いた後の「上へ」は**実の親フォルダ**へ行く
  （パンくずが実ノードの祖先チェーンだから）。お気に入り一覧に帰るのは「戻る」だけ。
  Explorer の検索結果と同じ非対称で、意図どおり
- **戻るのたびに全ドライブ再検索が走る。** スタックに載った `{Favourites}` はハンドルではなく
  クエリなので、pop したら引き直すしかない（§5.1 の同期コストがそのまま乗る）。
  緩和するならコントローラに直近の一覧をキャッシュし、pop 時はそれを即出ししてから裏で
  引き直す。v1 は素直に引き直しでよいが、§5.1 の計測次第でここが最初の調整点になる
- **ログアウト**は `resetToRoot()` がスタックごと捨てるので、種別込みで自然に消える

パンくず・タブタイトル・戻る/上への活性は、いずれも `applyResult()` → `refreshBreadcrumb()` の
既存経路で自動的に追従する（§3.3 の合成セグメントが `kind` で切り替わるだけ）。
**QML 側にこの節ぶんの改修は無い。**

### 3.3 パンくずの合成セグメント

`resolveCurrentPath()` は `mCurrent.kind == Favourites` のとき SDK を呼ばず、

```
PathSegment{ name: "", handle: 0, isRoot: false, kind: Favourites }
```

を 1 つだけ返す。これで `atRoot()`、`canGoUp()`、`currentHandle()` の 3 つが**無改修で正しい
答えを返す**。

**F5 実装時の訂正（2026-08-11）**: `name` は当初 `"Favourites"` と書いていたが、**空にした**。
置き場所である `FolderNavigationService` は `src/core` = Qt 非依存層で `qsTr()` が使えず、
ゴミ箱・アルバム・最近の更新まで含めた 4 つの UI 文字列が翻訳不能なまま core に固定される。
root のラベル（`qsTr("Cloud Drive")`）が既に QML 側にある前例にも反し、「合成ロケーションの
ラベルはどこか」が 2 層に割れる。代わりに `qml/ViewLabels.qml`（シングルトン）の
`label(kind, isRoot, name)` が全ロケーションのラベルを持ち、`Breadcrumb.qml` と `TabStrip.qml`
がそれを呼ぶ。名前を持つロケーション（フォルダ、将来のアルバム）は C++ の `name` がそのまま
勝つので、**新しい特殊ビュー 1 種類 = `ViewLabels.qml` に 1 行**で済む。
タブ名は `TabsController` に `ViewKindRole`（`"kind"`）を足して渡す。

`PathSegment` に `kind` を足すのは、パンくず側で**クリックとドロップを止める**ため。
`Breadcrumb.qml` は各セグメントに `NodeDropArea` を置き、クリックで `navigateTo(handle, isRoot)`
を呼ぶ。`handle = 0` のまま放置すると「押しても何も起きない（か、kENoEnt でエラートースト）」
という挙動になる。**押せないことを明示する**ほうが正しい。

**F6 実装時の補足（2026-08-11）**: ドロップ側は F4 の `targetKind: modelData.kind` で既に済んで
いたので、F6 で足したのはクリック側の 1 条件だけ（`navigable` に `kind === CloudDrive` を AND）。
合成セグメントは 1 つしか無く既に最後尾なので**今日の挙動は何も変わらない**。それでも書くのは、
将来の複数セグメントな特殊ビューがクリック可能性を継承しないようにするため。

> ⚠ `currentHandle()` が 0、`atRoot()` が false になる点は SPECIAL_VIEWS §1.3 が指摘した
> 「偶然正しく落ちる」状態そのもの。`checkMove` が kENoEnt で弾くので結果的に安全だが、
> **保証ではない**。§4 の可否判定は `viewKind` を見て明示的に落とす。

### 3.4 一覧の取得

`IMegaClient` に 1 メソッド追加:

```cpp
// Cloud Drive 配下のお気に入りノードを再帰的に返す。search() と同じく同期・呼び出しスレッドで onDone。
virtual void listFavourites(SortOrder order,
                            const std::string& nameFilter,   // 空なら絞り込み無し
                            std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;
```

`MegaSdkClient` 側の実装は `search()` とほぼ同型:

```cpp
filter->byFavourite(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
filter->byLocationHandle(rootnode->getHandle());   // ゴミ箱・Vault・inshare を除外
if (!nameFilter.empty()) filter->byName(nameFilter.c_str());
mApi->search(filter.get(), toMegaOrder(order));
```

- `MegaApi::getFavourites()` は deprecated かつハンドルしか返さないので使わない（Phase 24b の
  ロードマップ記述どおり）
- `byLocationHandle(root)` の一行が**スコープの決定 4・5 をまとめて実装している**:
  ゴミ箱の中のお気に入りも、Vault / 共有フォルダ（inshare）のお気に入りも入らない。
  逆に将来これらを含めたくなったら `byLocation(SEARCH_TARGET_ALL)` になり、
  ゴミ箱だけを弾く除外ロジックを別途書くことになる（今回はやらない）
- ✅ **決着（2026-08-11、SDK ソースで静的に確認。実アカウント不要）**: `byName` 無しで全件返る。
  `NodeSearchFilter::isValidName()`（`third_party/sdk/src/nodemanager.cpp:71`）はパターンが空なら
  無条件 `true`、sqlite 側の述語（`third_party/sdk/src/db/sqlite.cpp:4605`）も name/description/tag
  の条件が 0 個なら `result = true`。`MegaApiImpl::search()` に「名前必須」のガードも無い。
  よって `nameFilter` が空のときは **`byName` を呼ばない**（`byName("")` でも同じ結果だが、
  「空文字は絞り込み無し」という意図がコードから読めない）
- 戻りは `nodeListToEntries()` を通るので `isFavourite` は自動で埋まる（全 true）

### 3.5 `SearchService` との関係

`SearchService` は「今いるフォルダ配下の再帰検索」なので、お気に入り一覧では意味が変わる。
検索欄は**お気に入りの中の絞り込み**にする（決定 2）ので、`FolderNavigationController::search()`
が `viewKind` で分岐し、Favourites のときは `SearchService` を迂回して
`listFavourites(order, query)` を呼ぶ。`mLastSearchQuery` / `mLastFolderEntries`
（検索クリアで復元される元一覧）の仕組みはそのまま使える。

`SearchService` 自体は無改修。分岐がコントローラに寄るので、サービスは
「フォルダ配下の検索」という自分の定義を保ったままでいられる。

---

## 4. アクション可否

### 4.1 可否マトリクス

| アクション | 一覧内 | 根拠 / 備考 |
| --- | --- | --- |
| ダブルクリック（フォルダ） | ✅ | 同一タブで `navigateTo`。バックスタックにお気に入り一覧が積まれる |
| ダブルクリック（ファイル） | ✅ | ダウンロード。通常タブと同じ（決定 8） |
| 中クリック（フォルダ） | ✅ | 新規タブ（通常タブとして開く） |
| ダウンロード | ✅ | |
| コピー（Ctrl+C / メニュー） | ✅ | 要件どおり |
| カット（Ctrl+X / メニュー） | ❌ | カットは遅延移動なので移動不可に含める（決定 1） |
| 貼り付け（Ctrl+V / メニュー） | ❌ | 貼り付け先フォルダが無い |
| リネーム（F2 / メニュー） | ✅ | 要件どおり |
| 削除（Delete / メニュー） | ❌ | 要件どおり。`ConfirmRubbishDialog` にも到達させない |
| 新しいフォルダ | ❌ | 作成先が無い |
| すべて選択 / 更新 | ✅ | |
| クイックアクセスにピン留め | ✅ | 対象は実フォルダなので問題なし |
| お気に入りの解除 | ✅ | 解除すると**一覧を引き直して行が消える**（§4.4） |
| ドラッグ開始 | ✅ | 止めてはいけない（§0-4） |
| ドロップ（移動） | ❌ | 出所・行き先の両方向 |
| ドロップ（Ctrl+コピー） | ✅ | 一覧内のフォルダ行へのコピーも可 |
| 外部（Explorer）からのアップロード Drop | ❌ | アップロード先フォルダが無い |

### 4.2 実装: 可否の問い合わせ口を 1 本にする

`MenuActionSpec` に `sites` と同型の `scopes`（`ViewKind` の集合）を足し、
`menuActionApplies()` に `scopeMatches()` を 1 つ増やす。`MenuContext` に `kind` を足す。
`FileListModel` は自分の `ViewKind` を知る必要があるので、`setEntries()` と同じ経路で注入する。
`MenuActions::forSite(site)` は `forSite(site, kind)` になる（`FolderPinMenu` は常に `CloudDrive`）。

**ここまでは安い。本体は次**: `MenuActionResolver.h` 冒頭が明言するとおり resolver の契約は
「メニューに何を並べるか」であって「その操作が許可されているか」ではない。以下は今どれも
resolver を通っていない:

| 経路 | 場所 | 対応 |
| --- | --- | --- |
| Delete → ゴミ箱 | `FileViewInput.handleKey` | `canPerform("moveToRubbish")` で早期 return |
| Ctrl+X | 同上 | `canPerform("cut")` |
| Ctrl+V | 同上 | `canPerform("paste")` |
| Ctrl+C | 同上 | 常に可。ガード不要だがクリップボード出所は §7.4 |
| F2 | 同上 | 常に可 |
| ドラッグ開始 | 各 delegate の `DragHandler` | **ガードしない**（§0-4） |
| ツールバー | `AddressToolBar` | 戻る/上へ/更新/検索のみ。可否は既存の `canGoBack` 等で足りる |

したがって `FolderNavigationController` に 1 メソッド:

```cpp
Q_INVOKABLE bool canPerform(const QString& actionId) const;   // resolver に viewKind + 選択状況を渡す
```

`ActionCatalog.isEnabled(actionId, ctx)` と形が揃うので、QML 側の読み口は 2 つのままで済む。

### 4.3 D&D の判定

`DragProxy` に 1 フィールド:

```qml
property int sourceKind: 0        // ViewKind。begin() で設定、finish()/cancel() で戻す
```

`canDropOn()` の先頭に 2 行:

```qml
// お気に入り一覧から出た移動は、どのドロップ先でも成立しない（Ctrl+コピーは通す）
if (root.sourceKind === ViewKind.Favourites && !root.copyMode)
    return false;
```

ターゲット側は 2 箇所:

- `FileViewDropArea.updateNodeDropTarget()` の「行に当たらなければ現在フォルダへ」フォールバックを、
  お気に入り一覧では**無効化**（`dropOnCurrentFolder = false` 固定）。フォルダ行へのコピーは残る
- `NodeDropArea` に `targetKind`（既定 `CloudDrive`）を足し、`CloudDrive` 以外は常に拒否。
  タブ／サイドパネルの Favourites 行がこれに当たる。SPECIAL_VIEWS §3.4 が言う「target を
  記述子に広げる」の**最小版**（種別 1 フィールドだけ足す。ハンドルの意味は変えない）

外部（`text/uri-list`）ドロップは `uploads.canUploadTo(handle=0)` が kENoEnt で落ちるが、
これも `targetKind` / `viewKind` で明示的に落とす。

### 4.4 お気に入り解除は一覧を引き直す（決定 6）

通常タブでは `FolderNavigationController::applyFavouriteChange()` が
`FileListModel::setFavourite()` で**その行だけ** `dataChanged` する（Phase 24a: `setEntries()` は
モデルリセットでスクロール位置が飛ぶため）。お気に入り一覧では**解除 = 一覧から消える**ので、
その場でフラグを書き換えるだけでは足りない。

`applyFavouriteChange()` が `viewKind` で分岐し、Favourites のときは
`refreshVisibleListing()` を呼ぶ。通常タブ側の 24a の挙動は**そのまま**。

```cpp
void FolderNavigationController::applyFavouriteChange(quint64 handle, bool favourite)
{
    if (viewKind() == ViewKind::Favourites)
    {
        refreshVisibleListing();   // 行が消える。検索中なら byName 付きで引き直る
        return;
    }
    /* 24a のまま: setFavourite() + mLastFolderEntries の同期 */
}
```

**当初は部分削除（`FileListModel::removeHandle()` + `beginRemoveRows`）を提案していたが、
採らない。** 24a が再取得を避けたのは「1 行の見た目が変わるだけなのにスクロール位置が飛ぶ」
のが割に合わないからで、こちらは**行が消えて以降の行が全部ずれる**以上、どのみち見た目は
大きく動く。引き換えに `FileListModel` の部分削除経路が要らなくなり、そこに必要だった
3 つの後片付け（選択集合、カーソル行、バンド選択のアンカー — `setEntries()` は全部やっている）
がまるごと不要になる。**新しい壊れ方を 1 つ作らずに済む**ほうを取る。

代償は 2 つ、どちらも受け入れる:

- ハート 1 回につき全ドライブ再検索（§5.1 の同期コスト）。頻度は低い（一覧に来て解除する、
  という明示操作）ので v1 は許容。遅ければ §3.2 のキャッシュ方針と同じ土俵で考える
- スクロール位置が先頭に戻る

---

## 5. 性能

### 5.1 一覧取得は同期・GUI スレッド

`MegaSdkClient::search()` は `mApi->search()` を直に呼び、`onDone` を**同じスタックで**叩く
（`IMegaClient.h` の規約 2、および `FolderNavigationService` が mutex を持たない根拠）。
お気に入り一覧はこれを**ドライブ全体**に撃つ。既存の検索も同じ構造だが、あちらは
ユーザーが Enter を押した明示操作で、こちらは**タブを開いた瞬間**に走る。

`FETCHNODES_PROGRESS_INVESTIGATION.md` の 60 万ノード級アカウントで所要が体感に乗るなら、選択肢は:

- **(A) そのまま同期**。`BusyState` は既に per-tab のスピナーを持っているが、同期呼び出しの
  間は UI ごと止まるのでスピナーは回らない。**まず計測してから決める**のが正しい順序
- **(B) ワーカースレッドへ**。`FolderNavigationService` の「mutex 不要」は
  getChildren/search が同期であることに寄りかかった設計なので、**ここだけ非同期にすると
  その前提が崩れる**。やるなら `IMegaClient` の規約コメントごと更新が要る
- **(C) キャッシュ**。お気に入りは滅多に変わらないので、タブ生成時に 1 回引いて以降は
  差分だけで維持する。ただし他デバイスからの変更に弱く、決定 6（解除で引き直す）とも噛み合わない

推奨は (A) で出して計測 → 遅ければ (C) → それでも駄目なら (B)。

### 5.2 再取得の頻度

`refreshVisibleListing()` は rename 成功後・ソート順変更後・**お気に入り解除後（決定 6）**・
戻るでお気に入り一覧に帰るたび（§3.2）に走る。お気に入り一覧ではそのどれもが全ドライブ
再検索になるので、**この画面は「引き直しの回数」が性能そのもの**。§5.1 の計測はこの 4 経路を
まとめて左右する。

rename 後だけは **その行の名前を書き換えるだけ**にする案も
あるが、名前が変わればソート位置も変わる（既存が再取得している理由）。v1 は再取得のままにし、
遅ければ後で考える、で良い。

### 5.3 他タブからの変更で古くなる

`refreshIfShowing(handle, isRoot)` はハンドル一致でファンアウトする。お気に入り一覧は
どのハンドルとも一致しないので、**別タブでゴミ箱に移したお気に入りが消えずに残る**。
これは実際に起きる（要件でお気に入り一覧からの削除を禁じた結果、削除は必ず別タブで行われる）。

対処は `TabsController::createTab()` のファンアウトを種別対応にする:

```cpp
tab.navigation->refreshIfAffectedBy(destination, destinationIsRoot);   // Favourites は常に true
```

ただし**素直にやるとリフレッシュ嵐**（1 ノード移動ごとに全ドライブ再検索）になる。
`dirty` フラグを立てておいて、そのタブがアクティブになった時に初めて引き直すのが妥当。

なお Phase 24a は**お気に入りトグル自体のクロスタブ伝播をしていない**（`nodesMoved` 相当の
シグナルが無い）ので、別タブでハートを付けてもお気に入り一覧には出てこない。
`FileMutationController` に `favouriteChanged(handle, favourite)` を足して同じファンアウトに
乗せるのが素直（この phase でやるべき。無いと「付けたのに出ない」が普通に起きる）。

---

## 6. 躓きどころチェックリスト

★は設計判断が要るもの。

1. ★**戻るの往復**。お気に入り一覧 → フォルダA → フォルダB →（戻る×2）→ お気に入り一覧。
   `mBackStack` に種別を持たせていれば自動。持たせないと詰む（§3.2）
2. **お気に入り一覧を開いている状態で Favourites を再クリック**すると `{Favourites}` が
   スタックに積み上がる。`openFavourites()` の先頭でガードする（§3.2）
3. **戻るでお気に入り一覧に帰るたびに全ドライブ再検索**。ハンドルではなくクエリを pop する
   ので避けようがない。キャッシュするなら §3.2 の方針（§5.1 の計測後）
4. ★**ドラッグ開始をガードしない**という反直感。移動禁止 = ドラッグ禁止ではない（§0-4）
5. **お気に入り解除は引き直し**（決定 6）。`FileListModel` の部分削除は作らないので、
   選択・カーソル・バンド選択の後片付けを新規に書く必要は無い。代わりに 1 回の解除が
   全ドライブ再検索になる（§4.4, §5.2）
6. `currentHandle() == 0` / `atRoot() == false` に依存するコードが 6 箇所ある
   （タブタイトル、`refreshIfShowing`、ツリー/ピンの現在地強調、ドロップ先指定、
   クリップボード出所、`TabsController::data()`）。**偶然の kENoEnt に頼らない**
7. クリップボードの出所 `(currentHandle, atRoot)` がお気に入り一覧では嘘になる。
   コピーの場合、出所は `canPasteInto`（カット専用の判定）と `nodesMoved` のファンアウトに
   しか使われないので**実害は無い**が、`(0, false)` を書き込むこと自体は明示的に決めておく
8. `MenuActions::forSite()` の呼び出し側（`FolderBackgroundMenu` / `FolderPinMenu`）が
   種別を渡すよう全部直す必要がある。渡し忘れると「お気に入り一覧の背景メニューに
   新しいフォルダ・貼り付けが出る」
9. `FileContextMenu.buildContext()` の `isRoot: false` は「選択行はルートではない」という
   コメント付きハードコード。お気に入り一覧でもこれは正しい（行は必ず実ノード）
10. 新規お気に入りタブは、最初のフェッチが返るまでパンくずが空 → タブタイトルも空。
    `addTabAt` と同じ順序（行挿入 → フェッチ）なので通常タブと同じ挙動だが、
    「Cloud Drive」フォールバックが効かないぶん**空文字のタブが一瞬見える**
11. `byName` 無しの `MegaSearchFilter` で全件返るか未検証（§3.4）
12. 空状態（0 件）の表示が既存ビューに無い。お気に入り一覧では初期状態が必ずそれ（§2.3）
13. お気に入りタブの永続化は未実装（タブ復元自体が無い）。将来入れるなら種別込みの表現が前提
    ——**バックスタックに種別が入った以上、永続化の単位も `Location` の列になる**
14. `MegaExplorerQmlTests` は `clipboardController` を提供できないので、
    デリゲート本体のテストは今回も書けない（24a と同じ制約）

---

## 7. 代替案と、採らなかった理由

### 7.1 画面の作り

| 案 | 内容 | 判定 |
| --- | --- | --- |
| **A（採用）** | `Location` に `ViewKind` を足し、`FolderNavigationService`/`Controller` が分岐 | 戻るの往復が自動で成立。QML 無改修 |
| B | `IViewController` を抽出して実装を分ける | QML はどのみち `var` 受けなので見返りが小さい。バックスタックの共有が逆に面倒になる |
| C | 独自コントローラ + 独自 QML | **要件と非両立**（戻るで帰れない）。加えて選択/プレビュー/ステータスバー/D&D を再配線 |
| D | 2 層ハイブリッド（`TabContentPane` を Loader 化） | 正しい方向だが、お気に入り一覧に**必要な部分がゼロ**。アルバム/最近の更新で入れる |

**一度検討して落とした変種**: 「戻るでお気に入り一覧に帰る」を要件から外す案。外すと種別は
`FolderNavigationController` 内で完結し、`FolderNavigationService` / `Location` /
`CurrentLocation` / `PathSegment` が**全部無改修**になる（§8 の手順 1 がまるごと消える）。
残る 5 ブロック（可否配線・D&D・行削除・取得・クロスタブ）はどれも変わらないので、
消えるのは 6 分の 1 だが、**消えるのが唯一 `src/core` に広く触るブロック**という点で
効果は見た目より大きい。それでも採らないのは、MEGA 公式アプリの挙動に合わせるため。
この値型はゴミ箱・アルバム・最近の更新でも要るので、前倒しで払う投資でもある。

### 7.2 「できない」の表現

要件は「ゴーストで表現、難しければトースト」。現状の仕組みは:

- 内部ドラッグ（`Drag.active` + 自前ゴースト）なので、**OS のドロップ不可カーソルは出ない**
- 各ドロップ先が `accepting` をローカルに計算し、**受け入れる時だけ**枠を光らせる。
  拒否は「何も光らない」という消極的表現

| 案 | 内容 | コスト |
| --- | --- | --- |
| **(a)** | 現状のまま（光らない = 不可） | ゼロ。既存の全操作と一貫 |
| **(b) 推奨** | `DragProxy` に `hoverAccepting` を持たせ、拒否中はゴーストに 🚫 バッジ／減光 | 中。ドロップ先は `NodeDropArea` と `FileViewDropArea` の 2 コンポーネントに集約済みなので**設定箇所は 2 つ** |
| (c) | ドロップ後にトースト | 小。ただし「握って運んで離すまで分からない」ので体験は最下位 |

(b) の罠: 隣接するドロップ先を跨ぐと `entered` と `exited` の順序が保証されない。
素朴に `exited` でクリアすると新しいターゲットの `entered` を打ち消す。
**「自分がセットした時だけクリアする」トークン方式**が要る。`copyMode` 変更時の
再評価（既存の `Connections`）と同じ経路でゴースト側も更新すること。

### 7.3 サイドパネルの Favourites 行へのドロップ

要件では不可。ただし Explorer 的には「ここへドロップ = お気に入りに追加」が自然で、
ゴミ箱行への「ドロップ = 削除」と同じ発想。**やるなら `NodeDropArea` の target を
本物の記述子（種別 + 任意のハンドル）に広げる必要がある**（SPECIAL_VIEWS §3.4）。
今回は `targetKind` で拒否するだけに留め、記述子化はゴミ箱/アルバムの phase に送る。

### 7.4 「場所」列 — 今回はスコープ外（決定 3）

フラットな横断一覧なので、`/photos/2024/IMG_1.jpg` と `/backup/2024/IMG_1.jpg` が
名前・サイズ・日付とも同じ 2 行として並びうる。Explorer の検索結果はパス列を持つ。

**この画面だけで解くのではなく、「ノードの場所を出す」共通機能を後で入れて解決する**という
判断（決定 3）。したがって Phase 24b では列も、それに代わるツールチップも作らない。
既存の全ドライブ検索が同じ問題を既に抱えている以上、片方だけ直すのは筋が悪い、という整理。

将来その共通機能を設計するときに効く材料だけ、ここに残しておく:

- `FileEntry` に親情報は無いが、`IMegaClient::getPath(handle)` が祖先チェーンを返す
  （パンくずが既に使っている）ので**取得手段はある**。ただし同期 API なので行数ぶん直列
- `FileTableView` は 3 列固定（Name/Date modified/Size）、幅はアプリ全体で 3 つの
  `Settings` に永続化、`columnWidthFor()` / `minColumnWidths` / ソートの
  列→キー対応まで 3 の前提。**4 列目は見た目より配線が重い**
- 列を足さない出し方なら、プレビューペイン（既に選択行 1 件を見ている）が最も安い置き場所

---

## 8. 実装順序

画面を作らないリファクタを先に 2 つ済ませる（SPECIAL_VIEWS §6 と同じ方針）。
各段階で全画面 `CloudDrive` のまま既存テストが緑を保てる。

1. **`ViewKind` の導入とロケーション値型化**。`FolderNavigationService::Location` /
   `CurrentLocation` / `PathSegment` に `kind` を足す。全画面 `CloudDrive` のままなので
   挙動は変わらない純粋なリファクタで、既存の `FolderNavigationServiceTest` が
   そのまま緑であることが完了条件になる
2. **可否の問い合わせ口の一本化**。resolver に `scopes` 軸、`MenuContext`/`FileListModel` に
   `kind`、`FolderNavigationController::canPerform()` を追加し、`FileViewInput.handleKey` の
   Delete / Ctrl+X / Ctrl+V をそこ経由にする。**全画面 `CloudDrive` のまま検証可能**
3. **D&D の出所/行き先に種別**。`DragProxy.sourceKind`、`NodeDropArea.targetKind`、
   `FileViewDropArea` のフォールバック抑止。ここまでで枠組み完成
4. **一覧の取得**。`IMegaClient::listFavourites()` + `MegaSdkClient` 実装 + `MockMegaClient`。
   `FolderNavigationService::openFavourites()`、`resolveCurrentPath` の合成セグメント
5. **画面を繋ぐ**。`TabsController::addFavouritesTab()`、`SpecialViewsSection.qml`、
   解除時の引き直し（§4.4）、ハートマーカーの抑止、
   `FileMutationController::favouriteChanged` のファンアウト
6. **仕上げ**。空状態（§2.3）、ゴースト拒否表現（§7.2-b）、性能計測（§5.1）

1〜3 を先にやるのは、後回しにすると同じ 6 箇所を二度触ることになるから。
またこの 3 つは**ゴミ箱・アルバム・最近の更新でもそのまま効く**共通投資。

---

## 9. 決定事項（2026-08-09 確定）

仕様の分岐点として挙げた 9 件は全て確定済み。以降この番号で参照する。

| # | 論点 | 決定 | 効いてくる場所 |
| --- | --- | --- | --- |
| 1 | カット（Ctrl+X） | **禁止**。カットは遅延移動なので「移動不可」に含める | §4.1, §4.2 |
| 2 | 検索欄 | **お気に入りの中を名前で絞り込む**。filter に `byName` を足すだけ | §2.4, §3.5 |
| 3 | 「場所」列 | **出さない。今回はスコープ外。** ノードの場所を出す共通機能を後で入れ、そちらで解決する | §7.4 |
| 4 | ゴミ箱内のお気に入り | **含めない** | §3.4 |
| 5 | 共有フォルダ（inshare）/ Vault | **含めない** | §3.4 |
| 6 | 行のハートマーカー | **表示しない。** 解除時は一覧を引き直す（部分削除は作らない） | §2.3, §4.4, §5.2 |
| 7 | ソート順・表示形式 | **通常タブと共有**（last-write-wins のまま） | §2.3 |
| 8 | ファイルのダブルクリック | **ダウンロードのまま** | §4.1 |
| 9 | 空状態 | **文言のみ**（アイコン無し）、左右中央・上寄せ | §2.3 |

決定 3 と 6 は、どちらも「この画面だけで解かない」方向に振れている点が共通している。
3 は共通機能へ先送り、6 は既存の再取得経路に寄せて新しい部分更新経路を作らない。
結果として **Phase 24b で新規に書く QML は `SpecialViewsSection.qml` と空状態の 2 つだけ**になる。

---

## 10. テスト方針

既存の資産にそのまま乗る。

| 対象 | テスト |
| --- | --- |
| resolver の `ViewKind` 軸 | `MenuActionResolverTest`（membership。画面を作る前に検証できる） |
| バックスタックの種別往復 | `FolderNavigationServiceTest`（お気に入り → フォルダ → 戻る） |
| 合成パンくず / `canGoUp` / タイトル | `FolderNavigationControllerTest` |
| 解除で引き直すこと（決定 6） | `FolderNavigationControllerTest`（Favourites では `setFavourite()` を呼ばず再取得する / 通常タブでは 24a のまま再取得しない、の両方向） |
| 検索が `listFavourites(query)` に落ちること | `FolderNavigationControllerTest`（`SearchService` を経由しない） |
| `listFavourites` の素通し | `FileOperationServiceTest` 相当 + `MockMegaClient` |
| クロスタブのファンアウト | `TabsControllerTest` |
| ゴースト/ドロップ判定 | `tst_DragProxy.qml`（`sourceKind` × `copyMode` の 4 通り）、`tst_FileViewDropArea.qml` |
| メニュー文言 | `tst_ActionCatalog.qml` |

`MockMegaClient.h` に `listFavourites` を足すのを忘れないこと（純粋仮想なので足さないと
全テストがビルドで落ちる — これは良い強制力）。

---

## 11. 作業フェーズ分割

§8 は「何を先にやるか」の順序、こちらは**1 セッション ＝ 1 フェーズ**に切った作業単位。
各フェーズを Plan mode で計画 → 承認 → 実行 → 次のセッション、で回す。
セッションを跨ぐので、**前提はこの仕様書に書いてあることだけ**という形に各フェーズを閉じてある。

### 共通ルール

- **読むのはその節だけ。** フェーズ冒頭で `ast-outline show docs/investigations/FAVOURITES_VIEW_SPEC.md
  '<該当節の見出し>'` で必要な節だけ引く。この仕様書は既に ~6k トークンあり、全文読みを毎回
  やると分割した意味が消える
- **完了条件は 3 つ共通**: ①該当テストが緑（`ctest --preset msvc-debug`）②既存テストが 1 つも
  赤くない ③`/W4` の新規警告ゼロ（`mcp__qtcreator__build` → `list_issues`）
- **F1〜F4 は挙動を変えない。** 全画面 `ViewKind::CloudDrive` のままなので、既存テストが
  そのまま緑であること自体が回帰テストになる。「画面がまだ無いのに検証できる」のがこの
  並びの狙い（SPECIAL_VIEWS §6 と同じ方針）
- **`docs/PROGRESS.md` への記録はフェーズごとに書かない。** 24b 全体で 1 エントリ（~100 行）。
  途中で「後から再導出できない判断」が出たら**この仕様書に追記**しておき、最後にまとめる
- `.qml` ファイルを `qt_add_qml_module` の `QML_FILES` に足したら**リコンフィグ必須**
  （F5・F7 が該当。`cmake --preset msvc-debug`）

### フェーズ一覧

| # | 名前 | 主な参照節 | 挙動変化 |
| --- | --- | --- | --- |
| F1 | `ViewKind` とロケーション値型 | §3.1 | 無し（純粋リファクタ） |
| F2 | アクション可否の一本化 — C++ | §4.1, §4.2 | 無し |
| F3 | アクション可否の一本化 — QML 経路 | §4.2 | 無し |
| F4 | D&D の出所・行き先に種別 | §4.3 | 無し |
| F5 | お気に入りの取得 | §3.2, §3.4, §3.5 | UI からは到達不能（テストのみ） |
| F6 | 導線とタブ — **初めて画面が出る** | §2.1, §2.2, §3.3 | 大 |
| F7 | 画面挙動の詰めと仕上げ | §2.3, §4.4, §5.3, §7.2 | 中 |

### F1 — `ViewKind` とロケーション値型

`src/core/ViewKind.h` を新設し、`FolderNavigationService::Location` / `CurrentLocation` /
`PathSegment` に `kind`（既定 `CloudDrive`）を足す。`FolderNavigationController` に
`viewKind` の `Q_PROPERTY` を通す（この時点では常に `CloudDrive`）。

- **やらない**: `openFavourites()`、fetch の分岐、QML 側の参照
- 完了条件: `FolderNavigationServiceTest` / `FolderNavigationControllerTest` が**無改修で緑**。
  1 行でもテストを直す必要が出たら、それは挙動を変えてしまったサイン

### F2 — アクション可否の一本化（C++）

`MenuActionSpec` に `scopes`、`MenuContext` に `kind`、`menuActionApplies()` に
`scopeMatches()`。`FileListModel` に `ViewKind` の注入口（`setEntries()` と同じ経路）。
`MenuActions::forSite(site)` → `forSite(site, kind)`。
`FolderNavigationController::canPerform(actionId)` を追加。

- 決定 1（カット禁止）と §4.1 のマトリクスを `defaultMenuActions()` の `scopes` に落とす。
  **この時点で Favourites 側の期待値もテストに書ける**（画面はまだ無くてよい）
- **やらない**: キー経路の付け替え（F3）
- 完了条件: `MenuActionResolverTest` に Favourites の membership ケース、
  `FileListModelTest` に `availableActions` の種別分岐

**F2 実装時に確定した 3 点（2026-08-09）**:

- `MenuActions::forSite()` の QML 呼び出し側 2 箇所は **F3 ではなく F2 で直した**。C++ 側で
  シグネチャを変えた時点で 1 引数呼び出しは実行時エラーになり、F2 単体で緑を保つには
  同時に直すか一時的な既定引数を置くしかない。後者は F3 で剥がす往復になる
- `canPerform(actionId)` は **FileSelection 文脈と `folderTargetContext(FolderBackground)` の
  OR**。ショートカットは「ファイルビューが開ける 2 つのメニューのどちらかの行の代替」
  という意味付けで、どの action がどの site に載るかの知識を QML 側に漏らさずに済む。
  ID 引き当ては `menuActionAllowed(actionId, ctx)` として resolver 側に置いた
- `FileListModel` への種別注入は `setEntries()` の引数ではなく `setViewKind()`。前者は
  テスト側の呼び出しが約 50 箇所ある一方、既定引数を付ければ §6-8 と同じ「渡し忘れが黙って
  CloudDrive」の罠を作る。コントローラ側は `publishViewKind()` 1 つを `refreshBreadcrumb()` の
  commit と `reset()` の 2 箇所から呼ぶ（＝ `mBreadcrumb` が変わる場所と同じ）

### F3 — アクション可否の一本化（QML 経路）

`FileViewInput.handleKey` の Delete / Ctrl+X / Ctrl+V を `canPerform()` 経由にする。
（`MenuActions.forSite()` の呼び出し側 2 箇所は F2 で済ませた —— 上記参照）

- **ここが 24b で一番「静かに壊れる」箇所**（§4.2 の表）。resolver に軸を足しただけでは
  キー経路は素通しのまま、という SPECIAL_VIEWS §2.3 の指摘そのもの
- **ドラッグ開始はガードしない**（§0-4）。触りたくなるが触らない
- 完了条件: `tst_FileViewInput.qml` に「`CloudDrive` では従来どおり全部通る」ケース。
  Favourites 側は F6 以降に実機で確認

**F3 実装時に確定した 2 点（2026-08-09）**:

- ガードは**分岐の中**に置き、弾いても `event.accepted = true` は立てる（§4.2 の表は「早期 return」と
  書いていたが、そうしない）。理由は 2 つ。①`canPerform("moveToRubbish")` / `("cut")` は**選択ゼロでも
  false** になる（`menuActionApplies` が `selection.total() == 0` で落とす）ので、素通しにすると
  CloudDrive で選択ゼロの Delete / Ctrl+X が `accepted` を立てなくなり、「挙動変化なし」でなくなる。
  今日そこは `confirm()` / `putOnClipboard()` が自分で bail したうえで true を立てている。
  ②分岐構造が変わらないので、Shift+Delete が Cut 分岐へ落ちない保証（`StandardKey.Cut` は Windows で
  Ctrl+X **かつ** Shift+Delete）が現状のまま残る
- **F2 をガードしない理由は「仕様がそう言っているから」ではない。** `beginRename()` は選択ゼロのとき
  カーソル行を選んでから始める設計なので、キー到達時点の選択で `canPerform("rename")` を引くと
  SingleOnly に落ちてこのフローが壊れる。Ctrl+C のほうは copy が両 `ViewKind` に載っている以上
  純粋に死にコードなので足さなかった（ゴミ箱など copy を禁じる画面が来たらその phase で足す）

### F4 — D&D の出所・行き先に種別

`DragProxy.sourceKind`（`begin()` で設定、`finish()`/`cancel()` で戻す）、
`canDropOn()` の先頭ガード、`NodeDropArea.targetKind`、
`FileViewDropArea` の「現在フォルダへ」フォールバック抑止。

- 完了条件: `tst_DragProxy.qml` に `sourceKind` × `copyMode` の 4 通り、
  `tst_NodeDropArea.qml` に `targetKind` の拒否、`tst_FileViewDropArea.qml` にフォールバック抑止

**F4 実装時に確定した 3 点（2026-08-09）**:

- **お気に入り一覧の中のフォルダ行は「通常のフォルダ」のまま。** §4.1 のマトリクスは「移動 Drop は
  両方向 ❌」と読めるが、そう実装すると*ツリーやピンの上の同じフォルダは移動を受けるのに一覧の上では
  受けない*という非対称が残る。行は実ノードなので、抑止するのは §4.3 が言うフォールバック
  （＝行に当たらないときの「現在フォルダへ」）だけとし、行への move / copy / 外部アップロードは
  素通しにした。禁じられるのは *(a)* 一覧から出る移動と *(b)* 一覧そのもの（`currentHandle == 0`）
  への投入の 2 つで、要件の「⇄ 移動不可・Ctrl+コピー可」はこの 2 つで満たされる
- **出所と行き先で判定の綴りを意図的に変えた。** 出所側 `DragProxy.canDropOn()` は
  `sourceKind === Favourites` で落とす（その画面の**ポリシー**なので、ゴミ箱やアルバムが
  黙って継承しないよう名指しする）。行き先側 `NodeDropArea.targetTakesDrops()` は
  `targetKind === CloudDrive` の default-deny（フォルダ一覧でない画面に**投入先が存在しない**という
  構造的事実なので、未知の種別は拒否が正しい）。同じ enum を見ていても既定の向きが逆になる
- **`NodeDropArea.targetKind` は仕様書の「既定 `CloudDrive`」ではなく `required`。** F2 が
  `setViewKind()` を選んだのと同じ §6-8 の罠を避けるため。隣の `targetHandle` / `targetIsRoot` も
  required で形が揃う。**ただし設定漏れはコンパイルではなく実行時**（デリゲート生成が null を返す）に
  出るので、F4 の検証には実機起動で 4 設置箇所すべてを描画させる手順が要った

### F5 — お気に入りの取得

`IMegaClient::listFavourites()` + `MegaSdkClient` 実装 + `MockMegaClient`。
`FolderNavigationService::openFavourites()` と `goBack` / `refreshCurrent` /
`resolveCurrentPath` の 3 分岐。`FolderNavigationController` の `openFavourites` /
`search` 分岐と合成パンくず。

- **最初に潰す**: `byName` 無しの `MegaSearchFilter` が全件返るか（§3.4 の ⚠）。
  ここで返らないと以降の設計が変わるので、実装の頭で確認する
- §3.2 の端の挙動 4 つ（再クリックのガード、上へは戻らない、pop ごとの再検索、ログアウト）を
  そのままテストケースにする
- **やらない**: UI からの到達手段。この時点では**テストからしか呼べない**のが正しい
- 完了条件: `FolderNavigationServiceTest` の種別往復、`FolderNavigationControllerTest` の
  合成パンくず / `canGoUp` / タイトル

### F6 — 導線とタブ（初めて画面が出る）

`TabsController::addFavouritesTab()`、`SpecialViewsSection.qml`（新規）、`SidePanel.qml` への
組み込みと区切り線の条件見直し、`Breadcrumb.qml` のクリック・ドロップ抑止。

- **`QML_FILES` に足すのでリコンフィグ必須**
- ここで初めて `ui-style` skill（`cycle` でビルド→起動→スクショ）が使える。
  ライト/ダーク両方で見る（`--theme light|dark`）
- 完了条件: `TabsControllerTest` にお気に入りタブ生成。実機で「開ける・タイトルが出る・
  戻るで帰る・上へが無効・ドロップが全部拒否される」を目視

### F7 — 画面挙動の詰めと仕上げ

解除時の引き直し（§4.4）、ハートマーカーの抑止（決定 6）、空状態（決定 9・§2.3）、
`FileMutationController::favouriteChanged` のクロスタブ・ファンアウト（§5.3）、
ゴーストの拒否表現（§7.2-b）、`listFavourites` の性能計測（§5.1）。

- **分量が多いので、Plan mode の時点で 2 つに割る判断をしてよい**。割るなら
  「解除・マーカー・空状態」／「ファンアウト・ゴースト・計測」
- ゴーストの拒否表現は §7.2 の罠（隣接ターゲットの `entered`/`exited` 順序）に注意。
  難航したら (a) の「光らない＝不可」に落として次に送ってよい
- 性能計測の結果が悪ければ、その場で直さず**この仕様書の §5.1 に数字を追記**して、
  対策は別フェーズに切る

---

## 12. この仕様書で確認していないこと

- `byName` 未設定の `MegaSearchFilter` の実挙動（§3.4）
- 大規模アカウントでの `listFavourites` 実測（§5.1）。この phase で唯一の性能リスク
- `MegaNode::CHANGE_TYPE_FAVOURITE` によるサーバー起点の更新。Phase 16（リアルタイム反映）の
  領分で、そちらが入ればお気に入り一覧の鮮度問題（§5.3）は根本的に解ける。
  **順序として Phase 16 を先にやる価値はある**が、依存ではない
- お気に入りフォルダの**中身**（フォルダをお気に入りにした場合、一覧にはそのフォルダ 1 行が
  出る。中身は出ない）で問題ないか — 要件から自明と判断した
- 決定 3 の「ノードの場所を出す共通機能」の中身。お気に入り一覧・全ドライブ検索の両方が
  客なので、24b とは別に設計する（§7.4 に材料だけ置いた）
