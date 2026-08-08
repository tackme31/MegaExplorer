# 特殊画面（ゴミ箱・お気に入り・アルバム・最近の更新）— 実装前調査

2026-08-09。個々の特殊画面の仕様ではなく、**「特殊画面という枠組み」を今のコードに載せられるか**
だけを見た机上調査。実装・計測は行っていない。

想定している仕様（依頼時点）:

- ゴミ箱 / お気に入り一覧 / アルバム / 最近更新したファイル、といった画面を**タブとして開ける**
- 左ペインには**固定行として常時表示**する
- 画面ごとに取れるアクションが違う（ゴミ箱: 復元可・コピー/DL 不可、お気に入り: 解除/コピー可・
  移動/削除不可、アルバム: 除外/コピー可・移動/削除不可、など）
- したがって**コンテキストメニューの出し分けと D&D の可否を決めるロジック**が要る
- 既存のテーブル/グリッド UI で足りない画面（アルバム、最近の更新）がありそう

---

## 結論（先に要約）

1. **「タブ＝1フォルダ」の前提は、C++ 側には構造として焼き付いている（`TabContext` と
   `TabsController` の生成経路）が、QML 側にはほとんど焼き付いていない。** QML は `navController`
   を `property var` でダックタイピング的に受け取っているだけなので、同じプロパティ/メソッドを
   持つ別クラスを差し込めば 19 ファイルは無改修で動く。ここが最大のレバレッジ点で、
   「大きな作り替えが要るか」への答えは **要らない（局所的な拡張で足りる）** side に寄る。
2. **ただし最大の躓きどころは、コンテキストメニュー側ではなく「メニューを通らないアクション経路」。**
   `MenuActionResolver` は設計上「メニューに何を並べるか」しか答えない契約であり、Delete キー・
   Ctrl+C/X/V・F2・ダブルクリック・ドラッグ開始・ツールバーはどれもそれを参照していない。
   resolver に画面種別の軸を足すだけでは、**ゴミ箱で Ctrl+C が効いたまま**になる（§2.3）。
   ここは「メニュー項目の membership」から「操作の可否」への**契約の格上げ**が要る。
3. **ロケーションの表現 `(handle, isRoot)` の 2 値が足りなくなる。** タブのタイトル、左ペインの
   現在地強調、`refreshIfShowing` のファンアウト、ドロップ先の識別、と 6 箇所に同じ 2 値が流れて
   おり、フォルダに紐づかない画面はそのどれにも乗らない。**`ViewKind` を含むロケーション値型を
   先に入れる**のが、後戻りを最小にする唯一の前倒し作業（§5-1）。
4. **D&D の判定は既に 1 点に集約されているので、拡張しやすい。** 6 つのドロップ先すべてが
   `DragProxy.canDropOn()` を呼ぶ形になっており、判定軸を足す場所は 1 つ。逆に**ドラッグ「元」を
   理由に拒否する仕組みだけが無い**（`DragProxy` が出所を持っていない）。ここは 1 フィールド追加
   で済む（§3.3）。
5. **UI の共通化は「ノード行を持つ画面」までで線を引くのが良い。** ゴミ箱とお気に入りは既存の
   テーブル/グリッドがそのまま使える。一方「最近の更新」は SDK の戻り値が
   `MegaRecentActionBucketList`（ユーザー×親フォルダ×6時間窓のバケット）であって
   `vector<FileEntry>` ですらなく（`RECENTLY_UPDATED_FILE_API.md`）、日付セクション表示も
   `TableView`/`GridView` には `section` 相当が無いので**別ビューを書くしかない**。
   → 無理に 1 ビューへ寄せず、**モデル層は共有・ビュー層は自由**の 2 層に割る（§5 案D）。

---

## 1. 現状: 「タブ＝1フォルダ」がどこまで効いているか

### 1.1 タブの実体は `TabContext`（固定 5 メンバの struct）

`src/qml/TabsController.h`。1 タブ = `FolderNavigationService` / `SearchService` /
`FolderNavigationController` / `FileMutationController` / `ThumbnailController` の 5 つ。生成は
composition root が渡す `std::function<TabContext()>` 1 本きりで、**種類の概念が無い**。
タブ追加の入口も `addTab()` と `addTabAt(handle, isRoot)` の 2 つだけ。

`TabsController::data()` の `TitleRole` は `navigation->currentFolderName()`、`AtRootRole` は
`navigation->atRoot()` を直読みしている。**特殊タブのタイトルの出どころが無い。**

### 1.2 事実上の「画面インターフェース」は `FolderNavigationController` の公開面

QML が実際に使っているのは以下だけ（`navController` を参照する QML は 19 ファイル）:

| 種別 | 使われているもの |
| --- | --- |
| プロパティ | `fileListModel` / `canGoBack` / `canGoUp` / `breadcrumb` / `currentFolderName` / `atRoot` / `currentHandle` / `busy` |
| メソッド | `openFolder` / `goBack` / `goUp` / `navigateTo` / `search` / `setSortOrder` / `refresh` / `refreshIfShowing` / `reset` |
| 併走 | `mutController`（rename/rubbish/createFolder/move/copy/paste + can* 系）、`thumbController.requestThumbnail` |

**これらは C++ の型として要求されていない。** QML 側は全部 `required property var` で受けている
ので、同じ名前を持つ別の QObject を差し込めば通る。つまり画面を増やすコストは「QML の書き換え」
ではなく「この面をどう満たすか」に集約されている。

### 1.3 `currentHandle` / `atRoot` は表示以外に 2 つの役割を持っている

見落としやすいので明記する。この 2 値は「今どこを見ているか」の表示だけでなく、

- **ドロップ先の指定**: `FileViewDropArea.performDrop()` は空きスペースへのドロップを
  `navController.currentHandle` 宛てにする。`TabStrip` の `NodeDropArea` も
  `targetHandle: tabButton.navigation.currentHandle` を渡す。
- **クリップボードの出所**: `FileViewInput.putOnClipboard()` が cut/copy に添えて記録する。

に使われている。特殊画面には対応する「現在のフォルダ」が無いので、ここに何を答えさせるかを
決めないといけない。今のまま `handle = 0, atRoot = false` を返せば `checkMove` が `kENoEnt` で
拒否するため**結果的には正しく落ちる**が、それは偶然であって保証ではない。明示フラグが要る。

---

## 2. アクション可否のロジック — 今どうなっていて、何が足りないか

### 2.1 現状は 3 分割

| 層 | 担当 | 場所 |
| --- | --- | --- |
| どのアクションが該当するか | site × target × arity で絞り込み | `src/core/MenuActionResolver.cpp` |
| 誰がそれを聞くか | 選択依存は `FileListModel::availableActions`、固定ターゲットは `MenuActions` シングルトン | `src/qml/` |
| 文言・グレーアウト・実行 | `ActionCatalog.qml` の `label/icon/enabled/trigger` | `qml/ActionCatalog.qml` |

C++ が構造を、QML が文言と実行を持つという分担が全体で徹底されている。

### 2.2 足りない軸は 1 本だけ ＝ 画面種別

`MenuActionSpec` は `{action, sites, target, arity}`、`MenuContext` は `{site, selection}`。
ここに `scope`（`CloudDrive` / `Rubbish` / `Favorites` / `Album` / …）を 1 本足し、
`menuActionApplies()` に `scopeMatches()` を 1 つ増やすだけで「ゴミ箱ではコピーを出さない」は
表現できる。`FileListModel` が自分の scope を知っている必要があるが、これは `setEntries()` と
同じ経路で注入できる。`MenuActions::forSite(site)` は `forSite(site, scope)` になる。

**この部分は本当に安い。** 既に `MenuActionResolverTest` と `tst_ActionCatalog.qml` があるので、
画面を 1 つも作らないまま（全画面が `CloudDrive` のまま）テストで検証できる。

新しいアクション（`Restore` / `Unfavorite` / `RemoveFromAlbum` / `RevealInFolder`）を足すのも
enum + `menuActionId()` + `ActionCatalog.entries` への追記で、既存の型は変えなくて済む。

### 2.3 ★最大の躓きどころ: メニューを通らない経路が resolver を見ていない

`MenuActionResolver.h` の冒頭コメントが明言しているとおり、この resolver の契約は
**「right-click-menu にどの項目を並べるか」**であって「その操作が許可されているか」ではない。
実際、同じ操作に到達する以下の経路はどれも resolver を参照していない:

| 経路 | 場所 | resolver 経由か |
| --- | --- | --- |
| Delete キー → ゴミ箱へ | `FileViewInput.handleKey` | ✗ |
| Ctrl+C / Ctrl+X / Ctrl+V | 同上 | ✗ |
| F2 → リネーム | 同上 | ✗ |
| ダブルクリック → 開く/DL | `TabContentPane.activate()` | ✗ |
| 中クリック → 新規タブ | 各ビューの delegate | ✗ |
| ドラッグ開始 | 各 delegate の `DragHandler` | ✗ |
| 戻る/上へ/更新/検索 | `AddressToolBar`（`tabsController.currentNavigation` 直結） | ✗ |
| 表示形式・プレビュー切替 | `StatusBar`（`currentPane` 直結） | ✗ |

つまり **resolver に scope を足しただけでは「お気に入り一覧で Delete キーが効く」「ゴミ箱で
Ctrl+C が効く」が残る**。しかもこれらは静かに壊れる（メニューには出ないので気付きにくい）。

対処は「resolver に何かを足す」ではなく、**可否の問い合わせ口を 1 つ作って、メニューと
キーハンドラとドラッグ開始の 3 者に同じ答えを読ませる**こと。幸い `ActionCatalog` には既に
`isEnabled(actionId, ctx)` があるので、形は揃っている。足りないのは「ビューが自分の scope に
対する可否を 1 メソッドで答える」入口（例: `navController.can("copy")`）と、
`FileViewInput.handleKey` の各分岐がそれを通ること。

---

## 3. D&D の可否ロジック

### 3.1 ドロップ先は 6 箇所、コンポーネントは 2 つ

- `NodeDropArea.qml`（単一ノードが対象）: パンくずのセグメント / フォルダツリーの行 /
  クイックアクセスのピン / タブ
- `FileViewDropArea.qml`（行インデックスを解決する）: テーブルビュー / グリッドビュー

前者は `(targetHandle, targetIsRoot)` を site から渡される、という一点だけが違う設計。

### 3.2 判定は既に 1 点に集約されている（good news）

6 箇所すべてが `dragProxy.canDropOn(handle, isRoot)` を呼び、その中で copy/move を分岐して
`sourceMutations.canDropHandlesOn()` / `canCopyEntriesOn()` → `IMegaClient::checkMove()` に落ちる。
**判定軸を足す場所は `DragProxy.canDropOn()` 1 箇所**で済む。

制約として `checkMove` はホバー中マウス移動ごとに呼ばれる**同期・インメモリ**判定である
（`IMegaClient.h` の規約）。特殊画面用に足す判定も同期で答えられる必要がある。アルバム所属の
判定などが非同期しか無い場合は、ドラッグ開始時に事前解決して `DragProxy` に載せる必要がある。

### 3.3 足りないのは「ドラッグ元による拒否」

`DragProxy` が持つのは `entries`（`{handle, name, isFolder}`）と `sourceMutations` だけで、
**どの画面から始まったドラッグかを持っていない**。しかし要件側は出所で挙動が変わる:

- ゴミ箱からタブ/左ペインへのドロップ = **復元**（通常の move とは意味が違う）
- お気に入り一覧のアイテムは移動禁止 = **ドラッグそのものを始めさせない、あるいは全ドロップ先で拒否**
- アルバムのアイテムも同様

対処は `DragProxy` に `sourceScope`（もしくはロケーション値型そのもの）を 1 つ足し、
`begin()` で設定、`canDropOn()` で見る。6 つのドロップ先は無改修。

### 3.4 ★`NodeDropArea` の「ターゲット＝フォルダ」前提は、いずれ広げる必要がある

`(targetHandle, targetIsRoot)` はフォルダしか表現できない。要件に含まれる

- **ゴミ箱タブ / 左ペインのゴミ箱行へのドロップ = 削除**
- **アルバムタブへのドロップ = アルバムに追加**

はこの 2 値では書けない。`NodeDropArea` の target を**記述子**（種別 + 任意のハンドル）に広げる
のが唯一の道で、これが今回「既存の抽象を実際に広げないといけない」ほぼ唯一の箇所。逆に言えば、
広げる必要があるのはここと §1.3 の 2 箇所だけ。

なお、特殊タブが**ドロップを一切受けない**方針なら当面は不要（§1.3 のとおり偶然拒否される）。
ただし「ゴミ箱へのドロップで削除」は Explorer 的にはほぼ必須なので、先送りしても戻ってくる。

---

## 4. データモデル側

### 4.1 `FileEntry` に足りないもの / 足りているもの

`{name, handle, sizeBytes, isFolder, modificationTime, hasThumbnail}`。親ハンドルも
お気に入りフラグも「ゴミ箱内かどうか」も無い。ただし:

- **「ファイルの場所へ移動」は `FileEntry` を触らずに実装できる。** `IMegaClient::getPath(handle)`
  が祖先チェーンを返す（パンくずが既に使っている）ので、そこから親を取れば良い。
- お気に入りフラグ等が要るのは「その画面の行に表示する」場合だけ。一覧自体が
  「お気に入りのものだけ」なら不要。

→ `FileEntry` の拡張は**当面は不要**と見てよい。必要になったときも 1 フィールド追加で、
`operator==` を手書きしている点にだけ注意（C++17 なので `<=>` は使えない）。

### 4.2 `FileListModel` は既に十分に汎用

`data()` が `index.column()` を無視して role だけで分岐しているため、テーブルとグリッドが同一
インスタンスを共有できている。**行がノード形状でありさえすれば、どの特殊画面でもそのまま使える。**
`pruneSelection()` がハンドルの一意性に依存している点だけ、同一ノードが 2 行に出る画面
（アルバム内の重複など）を作るなら要検討。

### 4.3 既存 UI に載らないのはどれか

| 画面 | 既存テーブル/グリッドで足りるか | 理由 |
| --- | --- | --- |
| ゴミ箱 | **足りる** | 行はノード。ゴミ箱内フォルダへ潜る操作もあるので、むしろ通常画面の面がほぼ丸ごと要る |
| お気に入り一覧 | **足りる** | フラットなノード一覧 |
| アルバム（中身） | ほぼ足りる | サムネイル前提なのでグリッド寄り。タイル寸法など見た目の調整は要る |
| アルバム（一覧） | **足りない** | 行がノードではない（アルバムそのもの）。`FileListModel` を持たない画面になる |
| 最近の更新 | **足りない** | ①日付セクションが要るが `TableView`/`GridView` に `section` 相当が無い（`ListView` にしかない）②SDK の戻り値が `MegaRecentActionBucketList` でバケット構造（`RECENTLY_UPDATED_FILE_API.md`）。`vector<FileEntry>` に潰すと「誰が・どのフォルダで」の情報が落ちる |

そして `TabContentPane.qml` は `StackLayout { FileTableView; FileGridView }` を**ハードコード**して
`currentIndex: viewMode` で切り替えている。ここが「タブごとにビュー構成を変えられない」唯一の
実質的な障害。`Loader` 化すれば解ける。

### 4.4 共有シャーシ側の null 安全化

`FileListModel` を持たない画面（アルバム一覧など）を許すなら、以下が `?.` / `??` の追加で済む
かどうかを確認する必要がある:

- `PreviewPane`（`currentPane.navController.fileListModel.selectedEntry()` を読む）
- `StatusBar`（`currentPane?.navController?.fileListModel` は既に `?.` 済み、`viewMode` は素通し）
- `FolderTreePanel` / `QuickAccessSection` の現在地強調（`navController.currentHandle` 比較）

---

## 5. 設計の選択肢

### 案A — 既存クラスにモードを足す

`FolderNavigationController` に `ViewKind` を持たせ、resolver に scope 軸を足す。5 メンバの
`TabContext` はそのまま。

- ○ 最小。ゴミ箱・お気に入りだけなら本当にこれで終わる
- ✗ `FolderNavigationController` が「フォルダ移動」と「特殊一覧」の両方を抱えて肥大する
- ✗ ビュー構成を画面ごとに変えられないので、アルバム/最近の更新で行き止まる

### 案B — C++ で画面インターフェースを抽出

`IViewController` を定義し `FolderNavigationController` がその実装になる。`TabContext` は
`{view, ...}` に。QML はダックタイピングなので無改修で通る。

- ○ 筋は通る。特殊画面ごとにクラスが分かれる
- ✗ 「アルバム一覧」までフォルダ形状のインターフェースに合わせさせる圧力がかかる
- ✗ QML 側はどのみち `var` 受けなので、C++ の抽象基底クラスの見返りが小さい（テスト以外）

### 案C — タブシステムだけ流用して全部独自実装

`TabsController` は不透明なペイロード + QML コンポーネント URL だけ持ち、各特殊画面は独自の
コントローラと独自の QML を持つ。

- ○ 最大の自由度。アルバム/最近の更新に無理が無い
- ✗ 選択モデル、プレビューペイン、ステータスバー、D&D 判定、左ペイン強調、クリップボードを
  画面ごとに再配線または重複させることになる。ゴミ箱・お気に入りにとっては完全に過剰

### 案D（推奨）— 2 層ハイブリッド

線は **「行がノードかどうか」** で引く。

- **共通層（ノード行を持つ画面）**: `FileListModel` + 選択 + プレビュー + ステータスバー +
  ドラッグ元 + アクション可否解決。ゴミ箱 / お気に入り / アルバム中身 が該当。
  **可否は 1 箇所（拡張した resolver）で宣言し、メニュー・キー・ドラッグ開始の 3 経路が同じ答えを読む。**
- **自由層（画面ごと）**: 一覧の取得元と、ビューコンポーネントそのもの。`TabContentPane` を
  種別キーの `Loader` にして、通常画面は今の 2 ビュー、最近の更新は日付セクション付きの
  `ListView`、アルバム一覧は独自 UI、と差し替える。共通層を持たない画面は
  `fileListModel` を提供せず、共有シャーシ側が null 安全に無効化する。

案A の安さ（ゴミ箱・お気に入り）と案C の自由度（アルバム・最近の更新）を、境界 1 本で両取りする形。

| | A | B | C | D |
| --- | --- | --- | --- | --- |
| ゴミ箱/お気に入りの実装コスト | 小 | 中 | 大 | 小 |
| アルバム/最近の更新の実現性 | ✗ | △ | ○ | ○ |
| 既存コードへの侵襲 | 小 | 中 | 中 | 小〜中 |
| 共通機能の重複 | 無 | 無 | 大 | 無 |

---

## 6. 推奨する着手順

`CLAUDE.md` の「リリース前なので既存コードの作り替えは可」を前提に、**画面を 1 つも作らない
リファクタを 2 つ先に済ませる**のが後戻りを最小にする。

1. **ロケーション値型の導入。** `(handle, isRoot)` が流れている 6 箇所（タブタイトル、
   `refreshIfShowing` のファンアウト、ツリー/ピンの現在地強調、ドロップ先指定、クリップボードの
   出所、`TabsController::data()`）を `{kind, handle, isRoot}` 相当に統一する。純粋なリファクタで、
   全画面 `kind = CloudDrive` のまま既存テストが通る状態を保てる。**後回しにすると同じ 6 箇所を
   二度触ることになる。**
2. **可否の問い合わせ口を 1 本化。** resolver に scope 軸を足し、同時に §2.3 の
   「メニュー以外の経路」をその答え経由に付け替える。ここも全画面 `CloudDrive` のまま検証可能。
3. **`DragProxy` に出所を、`NodeDropArea` の target を記述子に。** ここまでで枠組みは完成。
4. **`TabContentPane` を種別キーの `Loader` 化**、`TabsController` に種別付きのタブ生成入口
   （`addSpecialTab(kind)`）と、種別をパラメータに取るファクトリを追加。
5. **最初の実画面はゴミ箱。** フォルダ形状に最も近く（ゴミ箱内のフォルダに潜れる）、かつ
   「復元」「ドロップで削除」という特殊アクションの両方を最初に踏める。次にお気に入り、
   最後にアルバム/最近の更新（自由層が要る 2 つ）。

---

## 7. 躓きどころチェックリスト

実装時に明示的に潰すべき点。★は設計判断が要るもの。

1. ★キーボード/ダブルクリック/ドラッグ開始/ツールバーが resolver を通っていない（§2.3）
2. ★`(handle, isRoot)` がロケーション表現として足りない — 6 箇所に波及（§1.3, §6-1）
3. ★`NodeDropArea` の target がフォルダ固定 — ゴミ箱/アルバムタブへのドロップで破綻（§3.4）
4. `DragProxy` に出所が無く、ドラッグ元を理由に拒否できない（§3.3）
5. `TabsController::data()` の `TitleRole` が `navigation->currentFolderName()` 直読み（§1.1）
6. `TabContentPane` が 2 ビュー固定で、日付セクション表示が載らない（§4.3）
7. `refreshIfShowing(handle, isRoot)` のファンアウトがハンドル基準 — ゴミ箱画面が興味を持つのは
   「ゴミ箱へ移動というイベントが起きたか」であってハンドル一致ではない
8. `PreviewPane` / `StatusBar` / 左ペイン強調が `fileListModel` と `currentHandle` の存在前提（§4.4）
9. 「最近の更新」の SDK 戻り値がバケット構造で `vector<FileEntry>` に素直に落ちない（§4.3）
10. `checkMove` の同期性を新しい判定でも維持する必要がある（§3.2）
11. タブの永続化/復元はまだ実装されていない。将来入れるなら種別を含む表現が前提になるので、
    §6-1 の値型はそこにも効く
12. 左ペインの固定行は `SidePanel.qml`（ColumnLayout）にセクションを足すだけで構造的には済むが、
    ドロップ先にするなら §3.4 と同じ記述子が要る

## 8. この調査で確認していないこと

- 各特殊画面を実際に取得する SDK API（ゴミ箱ルート、お気に入りフラグ、アルバム API）。
  「最近の更新」だけは `RECENTLY_UPDATED_FILE_API.md` に既出。
- アルバムに MEGA 側の書き込み API（追加/除外）があるか、あるならその同期/非同期の別。
  §3.2 の「ホバー判定は同期でなければならない」制約に直接効く。
- 特殊画面での検索（`SearchService`）と並び替え（`SortOrder`）をどう扱うか。現状の検索は
  「開いているフォルダ配下の再帰検索」なので、フォルダに紐づかない画面では意味が変わる。
- 特殊画面をパンくずにどう出すか（出さないのか、種別名 1 段だけ出すのか）。
