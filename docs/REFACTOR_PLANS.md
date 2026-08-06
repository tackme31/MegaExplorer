# REFACTOR_PLANS.md — リリース前コード整理の計画

Phase 15/16 に入る前の一斉整理。**一気にレビューしない**ためのスコープ分割と実施順を定める。

前提: CLAUDE.md「Pre-release: refactoring existing code during planning is allowed」— 外部 API /
データ互換性の制約はないので、製品挙動を変えない限り既存設計の作り直しを優先してよい。ただし
**製品レベルの挙動・スコープを変える変更は事前に合意を取る**（この文書の各スコープも同じ扱い）。

このファイルの位置づけ:

- 何を・どの順で見るかの**計画**。個々の指摘と修正結果は各スコープ実施時にこのファイルの
  「実施ログ」節へ追記する（`docs/DESIGN_IMPROVEMENT.md` が S* 段階でやっているのと同じ形）。
- C++/構造の話。見た目・余白・配色は `docs/DESIGN_IMPROVEMENT.md` の担当で、ここには書かない。

---

## 0. 現状の測定値（2026-08-06 時点）

判断の根拠なので先に置く。

| 領域 | 規模 |
| --- | --- |
| C++ (`src/` + `main.cpp`) | 10,089 行 / 88 ファイル |
| QML (`qml/`) | 7,176 行 / 28 ファイル |
| テスト (`tests/`) | 7,348 行 / 25 ファイル |

上位ファイル（肥大の集中点）:

| ファイル | 行数 | 備考 |
| --- | --- | --- |
| `src/mega/MegaSdkClient.cpp` | 905 | SDK アダプタ。IMegaClient 全メソッドの実装が 1 ファイル |
| `src/qml/FolderNavigationController.{cpp,h}` | 822 + 384 | **最大の集中点**。ヘッダだけで 384 行 |
| `qml/views/FileTableView.qml` | 1,060 | |
| `qml/Main.qml` | 1,055 | ダイアログ 5 種 + header/footer Component + SplitView |
| `qml/views/FileGridView.qml` | 766 | FileTableView と大量に重複 |
| `src/qml/FileListModel.cpp` | 472 | 選択モデル + バンド選択セッション + ロール |
| `qml/components/TabStrip.qml` | 537 | タブ並べ替え + spring-loaded drop + スピナー |
| `tests/FolderNavigationControllerTest.cpp` | 1,152 | 最大のテスト。対象の肥大がそのまま出ている |

---

## 1. スコープ一覧（推奨 7 分割）

各スコープは **1 セッション 1 スコープ**で完結する粒度に切ってある。走査時に実際に見つかった
「種」を各スコープに付けてあるので、レビューはゼロから探すのではなく**種の検証から始める**こと。
種は現時点の推測を含む。断定せず、必ず現物を読んで確認する。

### R1 — セキュリティ / ライセンス遵守

**対象**: `src/qml/DownloadController.cpp`, `src/core/DownloadService.cpp`,
`src/core/UploadService.cpp`, `src/platform/WindowsSessionStore.cpp`, `src/app/Logging.cpp`,
`CMakeLists.txt` の `install()`。

**観点**: サーバ由来の文字列がローカルパス/プロセス起動に到達する経路、機微情報のログ出力、
セッション秘密の保存強度、配布物のライセンス同梱。

**種**:

- `DownloadController::computeDestinationPath` が `dir + "/" + fileName` を無検証で連結している。
  `fileName` は **MEGA のノード名（サーバ由来 = 攻撃者制御しうる）**。`..\` や絶対パス片が
  含まれた場合に Downloads 外へ書けないか要検証。SDK 側 (`LocalPath`) が正規化してくれる可能性は
  あるが、**依存先の副作用に頼っている状態は明文化されていない**。
- `DownloadController::openFile` が `QDesktopServices::openUrl` でダウンロード物を開く。
  クラウド上の `.exe` / `.lnk` / `.scr` をダブルクリックで実行できる導線になっていないか。
  ブラウザ相当の警告が要るか（= 製品挙動の変更なので要合意）。
- `WindowsSessionStore` の DPAPI スコープ（`CRYPTPROTECT_LOCAL_MACHINE` を使っていないか、
  エントロピー有無、ファイル ACL）。セッショントークンは**パスワード同等の資格情報**。
- ログ: `lcSession` などにセッション文字列・メール・パスが乗っていないか全カテゴリを一巡。
  現状の走査では平文出力は見つからなかったが、**ログファイルの場所と寿命**は未確認。
- `CMakeLists.txt` の `install()` が `appMegaExplorer` のみ。**`LICENSE` と
  `THIRD-PARTY-NOTICES.txt` が配布物に入らない**（Phase 20b が「still outstanding」と記録した
  宿題）。GPLv3 の同梱義務に直結するので、ここで閉じる。

**成果物**: 修正 + 「サーバ由来文字列の信頼境界」を `docs/ARCHITECTURE.md` に 1 節追記。

---

### R2 — スレッド安全性 / オブジェクト寿命

**対象**: `src/core/DownloadService.cpp`, `src/core/UploadService.cpp`,
`src/core/ThumbnailService.cpp`, `src/mega/MegaSdkClient.cpp`, 全 `src/qml/*Controller.cpp`。

**観点**: SDK スレッドから来るコールバックの扱い、`shared_from_this` の実効性、非所有生ポインタの
寿命、`new Listener` の所有権、`Result` を跨いだデータ競合。

**種**:

- `invokeOnGuiThread` が **4 箇所に重複定義**されている（`AuthController` / `DownloadController` /
  `FolderNavigationController` / `AccountController`）。しかも実装が 2 種類ある
  （`qApp` を target にするものと、`this` を target にするもの）。後者だけがコントローラ破棄時に
  キューを落とせるので、**この差は意味を持つ**。統一するか、差を意図として明記するか決める。
- `DownloadService::startNextIfIdle` の末尾でロック外から自身を再帰呼び出ししている
  （`// auto-advance; mMutex isn't held here`）。コールバックが SDK スレッドから来ることを考えると
  再入・スタック深度・キュー先頭の同一性前提（`mQueue.front()` 依存が全域）を確認する。
- `mQueue.front()` を各所で使っており、**キューが常に先頭 1 件だけ処理される前提**が
  コード全体に散らばっている。前提を型で表現できないか（`std::optional<DownloadJob> mActive` +
  待ち行列の分離）。
- `NotificationController*` / `ClipboardController*` を全タブのコントローラが非所有で持つ。
  `main.cpp` はスタック変数の**宣言順**で寿命を保証している（コメントで明示済み）。この保証が
  コメントだけに載っている状態でよいか。
- `MegaSdkClient` の `new SimpleResultListener(...)` 群（18 箇所）— SDK 側が delete する契約の
  確認と、**リクエストが発行されず listener が漏れる経路**がないか。

---

### R3 — エラーハンドリングと `Result<T>` の設計

**対象**: `src/core/Result.h`, `src/core/MegaErrorCodes.h`, 全 service、
`src/qml/NotificationController.*`。

**観点**: 失敗の表現方法の一貫性、無音で握り潰される経路、エラーコードの意味づけ。

**種**:

- `Result<T>` は `success` フラグ + `value{}` の**常時デフォルト構築**。`T` が
  `std::vector<FileEntry>` なら安いが、失敗時にも `value` が読めてしまう（型で防げていない）。
  `std::expected`（C++23）/ `std::variant` への置き換えが妥当か、`/W4` と MSVC の C++ 標準設定を
  見て判断する。**置き換えるなら全 service に波及する = 単独セッション必須**。
- `errorCode` のデフォルトが `-1`、成功時は `0`。`MegaErrorCodes.h` の値域と衝突しないか。
- `fail()` の `errorMessage` が英語の生文字列で、そのまま UI に出る経路がないか
  （C++ は構造、QML が文言、という Phase 19 以降の分担が守られているか）。
- 無音失敗の棚卸し: `if (!result.success) return;` で終わっている箇所を全部数え、
  「意図的に黙る」と「報告漏れ」を分ける。

---

### R4 — テスト戦略の見直し

**対象**: `tests/` 全体, `tests/CMakeLists.txt`, `CMakeLists.txt`。

**観点**: 「src/qml は GUI glue なのでテストしない」という**明文化された慣例が既に崩れている**
事実の追認と再定義、カバレッジの穴、ビルド定義の重複。

**種**:

- `FolderNavigationController.h` は今も「Untested by convention: src/qml is GUI glue」と書いて
  いるが、実際には `FolderNavigationControllerTest.cpp` が **1,152 行**で最大のテストになって
  いる。**コメントが嘘になっている**。慣例を「ロジックを持つコントローラはテストする」に
  書き換えるのが実態に合う。
- 未テストの `src/qml`: `AuthController`, `DownloadController`, `ThumbnailController`,
  `MenuActions`, `KeyboardState`。このうち `AuthController` は `LoadingStage` 状態機械を持つので
  **テスト価値が高いのに穴**。
- QML テストが **0 件**（7,176 行が全面ノーテスト）。`qt-development-skills:qt-qml-test` スキルが
  使える。全面導入はコスト過大なので、対象を絞る（例: `ActionCatalog` の適用可否、
  `DragProxy` のモード判定）。R6 の QML 再構成の**安全網**になるので R6 より前に置く。
- `tests/CMakeLists.txt` が `src/qml/*.cpp` を **20 ファイル手書きで再列挙**している。
  ルートの `qt_add_qml_module` と二重管理で、ファイルを増やすたび両方直す必要がある。
  `MegaExplorerCore` に寄せるか、共通の `set(QML_LAYER_SOURCES ...)` に括る。

---

### R5 — C++ 構造 / 責務分割

**対象**: `src/qml/FolderNavigationController.{h,cpp}` を中心に、`src/qml/FileListModel.cpp`,
`src/mega/MegaSdkClient.cpp`, `src/core/IMegaClient.h`, `main.cpp`。

**観点**: 単一責務、god object の解体、レイヤ境界の再確認。**このプロジェクト最大の負債**。

**種**:

- `FolderNavigationController` が抱えているもの: ナビゲーション（back スタック / breadcrumb）、
  検索、ソート、リネーム、ゴミ箱移動、フォルダ作成、移動、コピー、貼り付け、
  bulk fan-out バッチ機構、busy カウンタ + 遅延タイマ、`FileListModel` の所有。
  **Q_INVOKABLE だけで 18 個**。少なくとも「ナビゲーション状態」「ミューテーション実行」
  「busy 表示」の 3 つには割れる。
  - 有力な切り出し先: `BulkOperationBatch` + `accountForBulkOutcome` + `startCopyBatch` を
    独立クラス（`BulkOperationRunner` 等）へ。移動/コピー/ゴミ箱の 3 経路が共有している。
  - busy カウンタ + `QTimer` も独立の小クラスにできる（`TabsController` から見えるのは
    `busy` 1 プロパティだけ）。
- `ClipboardController.h` を `FolderNavigationController.h` が**実インクルード**している
  （前方宣言では済まない、と自らコメントで説明済み）。`ClipboardController::Entry` が
  クリップボードと無関係なドラッグコピーにも使われているのが原因 — **`Entry` を
  `src/core` の値型（例: `NodeRef`）へ降ろせば依存が消える**。
- `IMegaClient` の「同期例外」が **7 個**まで増え、しかも「末尾に追加する」という位置依存の
  規約で数えられている（`currentAccountIdentity` のコメント）。同期/非同期を型か命名で
  分離するか、少なくとも規約を位置非依存にする。
- `MegaSdkClient.cpp` 905 行 — listener 群（`SimpleResultListener` ほか匿名 namespace 内）を
  別ファイルへ出すだけで大きく減る。
- `main.cpp` の合成ルートは現状読みやすいが、`tabFactory` のキャプチャリストが 5 個まで
  伸びている。R5 でコントローラを割ったら再点検する。

**注意**: このスコープは**テストの緑を維持しながら**進める。R4 を先に済ませておく理由がこれ。

---

### R6 — QML 構造 / 重複

**対象**: `qml/views/FileTableView.qml`, `qml/views/FileGridView.qml`, `qml/Main.qml`,
`qml/components/{Breadcrumb,FolderTreePanel,QuickAccessSection,TabStrip}.qml`。

**観点**: 6 つのドロップ先に散った同一ロジック、2 つのビューに散った同一ロジック、
`Main.qml` の解体。

**種**:

- **ドロップ先 6 箇所**（グリッド / リスト / 空白 / フォルダツリー / クイックアクセス /
  ブレッドクラム / タブ）が、それぞれ `keys: ["application/x-megaexplorer-nodes", "text/uri-list"]`
  + `onEntered` の内外判定 + `Connections { target: dragProxy; onCopyModeChanged: ... }` +
  `onDropped` の分岐を**手書きで重複**させている。1 箇所 40〜60 行 × 6。
  `NodeDropArea.qml` として括り出せば、Phase 23a が各所を短くした流れの続きになる。
- 2 ビューの `Keys.onPressed` が **9 分岐まるごと重複**（F2 / Delete / SelectAll / Copy / Cut /
  Paste / 矢印）。矢印キーの次元数だけが違う。`FileViewKeyHandler.qml` へ。
- `Main.qml` 1,055 行に **Dialog が 5 つインライン**（signOut / missingPin / folderDrop /
  nameConflict / about・license は既に別ファイル）。既に別ファイル化された 2 つと不揃いなので、
  残り 3 つも `qml/components/` へ出すのが一貫する。
- `header`/`footer` の `Component {}` がインラインで計 270 行。`qml/components/` へ。
- **検証手段**: QML はコンパイル通過が正しさを保証しない。`ui-style` スキルの
  `scripts/ui_shot.py cycle` でスクリーンショット差分を取りながら進める。
  `qt-development-skills:qt-qml-review` も併用できる。

---

### R7 — ドキュメント / コメント量の再調整

**対象**: `CLAUDE.md`, `docs/PROGRESS.md`, 各ヘッダのコメント。

**観点**: R1–R6 の結果を反映しつつ、**説明が実装より長い**状態を是正する。

**種**:

- `FolderNavigationController.h` は 384 行中おそらく 250 行超がコメント。
  「WHY のみ・簡潔に」というユーザーの指示（ユーザーメモリに記録済み）から乖離している。
  R5 で分割すればコメントも自然に分散するので、**R5 の後に回す**。
- `CLAUDE.md` の「Project status」節が 1 段落で全フェーズを語る巨大ブロックになっている。
  毎セッション読み込まれるので、ここが一番トークンを食う。`docs/PROGRESS.md` へ移して
  要約に置き換える余地がある。
- R4 で慣例を書き換えたら `FolderNavigationController.h` の「Untested by convention」も直す。

---

## 2. 実施順と根拠

```
R1 セキュリティ/ライセンス
   ↓  （独立・修正が小さい・見つかれば最優先で潰すべき種類）
R2 スレッド/寿命
   ↓  （バグクラス。構造を割る前に「どこが本当に危ういか」を把握しておく）
R3 エラーハンドリング / Result
   ↓  （R5 で全 service に触るので、型の方針はその前に決める）
R4 テスト戦略
   ↓  ★ここが分水嶺。以降の大改造の安全網
R5 C++ 構造（最大）
   ↓  （R3 の Result 方針・R2 の寿命方針が入力になる）
R6 QML 構造（最大・最も検証が難しい）
   ↓  （C++ 側の API が固まってから。逆順だと二度手間）
R7 ドキュメント/コメント整理
       （R1–R6 の結果を書き戻す締め）
```

順序の要点:

1. **R1 → 最初**: 他と依存がなく、見つかった場合の重大度が最も高い。空振りでも安い。
2. **R2/R3 → 構造変更の前**: 「どう割るか」の判断材料が寿命とエラー表現だから。逆順にすると
   割った後で同じ場所をもう一度触ることになる。
3. **R4 → R5/R6 の直前**: テストが緑であることが大改造の唯一の担保。ここで穴を埋めておく。
4. **R5 → R6 の前**: QML は C++ の API に張り付いている。C++ を後から割ると QML を二度直す。
5. **R7 → 最後**: 途中で書くと書き直しになる。

### スコープを跨ぐ場合のルール

- 1 スコープ = 1 セッション（`/clear` で区切る）。文脈を持ち越さない。
- 他スコープの領分の問題を見つけたら、**直さずこの文書の「持ち越し」節に書く**。
- 各スコープの終了時に `MegaExplorerTests` が緑 + `/W4` 警告ゼロを確認してからコミット。

---

## 3. 各スコープの実施手順（共通テンプレート）

1. **準備**: `cmake --build --preset msvc-debug --target MegaExplorerTests` → `ctest --preset
   msvc-debug` でベースラインが緑であることを確認。
2. **調査**: そのスコープの「種」を 1 件ずつ現物で検証。Serena の symbolic tools で対象シンボルだけ
   読む（ファイル全読みは避ける）。結果を「確認された問題 / 誤りだった種 / 新規発見」に仕分ける。
3. **合意**: 修正方針を提示して承認を取る。特に**製品挙動が変わるもの**（R1 の実行ファイル警告、
   R3 の Result 置き換えなど）は必ずここで止める。
4. **修正**: 小さいコミットに割る。1 コミット = 1 論点。
5. **検証**: テスト緑 + `/W4` 警告ゼロ。R6 は加えて `ui_shot.py cycle` でスクショ比較。
6. **記録**: 下の「実施ログ」に追記。`docs/PROGRESS.md` には書かない（あちらは機能フェーズの
   ログ、こちらは整理のログ）。

---

## 4. 実施ログ

（各スコープ完了時にここへ追記。形式は `docs/DESIGN_IMPROVEMENT.md` の段階ログに揃える）

### R1 — 調査済み / R1-1 のみ修正済み（2026-08-06）

計画の「種」5 件を現物で検証した結果。**確認 3 件 / 種の誤り 1 件 / 問題なし 3 件 / 新規 3 件**。
以下はそのまま plan mode の作業単位として使える粒度で書いてある。

#### 確認された問題

**R1-1 [高] ダウンロード先パスにサーバ由来のノード名を無検証で連結（パストラバーサル）** — **修正済み（2026-08-06）**

- 現物: `src/qml/DownloadController.cpp:114-132` (`computeDestinationPath`)。
  `QDir::toNativeSeparators(dir + "/" + fileName)` のみで、`fileName` に一切の検証がない。
- 到達経路: `qml/views/TabContentPane.qml:70-75`（ダブルクリック）と
  `qml/ActionCatalog.qml:44-53`（コンテキストメニュー "Download"）→
  `downloadController.downloadFile(handle, name, sizeBytes)`。`name` は `FileEntry::name`
  = **サーバ由来**。
- **SDK 側の正規化は存在しないことを確認した**（種が「依存先の副作用に頼っている」と書いた点の答え）:
  - `third_party/sdk/src/megaapi_impl.cpp:10416-10438` — `startDownload` は `localPath` を
    `LocalPath::fromAbsolutePath()` に渡すだけ。
  - エスケープ（`fsaccess->escapefsincompatible`）は同 `:10493-10499` にあるが、**`customName`
    にしか適用されない**。そして `src/mega/MegaSdkClient.cpp:528-537` は `customName` に
    `nullptr` を渡している。→ セパレータは一切潰されない。
- 影響: 名前が `..\..\evil.exe` のノードで Downloads 外へ書ける。MEGA のノード名は
  **E2E 暗号化なのでサーバ側検証が原理的に不可能**（= 「MEGA が弾いてくれる」は成り立たない）。
  ただし本アプリが見るのは自分のルートのみ（`MegaSdkClient::resolveNode`,
  `src/mega/MegaSdkClient.cpp:884-888` — インシェアは扱わない）なので、実際の混入経路は
  「公開リンクの import（元の名前がそのまま自分の口座に入る）」「別クライアントでの改名」に限られる。
  重大度は高いが、悪用の前提条件はある。
- 修正方針 A（推奨・堅い）: `IMegaClient::download` を `destinationDir` + `fileName` の 2 引数に変え、
  `MegaApi::startDownload` の `customName` に名前を渡す。SDK の `escapefsincompatible` が
  セパレータを潰してくれるので、正規化ロジックを自前で持たなくて済む。
  波及: `IMegaClient.h` / `MegaSdkClient` / `DownloadService` / `DownloadController` /
  `tests/MockMegaClient.h` / `tests/DownloadServiceTest.cpp`。
- 修正方針 B（小・自前）: `computeDestinationPath` 内で `QFileInfo(fileName).fileName()` を取り、
  空 / `.` / `..` を弾き、Windows 予約名（`CON` `PRN` `AUX` `NUL` `COM1-9` `LPT1-9`）と
  末尾のドット・空白も避ける。波及は 1 ファイル。
- どちらでも `COLLISION_RESOLUTION_NEW_WITH_N` と `resolvedLocalPath` 表示ロジック
  （`DownloadController.cpp:52-55`）はそのまま生きる。
- テスト: **`DownloadController` にテストが無い**（`tests/` に `DownloadControllerTest.cpp` は不在。
  理由は `tests/UploadControllerTest.cpp:19` のコメント = `QDesktopServices` が QtGui を引く）。
  B を採るなら判定を `src/core` の純関数へ出せばテストできる。R4 の「未テストの src/qml」と直結。

**修正結果**（方針 B の変種 = B'）:

- `DownloadService::safeLocalFileName`（`src/core/DownloadService.h/.cpp`）を新設。`static`・Qt 非依存の
  純関数で、リーフ抽出 → ドライブレター除去 → 不正文字と制御文字の `_` 置換 → 末尾ドット/空白除去 →
  空なら `download` → Windows 予約デバイス名に `_` 前置、の順。長さ上限は意図的に持たない
  （切り詰めは衝突を生み、超過は OS のエラーであって信頼境界ではない）。
- `DownloadController::computeDestinationPath` がこれを通してから連結する。`enqueue` に渡す
  `name` は元のノード名のまま（失敗トースト用。成功時の表示は既存どおり `resolvedLocalPath` 由来）。
  **QML 側の変更はゼロ**。
- **不正名は拒否せずサニタイズして続行**。`CON.txt` は MEGA 上に正当に存在しうるので、拒否すると
  正当なファイルがダウンロード不能になる。
- **方針 A（`IMegaClient::download` を dir + name に割り `customName` に委譲）は採らなかった**：
  SDK の `escapefsincompatible` は*文字*単位のエスケープなので、`..` という*名前*を潰す保証がない。
  暗黙の SDK 挙動に信頼境界を預ける形になるのも、種が問題視していた点そのもの。
- テストは `tests/DownloadServiceTest.cpp` に 6 件追加（トラバーサル / 不正文字 / 末尾ドット /
  フォールバック / 予約名 / 通常名と UTF-8 の素通し）。`DownloadService.cpp` は既に
  `MegaExplorerCore` に入っているので **`tests/CMakeLists.txt` の変更は不要**だった。
  `DownloadController` 自体は QtGui 依存でテスト対象外のまま（R4 の持ち越しは有効）。
- 併せてコメント是正: `FileOperationService::isValidName`（「download 時ではなくここで弾く」が
  嘘になっていた）と `DownloadService::enqueue` の doc。
- 検証: `MegaExplorerTests` 370 件全緑、`appMegaExplorer` の `/W4` 新規警告ゼロ。

**手動テスト宿題（R1-1、未実施）**

実アカウントでの確認は残っている。ノード名を作る手段で 2 群に分かれる。

*群 A — MegaExplorer 自身の F2 リネームで作れる*（`isValidName` が弾くのは `/` `\` と
空白のみの名前だけなので、以下はそのまま付けられる）:

| MEGA 上の名前 | 検証する規則 | Downloads に落ちる名前 |
| --- | --- | --- |
| `CON.txt` | 予約デバイス名 | `_CON.txt` |
| `COM1.log` | 予約デバイス名（番号つき） | `_COM1.log` |
| `CONS.txt` | 予約名を誤検出しない | `CONS.txt`（無変換） |
| `a<b>c:d?.txt` | 禁止文字の置換 | `a_b_c_d_.txt` |
| `test.txt. `（末尾ドット + 空白） | 末尾ドット/空白の除去 | `test.txt` |
| `..` / `...` | 名前が全部消える → フォールバック | `download` |
| `日本語 ファイル.txt` | UTF-8 が壊れない回帰確認 | 無変換 |

*群 B — パストラバーサル本体。外部クライアントが要る*（名前にパス区切りが要るため、本アプリの
リネームでは作れない。mega.nz の web クライアントで弾かれたら WSL + MEGAcmd。Linux では `\` `<`
`>` `:` `?` も末尾ドットも正当なファイル名文字なので、その名前のままアップロードすればノード名に
そのまま入る）:

| MEGA 上の名前 | 修正前の書き込み先 | 修正後 |
| --- | --- | --- |
| `..\..\..\pwned.txt` | `C:\pwned.txt` | `Downloads\pwned.txt` |
| `../../pwned.txt` | `C:\Users\pwned.txt`（`toNativeSeparators` が `\` に直すため） | `Downloads\pwned.txt` |
| `C:\Windows\Temp\pwned.txt` | `C:\Windows\Temp\pwned.txt` | `Downloads\pwned.txt` |
| `C:pwned.txt`（ドライブ相対） | カレントドライブ基準の別の場所 | `Downloads\pwned.txt` |

確認ポイント: 完了トーストの表示名は保存後の実ファイル名（`resolvedLocalPath` 由来）なので、
`CON.txt` のダウンロードでは**トーストが `_CON.txt` と出るのが正しい**。群 B では `C:\` /
`C:\Users\` / `C:\Windows\Temp\` に何も出ていないことも見る。併せて普通の名前を 2 回ダウンロード
して 2 個目が `report (1).pdf` になること（`COLLISION_RESOLUTION_NEW_WITH_N`）を確認すれば、
既存のリネーム経路を壊していないことも分かる。

**R1-2 [中] `install()` に `LICENSE` / `THIRD-PARTY-NOTICES.txt` が入っていない（Phase 20b の宿題）** — **修正済み（2026-08-06）**

- 現物: `CMakeLists.txt:222-226` — `install(TARGETS appMegaExplorer ...)` のみ。
- 前提の確認: リポジトリ直下に `LICENSE` と `THIRD-PARTY-NOTICES.txt` は**存在する**。
  `licenses/licenses.cmake` 経由で qrc 埋め込みもされており、**アプリ内表示（Phase 20b のダイアログ）
  は成立している**。足りないのは「配布物のファイルとしての同梱」だけ。
- 修正: `install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt
  DESTINATION ${CMAKE_INSTALL_BINDIR})` を追加。
- **併走する未整備**: `windeployqt` / `qt_generate_deploy_qml_app_script` / CPack が一切ない
  （`grep` 済み、`CMakeLists.txt` にヒットなし）。つまり `install` してもそのままでは動かないツリーになる。
  **R1 でどこまでやるかは要合意** — 提案は「ファイル同梱だけ R1 で閉じ、デプロイ整備は別スコープ（持ち越し）」。
- 要確認（ユーザー判断）: GPLv3 の対応ソース提供義務を「public な GitHub リポジトリ」で満たす方針か。
  About ダイアログには URL がある（`qml/components/AboutDialog.qml:87`）ので、それを提供手段と
  みなす旨をどこかに明記すれば足りる想定。

**修正結果**（同梱のみ。デプロイ整備は持ち越しのまま）:

- `CMakeLists.txt` に `install(FILES LICENSE THIRD-PARTY-NOTICES.txt DESTINATION ${CMAKE_INSTALL_BINDIR})`
  を追加。宛先を `BINDIR`（= `bin`）にしたのは exe と同じ場所になるからで、NOTICES 前文の
  「in the root of this distribution」という記述とも一致する（`DOCDIR` だと exe から離れる）。
  併せて **R1-10 もここで閉じた** — `project()` 直後に `include(GNUInstallDirs)` を明示。
- **調査の副産物: 種の前提が半分外れていた**。`THIRD-PARTY-NOTICES.txt` は Phase 20b の生成物で
  36 件すべて `licenses/` と同期しており、内容の更新は不要だった。一方 `LICENSE` は
  プロジェクト作成時のままで、中身は完全な GPLv3 だが **gnu.org の HTML 版を貼った体裁**
  （曲線引用符 `“ ”`、折り返しなし、センタリング見出しなし）。GPLv3 自身が「この license document の
  変更は不可」と述べているので、正典 `gpl-3.0.txt` に差し替えた。
- `LICENSE` は生成スクリプトの入力（`gen_third_party_notices.py` の `megaexplorer` エントリ本文）
  なので、差し替え後に再生成。変わったのは `licenses/texts/megaexplorer.txt` と
  `THIRD-PARTY-NOTICES.txt` だけで、`manifest.json` / `licenses.cmake` は名前もバージョンも
  変わらないため不変。
- **`ABOUT.txt` を削除**。Phase 20b 以前の下書きで、About ダイアログ（`AboutDialog.qml:44-92`）が
  同じ内容を全部表示しており、コードからもビルドからも参照されていなかった。
  `docs/PROGRESS.md:2630-2632` の記述は Phase 20b 当時の履歴なので**書き換えていない**。
- ただし削除すると、プレーンテキスト側（`LICENSE` は FSF の文書、NOTICES 前文は「GPLv3 です」としか
  言わない）に**著作権者名とソース入手先が一切出なくなる**。そこで `NOTICES_PREAMBLE` に 3 点追加した:
  著作権表示 + §15/§16 の無保証告知、**GPLv3 §6 の対応ソース提供先**（= public な GitHub リポジトリ、
  上の「要確認」への回答）、FFmpeg を DLL で配るので再リンク可能である旨
  （`docs/PROGRESS.md:2623-2624` が「実質満たしているがどこにも書かれていない」と記録していた穴）。
- `AboutDialog.qml` の裸だった GitHub URL に `Source code:` のラベルを付けた。URL だけでは
  「これが §6 の提供手段」と読めないため。
- 検証: `--check` 緑（前後とも）、`MegaExplorerTests` 370 件全緑、`/W4` 新規警告ゼロ、
  `cmake --install --prefix build/_installtest` で `bin/` に exe + 2 ファイルが揃うことを実地確認、
  `ui_shot.py` で About ダイアログとライセンス一覧 1 件目（正典テキストに切り替わっている）を目視確認。
- **意図的にやらなかったこと**: `windeployqt` / `qt_generate_deploy_qml_app_script` / CPack /
  Release プリセット。`cmake --install` しても Qt/FFmpeg の DLL が無く動くツリーにはならないが、
  R1-2 の論点は「同梱の義務を閉じる」ことなので、配布パイプラインは持ち越し節に残したままにする。

**R1-3 [中] MEGA SDK のログレベルが明示されておらず、デフォルト値に依存している**

- 現物: `src/mega/MegaSdkClient.cpp:391-399` — `addLoggerObject` するだけで `setLogLevel` を呼ばない。
- 実効値は `third_party/sdk/src/logging.cpp:52` の `logInfo`（`src/app/Logging.cpp:16-17` の
  コメント通り）。**INFO 以下では資格情報が出ないことは SDK 側を grep して確認済み**。
- ただし `third_party/sdk/src/megaclient.cpp:2368` は `req->posturl` を `LOG_debug` で出力する。
  この URL には `sid=` が含まれる。→ **レベルが上がった瞬間にセッション ID が平文ログへ落ちる**。
- ログの出口も確認: `src/app/Logging.cpp:157-163` → `%LOCALAPPDATA%/MegaExplorer/MegaExplorer.log`。
  **世代 1 世代のみ（`.1`）、サイズ上限なし、ログアウトでも削除されない**（種の「場所と寿命は未確認」への答え）。
- 修正: `MegaSdkClient` のコンストラクタで `mega::MegaApi::setLogLevel(...)` を明示し、
  「上げると sid が平文で落ちる」旨をヘッダに 1 行。デバッグ用に上げたいときのために
  環境変数ゲートを付けるかは任意（付けるなら既定は必ず INFO 以下）。

#### 種が不正確だった / 判断が要るもの

**R1-4 [要合意] `openFile` の実行導線 — 種の記述は事実と違う**

- 種は「クラウド上の `.exe` をダブルクリックで実行できる導線」と書いているが、**ダブルクリックは
  ダウンロードするだけ**（`qml/views/TabContentPane.qml:70-75`）。`openFile` を呼ぶのは
  **完了トーストの "Open" ボタン 1 箇所のみ**（`qml/components/ToastStack.qml:304`、
  ボタンは `showDownload` が成功時に必ず出す `:82-91`）。つまり「もう 1 クリック必要」。
- とはいえ `QDesktopServices::openUrl`（`src/qml/DownloadController.cpp:107`）が
  `.exe` / `.lnk` / `.scr` を無警告で起動するのは事実。
- **製品挙動の変更なので R1 では実装せず、方針だけ決める**。選択肢:
  - (a) 何もしない（自分がダウンロードしたファイルを開くだけ、という現状の位置づけを明記）
  - (b) 実行可能拡張子のときだけ確認ダイアログ
  - (c) "Open" を "Show in folder" に変える（Explorer 的にも自然で、挙動変更が最小）
- なお `Qt.openUrlExternally` は他に 2 箇所あるが、いずれも `AboutDialog.qml:82,88` の
  **静的リテラル URL** で、外部入力は入らない（確認済み）。

#### 問題なしと確認できた種

**R1-5 DPAPI の使い方は妥当** — `src/platform/WindowsSessionStore.cpp`

- フラグ `0` = current-user スコープ。`CRYPTPROTECT_LOCAL_MACHINE` は**使っていない**（`:90`, `:111`）。
- `pOptionalEntropy` あり（`:26-51`、pepper であって秘密ではない旨もコメント済み）。
- 保存先は `%LOCALAPPDATA%/MegaExplorer/session.dat`（`main.cpp:82-83`）で、ユーザースコープの
  ACL を継承。ログアウトで `clearSession` される（`src/core/AuthService.cpp:89-97`）。
- 残るのは R1-8 の細かい点のみ。**このスコープでの修正対象なし**。

**R1-6 ログに資格情報は出ていない** — `src/` 全体の `qCWarning`/`qCInfo`/`qCDebug` を一巡

- email / password / セッショントークンを出す箇所は皆無。`AuthController` は `errorMessage` と
  `errorCode` のみ（`:113`, `:153`, `:186`）、`AccountController` も同様（`:85`, `:123`, `:170`, `:178`）。
- サムネイル / アバターのローカルパスは**ハンドル数値のみ**でノード名を含まない
  （`src/qml/ThumbnailController.cpp:79`, `src/qml/AccountController.cpp:195`）。
- 失敗時に限りノード名 / ローカルパスがログに出る（`DownloadController.cpp:61,109`,
  `UploadController.cpp:84`, `FolderNavigationController.cpp:363`）。単一ユーザーのデスクトップ
  アプリとしては許容だが、R1-3 のログ寿命と併せて成果物のドキュメントに明記する。

**R1-7 外部ドロップ（アップロード）側の信頼境界は正しく引けている** — `src/qml/UploadController.cpp:153-190`

- `url.isLocalFile()` で非ローカル URL を落とし、`isDir`/`isFile` で分類、`absoluteFilePath()` で正規化。
- R1-1 の対比として使える「良い側」の実例。成果物のドキュメントでこちらを参照する。

#### 新規発見（いずれも低）

**R1-8 [低] 資格情報のメモリ寿命** — `src/qml/AuthController.cpp`

- `mPendingPassword` は 2FA 用に平文保持され、成功 / キャンセルで `clear()`（`:141`, `:181`, `:219`）。
  `std::string::clear()` はゼロ化しない。`login` のラムダも `passwordStd` をコピーキャプチャで持つ（`:137-138`）。
- 同一ユーザーのプロセスメモリなので実効リスクは低い。やるなら `SecureZeroMemory` 相当。
  **「やらないと決めて記録する」でも可** — plan mode で判断する。

**R1-9 [低] サムネイル / アバターのキャッシュがログアウト後も残る**

- `%TEMP%/MegaExplorerThumbnails/<handle>.jpg`（`src/qml/ThumbnailController.cpp:72-79`）、
  `%TEMP%/MegaExplorerAvatars/<handle>.jpg`（`src/qml/AccountController.cpp:189-195`）。
- ログアウトでも別アカウントログインでも削除されない。前アカウントの画像プレビューが残る軽微な
  プライバシー問題。修正はログアウト時に両ディレクトリを削除するだけ（`AuthController::logout` の完了時）。

**R1-10 [低・ハイジーン] `include(GNUInstallDirs)` が無いまま `CMAKE_INSTALL_BINDIR` を使っている**

- `CMakeLists.txt:224-225` が使っているが、`grep` の通り `include(GNUInstallDirs)` は無い。
- 生成物を確認したところ `build/msvc-debug/cmake_install.cmake:52` は `${CMAKE_INSTALL_PREFIX}/bin`
  に解決されており、**現状は壊れていない**（Qt の CMake パッケージが推移的に include している）。
  暗黙依存なので R1-2 のついでに明示的な `include(GNUInstallDirs)` を足すのが安全。

#### 成果物（計画通り）

- `docs/ARCHITECTURE.md` に「サーバ由来文字列の信頼境界」を 1 節。R1-1（悪い例）と R1-7（良い例）、
  R1-3 のログ出口と寿命をここに集約する。
  → **R1-1 / R1-7 ぶんは記述済み**（`## Trust boundary: strings that come from the server`）。
  R1-3 のログ出口と寿命は R1-3 実施時にこの節へ追記する。

### R2 — 調査済み / R2-1・R2-2・R2-3・R2-6・R2-7 修正済み（2026-08-06）

計画の「種」5 件を現物 + **ベンダーされた MEGA SDK 本体**（`third_party/sdk`）で検証した結果。
**確認 3 件 / 種の誤り 1 件（重要）/ 問題なし 6 件 / 新規 4 件**。R1 と同じく、以下はそのまま
plan mode の作業単位として使える粒度で書いてある。**修正は各項目ごとに別セッション**。

#### 前提: このアプリのスレッドモデルは 2 本しかない

先に確定させた事実。以降の判断は全部これに乗っている。

- SDK 側のコールバックは **`MegaApiImpl` のループスレッド 1 本**に集約される
  （`third_party/sdk/src/megaapi_impl.cpp:7140` で生成）。別ワーカから来た完了は
  `:17992-18011` の `executeOnThread` で同スレッドへマーシャルされる。
  → **リスナ同士は並行しない**。競合は常に「SDK スレッド vs GUI スレッド」の 1 組だけ。
- そのコールバックは **`sdkMutex` を保持したまま**配達される
  （`megaapi_impl.cpp:20809`, `:19904`, `:18046`）。この事実が R2-9 / R2-10 の根拠。

#### 確認された問題

**R2-1 [高] `currentJob()` の TOCTOU — 空 vector への `front()`（UB）** — **修正済み（2026-08-06）**

- 現物: `src/core/DownloadService.cpp:116-126` が 2 つの別々のロックとして公開されている。

  ```cpp
  bool DownloadService::hasCurrentJob() const   { std::lock_guard ...; return !mQueue.empty(); }
  DownloadJob DownloadService::currentJob() const { std::lock_guard ...; return mQueue.front(); }
  ```

- 呼び出し側は GUI スレッドで 2 回に分けて呼ぶ（`src/qml/DownloadController.cpp:142-148`）:
  `mHasActiveJob = mService->hasCurrentJob();` → `mActiveJob = mService->currentJob();`
- その隙間で SDK スレッドが `DownloadService.cpp:209` の `mQueue.erase(mQueue.begin())` を実行し、
  それが最後の 1 件だった場合、`currentJob()` は空 vector に `front()` を呼ぶ。**例外ではなく UB**。
  `src/core/DownloadService.h:89` の `// precondition: hasCurrentJob()` は、スレッドを跨いだ時点で
  構造的に満たしえない。
- `UploadService.cpp:30-40` + `UploadController.cpp:285-287` がまったく同じ形。
- 修正方針: `std::optional<Job>` を返す 1 回ロックの API に変え、`hasCurrentJob()` を削除。
  波及は 2 service + 2 controller + 該当テストのみ。**このスコープで最も直しやすく、最も明確な UB**。

**修正結果**（方針どおり）:

- `DownloadService::currentJob()` / `UploadService::currentJob()` が `std::optional<Job>` を返し、
  空判定と `front()` の読みが同一ロック内に入った。`hasCurrentJob()` は**残さず削除**
  （残すと同じペア呼びが再発しうる）。`// precondition: hasCurrentJob()` は「なぜ optional か」の
  コメントに置き換え、R2 の調査結果を現場に残した。`jobs()` / `hasJobForHandle()` /
  `queueLength()` は元から 1 回ロックなので変更なし。
- コントローラ側の `bool mHasActiveJob` + `Job mActiveJob` のペアも `std::optional<Job>` 1 本に統合。
  サービスが optional を返す以上ペアは冗長で、「フラグと値が食い違う」状態が型として消える。
  `refreshActiveJob()` は代入 1 行になった。
- **挙動差は 1 点のみ**: ジョブ完了後 `activeFileName` が空文字になる（従来は直前ジョブの名前を
  保持し続けた）。`qml/Main.qml` の当該 Label / ProgressBar は全て
  `visible: downloadController.downloadActive` / `uploadController.uploadActive` で括られており、
  **QML 側の変更はゼロ・見た目も不変**。
- **新規テストは追加していない**。R2-19 のとおりテストは全てシングルスレッドで、この TOCTOU は
  シングルスレッドでは到達不能。空キューでの `nullopt` は既存の `InitiallyHasNoCurrentJob` が
  カバーしている。マルチスレッド配達モードを `MockMegaClient` に足す話は R4 の領分。
- **途中で見つかったビルド設定の穴**（別コミット）: `MegaExplorerCore` は Qt を一切リンクしないため
  C++ 標準の指定が無く、MSVC 既定の **C++14** でビルドされていた（`std::optional` が使えない）。
  ルート `CMakeLists.txt` は `CMAKE_CXX_STANDARD_REQUIRED ON` だけで `CMAKE_CXX_STANDARD` を
  設定していない。`target_compile_features(MegaExplorerCore PUBLIC cxx_std_17)` で当該ターゲットに
  限定して修正（ベンダーされた `third_party/*` の標準を動かさないため、ディレクトリ変数ではなく
  ターゲット単位）。他の全ターゲットは Qt の interface 要求から C++17 を得ていたので影響なし。

**R2-2 [高] 同期エラーパスによる無制限再帰 — 既存コメント 2 箇所が事実に反する** — **修正済み（2026-08-06）**

- `src/core/DownloadService.h:117-120` は「`MockMegaClient` ベースのテストが同期に呼ぶので」
  ロックを持たずに `download()` を呼ぶと書き、`src/core/UploadService.cpp:80-83` は
  「Download 側の同等物はモック下でしか再帰しない」と明言する。**どちらも誤り**。
- 現物: `src/mega/MegaSdkClient.cpp:515-521` — `resolveNode` 失敗時、リスナを作る前に
  **呼び出し側スレッドで同期に** `onDone` を呼んで return する。`resolveNode` は
  `getNodeByHandle` なので、ログアウト後・`fetchNodes` 前・他クライアントで削除されたハンドル、
  いずれでも null になる。
- 経路: `startNextIfIdle()`(`DownloadService.cpp:213`) → `download()` → 同期 `onDone` →
  `:188` のラムダ → `:209` erase → `:213` `startNextIfIdle()` → …
  **N 件キューして全部 resolve 失敗すると N 段のスタックフレーム**。積むのは「最初の完了を配達した
  スレッド」＝通常 SDK スレッドで、こちらはスタックサイズを制御していない。
  再現条件: 複数選択ダウンロード中にログアウト、または別クライアントでフォルダ削除。
- 同じ形が `src/core/ThumbnailService.cpp:84`（`startNextIfCapacity` → `getThumbnail` →
  `MegaSdkClient.cpp:570-576` の同期失敗 → `finishJob` → `:84`）。グリッド高速スクロールで
  古いハンドルが大量にキューされた状態で起きうる。
- `UploadService` だけは `UploadService.cpp:84` の `for(;;)` で **`checkUpload` の同期拒否パスのみ**
  保護済み。`:165` の `onDone` 内 `startNextIfIdle()` は素の末尾再帰のままで、
  `MegaSdkClient.cpp:546-553`（親フォルダ resolve 失敗）はそこを通る。
- 根本原因は **`MegaSdkClient` が 3 通りの配達をしている**のに `IMegaClient.h:17` の契約が
  1 通りしか書いていないこと:
  1. 真に非同期（SDK スレッド）— リスナ 18 箇所
  2. **常に同期・呼び出し側スレッド** — `getRootChildren`(:468) / `getChildren`(:477) /
     `search`(:488) / `getPath`(:584) / `getNodeInfo`(:616)。`listChildren`(:892-905) が inline で `onDone`
  3. **エラー時のみ同期・呼び出し側スレッド** — `resolveNode` 失敗の全 15 箇所
     （`:497,518,549,573,591,621,644,657,667,682,691,709,718,742,899`）
- モード 2 は `FolderNavigationService` が mutex を持たない根拠にもなっている
  （`DownloadService.h:49-51` が明言）。つまり**別ファイルのスレッド安全性が `MegaSdkClient` の
  実装詳細に依存しており、その根拠が当該ファイルに書かれていない**。
- 修正方針: 3 サービスとも `for(;;)` ループ化で統一 + `IMegaClient.h` の契約を実態に書き直す
  （「呼び出し側スレッドで同期に呼ばれることがある」）+ 上記の暗黙依存を当該ファイルへ移す。

**修正結果**:

- **`for(;;)` 単体では足りなかった**。onDone 経由の再帰は「ループの次周回」ではなく**入れ子呼び出し**
  として起きるので、ループ化だけでは 1 段も減らない（`UploadService` の既存 `for(;;)` が
  `checkUpload` の同期拒否しか救えていなかったのは、まさにこれが理由）。3 サービスとも
  `mAdvancing` / `mAdvanceRequested` の 2 bool による**トランポリン**にした: 入れ子呼び出しは
  `mAdvanceRequested` を立てて即 return し、既にループを回しているフレームがもう一周する。
  スタック深さは同期完了が何段続いても **O(1)**。
- 要注意点が 2 つあり、どちらもコメントで現場に残した:
  1. 末尾の「フラグ確認」と `mAdvancing = false` は**同一ロック内**でなければならない。分けると
     非同期完了が隙間に割り込んで `mAdvanceRequested` を立てたまま誰も回さず、キューが恒久停止する。
  2. `mAdvancing = true` を立てた後の `return` は**全部**フラグを戻す必要がある。
     `UploadService` の `checkUpload` 失敗ブランチ内にある `if (mQueue.empty()) return;` が該当した。
- 抽象化はしなかった（共通ヘルパを作るとフラグがサービスの `mMutex` 下にある都合でロックを跨ぐ
  不自然なインタフェースになる）。R5 のサービス解体と方向が衝突しないことも理由。
- `IMegaClient.h` の冒頭コメント（1 行だった）を**配達 3 モードの記述**に差し替え、
  モード 2 のメソッド名とモード 3 の発生条件を明記。加えて「onDone は自分の呼び出しの中で走りうるので、
  ロックを跨がない・自己再帰に備える」という**呼び出し側への要求**まで書いた。
  `download` / `upload` / `getThumbnail` と mutating 群の先頭コメントにも個別に注記
  （mutating 群の "genuinely asynchronous" はモード 3 を無視していたので是正）。
  数字付きの「synchronous 例外 N 番目」の連番は別軸なので**触っていない**。
- 暗黙依存を移した: 「`FolderNavigationService` が mutex を持たないのは `getChildren` 等が同期だから」
  という根拠は `DownloadService.h` に書かれていた。`FolderNavigationService.h` の先頭へ移し、
  「借り物の保証であり、モード 2 が崩れたら mutex が要る」と条件付きで書いた。
- R2-11 のコメント 4 件のうち 2 件（`DownloadService.h` / `UploadService.cpp` の「モック下でしか
  再帰しない」）は嘘なので削除・書き直し済み。`ThumbnailService.h` の「no loop is needed」も同様に是正。
  残る 2 件（`MegaSdkClient.cpp:338,380` と `MegaSdkClient.h:147-149`）は締めのセッションに残す。
- **新規テスト 3 本**（`DownloadServiceTest` / `UploadServiceTest` / `ThumbnailServiceTest` の
  `*WithoutRecursing`）。モックのアクションを `Invoke` ラムダにして**再入深さを実測**し `maxDepth == 1`
  を assert する。単なるドレイン件数の assert ではスタックオーバーフロー頼みになり検出できない。
  **テストの形に 1 つ罠があった**: 全ジョブが同期失敗するだけでは 1 件ずつ独立に排水されて再帰が積まれない。
  先頭ジョブだけ `SaveArg` で in-flight にし、残り 49 件を積んでからその onDone を発火させて初めて
  カスケードが起きる（＝実運用の「ダウンロード中にログアウト」そのもの）。修正前のコードに当てると
  3 本とも `maxDepth == 49` になることを確認してから修正した。
- 既存 373 テスト全通過、`/W4` 新規警告ゼロ。**挙動差・QML 側の変更はゼロ**（キューの処理順・通知内容・
  完了件数はすべて不変で、変わるのはスタックの積み方だけ）。
- ついでに `FolderNavigationService.h` / `FileEntry.h` の C++ 標準に関する古いコメントを是正
  （R2-1 で `cxx_std_17` が入ったため前者は事実に反し、後者は消滅した `Result.h` のコメントを参照していた）。

**R2-3 [高] シャットダウン時の use-after-free — `~MegaApi` が破棄済みサービスへコールバックする** —
**修正済み（2026-08-06）**

- `main.cpp` の宣言順は `client`(:81) → 各サービス(:85-97) → コントローラ(:100-107) → `tabs`(:143)。
  スタック変数は逆順に壊れるので、**`MegaSdkClient` が最後**。
- SDK のデストラクタは保留中の全リクエストを**発火してから**終わる:

  ```cpp
  // third_party/sdk/src/megaapi_impl.cpp:7146
  MegaApiImpl::~MegaApiImpl() {
      auto shutdownRequest = ... TYPE_DELETE ...;
      requestQueue.push(shutdownRequest.get());
      waiter->notify();
      thread.join();   // ← この間に SDK スレッドが abortPendingActions() を走らせる
  ```

  `abortPendingActions`（`:9669-9699`、既定 `preverror = API_EACCESS`）が
  `fireOnRequestFinish` を全件回す。
- 一方 `src/core` のサービスは**全て生 `this` を捕獲**し（`ThumbnailService.cpp:63`,
  `DownloadService.cpp:173,188`, `UploadService.cpp:126,141`,
  `AuthService.cpp:45,62,79,91,102`, `AccountService.cpp:46`）、`enable_shared_from_this` を
  使うものはゼロ、**デストラクタも存在しない**（`src/core/*.h` の `~` はインタフェースの
  `= default` のみ）。
- したがって: ウィンドウを閉じる → `app.exec()` が返る → サービス群が壊れる → `~MegaSdkClient` →
  `~MegaApi` → SDK スレッドが `abortPendingActions()` → `AttributeFileListener::onRequestFinish`
  (`MegaSdkClient.cpp:155`) → `ThumbnailService::finishJob` が**破棄済み `std::mutex` を lock**。
  メインスレッドは `thread.join()` でブロック中なので助けられない。
  ダウンロード中の終了はさらに悪く、`DownloadService.cpp:188` の onDone が死んだオブジェクトで走り、
  末尾で `startNextIfIdle()`(:213) まで呼ぶ。
- 唯一の緩衝材: `QGuiApplication app` は `main.cpp:39` なので `client` より後に壊れ、`qApp` 宛の
  queued イベントは「イベントループが無いので実行されない」だけで済む。
  **だが外側のラムダが `this` を触る時点で既に手遅れ**。
- 修正方針（推奨）: `MegaSdkClient` に明示的な `shutdown()`（`mApi.reset()`）を足し、
  `app.exec()` の直後に `main.cpp` から呼ぶ。`client` は複数の shared_ptr に保持されているので
  **宣言順の入れ替えでは直せない — 明示的な停止点を作るのが唯一の手**。

**修正結果**:

- 方針どおり `MegaSdkClient::shutdown()`（`mApi.reset()`）を足し、`main.cpp` は
  `return app.exec();` を `const int exitCode = app.exec(); client->shutdown(); return exitCode;`
  にした。`shutdown()` は **`IMegaClient` には足していない** — `main.cpp:81` の `client` は
  `shared_ptr<MegaSdkClient>` で具象型が見えており、`MockMegaClient` の 28 個の `MOCK_METHOD` を
  増やす理由がない。
- **`mApi.reset()` だけでは足りなかった。** `unique_ptr::reset()` はポインタを null にしてから
  デリータを呼ぶので、`~MegaApi` の `thread.join()` 中に SDK スレッドから戻ってくるコールバックが
  `MegaSdkClient` に再入すると null 参照になる。**この再入は仮定ではなく現物にある**:
  `DownloadService` / `ThumbnailService` は onDone から（R2-2 のトランポリン経由で）次のジョブを
  開始し、`AccountService.cpp:51` は `getMyUserAttribute` のコールバック内から
  `getMyUserAttribute` を再発行する。よって **shutdown 後は不活性（inert）** にする必要がある。
- **ミューテックスは使えない**。ここが今回いちばん再学習したくない知見: コールバックは `sdkMutex` を
  保持したまま届く（R2-10）ので、自前ミューテックスで「mApi を触る」と「mApi を壊す」を囲むと
  GUI 側「自前 mutex → `sdkMutex`」/ SDK 側「`sdkMutex` → 自前 mutex」のロック順逆転になり、
  確実にデッドロックする。したがって `std::atomic<bool> mShuttingDown` を `mApi.reset()` の**前に**
  立て、**全 public メソッド 29 本 + `resolveNode`** がその先頭で早期失敗する形にした。
  1 本でも漏らすと teardown 中の再入が null 参照になるので、部分適用（`resolveNode` 1 箇所に集約する等）
  ではなく一律にしてある。エラー文言は無名 namespace の `kShutDownMessage` 1 本。
  同期エラー返しが多段再帰にならないのは R2-2 のトランポリンのおかげで、既に払ってある代償。
- 残る理論上の窓は「フラグを false と読んだ直後に GUI が reset する」だが、SDK スレッドが
  コールバックを出すのは join 中＝フラグ true・`mApi` null 確定後なので到達しない。
  ロックで塞げない以上ここが限界であることをヘッダのコメントに残した。
- `~MegaSdkClient` は `shutdown()` → `removeLoggerObject` の順にした（`shutdown()` は
  `exchange(true)` で冪等）。副次効果として **R2-16 が「代償」と記録していた「SDK の終了時ログが
  失われる」が解消した** — ロガーが `~MegaApi` の間ずっと登録されたままになる。
  併せて `MegaSdkClient.h:147-149` の宣言順コメントを R2-11 のとおり是正
  （「先に構築されるので先に登録される」は誤り、正しい理由は「後に破棄される」だけ）。
- **R2-12 の申し送りを消化**: リスナ群の先頭に「`removeRequestListener` /
  `removeTransferListener` を呼んではならない」理由をコメントで書いた。あれは unsubscribe ではなく
  `setListener(NULL)` なので、呼ぶと finish コールバックが届かず `delete this` に到達せず
  **リークする** — 名前から期待する動作の正反対、という点まで明記。
- **R2-5 の窓は狭まった**（SDK スレッドが動くのは全オーナー生存中だけになった）が、
  タブを閉じた直後の in-flight は残る。R2-5 は未着手のまま。
- 既存 373 テスト全通過、`/W4` 新規警告ゼロ。**挙動差・QML 側の変更はゼロ**（`shutdown()` は
  イベントループ終了後にしか走らない）。**自動テストによる検証は無い** — `MegaSdkClient` は
  テストターゲットに入っておらず（`tests/CMakeLists.txt:4`）、`MockMegaClient` では
  shutdown 後の不活性挙動を突けない。R4 への入力として記録する。

**手動検証の状況と、決定的にするための宿題**

ダウンロード中にウィンドウを閉じてログを確認した。**確認できたのは 2 点だけ**で、肝心の中断パスは
まだ踏めていない。

- **確認済み: R2-16 の解消**。ログに `megaapi_impl.cpp:17968 Request (DELETE) starting` が出る。
  これは `fireOnRequestStart` の行で、その `TYPE_DELETE` リクエストは **`~MegaApiImpl` の中で
  生成される**もの ＝ 旧コード（`removeLoggerObject` が `~MegaApi` より先）では**原理的に出得ない行**。
  続く `utils.cpp:2944/2949 ~MegaClientAsyncQueue() joining threads / ends` も `thread.join()` 中の
  SDK スレッド側の行で、ログが `~MegaApi` の最後まで届いていることを示す。
- **確認済み: teardown が最後まで走る**。`~MegaClientAsyncQueue() ends` は SDK 終了処理のほぼ最終行。
- **未確認: 中断コールバックが破棄済みオブジェクトに届かないこと（＝ R2-3 の本題）**。ログの
  `megaapi_impl.cpp:18157 Transfer (DOWNLOAD) finished. File: ...` は**成功ブランチ**で、
  `abortPendingActions` 経由なら `setState(STATE_FAILED)` ＋ エラー付き `fireOnTransferFinish` に
  なりその手前のエラーブランチが出る。つまり実際に起きたのは「最後の 1 件が自然完了した 1ms 後に
  閉じた」で、中断は 1 件も発生していない。完了直後に新しい転送開始のログが無い点も、
  「キューが空だった」のか「キュー送りが shutdown ガードで弾かれた」のか**ログでは区別できない**
  （後者なら SDK ログは一切出ない）。

**宿題（決定的にする手順）**:

1. 数十 MB 以上のファイルを先頭に、後ろに数件キューしてダウンロードを開始する。
2. 進捗バーが**動いている最中**にウィンドウを閉じる（自然完了を待たない）。
3. 期待する所見:
   - `Request (DELETE) starting` の**後**に、中断された転送のエラー行（`API_EACCESS` 相当）が出る。
   - その後もログが `~MegaClientAsyncQueue() ends` まで到達する。
   - プロセスが 0 で終了する（`echo %ERRORLEVEL%`）。
4. ガードが実際に踏まれたかまで見たい場合は、`MegaSdkClient::download` のガード内に一時的な
   `qCWarning` を 1 行入れて同じ操作をする。恒久的に入れないのは、shutdown 後の失敗は
   誰にも surface されない正常系であり、ログを汚すだけのため。
5. サムネイル側（`ThumbnailService` の再入経路）は、大きめのフォルダをグリッド表示で開いて
   サムネイル取得中に閉じることで同様に踏める。

**R2-4 [中・実行時到達可能] `ThumbnailController::mModel` が他オブジェクトの内部を指す**

- `ThumbnailController.h:56` の `FileListModel* mModel` は
  `navigation->fileListModelForThumbnails()`(`main.cpp:137`) ＝
  `&FolderNavigationController::mFileListModel`（`FolderNavigationController.h:366` の**値メンバ**、
  `.cpp:76-79` がそのアドレスを返す）。
- `TabContext`（`TabsController.h`）は `navigation` と `thumbnails` を**独立した shared_ptr** で持ち、
  逆順（thumbnails → navigation）に壊れる。
- 再現シナリオ（シャットダウンではなく**通常操作**）: サムネイル取得中にタブを閉じる
  （`TabsController.cpp:138` の `mTabs.erase`）。`thumbnails` は `ThumbnailController.cpp:49` の
  `self = shared_from_this()` で生き残るが、`navigation` の参照カウントは 0 になり
  `FolderNavigationController` ごと `mFileListModel` が消える。`ThumbnailController` 自身は
  生きているので `removePostedEvents` は働かず、GUI スレッドのラムダが `.cpp:60` の
  `mModel->setThumbnailPath(...)` を解放済みメモリに対して実行する。
- **`enable_shared_from_this` は自分自身しか守らない**、という点が既存コメント
  （`ThumbnailController.h:23-30`）から抜けている。
- 修正方針: `FileListModel` を値メンバから `std::shared_ptr` にして `ThumbnailController` が
  共有所有する（**R5 の `FolderNavigationController` 解体と方向が一致する**）。
  応急なら `ThumbnailController` に `navigation` の shared_ptr を持たせるだけでも塞がる。

**R2-5 [中] `self` が SDK スレッドで最後の参照を落とすと QObject が異スレッドで破棄される**

- 外側ラムダは SDK リスナが所有し、リスナは SDK スレッドで `delete this` する
  （`MegaSdkClient.cpp:91,137,167,195,235,338,380`）。その時点でクロージャが壊れて `self` が解放される。
- タブが既に閉じられていれば `self` が**最後の参照**となり、`~FolderNavigationController` が
  **MEGA SDK スレッドで走る**。このオブジェクトは GUI スレッド affinity の
  `QTimer mBusyDelayTimer`（`FolderNavigationController.h:383`）を値で持つので、`~QTimer` が
  「Timers cannot be stopped from another thread」経路に入りタイマ ID を漏らし、
  `removePostedEvents(this)` が GUI スレッドのキュー処理と競合する。
- `ThumbnailController` も同型（タイマが無いぶん軽症）。既存コメント群はこの向きに一切触れていない。
- R2-3 を直せば「SDK スレッドが動くのは全オーナー生存中だけ」が保証されるので窓は狭まるが、
  タブを閉じた直後の in-flight は残る。

**R2-6 [中] `invokeOnGuiThread` が 8 コピー・2 セマンティクス**（種は「4 箇所」と書いていた）—
**修正済み（2026-08-06）**

| 変種 | ターゲット | 定義箇所 |
| --- | --- | --- |
| A | `qApp` | `AuthController.cpp:29`, `DownloadController.cpp:24`, `UploadController.cpp:21`, `FolderTreeModel.cpp:20`, `QuickAccessModel.cpp:17` |
| B | 引数 `target`（全呼び出しで `this`） | `FolderNavigationController.cpp:40`, `ThumbnailController.cpp:22`, `AccountController.cpp:19` |

- 差の意味は `FolderNavigationController.cpp:31-39` のコメントが正確に説明している
  （`~QObject` の `removePostedEvents(this)` が B のイベントだけを落とす）。**種の指摘どおり差は意味を持つ**。
- 問題は**分類の根拠が「アプリ寿命かどうか」という散文にしかない**こと: `AccountController` は
  アプリ寿命なのに B、`FolderTreeModel`/`QuickAccessModel` はアプリ寿命で A。コメントが互いを
  相互参照する連鎖（`AccountController.cpp:16-18` → Thumbnail/FolderNav、
  `FolderTreeModel.cpp:15-19` → Download）になっており、規則ではなく慣習。
- **B は A の上位互換**（アプリ寿命のオブジェクトが `this` を渡しても損は無い）なので、
  統一先は B 一本でよい。header-only の `src/qml/GuiThread.h` に 1 つ置いて 8 コピーを消す。
- なお `src/qml` の他の跨スレッド手段は**ゼロ**: `QTimer::singleShot` 0 件、
  `Qt::QueuedConnection` の `connect` 0 件、非 GUI スレッドからの `emit` 0 件、`moveToThread` 0 件。

**修正結果**:

- `src/qml/GuiThread.h`（header-only、グローバル `inline`、名前空間なし ＝ `tests/TestApp.h` の
  `flushQueuedEvents()` と同じ既存様式）に 1 つ置き、8 つの匿名名前空間内コピーを消した。
  シグネチャは `std::function<void()>` 据え置き、target のスレッド affinity を検査する
  `Q_ASSERT` は入れていない（規約は文章で残す方針、R2-10 と同じ扱い）。
- 呼び出しは全 33 箇所で、**すべて QObject のメンバ関数内で `[this, …]` をキャプチャしていた**ので、
  A 変種の 11 箇所に `this,` を足すだけで済んだ。B 変種の 22 箇所は無変更。
  8 ファイルとも `<QCoreApplication>`/`<QMetaObject>` が未使用になったので落とした。
- **挙動が変わったのは A→B にした 5 クラス（`Auth`/`Download`/`Upload`Controller、
  `FolderTree`/`QuickAccess`Model）の破棄時だけ**: 未処理のキュー済みイベントが
  `~QObject` の `removePostedEvents` で落ちるようになった。5 つとも `main.cpp` の
  スタックローカル＝アプリ寿命なので実質シャットダウン時のみで、かつ安全側の変化。
- **統一しても覆えない窓が残る**ことを `GuiThread.h` のコメントに明記した:
  「SDK スレッドが外側ラムダに入ってから、この関数がイベントを投函するまで」。タブ単位の
  コントローラは外側ラムダの `shared_from_this()`、アプリ寿命のクラスは R2-3 の
  `MegaSdkClient::shutdown()` 停止点が覆っている。**R2-5 はこれでは塞がらない**。
- コメントの相互参照連鎖（`AccountController` → Thumbnail/FolderNav、`FolderTreeModel` → Download）は
  定義ごと消滅。これで嘘になる 4 件（`ThumbnailController.h` / `FolderTreeModel.h` /
  `QuickAccessModel.h` の「qApp を狙う」記述、`tests/TestApp.h` と `tests/FolderTreeModelTest.cpp` の
  参照先）も同時に直した。
- 副作用: `QuickAccessModel.cpp` の `std::find_if` 周り 6 行が、リポジトリの clang-format
  （設定は v18 想定、ローカルは v22）の版差で再整形された。本件とは無関係だが、
  フォーマッタフックが編集時に自動適用するため戻せない。

**R2-7 [中] コントローラがサービスへ登録した生 `this` コールバックを解除しない** —
**修正済み（2026-08-06、R2-3 と同時）**

- `DownloadController.cpp:36/41`, `UploadController.cpp:35/40` は `setOnProgress`/`setOnJobFinished`
  に `[this]` を永続登録するが、**どのコントローラにもデストラクタが無い**
  （`src/qml` の `~` は `FolderTreeModel`/`QuickAccessModel` の `= default` のみ）。
- サービス（`main.cpp:85-86`）はコントローラ（`:104-105`）より先に宣言＝後に破棄されるので、
  R2-3 の窓でこの `std::function` が呼ばれうる。`DownloadService.cpp:183/208` は observer を
  ロック下でコピーしロック外で呼ぶ設計なので、コピーされた `std::function` がぶら下がった
  `this` を保持したまま起動する。
- 今日は外側ラムダ本体が `this` を実際には触らない（`invokeOnGuiThread` へ転送するだけ）ので
  辛うじて生きているが、`UploadController.cpp:42` の `--mBatch.pendingJobs;` は内側ラムダにあり、
  イベント処理が 1 回でも走れば死んだオブジェクトを触る。**デストラクタ 1 つで塞げる。**

**修正結果**:

- `~DownloadController` / `~UploadController` を足し、`setOnProgress(nullptr)` /
  `setOnJobFinished(nullptr)` で解除した。空の `std::function` を入れて壊れる経路は無い
  （setter は `mMutex` 下で代入し、呼び出し側はロック下でコピーしてから `if (onProgress)` で
  空チェックする ＝ R2-13 が確認済みの形）。
- **位置づけは R2-3 で変わった**: 両コントローラは `main.cpp` のスタック上でアプリ寿命なので、
  R2-7 の窓は**シャットダウンしか無かった**。その窓は R2-3 の停止点が閉じたので、この解除は
  実害を消す変更ではなく「登録したら解除する」という規律の担保である。
- **解除できないものが 1 つ残る**: `UploadController.cpp:59` の
  `mFileOps->moveToRubbish(..., [this]…)` は一発限りのコールバックで、解除 API が無い。
  安全なのは R2-3 の停止点のおかげ（サービスもコントローラも生存中）で、その旨をコメントに残した。

**R2-8 [中・設計] `mQueue.front()` の「先頭だけが in-flight」前提**

- コールバック側のガードは全て `if (mQueue.empty()) return;`（`DownloadService.cpp:179,194`,
  `UploadService.cpp:132,147`）で、**空かどうかしか見ておらず同一性を見ていない**。
  `job.id` はコールバックに渡っていない。
- 今日成立しているのは 3 つの偶然による:
  1. `startNextIfIdle` が `state = Active` を**ロック下で**立てる（`DownloadService.cpp:163-165`）
     ので、同時 `enqueue()` による二重開始は起きない（**ここは問題なし**）。
  2. キュー先頭を消せる API が存在しない（`cancel(jobId)` は `DownloadService.h:82` に
     「将来やる」と書かれているだけ）。
  3. SDK が 1 転送につき `onTransferUpdate` → `onTransferFinish` の順を保証する。
- `cancel(jobId)` を実装した瞬間に、古い `onProgress` が**別のジョブ**の
  `transferredBytes`/`state`/`errorMessage` を書き潰し、`:209` の `erase(begin())` が
  無関係なジョブを消す。上の §1 が書いている `std::optional<Job> mActive` ＋待ち行列分離が
  正しい方向で、加えて**コールバックに job.id を持たせる**必要がある。
  R5 のサービス整理とまとめたほうが安い。
- `ThumbnailService` はこの種のバグに**構造的に免疫**（`deque<handle>` をロック下で pop し、
  ジョブはハンドルで引く、`finishJob` もハンドル keyed）。良い側の実例。

#### 種が不正確だった / 判断が要るもの

**R2-9 [中・設計・R2 では直せない] 同期例外 7 個は「安全だが GUI をブロックする」**

- **種が心配していた「GUI スレッドが読む間に SDK スレッドがノードツリーを書き換える」データ競合は
  存在しない。** 7 つの同期例外は全て SDK 内部で `sdkMutex` を取ることを確認した:

  | # | `IMegaClient.h` | SDK 側のロック根拠（`megaapi_impl.cpp`） |
  | --- | --- | --- |
  | 1 | `currentSessionToken` | `:7906` |
  | 2 | `currentUserHandle` | `:7228-7231` |
  | 3 | `checkMove` | `:19572`, `:10924`, `:18737` |
  | 4 | `checkUpload` | `:19572`, `:12606` |
  | 5 | `findChildFiles` | `:19417` |
  | 6 | `hasSubfolders` | `:19210` |
  | 7 | `currentAccountIdentity` | `:7196`（`mLastRecievedLoggedMeMutex`）, `:7230` |

  （`getChildren` `:19275` / `search` `:13268` も同様。返る `MegaNode` は
  `MegaNodePrivate::fromNode` によるスナップショットコピーなので、ロック解放後も有効。）
- **実在するのは競合ではなく待ち時間**。`sdkMutex` は `mutable std::recursive_timed_mutex`
  （`megaapi_impl.h:5307`）で、SDK ループは `megaapi_impl.cpp:8147` の
  `SdkMutexGuard g(sdkMutex); client->exec();` として長時間保持する。`IMegaClient.h:63-71` が
  自分で記録している 640k ノードの `fetchNodes` ≒ 218 秒の decrypt は**この `exec()` の中**。
  その間にドラッグホバーの `checkMove`（`IMegaClient.h:265-267` が「連続的に問い合わせられる」と明記）や
  `hasSubfolders`（`:316-319`）が来れば GUI がその分固まる。
- クラッシュではなくハングのクラスで、**インタフェースの設計そのもの**（「その場で答える」が定義）
  なので R2 の修正対象にはならない。`docs/ARCHITECTURE.md` に記録し、R5 の入力にする。
- ついでに確認できた良い性質: 再入は安全（`ThumbnailService::finishJob` → `startNextIfCapacity` →
  `getThumbnail` → `getNodeByHandle` が同一スレッドで `sdkMutex` を再取得する）。ただし
  **`recursive_timed_mutex` であることに依存している** — SDK の実装詳細への依存として記録。

**R2-10 [中・不文律] `Qt::BlockingQueuedConnection` は 1 つでも入れたら恒久デッドロック**

- コールバックは `sdkMutex` を保持したまま配達され（`megaapi_impl.cpp:20809`, `:19904`, `:18046`）、
  GUI スレッドは R2-9 の同期メソッドで同じ `sdkMutex` を取る。
- したがって将来どこかのコールバック経路に `Qt::BlockingQueuedConnection` を入れると、
  SDK スレッドが `sdkMutex` を持って GUI を待ち、GUI は `checkMove` の中で `sdkMutex` を待つ。
- 現状は全箇所 `Qt::QueuedConnection` なので安全だが、**この不文律はどこにも書かれていない**。

**R2-11 [低] 誤っているコメント 4 件**

- `DownloadService.h:117-120` / `UploadService.cpp:80-83` — 「モック下でしか再帰しない」＝**嘘**（R2-2）。
  **是正済み（2026-08-06、R2-2 と同時）**。`ThumbnailService.h` の「no loop is needed」も同様に是正した。
- `MegaSdkClient.cpp:338,380` — 「`delete this;` も `mCancelToken` を壊す。SDK がここまで生存を要求する」。
  SDK は要求していない: `convertToCancelToken`（`megaapi_impl.h:1467`）が値でコピーし、
  `CancelToken` 自体が `shared_ptr<bool>` を持つ共有ハンドル（`mega/types.h:1223-1227`）。
  害は無い（過剰保持）が、リスナの状態を削る改修を妨げる。なお `cancel()` は `src/` のどこからも
  呼ばれておらず、現状は純粋なオーバーヘッド（= `cancel(jobId)` 実装時のフック）。
- `MegaSdkClient.h:147-149` — **是正済み（2026-08-06、R2-3 と同時）**。
  「`mApi` より前に宣言＝先に構築されるので、`mApi` が何かログを出す前に
  ロガーが登録される」。前半（後に破棄される）は正しく load-bearing だが、後半は**誤り**:
  登録はコンストラクタの**本体**（`MegaSdkClient.cpp:398`）で、`mApi` のメンバ初期化＝
  `MegaApiImpl::init` は既に終わっており、`init` は最後に SDK スレッドを起動する
  （`megaapi_impl.cpp:7140-7143`）。その間のログ行は落ちる（実害は起動時ログのみ）。
- `main.cpp:98-103` — 宣言順による寿命保証。通常経路では正しいが、**shared_ptr で延命された
  コントローラ**（R2-5）は `clipboard`/`notifications` より長生きしうるので、そのケースを覆っていない。

#### 問題なしと確認できた種

**R2-12 リスナのリークは無い — 種の「リクエストが発行されず listener が漏れる経路」は空振り**

- アプリ側: 18 箇所すべて `new` は `mApi->…` 呼び出しの引数位置にあり、早期 return は全て `new` より前
  （`MegaSdkClient.cpp:497,518,549,573,591,621,644,657,667,682,691,709,718,742,899`）。
  「確保してから bail」という形はどこにも無い。
- SDK 側: `startDownload`(`megaapi_impl.cpp:10416`) / `startUpload`(`:10365`) / `getUserAttr`(`:9425`)
  に早期 return が無く、必ずキューに積まれる。
- 所有権契約: SDK はリスナを**削除しない**（`:18032`, `:18192` は request/transfer だけを delete、
  `delete listener` は 0 ヒット）。よって `delete this` の作法は正しい。
  エラー・中断時も `onRequestFinish` は必ず発火する（`:20830`, `:9692`）ので取りこぼしも無い。
- **落とし穴として記録すべき点**: `removeRequestListener`（`:17895-17903`）は `setListener(NULL)` する。
  つまり「終了処理が無い」と言って後からこれを呼ぶと**確実にリークする**。
  呼んでいないのが正解、という理由が今どこにも書かれていない（R2-3 の修正時に併記する）。
  → **併記済み（2026-08-06、R2-3 と同時）**。`MegaSdkClient.cpp` のリスナ群の先頭に、
  自己削除が唯一正しい作法である根拠ごと書いた。

**R2-13 `std::function` observer の扱いは正しい**

- 全てロック下でコピー/move してからロック外で呼ぶ（`DownloadService.cpp:183-186`,
  `UploadService.cpp:136,160`, `ThumbnailService.cpp:76,82`）。呼び出し中の再代入で壊れる穴は無い。

**R2-14 `shared_from_this()` は正しく捕獲パスに入っている**

- `FolderNavigationController` の非同期 17 箇所すべてと `ThumbnailController.cpp:49` で、
  外側ラムダの**キャプチャリスト内**で評価されている（構築時や登録時だけ、ではない）。
- 全インスタンスが `make_shared` 生成（`main.cpp:134,136`,
  `tests/FolderNavigationControllerTest.cpp:78`）なので `bad_weak_ptr` 経路は現時点で存在しない。
  ただし**コンストラクタが public のまま**なので不変条件は強制されていない（`static create()` 化は R5 で）。
- リポジトリ全体で `weak_ptr` は**ゼロ**。この設計には weak-lock のイディオムが無い、という事実。

**R2-15 QObject の生成スレッド / `moveToThread` / タイマの start・stop スレッドは全て正常**

- `src/qml` に `new <QObject>` は 1 つも無く、`moveToThread` はリポジトリ全体でゼロ。
- `mBusyDelayTimer`（`FolderNavigationController.h:383`）と `mStallTimer`（`AuthController.h:155`）の
  `start()`/`stop()` は全て GUI スレッド経路。「非 GUI スレッドから start して黙って発火しない」は無い。
- 唯一の affinity 違反は R2-5 の**破棄**。

**R2-16 SDK ロガーブリッジは両側でスレッド安全**

- SDK 側は `third_party/sdk/src/logging.cpp:92-102,130-150` の `recursive_mutex` で登録と配達を保護。
- アプリ側は `src/app/Logging.cpp:111-140` の `QMutexLocker`。`logFile()` は関数ローカル static で
  そのミューテックス下でしか触られない。
- 破棄順も正しい（`removeLoggerObject` が `~MegaSdkClient` の**本体** = `mApi` 破棄より前、
  `MegaSdkClient.cpp:403`）。代償として SDK の終了時ログは失われる（意図的かは不明、1 行コメント推奨）。
  → **この代償は R2-3 で解消（2026-08-06）**。デストラクタが `shutdown()` →
  `removeLoggerObject` の順になり、`~MegaApi` の間ロガーが登録されたままになった。

**R2-17 非所有生ポインタの宣言順保証は（R2-5 のケースを除き）成立**

- `NotificationController`(`main.cpp:100`) / `ClipboardController`(`:103`) は、それを持つ
  コントローラより前に宣言されている。通常の破棄経路では正しい。
- `src/qml` の非所有ポインタ全リスト: `DownloadController.h:77`, `UploadController.h:167`,
  `FolderNavigationController.h:364,365`, `ThumbnailController.h:56,57`, `TabsController.h:150`,
  `FolderTreeModel.cpp:136`（世代ガード付きで正しい）。
  **他オブジェクトの内部を指しているのは `ThumbnailController.h:56` だけ**（= R2-4）。

**R2-18 `std::atomic` が無いのは現状問題ない**

- `mHasActiveJob`/`mActiveJob`（`DownloadController.h:78-79`）、`mLoadGeneration`
  （`AuthController.h:148`）はいずれも GUI スレッドでしか読み書きしていない。

#### 新規発見

**R2-19 [検証の穴・修正の前提] テストが全てシングルスレッドで、R2 の修正を検証できない**

- `tests/MockMegaClient.h` は純 gmock で、全テストが `InvokeArgument<N>` により**テスト自身の
  スレッドで同期に**コールバックを呼ぶ（`tests/DownloadServiceTest.cpp:73,104,126,152-155`,
  `ThumbnailServiceTest.cpp:13,37,88,125` ほか）。
- `grep "std::thread\|QThread\|std::atomic\|std::async"` は `src/` `tests/` ともに**ゼロヒット**。
- 帰結:
  - mutex は CI で一度も競合せず、**ロックを消す変更もテストを通ってしまう**。
  - 同期再入（R2-2）は 1 段だけテストされており、多段再帰は見えない。
  - R2-1 の TOCTOU はシングルスレッドでは**到達不能＝現行の枠組みでは検証不可**。
- ただし**多段再帰（R2-2）だけは単一スレッドのまま再現できる**（「同期失敗を N 件返すモック」で足りる。
  再帰はスレッドと無関係）。TOCTOU とシャットダウン UAF は設計上の証明＋手動確認か、
  `MockMegaClient` へのワーカスレッド配達モード追加（R4 の領分）かの二択。

**R2-20 [低] `MegaCancelToken` を作っているが `cancel()` はどこからも呼ばれていない**

- `MegaSdkClient.cpp:523,555` で生成しリスナに持たせているが、`src/` 全体で `cancel()` の
  呼び出しはゼロ。現状は純粋なオーバーヘッドであり、同時に「転送キャンセル」を実装するときの
  既製フックでもある（R2-8 の `cancel(jobId)` と同じ話）。

**R2-21 [低] `QuickAccessService::pins()` がメンバ vector への参照を返す**

- 現状シングルスレッド・同期呼び出しなので実害なし。R2-2 のモード 2 依存と同じ「同期だから安全」
  の系列にあることだけ記録する。

**R2-22 [低] `currentAccountIdentity` は 2 つの別ロックの合成**

- `MegaSdkClient.cpp:835-859` は `getMyEmail`（`mLastRecievedLoggedMeMutex` 下）と
  `getMyUserHandleBinary`（`sdkMutex` 下）を続けて読む。理論上、間にログアウトが入ると
  email 非 null ＋ handle `INVALID` の裂けた値を返しうる（`:839` の `if (!email)` ガードは通過する）。
- 実害のある呼び出し側は見つからなかった。`AccountIdentity` を単位として検証するかは R5 で判断。

#### 推奨実施順（各項目 1 セッション）

```
R2-1  currentJob() の optional 化            … 単独・小・UB を確実に消す      [済 2026-08-06]
R2-7  コントローラのデストラクタで observer 解除 … 単独・極小                  [済 2026-08-06]
R2-6  invokeOnGuiThread を B に統一（8→1）    … 単独・機械的                  [済 2026-08-06]
R2-2  再帰のループ化 + IMegaClient 契約の書き直し … R2-19 の「同期失敗 N 件モック」とセット [済 2026-08-06]
R2-3  MegaSdkClient::shutdown() の導入        … 寿命系の根。R2-5 の窓もここで狭まる [済 2026-08-06]
R2-4  FileListModel の shared_ptr 化          … R5 の解体と方向一致。R5 に送る選択肢もあり
R2-9/R2-10/R2-11  docs/ARCHITECTURE.md への記録とコメント是正 … 締め
R2-8  キューの optional<Job> mActive 化       … R5 のサービス整理に合流させるのが安い
```

#### 成果物

- `docs/ARCHITECTURE.md` に「スレッドモデル」節を 1 つ。内容は上の「前提」+ R2-9（`sdkMutex` が
  GUI をブロックしうること）+ R2-10（`BlockingQueuedConnection` 禁止）+ R2-12（`removeRequestListener`
  を呼んではいけない理由）。R1 が同ファイルに足した「サーバ由来文字列の信頼境界」と並ぶ形にする。

### R3 — 未着手
### R4 — 未着手
### R5 — 未着手
### R6 — 未着手
### R7 — 未着手

## 5. 持ち越し（スコープ外で見つかった事項）

（担当スコープが来るまでここに置く）

- **[R3] SDK の英語 `errorMessage` がそのまま UI に出ている** — `qml/components/ToastStack.qml:85`
  の `qsTr("Failed to download %1: %2").arg(fileName).arg(errorMessage)`。`errorMessage` は
  `Result::fail()` の生文字列。`AuthController` は `classifyError` で構造化してから渡しているのに
  （`src/qml/AuthController.cpp:155-159`）、ダウンロードだけ生のまま。R3 の「`fail()` の
  `errorMessage` がそのまま UI に出る経路がないか」の答えの 1 つ。（R1 調査中に発見）
- **[R4] `DownloadController` にテストが無い** — `tests/UploadControllerTest.cpp:19` が理由を
  「`QDesktopServices` が QtGui を引く」と説明している。R1-1 の修正で `computeDestinationPath` に
  検証ロジックが入るなら、テスト可能な形（`src/core` の純関数）に出すのが望ましい。（R1 調査中に発見）
- **[スコープ未割当] 配布パイプラインが存在しない** — `windeployqt` /
  `qt_generate_deploy_qml_app_script` / CPack のいずれも `CMakeLists.txt` に無い。`install()` しても
  実行可能なツリーにならない。R1-2 はライセンスファイルの同梱だけを閉じ、デプロイ整備はここに残す。
  なお `CMakePresets.json` には `msvc-debug` しかなく、**Release プリセットも無い**ので、着手時は
  そこからになる。（R1 調査中に発見、Release の件は R1-2 実施時に追記）
- **[R4] テストが全てシングルスレッドで、スレッド起因の欠陥を構造的に検出できない** —
  `tests/MockMegaClient.h` は純 gmock で、全テストが `InvokeArgument<N>` によりテスト自身のスレッドで
  同期にコールバックを呼ぶ。`std::thread`/`QThread`/`std::atomic` は `src/` `tests/` ともにゼロヒット。
  mutex は CI で一度も競合せず、**ロックを消す変更もテストを通ってしまう**。R4 で
  「ワーカスレッドから完了を配達するモックモード」と ASan 構成を検討する。詳細は R2-19。（R2 調査中に発見）
- **[R5] `FolderNavigationService` / `QuickAccessService` が mutex 無しで安全な根拠が別ファイルにある** —
  根拠（`MegaSdkClient` の `getChildren`/`getNodeInfo` が同期であること）は
  `src/core/DownloadService.h:49-51` に書かれており、当の 2 ファイルには何も無い。R2-2 の契約書き直しで
  一部は移すが、サービス整理そのものは R5。（R2 調査中に発見）
