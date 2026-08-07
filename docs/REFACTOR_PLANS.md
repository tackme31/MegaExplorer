# REFACTOR_PLANS.md — リリース前コード整理の計画

Phase 15/16 に入る前の一斉整理。**一気にレビューしない**ためのスコープ分割と実施順を定める。

前提: CLAUDE.md「Pre-release: refactoring existing code during planning is allowed」— 外部 API /
データ互換性の制約はないので、製品挙動を変えない限り既存設計の作り直しを優先してよい。ただし
**製品レベルの挙動・スコープを変える変更は事前に合意を取る**（この文書の各スコープも同じ扱い）。

このファイルの位置づけ:

- 何を・どの順で見るかの**計画**。個々の指摘と修正結果は各スコープ実施時にこのファイルの
  「実施ログ」節へ追記する（`docs/DESIGN_IMPROVEMENT.md` が S* 段階でやっているのと同じ形）。
- C++/構造の話。見た目・余白・配色は `docs/DESIGN_IMPROVEMENT.md` の担当で、ここには書かない。
- **1 項目の実施ログは 100 行程度まで。** 書く価値があるのは後からコードを読んでも再現できない
  判断（外れた前提、試して捨てた案、形を決めた制約）で、diff を見れば分かることの言い換えは
  要らない。それ以上の分量が要る調査は `docs/investigations/` に独立した文書として置き、ここから
  リンクする。

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
  → **決着（2026-08-07）**: 本体を MIT に再ライセンスしたので GPLv3 §6 の義務自体が消えた。ただし
  LibRaw が静的リンクの LGPL であるため、同じ URL が今度は LGPL-2.1 §6 の再リンク手段として要る。
  `THIRD-PARTY-NOTICES.txt` に明記済み（`docs/PROGRESS.md` の Phase 20b 追補を参照）。

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

### R2 — 調査済み / R2-1〜R2-7・R2-9〜R2-11 対応済み、残るは R2-8（2026-08-06）

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

**R2-4 [中・実行時到達可能] `ThumbnailController::mModel` が他オブジェクトの内部を指す** —
**修正済み（2026-08-06）**

- `ThumbnailController.h:54`（種は `:56` と書いていた）の `FileListModel* mModel` は
  `navigation->fileListModelForThumbnails()`(`main.cpp:137`) ＝
  `&FolderNavigationController::mFileListModel`（`FolderNavigationController.h:366` の**値メンバ**、
  `.cpp:50-58` がそのアドレスを返す。種の `:76-79` は古い）。
- `TabContext`（`TabsController.h`）は `navigation` と `thumbnails` を**独立した shared_ptr** で持ち、
  逆順（thumbnails → navigation）に壊れる。
- 再現シナリオ（シャットダウンではなく**通常操作**）: サムネイル取得中にタブを閉じる
  （`TabsController.cpp:138` の `mTabs.erase`）。`thumbnails` は `ThumbnailController.cpp:31` の
  `self = shared_from_this()` で生き残るが、`navigation` の参照カウントは 0 になり
  `FolderNavigationController` ごと `mFileListModel` が消える。`ThumbnailController` 自身は
  生きているので `removePostedEvents` は働かず、GUI スレッドのラムダが `.cpp:42` の
  `mModel->setThumbnailPath(...)` を解放済みメモリに対して実行する（種の `.cpp:49/60` は古い）。
- **`enable_shared_from_this` は自分自身しか守らない**、という点が既存コメント
  （`ThumbnailController.h:23-30`）から抜けている。
- 修正方針: `FileListModel` を値メンバから `std::shared_ptr` にして `ThumbnailController` が
  共有所有する（**R5 の `FolderNavigationController` 解体と方向が一致する**）。
  応急なら `ThumbnailController` に `navigation` の shared_ptr を持たせるだけでも塞がる。

**修正結果**:

- 本線（`shared_ptr<FileListModel>`）を採った。応急案は `ThumbnailController` が
  `FolderNavigationController` 全体に依存することになり、R5 の解体と逆方向。
- `FolderNavigationController` 側: `mFileListModel` を `std::shared_ptr<FileListModel>` にして
  コンストラクタで `make_shared`、`fileListModelForThumbnails()` の戻り値を `shared_ptr` に変更、
  `.cpp` の使用 7 箇所を `->`/`.get()` に機械変換。**`Q_PROPERTY(QObject* fileListModel …)` と
  `fileListModel()` の `QObject*` は据え置いたので QML 側の変更はゼロ** — 両ビューの `model:` も
  約 19 箇所ずつの `navController.fileListModel.…` 呼び出しもそのまま。
- `ThumbnailController` 側: コンストラクタ引数とメンバを `shared_ptr` に。`.cpp:42` の
  `mModel->setThumbnailPath(...)` は表記そのまま。
- `TabsController::createTab()` に `setObjectOwnership(…CppOwnership)` を 1 行追加した。
  モデルが親無しのヒープ QObject になり `navigation`/`thumbnails` と同じ条件になったため
  （プロパティ読み取りは既定で CppOwnership なので実害があったわけではなく、同じ理由の同じ保険）。
- **R2-5 との関係**: 本件の修正は「SDK スレッドで壊れうる QObject」を 1 個増やす
  （タブを跨いで生き残った `ThumbnailController` がモデルの最後の参照を持ちうる）。
  先に入れた R2-5 の `makeGuiOwned` が `~ThumbnailController` を GUI スレッドに戻すので、
  そこから落ちるモデルの参照も GUI スレッドで落ちる。**この順序で入れたのはそのため**。
- `FileListModel.h:13-14` の「`setContextProperty()` で QML に出している」というコメントは
  Phase 9 以降すでに嘘（実際は `FolderNavigationController` のプロパティ経由）だったので同時に是正。
- 既存 373 テスト全通過、`/W4` 新規警告ゼロ。テスト側の変更は
  `tests/FolderNavigationControllerTest.cpp:161` のヘルパに `.get()` を足した 1 箇所のみ。

**R2-5 [中] `self` が SDK スレッドで最後の参照を落とすと QObject が異スレッドで破棄される** —
**修正済み（2026-08-06）**

- 外側ラムダは SDK リスナが所有し、リスナは SDK スレッドで `delete this` する
  （`MegaSdkClient.cpp:108,154,184,212,252,355,397` ＝ 7 クラスすべて、コールバックを呼んだ**後**に
  自分を消す。種が挙げていた `:91,137,167,195,235,338,380` はコンストラクタ行で、`delete this` の
  位置ではない）。その時点でクロージャが壊れて `self` が解放される。
- タブが既に閉じられていれば `self` が**最後の参照**となり、`~FolderNavigationController` が
  **MEGA SDK スレッドで走る**。このオブジェクトは GUI スレッド affinity の
  `QTimer mBusyDelayTimer`（`FolderNavigationController.h:383`）を値で持つので、`~QTimer` が
  「Timers cannot be stopped from another thread」経路に入りタイマ ID を漏らし、
  `removePostedEvents(this)` が GUI スレッドのキュー処理と競合する。
- `ThumbnailController` も同型（タイマが無いぶん軽症）。既存コメント群はこの向きに一切触れていない。
- R2-3 を直せば「SDK スレッドが動くのは全オーナー生存中だけ」が保証されるので窓は狭まるが、
  タブを閉じた直後の in-flight は残る。

**修正結果**:

- `src/qml/GuiThread.h` に `makeGuiOwned<T>(args…)` を足した（`invokeOnGuiThread` の直下、
  同じ header-only 様式）。`std::shared_ptr<T>(new T(…), deleter)` で、デリータは
  **`object->thread() == QThread::currentThread()` なら素の `delete`、そうでなければ
  `deleteLater()`**。生成箇所を `make_shared` から差し替えたのは 3 ファイル 5 箇所
  （`main.cpp` の `tabFactory` 2 つ、`tests/TabsControllerTest.cpp` 2 つ、
  `tests/FolderNavigationControllerTest.cpp` 1 つ）。`TabContext`/`TabsController` は無変更 —
  デリータは `std::shared_ptr<T>` の型に出ない。
- **同スレッド分岐を素の `delete` にしたのが設計の要**。通常経路（GUI スレッドでタブを閉じる）は
  今日とビット単位で同じ挙動になり、R2-6 が確立した「未処理のキュー済みコールバックは
  `~QObject` の `removePostedEvents` で捨てる」意味論が保たれる。
- **採らなかった案**: 18 箇所の内側ラムダにも `self` を持たせて所有権ごと GUI スレッドの
  イベントへ渡す方式。破棄スレッドは同じく直るが、オブジェクトが必ず生き残るので
  `removePostedEvents` が効かなくなり、**閉じたタブのコールバックが必ず走る**
  （閉じたタブのエラートーストが出る等の挙動変化）。編集箇所も 18 と多い。
- **受け入れた残余**: `client->shutdown()`（`app.exec()` の後）の最中に SDK スレッドが最後の参照を
  落とすと、DeferredDelete を配達するイベントループがもう無く、そのコントローラは破棄されずに
  プロセスが終わる。**クラッシュが終了時リークに変わるだけ**。
  `sendPostedEvents(nullptr, QEvent::DeferredDelete)` で流せるが QML エンジン内部の
  `deleteLater` まで巻き込むので入れていない。判断は `GuiThread.h` のコメントに残した。
- ゾンビ期間（デリータが走ってから DeferredDelete が処理されるまで）にキュー済みコールバックが
  走りうるが、**安全であることを確認済み**: オブジェクトは完全に生きており、
  `TabsController::emitRowChangedFor` は `mTabs` を線形探索して見つからなければ何もしない
  （`TabsController.cpp:282-`）、`nodesMoved`/`nodesCopied` のファンアウトも残ったタブへの
  `refreshIfShowing` で無害。
- 既存 373 テスト全通過、`/W4` 新規警告ゼロ（残る 51 件は Qt ヘッダ内の C4702 で既存）。
  **自動テストによる検証は無い** — R2-19 のとおりテストにはそもそも SDK スレッドが存在しない。

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
  `MegaSdkClient::shutdown()` 停止点が覆っている。**R2-5 はこれでは塞がらない**
  （→ 同じヘッダに足した `makeGuiOwned` が別途覆った。R2-5 の修正結果を参照）。
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

**R2-9 [中・設計・R2 では直せない] 同期例外 7 個は「安全だが GUI をブロックする」** —
**記録済み（2026-08-06）**。`docs/ARCHITECTURE.md` の「Threading model」節へ。

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

**R2-10 [中・不文律] `Qt::BlockingQueuedConnection` は 1 つでも入れたら恒久デッドロック** —
**成文化済み（2026-08-06）**。同じく `docs/ARCHITECTURE.md` の「Threading model」節へ。

- コールバックは `sdkMutex` を保持したまま配達され（`megaapi_impl.cpp:20809`, `:19904`, `:18046`）、
  GUI スレッドは R2-9 の同期メソッドで同じ `sdkMutex` を取る。
- したがって将来どこかのコールバック経路に `Qt::BlockingQueuedConnection` を入れると、
  SDK スレッドが `sdkMutex` を持って GUI を待ち、GUI は `checkMove` の中で `sdkMutex` を待つ。
- 現状は全箇所 `Qt::QueuedConnection` なので安全だが、**この不文律はどこにも書かれていない**。

**R2-11 [低] 誤っているコメント 4 件** — **4 件とも是正済み（2026-08-06）**。

- `DownloadService.h:117-120` / `UploadService.cpp:80-83` — 「モック下でしか再帰しない」＝**嘘**（R2-2）。
  **是正済み（2026-08-06、R2-2 と同時）**。`ThumbnailService.h` の「no loop is needed」も同様に是正した。
- `MegaSdkClient.cpp:338,380` — 「`delete this;` も `mCancelToken` を壊す。SDK がここまで生存を要求する」。
  SDK は要求していない: `convertToCancelToken`（`megaapi_impl.h:1467`）が値でコピーし、
  `CancelToken` 自体が `shared_ptr<bool>` を持つ共有ハンドル（`mega/types.h:1223-1227`）。
  害は無い（過剰保持）が、リスナの状態を削る改修を妨げる。なお `cancel()` は `src/` のどこからも
  呼ばれておらず、現状は純粋なオーバーヘッド（= `cancel(jobId)` 実装時のフック）。
  → **是正済み（2026-08-06）**。`delete this;` 行の嘘コメントを落とし、`mCancelToken` の宣言側に
  「SDK は要求していない / `cancel(jobId)` 実装時のフックとして持っているだけ」を書いた。
- `MegaSdkClient.h:147-149` — **是正済み（2026-08-06、R2-3 と同時）**。
  「`mApi` より前に宣言＝先に構築されるので、`mApi` が何かログを出す前に
  ロガーが登録される」。前半（後に破棄される）は正しく load-bearing だが、後半は**誤り**:
  登録はコンストラクタの**本体**（`MegaSdkClient.cpp:398`）で、`mApi` のメンバ初期化＝
  `MegaApiImpl::init` は既に終わっており、`init` は最後に SDK スレッドを起動する
  （`megaapi_impl.cpp:7140-7143`）。その間のログ行は落ちる（実害は起動時ログのみ）。
- `main.cpp:98-103` — 宣言順による寿命保証。通常経路では正しいが、**shared_ptr で延命された
  コントローラ**（R2-5）は `clipboard`/`notifications` より長生きしうるので、そのケースを覆っていない。
  → **是正済み（2026-08-06）**。保証がスタック上の保持者に限る旨と、そこから出る規約
  （コントローラのデストラクタからこの 2 つを触ってはならない）を追記。現状 `FolderNavigationController` /
  `ThumbnailController` はデストラクタを持たないので、規約違反は無い。

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
  → **R2-5 で `makeGuiOwned`（`shared_ptr<T>(new T(…), deleter)`）に変わった**ので「`make_shared`」は
  文字どおりには偽になったが、`shared_ptr` に所有されている点は同じで結論は変わらない。
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
  **他オブジェクトの内部を指しているのは `ThumbnailController.h:54` だけ**（= R2-4）。
  → **R2-4 で解消（2026-08-06）**。`shared_ptr<FileListModel>` になり、非所有生ポインタで
  他オブジェクトの内部を指す箇所は `src/qml` から無くなった。

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
R2-5  makeGuiOwned で破棄を GUI スレッドへ    … 元の一覧に無かった。R2-4 の前に置くと安全 [済 2026-08-06]
R2-4  FileListModel の shared_ptr 化          … R5 の解体と方向一致 [済 2026-08-06]
R2-9/R2-10/R2-11  docs/ARCHITECTURE.md への記録とコメント是正 … 締め [済 2026-08-06]
R2-8  キューの optional<Job> mActive 化       … R5 のサービス整理に合流させるのが安い
```

#### 成果物

- `docs/ARCHITECTURE.md` に「スレッドモデル」節を 1 つ。内容は上の「前提」+ R2-9（`sdkMutex` が
  GUI をブロックしうること）+ R2-10（`BlockingQueuedConnection` 禁止）+ R2-12（`removeRequestListener`
  を呼んではいけない理由）。R1 が同ファイルに足した「サーバ由来文字列の信頼境界」と並ぶ形にする。
  → **作成済み（2026-08-06）**。`## Threading model` として「Design: testability and dependency
  injection」と「Trust boundary」の間に挿入。上記 4 点に加えて、再入が `recursive_timed_mutex`
  という SDK 実装詳細に依存している事実（R2-9 の副産物）も残した。3 つの配達モードそのものは
  R2-2 で `IMegaClient.h` の先頭に書いたので、重複させず参照だけ張っている。

### R3 — 全項目対応済み（調査 2026-08-06、R3-9/R3-7 で完了 2026-08-07）

計画の「種」4 件を現物で検証した結果。**確認 8 件 / 種の誤り 3 件（うち 1 件は方針判断が要る）/
問題なし 6 件 / 新規 5 件**。R1・R2 と同じく、以下はそのまま plan mode の作業単位として使える粒度で
書いてある。**調査セッションではコードを一切変更していない**（以降の修正は各項目の「対応」に記録）。

#### 前提: 失敗の表現は 1 つではなく 3 つある

種は `Result<T>` だけを対象にしているが、現物には並行する表現が 3 つある。以降の判断は全部これに
乗っている。

1. **`Result<T>`**（`src/core/Result.h`） — `success` / `value` / `errorMessage` / `errorCode`。
   79 箇所の `fail()` がこれ。
2. **「常に `ok()` を返す `Result`」+ 値側にエラー** — `AccountService::loadAvatar`
   （`src/core/AccountService.cpp:60-77`）は `Result<AvatarOutcome>::ok()` しか返さず、失敗は
   `AvatarOutcome::errorCode` / `errorMessage`（`src/core/AccountInfo.h:57`）に入る。外側の
   `Result` は情報をゼロビットしか運んでいない。
3. **ジョブの状態機械** — `DownloadJob` / `UploadJob` が `state == Failed` + `errorMessage` +
   `errorCode` を持つ。`src/core/DownloadService.h:34` と `src/core/UploadService.h:40` が自ら
   `// mirrors Result<T>::errorCode` と書いており、`Result` からのコピーであることを認めている。

加えて、`Result` を **`bool` に畳んで捨てる**境界が 2 つある（`FolderTreeService::hasSubfolders`、
`QuickAccessService::isUsable`）。後者が R3-2 の本体。

#### 確認された問題

**R3-1 [高] ✅対応済み `fail()` の既定コード `-1` は SDK の `API_EINTERNAL` そのもの — 79 中 53 箇所**

- `src/core/Result.h:13`・`:28` の `fail(std::string message, int code = -1)`。一方
  `third_party/sdk/include/megaapi.h:8933` が `API_EINTERNAL = -1`。つまり「コードを指定し忘れた」と
  「内部エラーだった」が同じ値で、**`MegaErrorCodes.h` には -1 のエントリが無い**ので表を見ても
  区別できない。
- 実測（`::fail(` の全 79 箇所を引数の最上位カンマで分類）。**修正時に既定引数を外して
  コンパイルエラーで数え直したところ、調査時の目視分類が数箇所ずれていた**ので、下表は
  訂正済みの実数（括弧内が調査時の記載）:

  | 区分 | 件数 | 備考 |
  | --- | --- | --- |
  | コードを明示 | 26（27） | `checkMove`/`checkUpload`/`createFolder` 系。正しい側 |
  | 既定 -1 のまま | 53（52） | 内訳 ↓ |
  | ├ `fail(kShutDownMessage)` | 29 | 表に出ない（→「問題なし」節） |
  | ├ `MegaSdkClient` のハンドル解決失敗 | 9（13） | **意味的には `kENoEnt`** |
  | ├ `WindowsSessionStore` | 6 | I/O・DPAPI 失敗 |
  | ├ `QSettingsPinnedFolderStore` | 2 | |
  | ├ `MegaSdkClient` の「not logged in」 | 3 | 調査時は「その他」に含めそこねていた |
  | ├ `MegaSdkClient:245`「Account details missing」 | 1 | 同上 |
  | └ `src/core`（`rename` / `goBack` / `search`） | 3（2） | R3-6 / R3-8 |

  `tests/` 側は「`Result<` が 452 箇所」と書いていたが、**波及するのは `::fail(` の 77 箇所、
  そのうちコード未指定は 15 箇所だけ**（残り 62 は既にコードを明示していた）。既定引数の撤廃が
  想定よりはるかに安く済んだ理由がこれ。

- 一番効く不整合はファイル内で完結している。`src/mega/MegaSdkClient.cpp:743`（`getNodeInfo`）、
  `:771`（`renameNode`）、`:819`・`:851`・`:889` などは「ハンドルが解決しない」を -1 で返すのに、
  同じファイルの `checkMove`（`:909`）と `checkUpload`（`:935`）は**まったく同じ条件**を
  `MegaErrorCode::kENoEnt` で返す。`src/core/IMegaClient.h:302-306` と `:320-326` は後者について
  「失敗コードこそが結果の本体で、呼び出し側は `errorCode` で分岐し `errorMessage` では分岐しない」と
  明記しており、規約はあるが**同期メソッド 3 本にしか適用されていない**。
- 波及先: R3-2（ピンの誤削除）、`AuthController::classifyError`（-1 → `UnknownError` → 生英文表示）。
- 修正方針: (a) `MegaErrorCodes.h` に `kEInternal = -1` を足し `static_assert` に載せる、
  (b) `fail()` の既定引数を**廃止してコードを必須**にする（呼び出し側 79 箇所が機械的に洗われ、
  「考えていない」箇所が漏れなく出る）、(c) ハンドル解決失敗 13 箇所を `kENoEnt` にする。
  (b) は 79 箇所に触るので単独セッション。
- **対応（2026-08-06、commit `007dc88` + `a8965a2`）**: (a)(b)(c) を方針どおり実施。決めた点:
  - **既定引数は完全撤廃**。`= -1` を外した状態で一度ビルドを落とし、出たエラーが上表の
    53 + 15 件と一致することを確認してから直した。**この「わざと落とす」手順が監査そのもの**なので、
    同種の作業では先にエラー一覧を取ること。
  - **シャットダウン 29 箇所は正のセンチネル** `kClientShutDownCode = 2`（`MegaSdkClient.cpp` の
    無名 namespace、`kShutDownMessage` の隣）。正にしたのは、`isSessionDefinitivelyInvalid` /
    `classifyError` の `switch` に**値域で**掛からないことを保証するため。`MegaErrorCodes.h` に
    正センチネルの台帳コメントを置き、`kNoStoredSession = 1` と並べた。
  - **SDK に対応物が無い失敗（ストア 8 + 「not logged in」3 + details missing 1）は `kEInternal`**。
    「I/O 失敗」と「保存データが壊れている」の区別は、それを実際に必要とする **R3-3 に渡した**。
  - ハンドル解決失敗 9 箇所を `kENoEnt` にしても**挙動は変わらない**ことを事前確認済み:
    `kENoEnt` で分岐するのは `UploadController.cpp:65`（供給元は `checkUpload`/`upload` で元から
    `kENoEnt`）と auth 経路 2 箇所だけで、後者に届くのは login/loginWithSession/fetchNodes の結果に
    限られ、それらは SDK リスナ経由かシャットダウンガードである。テスト 373 件が全緑で裏づけ。
  - `docs/ARCHITECTURE.md` に `## Error representation` 節を追加（R1 の信頼境界・R2 の
    スレッドモデルと並ぶ位置）。R3-4 / R3-9 は未決なので書いていない — 決まった時点で追記する
    （R3-4 / R3-9 とも 2026-08-07 に追記済み。現状は「成果物」節を見ること）。

**R3-2 [高] ✅対応済み `isUsable()` が「問い合わせ失敗」を「ピンが消えた」に畳み、ピンを永久削除する**

- `src/core/QuickAccessService.cpp:99-102`:
  `return resolved.success && resolved.value.isFolder && resolved.value.inCloud;`
  — 3 つの別々の意味（**呼べなかった** / フォルダでない / ゴミ箱にある）が 1 つの `false` になる。
- `src/qml/QuickAccessModel.cpp:183-195` はその `false` を「dangling pin」と断定してログに出し
  （`qCInfo(lcQuickAccess) << "dropping dangling quick-access pin"`）、`:228-234` で `replaceAll`
  → **`IPinnedFolderStore::save` まで走ってディスクから消える**。ユーザ操作は不要、ログイン直後の
  `validateAll()` が自動で行う。
- `getNodeInfo` の失敗は現状 shutdown 中か「ハンドル未解決」の 2 つで、**どちらも -1**（R3-1）。
  区別しようにも情報が無い、という形で R3-1 と一組になっている。
- 修正方針: R3-1(c) の後に、`isUsable` を 3 値（`Usable` / `Gone` / `Unknown`）にし、`Unknown` は
  ピンを**残す**。`AuthService` の `isSessionDefinitivelyInvalid` が「未知のコードは transient 扱い、
  誤って捨てるより残す方が安い」と同じ判断を既に文書化している（`src/core/AuthService.h:17-25`）ので、
  規約はそこから借りればよく、新しく決めることは無い。
- **対応（2026-08-06）**: 方針どおり 3 値化。決めた点:
  - **実害の形が調査時より具体化した**。R3-1 後の `getNodeInfo` の失敗はちょうど 2 種類
    （`kClientShutDownCode = 2` / `kENoEnt`）で、前者は**掃引中の全ピンに一斉に起きる**。つまり
    ログイン直後にアプリを閉じると N 件すべてが「dangling」判定 → `replaceAll({})` → `save()` で
    **ピンが全滅する**。これが直った本体。
  - **`kENoEnt` は `Gone` 側に置いた**（＝既存の削除挙動を維持）。MEGA の通常の削除はゴミ箱移動なので
    解決には成功し `inCloud == false` で落ちる。`kENoEnt` まで到達するのはゴミ箱を空にして完全削除
    された場合で、これを `Unknown` にすると**そのピンは永久に残り、クリックしても「確認できません」
    としか言えなくなる**。既存テスト `ReloadDropsAPinWhoseHandleNoLongerResolves` が無改変で通ることが
    この判断の固定になっている。
  - **allowlist 形（`default` → `Unknown`）にした理由は 2 つ**。`isSessionDefinitivelyInvalid` と
    同じ形にして規約の出所を明示すること、および `kClientShutDownCode` が `MegaSdkClient.cpp` の
    無名 namespace にあり `src/core` から名前で参照できないこと — 「シャットダウンだけ `Unknown`」と
    いう書き方はそもそも取れない。結果として将来 SDK コードが増えても勝手に削除側へ回らない。
  - **掃引の判定は `== Usable` ではなく `!= Gone`**。`Unknown` は名前も更新しないので、コミット時の
    `survivors` が現行リストと完全一致し、既存の等値ガード（`survivors == mService->pins()`）が効いて
    **`save()` が 1 回も呼ばれない**。「掃引が見なかったハンドルは素通り」という既存の扱いと形が揃う。
  - **クリック時の `Unknown` は `missing()` に流さない**。あれは「Quick access から削除しますか？」
    ダイアログ直結なので、確認できなかっただけで削除を提案してしまう。代わりに固定文トースト
    `quickAccessUnavailable` を出す。**新シグナルは作らず** `QuickAccessModel` から
    `NotificationController` を直接叩いた — R3-3 の永続化失敗ハンドラと同じ作法で、`Main.qml` は
    コメント以外無変更。文言に `%1` を使わないのも R3-3 と同じ理由（原因が打ち切りなので SDK の
    英語文字列は何も説明しない）で、**R3-4 の enum 化が来てもこの行は動かない**。
  - `FolderTreeService::hasSubfolders` も `Result` を `bool` に畳んでいる（前提節参照）が、
    **今回は触っていない**。失敗時に `false`（＝展開矢印を出さない）へ倒すのは既にコメント済みの
    安全側で、データが消える経路が無い。棚卸し表でも「対応不要」のまま。
  - `docs/ARCHITECTURE.md` の `## Error representation` に
    「Collapsing a code to a verdict: unknown means "don't act"」小節を追加。R3-1 が「決まった時点で
    追記する」と書いた枠に入れた。
  - テスト: `QuickAccessServiceIsUsableTest` を `QuickAccessServiceClassifyTest` へ移行（4 件書き換え）
    + `Unknown` 側 2 件を新規（シャットダウンのセンチネル / 未知の SDK コード）。モデル側は
    「答えが返らなかったピンを残し `save()` を呼ばない」「`Usable`/`Gone`/`Unknown` 混在で `Gone` だけ
    落とす」「クリック時に `missing` が 0 回でトーストが 1 回」の 3 件を追加。382 件全緑、
    `/W4` 新規警告ゼロ。

**R3-3 [高] ✅対応済み `IPinnedFolderStore::save()` の `Result<void>` が 4 箇所で破棄されている**

- `src/core/QuickAccessService.cpp:44`・`:56`・`:76`・`:84`（`pin` / `unpin` / `move` / `replaceAll`）。
  `(void)` キャストすら無い純粋な破棄。`QSettingsPinnedFolderStore::save` は
  `fail("failed to save quick-access pins")` を返しうる（`src/platform/QSettingsPinnedFolderStore.cpp:91`）。
- 結果: 保存に失敗するとピンは**次回起動で消えるのに、ログもトーストも出ない**。
- `Result` に `[[nodiscard]]` が無いため MSVC の C4834 が出ず、`/W4` は何も言わない。
  対照的に `AuthService` は同じ状況で `(void)mSessionStore->clearSession();` と意図を書いている
  （`src/core/AuthService.cpp:34`・`:49`・`:94`・`:106`・`:116`）。**同じコードベース内で作法が割れている**。
- 同ファイル `:23-24` の load 側も無音: `mPins = stored.success ? ... : {}` で、壊れた保存データは
  黙って空になり、**その直後の `save()` が上書きして復旧不能にする**。`src/core/IPinnedFolderStore.h:13-18`
  は「load 失敗と空を区別しない」ことを意図として書いているが、ログを出さない点までは正当化していない。
- 修正方針: `Result` に `[[nodiscard]]` を付ける（これ単体で破棄箇所が全部コンパイル警告として出る）→
  save 失敗をログ + `NotificationController` に流す。**R3 の中で最も小さく、最も明確に直る項目**。
- **対応（2026-08-06）**: 方針どおり実施。決めた点:
  - **`[[nodiscard]]` はクラスに付けた**（`Result` / `Result<void>` の両方）ので、`Result` を返す
    *あらゆる*関数の破棄が C4834 になる。事前に全 call site を洗った結果、**波及は上記 4 箇所だけ**で、
    `AuthService` の 5 箇所は既に `(void)` 付き（MSVC では `(void)` が C4834 を抑止する）、
    `tests/` の 26 箇所は全て値を読んでいた。C4834 は level-1 警告なので、`/W4` が
    `appMegaExplorer` にしか付いていないことは影響しない — `MegaExplorerCore` でも出る。
  - **4 箇所は private `QuickAccessService::persist()` に集約**した。`if (!mAccountKey.empty())` の
    ガードもここに移動。同じ破棄が 4 箇所に散る形そのものを消すのが狙いで、`[[nodiscard]]` は
    その再発防止。
  - **サービス → UI は sink コールバック** `setOnPersistenceFailed(std::function<void(const Result<void>&)>)`。
    `pin`/`unpin`/`move` の `bool`（= 変更が受理されたか）は永続化の成否とは別の問いなので**据え置き**、
    という切り分け。先例は `DownloadService::setOnJobFinished` で、ハンドラを張るのも
    `DownloadController` と同じく `src/qml` 側（`QuickAccessModel` のコンストラクタ）。
    sink は mutator と同じスレッド（= GUI スレッド）で同期に呼ばれるので `invokeOnGuiThread` は不要。
  - **トーストの文言は `%1` を使わない固定文**（`ToastStack.qml` の `quickAccessSave`）。原因は
    ローカル設定の書き込み失敗で、SDK の英語文字列は何も説明しない。結果として **R3-4 の enum 化が
    来てもこの行は動かない** — 新しい文脈を足しながら R3-4 の作業を増やさない形にした。
  - **`void load()` → `Result<void> load()`**。`QuickAccessService` は Qt-free でログを出せないので、
    失敗理由を戻り値で外に出し `QuickAccessModel::reload()` が `qCWarning` する。**トーストは出さない**
    （ログイン直後で、ユーザにはピンが空に見えるだけ／まだ何も失われていない）。
  - **「load 失敗の直後に save が上書きする」件は意図的に許容**し、その理由をヘッダに書いた。壊れた
    JSON はアプリ側で復旧しようが無く（QSettings を手で直す以外に無い）、保全のために以降の
    save を封じると**新しく付けたピンが全部消えるセッション**が生まれ、そちらの方が損。
  - テスト 4 件追加（永続化失敗が sink に届く / 成功時は呼ばれない / **拒否された mutation は
    書き込まないので sink も呼ばない** / モデルが `quickAccessSave` トーストを上げる）。
    既存の load テスト 2 件は戻り値の検証を追加、他の `service->load();` は
    `EXPECT_TRUE(service->load().success);` に変えた（`[[nodiscard]]` が機械的に炙り出した）。
    377 件全緑、`/W4` 新規警告ゼロ。`[[nodiscard]]` が実際に効くことは、`persist()` に破棄を
    一時的に戻して C4834 が出ることを確認して裏づけた。

**R3-4 [中] ✅対応済み トースト 8 文脈が SDK の英語文字列を生のまま `%1` に差し込む**

- `qml/components/ToastStack.qml:155-202` の `showError`。`navigation` / `search` / `thumbnail` /
  `openFile` / `rename` / `createFolder` / `paste` / `copy` の 8 つが
  `qsTr("...: %1").arg(errorMessage)` の形。`:85` の `showDownload` も同型（持ち越し節に記録済みの件、
  → 本調査で回収）。
- その `errorMessage` は SDK 経路では `MegaError::getErrorString()`
  （`third_party/sdk/src/megaapi.cpp:1572-1611`）＝ **`errorCode` から一意に引かれる固定英語表**。
  "Not found" / "Internal error" / "Invalid argument" の類で、**`errorCode` を超える情報はゼロ**。
  日本語化しても「フォルダを読み込めません: Not found」になる。
- 良い対比が同じコードベースにある: `AuthController::classifyError`
  （`src/qml/AuthController.cpp:307-322`）はコードを `AuthErrorKind` の enum に落とし、
  `UnknownError` のときだけ生文字列を渡す（`:146-149`）。Phase 19 以降の「C++ が構造・QML が文言」は
  ここでは守られていて、トーストでだけ破れている。
- 修正方針: `notifyError(context, errorMessage)` を `notifyError(context, reason)` に変え、`reason` は
  `errorCode` を数個の enum（`NotFound` / `NoPermission` / `Offline` / `Unknown`）に畳んだもの。
  文言は `ToastStack.qml` が持つ。**QML も動くので R6 と衝突しうる — R3 で API だけ決めて、
  文面の作り込みは R6 に渡すのが安い**。

**R3-5 [中] ✅対応済み `"refresh"` 文脈だけ QML 側に case が無く、生の英語メッセージが単独で出る**

- C++ 側の `notifyError` 文脈は 11 個（`navigation` / `search` / `thumbnail` / `openFile` / `rename` /
  `createFolder` / `paste` / `copy` / `refresh` / `uploadNothingToUpload` / `uploadReplaceFailed`）。
  `ToastStack.qml:155-202` が扱うのは 10 個で、**`refresh` が無い**
  （`src/qml/FolderNavigationController.cpp:735` が送っている）。
- `default: text = errorMessage;`（`:198`）に落ちるので、トーストには文章も `qsTr` も無い
  "Internal error" だけが出る。Phase 20a が `syncPendingChanges` を足したときの取りこぼし。
- `showOperation` 側（`move` / `copy` / `moveToRubbish` / `upload` / `uploadDestinationGone` /
  `createFolder`）は C++ 側と過不足なく一致している。抜けは `showError` だけ。
- 修正方針: R3-4 と同じ場所を触るので**同一セッションで片付ける**。文字列の対応表が C++ と QML に
  分かれている限り同じ取りこぼしが再発するので、R3-4 の enum 化がそのまま再発防止になる。

**R3-4 + R3-5 の対応（2026-08-07、1 セッション 1 コミット）**: 方針どおり `NotificationController`
に `Q_ENUM(ErrorReason){NotFound/NoPermission/Offline/Unknown}` と `classify(int)` を置き、
`errorOccurred(context, reason, rawMessage)` の 3 引数にした。`ToastStack.qml` の `showError` は
`describeReason(clause, reason, raw)` で「何が失敗したか」と「なぜか」を合成する形になり、
R3-5 の `refresh` はその 9 番目の case として入った。決めた点:

- **enum 化したのは `reason` だけで、`context` は文字列のまま**。context も `Q_ENUM` にすれば
  R3-5 型の取りこぼしをタイプミスの側から潰せるが、**QML の `switch` には網羅チェックが無い**ので
  「C++ に文脈を足して QML に case を足し忘れる」本体は防げない。得られるものの割に C++ 16 箇所 +
  QML 14 case + テスト 8 箇所のアサーションが動き、R6（QML 構造）との衝突が増える。
  代わりに `showError` の `default:` を**生文字列のパススルーから `console.warn` + 一般文に変えた**
  — R3-5 が 2 フェーズ生き延びたのは、取りこぼしが「穴」ではなく「メッセージ」に見えていたから。
- **生の英語を出すのは `Unknown` のときだけ**（`AuthController::rawErrorMessage` と同じ規約）。
  分類が付いた失敗には訳文があるので、そこに未翻訳を足しても悪くなるだけ。C++ 側で `classify()` が
  `Unknown` 以外を返したら `rawMessage` を空にして emit しており、**QML の書き方に依存しない**。
- **`classify` は呼び出し側ではなく `NotificationController` に 1 つだけ置いた**。16 箇所それぞれで
  畳むと `AuthService`/`QuickAccessService` と並ぶ 3 つ目の対応表が散らばる。
  `ARCHITECTURE.md` の「allowlist + `default`」の形に揃えてあり、名前を付けたのは
  `kENoEnt`/`kEAccess`/`kEAgain` の 3 つだけ。
- **文言は「動詞句 + 理由句」の連結にした**。context × reason の全文を並べると 32 文字列になる。
  翻訳片の連結という既知の匂いは残るが、**reason は値であって文ではない**ので、日本語で読みが硬い
  context だけ全文に開く作業は C++ 無変更で R6 が単独でできる。
- **詳細を持たない 5 文脈（`renameInvalidName` / `quickAccessSave` / `quickAccessUnavailable` /
  `uploadNothingToUpload` / `uploadReplaceFailed`）は 1 引数版 `notifyError(context)` に分けた**。
  `QString()` を渡していた形が消え、「SDK に届く前に弾いた失敗」であることが型に出る。
  **5 つとも文言は無変更**。
- **`openFile` は product-visible に変えた（小）**。`DownloadController.cpp` は `errorMessage` 引数に
  `localPath` を渡しており、`ToastStack` がそれをエラー文字列として `%1` に差していた
  （`QDesktopServices::openUrl` の失敗なので `errorCode` が無い）。固定文
  「Couldn't open this file」にし、パスは同行の `qCWarning` に残した — ユーザが今ダブルクリックした
  ファイルなので、名前を返しても情報が増えない。持ち越し節にあった `showDownload:85` の同型は
  **今回触っていない**（`fileName` は `%1` の意味が通る引数で、`%2` の `errorMessage` だけが問題。
  ダウンロード経路は `DownloadJob` という 3 つ目のエラー表現に乗っており、R3-11 で R5 に合流させると
  決めてある）→ **R6 ではなく R5 送り**。
- CMake: `QML_ELEMENT` 型は `qt_add_qml_module` の `SOURCES` に無いと型登録されないので、
  `NotificationController.h/.cpp` をそこへ移した（`AuthController` の先例。`target_include_directories`
  の `src/qml` は既に入っている）。`tests/CMakeLists.txt` はファイルを明示列挙しているので影響なし。
- テスト: `tests/NotificationControllerTest.cpp` を新規 4 件（3 コードの分類 / 未知コードが `Unknown` に
  落ちる / **`Unknown` のときだけ raw が乗る** / 1 引数版）。既存は harness 2 つを 3 引数化し、
  `refresh` と `rename`(`kEAccess`) のテストに reason と「raw が空」のアサーションを追加。
  387 件全緑、`/W4` 新規警告ゼロ。実機でも 3 経路（`refresh`+`Offline` / `navigation`+`NoPermission` /
  `renameInvalidName`）のトーストを目視確認した。

**R3-6 [中] ✅対応済み 同じ「名前が不正」を rename だけ -1、createFolder / copy は `kEArgs` で返す**

- `src/core/FileOperationService.cpp:38` — `rename` は
  `fail("Invalid name: empty, or contains a path separator")`（コード無し = -1）。
  `:73-74`（`copy`）と `:95-96`（`createFolder`）は同じ文言に `MegaErrorCode::kEArgs` を付けている。
- 結果の差が UI に出る: `createFolder` は `kEArgs` を見て `NewFolderDialog` 内の赤字
  （`folderCreationFailed("invalidName")`、`src/qml/FolderNavigationController.cpp:382-386`）に落とすが、
  rename は分岐できずトースト「名前を変更できません: Invalid name: ...」になる（`:340-347`）。
  **ユーザの入力ミスが「操作の失敗」として出る**。
- 修正方針: rename に `kEArgs` を付け、`renameEntry` に `createFolder` と同じ分岐を足す。
  R3-1(b) を先にやれば、この 1 件は自動的に炙り出される。
- **対応（2026-08-06、前半）**: C++ 半分（`FileOperationService.cpp:38` への `kEArgs` 付与）は R3-1 で完了
  — 実際に既定引数の撤廃で真っ先に落ちた 3 箇所の 1 つだった。**残るは `renameEntry` の QML 分岐**
  （`src/qml/FolderNavigationController.cpp:340-347` を `:377-386` と同じ形にする）で、これが
  済むまで挙動は変わらない。
- **対応（2026-08-06、後半＝完了）**: `renameEntry` の失敗分岐の先頭に `kEArgs` を置き、専用文脈
  `renameInvalidName` の固定文トーストに落とした。決めた点:
  - **「編集フィールドを開き直す」形は採らなかった**。`createFolder` の対応物（ダイアログを開いたまま
    赤字）に一番近いのはそれだが、rename はインライン編集で**コミット時点でフィールドが消えている**。
    復元するには新シグナル + 両 View（`FileTableView.qml` / `FileGridView.qml`）の受け口 +
    `InlineRenameField` に入力済みテキストを戻す口（現状 `originalName` は `cell.name` 束縛）が要り、
    得られるのは「F2 を押し直さずに済む」だけ。R6（QML 構造）と衝突しうる割に合わない。
    **結果として QML の View 3 ファイルは無変更**、増えたのは `ToastStack.qml` の 1 case のみ。
  - **文言に `%1` を使わない**のは R3-2 の `quickAccessUnavailable` / R3-3 の `quickAccessSave` と
    同じ理由（拒否したのは `FileOperationService::isValidName` で SDK に届いてすらいないので、
    SDK の英語文字列は存在しないし説明にもならない）。**R3-4 の enum 化が来てもこの行は動かない**。
    ただし文面は 2 者と違って理由まで書いた（「空・`\`・`/` は使えない」）— トーストは
    `NewFolderDialog` の赤字と違い、直せる入力欄が隣に無いため。
  - **`kEArgs` 側ではログを出さない**。`createFolder` の `kEExist`/`kEArgs` 分岐も出しておらず、
    ユーザの打ち間違いは異常ではない。権限エラー等は従来どおり `qCWarning` + `rename` 文脈に残る。
  - テスト: 既存 `RenameEntryReportsAnInvalidNameWithoutRefetching` の期待文脈を
    `renameInvalidName` に更新し、**`kEArgs` 以外（`kEAccess`）が `rename` 文脈に残る**ことを見る
    1 件を追加（分岐が全部を飲み込む退行止め）。383 件全緑、`/W4` 新規警告ゼロ。

**R3-7 [低] ✅対応済み 失敗した `Result` の `value` が読める — 実際に読んでいる箇所が 1 つある**

- 種の指摘どおり `T value{}` は常時デフォルト構築され、`success` を見なくても読める。
- 実例: `src/qml/AccountController.cpp:105-120` は `result.success` を確認せず
  `result.value.hasAvatar` を読む。**今は安全**（前提節 2 のとおり `loadAvatar` が常に `ok()` を返す）
  が、安全なのは型ではなく規約のおかげで、`loadAvatar` が将来 `fail()` を返すようになった瞬間、
  デフォルト構築された `AvatarOutcome`（`hasAvatar == false`）を読んで**黙って「アバター無し」に落ちる**。
- 他 40 箇所の `.value` 参照は全て成功分岐の中にあることを確認済み。
- 修正方針: R3-9（下記「判断が要るもの」）とセット。`value` を private + `value()` アクセサにして
  失敗時は `assert` にするだけでも、この形は型で防げる。
- **実施（2026-08-07、R3-9 承認と同一セッション）**: `mValue` を private 化し、`success` を
  `assert` する `T& value()` / `const T& value() const` を追加。`Result<void>` は `value` を
  持たないので変更なし。呼び出し側は `.value` → `.value()` の機械置換 83 箇所（`src` 42 /
  `tests` 41）。
  - 置換で唯一注意が要ったのは **`QVariant::value<T>()` の 14 箇所**（`tests/TabsControllerTest.cpp`）と
    **Qt の `.value(...)` 23 箇所**（`QSettings` / `QJsonObject` / `QVariantMap`）。
    `\.value\b(?!\s*[(<])` で除外した。`value` という名のメンバは `Result` 以外に無く、
    集約初期化・`.value` への外部代入・構造化束縛はいずれも 0 件だったので、書き換え漏れは
    全てコンパイルエラーになる形だった。
  - 種が言う「実際に読んでいる 1 箇所」＝ `AccountController.cpp:109` は**そのまま残した**。
    前提節 2 のとおり `loadAvatar` が常に `ok()` を返すので現状は正しく、将来 `fail()` を
    返すようになれば assert が Debug で落として気づける — R3-7 が欲しかったのはまさにこれ。
  - `success` を局所チェックせずに `value()` を読む箇所は他に `QuickAccessModel.cpp:232` が
    1 つだけあり、これは `classify()` が `success` 分岐の中でしか `Usable` を返さないことによる
    間接ガード。正しいが非局所なので `docs/ARCHITECTURE.md` に明記した。
  - 387 件全緑（assert は 1 件も発火せず）、`/W4` 新規警告ゼロ。

**R3-8 [低] ✅対応済み 「やることが無い」がエラーとして表現され、エラートーストの経路に乗っている**

- `src/core/FolderNavigationService.cpp:62` — 履歴が空の `goBack` が `fail("no back history")`。これは
  `applyResult`（`src/qml/FolderNavigationController.cpp:181-189`）に届き、
  **「フォルダを読み込めません: no back history」というトーストになる**。
- 現状の唯一の防御が QML の `enabled: tabsController.currentNavigation?.canGoBack ?? false`
  （`qml/Main.qml:324`）で、C++ 側に `canGoBack()` のガードが無い。`goUp()` は
  `if (!canGoUp()) return;` を持っている（`src/qml/FolderNavigationController.cpp:160-161`）ので、
  **同じクラスの中で非対称**。`goBack` は `Q_INVOKABLE` なので QML から誰でも呼べる。
- 同型が `src/core/SearchService.cpp:14` の `fail("empty query")`。こちらは呼び出し側
  （`FolderNavigationController::search`、`:238-242`）が早期 return するので到達しない。
- 修正方針: `goBack` に `goUp` と同じ早期 return を足す。`fail("no back history")` 自体は
  サービス単体の契約としては残してよい（テストが見ている）。
- **対応（2026-08-07）**: 方針どおり `FolderNavigationController::goBack` の先頭に
  `if (!canGoBack()) return;` を 1 つ足しただけ（`src/qml/FolderNavigationController.cpp:147-149`）。
  同クラスの `goUp` と字面まで同じ形にしてある。決めた点:
  - **`FolderNavigationService::goBack` の `fail("no back history")` は残した**。サービス単体では
    「戻れないのに呼ばれた」は正しく失敗であり、`FolderNavigationServiceTest.cpp` の 7 箇所が
    この契約に乗っている。潰したのは *UI 経路に流れること* だけで、契約そのものではない。
  - **ログも出していない**。R3-12〜R3-14 でログを足したのは「握り潰していた失敗」だが、こちらは
    そもそも失敗ではなく no-op（R3-6 の `kEArgs` 側と同じ理由）。
  - `SearchService.cpp:14` の `fail("empty query")` は**触っていない**。呼び出し側が既に早期 return
    しており到達しないため、ガードを重ねても増えるのは行数だけ。

#### 種が不正確だった / 判断が要るもの

**R3-9 [判断] ✅承認済み（2026-08-07） `std::expected` への置き換えは現状の標準設定では不可能**

- 種は「`std::expected`（C++23）/ `std::variant` への置き換えが妥当か、`/W4` と MSVC の C++ 標準設定を
  見て判断する」としていた。**設定を見た結果、選択肢が 1 つ消える**。
- `CMakeLists.txt:9` は `CMAKE_CXX_STANDARD_REQUIRED ON` **だけ**で `CMAKE_CXX_STANDARD` を設定して
  いない。Qt をリンクするターゲットは Qt の interface 要求から C++17 を得ており、Qt を引かない
  `MegaExplorerCore` は R2-1 で足した `target_compile_features(... cxx_std_17)`
  （`CMakeLists.txt:89`）から得ている。**プロジェクト全体が C++17**。
- `<expected>` は C++23。MSVC では `/std:c++latest` 相当が要り、安定した標準モードでは有効にならない。
  ベンダーされた `third_party/sdk` / `qwindowkit` も巻き込む変更になるため、**エラー表現の整理のために
  払うコストとして釣り合わない**。
- 推奨する結論（**要承認**）: 独自 `Result<T>` を維持し、次の 3 点だけ入れる。
  (a) `[[nodiscard]]`（**✅R3-3 で実施済み**）、(b) `value` の直接公開をやめてアクセサ化（R3-7）、
  (c) `fail()` の既定コードを廃止（R3-1）。`std::variant` 化は (b) と実質同じ効果しか無いのに
  全 service に波及するので**採らない**。
- 波及規模の実測: `Result` に触れるファイルは `src/` + `tests/` で 57、`tests/` 内の `Result<` は
  452 箇所。(a)(b)(c) はいずれもコンパイルエラーとして全件出るので機械的に洗えるが、**それでも
  単独セッション必須**という種の判断は正しい。
  - なお 452 は `Result<` という**型名の出現数**で、(b) が実際に触る `.value` 参照は **83 箇所**
    （`src` 42 / `tests` 41）だった。R3-7 の実施ログを参照。
- **承認結果（2026-08-07）**: 上記の推奨結論どおり (b) で実施。C++23 化は見送り、
  `std::variant` 化も採らない。決定内容は `docs/ARCHITECTURE.md` の
  `### value() may only be read on a successful Result` に恒久記録した（R5 の入力はそちらを見る）。

**種「`errorCode` のデフォルトが -1、成功時は 0」は半分不正確**

- メンバ既定値は **`0`**（`src/core/Result.h:10`・`:25`）で、-1 は `fail()` の既定**引数**。
- 実害のある帰結は種が書いていない方: `Result<T> r;` と素で作ると
  **`success == false` かつ `errorCode == 0`（= `API_OK`）** という、意味の付かない組み合わせになる。
  `ok()` も `errorCode` を触らないので 0 のまま。「成功時は 0」は結果的にそう見えるだけで、
  `fail("msg", 0)` も型としては書ける。

**種「`MegaErrorCodes.h` の値域と衝突しないか」→ 衝突している**

- -1 = `API_EINTERNAL` が表に無いまま既定値に使われている（R3-1）。
- 一方 `kNoStoredSession = 1`（`src/core/AuthService.h:11-15`）は正の値を選び、
  「`MegaErrorCodes.h` の値は全て 0 以下なので衝突しない」と根拠つきで書かれている。
  **センチネルの設計としては正しく、ここは真似すべき側**。

#### 問題なしと確認できた種

- **`kShutDownMessage` の -1 群 29 箇所は表に出ない。** `src/mega/MegaSdkClient.cpp:65-68` の
  「Nothing surfaces it」は正しい。`client->shutdown()` は `app.exec()` が返った**後**
  （`main.cpp:184`）に呼ばれるので、`invokeOnGuiThread` が投函したイベントは配達されない。
  R3-1 の 52 件のうち 29 件は、したがって**優先度が下がる**（直すなら機械的な一括のついで）。
- **`errorMessage` はサーバ由来文字列ではない。** SDK 経路のそれは
  `MegaError::getErrorString()`（`third_party/sdk/src/megaapi.cpp:1572-1611`）の固定 switch 表で、
  ネットワークから来た文字列は混じらない。**R1 の「サーバ由来文字列の信頼境界」は R3 には掛からない**。
  寿命も安全（静的表を指す `const char*` を `std::string` にコピーしている）。
- **`isSessionDefinitivelyInvalid` / `classifyError` は正しい形。** コードで分岐し、文言は UI が持つ。
  -1 は両方で「未知 → transient / UnknownError」に落ちるだけで、セッションを誤って捨てることはない
  （`src/core/AuthService.cpp:5-19`、`src/qml/AuthController.cpp:307-322`）。
- **`showOperation` の文脈は C++ と QML で一致している。** 取りこぼしは `showError` 側だけ（R3-5）。
- **`FolderTreeService::hasSubfolders` の `result.success && result.value`
  （`src/core/FolderTreeService.cpp:37-38`）は安全側に倒れている。** 失敗時に展開矢印を出さない側で、
  `src/qml/FolderTreeModel.cpp:88-92` が「出してしまうと後から取り消せない」理由を書いている。
- **`AccountService::loadDisplayName` が属性取得失敗を `""` に畳むのは意図どおり**
  （`src/core/AccountService.cpp:46-49`）。名前が未設定なのは異常ではない。

#### 新規発見

- **R3-10 [中] 「常に `ok()` を返す `Result`」が 2 つ目のエラー表現になっている。**
  前提節の 2。`Result<AvatarOutcome>` は型として何も保証しておらず、R3-7 の未チェック `.value` 参照は
  この設計の直接の帰結。R5 で service を割るときにこの形が増えると、失敗の表現が service ごとに
  変わる。R3 のうちに「常に成功する非同期処理は `Result` で包まない」と決めておくのが安い。
- **R3-11 [低] `DownloadJob` / `UploadJob` が 3 つ目の表現。** 前提節の 3。`Result` の
  `errorMessage` / `errorCode` をコピーして持つので、R3-1 でコードの意味を変えると 2 箇所
  （`src/core/DownloadService.cpp:217`、`src/core/UploadService.cpp:128`・`:173`）が自動的に追随する
  — つまり害は今のところ重複だけ。R2-8（キューの `optional<Job>` 化）と同じ場所なので、**R5 での
  サービス整理に合流させる**。
- **R3-12 [中] ✅対応済み 検索中の裏側リフレッシュが失敗を完全に無音で捨てる。**
  `src/qml/FolderNavigationController.cpp:318-324` は `refreshCurrent` の結果を
  `if (result.success) mLastFolderEntries = ...` とだけ扱い、**ログすら出さない**。失敗すると
  検索を消したときに古い一覧が出る。`:307-309` のコメントはまさにそれを防ぐためにこの呼び出しが
  あると説明しているので、**コメントと実装が食い違っている**。
- **R3-13 [低] ✅対応済み セッショントークンの取得失敗が無音。** `src/core/AuthService.cpp:111-117` は
  `currentSessionToken()` が失敗すると保存を飛ばす。ヘッダ（`AuthService.h:67-73`）が
  「best-effort」と書いているのは**保存の失敗**についてで、取得の失敗には触れていない。ユーザから見ると
  「次回起動でなぜかパスワードを聞かれる」だけになる。ログ 1 行で足りる。
- **R3-14 [低] ✅対応済み 同名チェックのスキップが無ログ。** `src/qml/UploadController.cpp:242-243` は
  `findChildFiles` 失敗時に同名チェックを丸ごと飛ばす（`// can't ask the question, so don't`）。
  意図は明示されているが、**MEGA は同名アップロードを新バージョンとして重ねる**ので、
  スキップの結果は「黙って上書き」に近い。ログ 1 行と、R3-4 の enum 化のときに
  「チェックできなかった」を言えるかの検討。

**R3-12 / R3-13 / R3-14 の対応（2026-08-07、1 コミット）**: 3 件とも `qCWarning` 1 行。トーストは
どれも出さない（R3-12 は検索結果側が自分で報告する／R3-13・R3-14 はユーザの操作が失敗したわけでは
ない）。決めた点:

- **R3-13 だけログを書く場所の判断が要った。** `AuthService` は `src/core` にあり、
  `MegaExplorerCore` は Qt を一切リンクしない（`CMakeLists.txt:86-89` にその旨のコメントがある）ので、
  **「ログ 1 行」が成立しない** — 調査時の見落とし。R3-3 の先例に倣うなら sink コールバックだが、
  それはヘッダ + `AuthController` 配線 + テストで ~30 行になり「極小 1 コミット」から外れる。
  代わりに**失敗の発生源であるアダプタ側**（`src/mega/MegaSdkClient::currentSessionToken`、
  こちらは Qt リンク済み）に置いた。呼び出し元は `AuthService` の 1 箇所だけなので誤爆しない。
  引き換えに「保存を飛ばした」文脈がアダプタ側のコメント頼りになる — その 1 文をコメントに書いた。
  `src/mega` で `app/Logging.h` を include するのは `MegaSdkLogger` 以外では初。
- **R3-14 の「チェックできなかった」をユーザに伝えるかは R3-4 に持ち越し**（トーストの文脈を
  足す話なので、enum 化と同じ場所を触る）。今回はログのみ。
  → **R3-4 で「伝えない」に決着（2026-08-07）**。R3-4 が触ったのは既存 14 文脈の引数の形だけで、
  文脈は 1 つも増やしていない。同名チェックが飛ぶのはアップロード**前**の判定で、その後の結果は
  `upload` の成功/失敗タリーとして必ず出る。ここでトーストを足すと「確認できませんでした」の直後に
  「N 件アップロードしました」が並び、**成功したのに何か失敗したように読める**。伝えるなら
  トーストではなく `NameConflictDialog` 側（「同名の確認ができませんでした、続行しますか」）で、
  それは R6 の範囲。
- テスト追加なし。`MegaSdkClient` は実アカウントが要るのでアダプタ級のテストが元々無く、
  他 2 件は `src/qml`。`/W4` 新規警告ゼロ。

#### 無音失敗の棚卸し（種 4 番目の答え）

`Result` を受け取って何も報告しない箇所は 10。「意図的に黙る」と「報告漏れ」の仕分けは以下。

| 箇所 | 現状 | 判定 | 対応 |
| --- | --- | --- | --- |
| `QuickAccessService.cpp:44,56,76,84` | `save()` の戻り値を破棄 | **報告漏れ** | ✅R3-3（`persist()` + sink → ログ + トースト） |
| `QuickAccessService.cpp:23-24` | load 失敗 → 空リスト、無ログ | **報告漏れ**（畳むこと自体は意図的） | ✅R3-3（`load()` が `Result<void>` を返す → モデルがログ） |
| `QuickAccessService.cpp:15-20` | `currentUserHandle()` 失敗 → ピン全消し、無ログ | **報告漏れ** | ✅R3-3（同上、client の `Result` をそのまま転送） |
| `QuickAccessModel.cpp:183-195` | 失敗を「dangling」と断定してピン削除 | **報告漏れ**（ログの文言も誤り） | ✅R3-2（`PinStatus` 3 値化 + `Unknown` 用のログ／トースト） |
| `FolderNavigationController.cpp:318-324` | 裏側 refresh 失敗、無ログ | **報告漏れ** | ✅R3-12（`qCWarning`） |
| `AuthService.cpp:111-117` | トークン取得失敗、無ログ | **報告漏れ** | ✅R3-13（core は Qt-free なのでアダプタ側で `qCWarning`） |
| `UploadController.cpp:242-243` | 同名チェック不能 → スキップ | 意図的（ただし無ログ） | ✅R3-14（`qCWarning`。ユーザへの通知は R3-4 で「しない」に決着、伝えるなら R6 のダイアログ側） |
| `AuthService.cpp:34,49,94,106,116` | `(void)` 付きで破棄 | 意図的・明示済み | 対応不要 |
| `AccountService.cpp:46-55` | 属性未設定 → `""` | 意図的・コメント済み | 対応不要 |
| `FolderTreeService.cpp:37-38` | 失敗 → `false`（安全側） | 意図的・コメント済み | 対応不要 |

#### 推奨実施順（各項目 1 セッション）

```
R3-1  errorCode の意味づけ（既定引数の廃止）    … ✅済（先行実施）。以降 3 件の前提
R3-3  [[nodiscard]] + save/load 失敗の報告      … ✅済。R3-9(a) もこれで入った
R3-2  isUsable の 3 値化（ピンを誤削除しない）  … ✅済。R3-1(c) の成果を直接使った
R3-6  renameEntry の QML 分岐だけ               … ✅済。C++ 半分は R3-1 で先に入っていた
R3-12 / R3-13 / R3-14  無音の 3 箇所にログ      … ✅済。予定どおり 1 コミット
R3-8  goBack のガード                           … ✅済。1 行、サービス側の契約は据え置き
R3-4 + R3-5  notifyError の enum 化（+ refresh 追加） … ✅済。1 セッション 1 コミット
R3-9  Result 型そのものの方針                   … ✅承認済（2026-08-07）。R5 の入力
R3-7  value のアクセサ化                        … ✅済。R3-9 承認と同一セッションで実施
```

R3-9 の承認は着手前に取った（`実施手順` 3 の「製品挙動が変わるもの」ではないが、R5 全体の入力に
なるため）。**R3 はこれで全項目完了**。

#### 成果物

- `docs/ARCHITECTURE.md` に「エラー表現の規約」節を 1 つ。R1 の「サーバ由来文字列の信頼境界」、
  R2 の「スレッドモデル」と並ぶ形にする。内容は前提節（表現が 3 つあること）+ R3-1 の結論
  （`errorCode` で分岐し `errorMessage` では分岐しない、を `IMegaClient.h` の同期 3 メソッドから
  全体規約に格上げ）+ R3-4 の C++/QML 分担 + R3-9 で決めた `Result` の方針。
  - **R3-1 で `## Error representation` として作成済み**。前提節と `errorCode` 規約（値域の
    使い分けを含む）まで入っている。**R3-4 で `### C++ carries the reason, QML carries the words`
    を追記済み**（context + reason の 2 値だけを渡す規約、生英語は `Unknown` のときだけ、分類表は
    境界ごとに 1 つ、`default:` が警告する理由）。**R3-9 / R3-7 の `Result` 方針も
    `### value() may only be read on a successful Result` として追記済み**（2026-08-07、
    assert の役割と Release での挙動、`std::expected` を採らなかった理由、`QuickAccessModel` が
    唯一の間接ガードであること）。**これで本節は完成**。

### R4 — 全項目対応済み（調査 2026-08-07、R4-6 で完了 2026-08-07）

計画の「種」4 件 +「持ち越し」の [R4] 2 件を現物で検証した結果。**確認 6 件 / 種の誤り 4 件 /
問題なしと確認 4 件 / 新規 3 件**。R1〜R3 と同じく、以下はそのまま plan mode の作業単位として使える
粒度で書いてある。**調査セッションではコードを一切変更していない**。

**R4-4 は (b)（`qt_add_qml_module` をライブラリ target へ）で決定済み（2026-08-07）**。
その決定が R4-3・R4-5・R4-9 の前提を動かしているので、各項目を単独で読まないこと。

#### 前提: 現状の測定値（2026-08-07 時点）

節 0 の表は 2026-08-06 のもので、R3 の修正で `tests/` が増えている。R4 の判断はこちらに乗る。

| 領域 | 規模 | テスト |
| --- | --- | --- |
| `src/core`（11 サービス） | 1,379 行（`.cpp` のみ） | **11/11 にテストあり。穴なし** |
| `src/qml`（31 ファイル） | 5,529 行 | 11 クラスに専用テスト、1 クラスはリンクのみ、3 クラスは未リンク（R4-2 で `AuthController` が移動） |
| `src/platform`（2 アダプタ） | 318 行 | 1/2（`WindowsSessionStore` のみ） |
| `src/mega` | 1,000 行超 | 0（実アカウントが要るため意図的） |
| `qml/`（29 ファイル） | 7,356 行 | **0 件** |
| `tests/` | 7,919 行 / 26 ファイル（22 テスト + 4 ヘッダ） | 387 ケース |

- 387 は `build/msvc-debug/tests/MegaExplorerTests[1]_tests.cmake` の `add_test` 実数。
  `DISABLED_` / `GTEST_SKIP` は**ゼロ**。
- 最大は `FolderNavigationControllerTest.cpp` の **1,190 行 / 49 ケース**（節 0 の 1,152 行は
  R3 前の値）。次が `MenuActionResolverTest.cpp` 47 ケース、`FileOperationServiceTest.cpp` 37 ケース。
- `src/qml` のテスト状況を正確に分けると 3 層ある。この区別が R4-1 の土台:

  | 層 | クラス |
  | --- | --- |
  | 専用テストあり（調査時 10、R4-2 後 11） | `FolderNavigationController` / `FileListModel` / `TabsController` / `UploadController` / `QuickAccessModel` / `AccountController` / `FolderTreeModel` / `ClipboardController` / `NotificationController` / `LicenseModel` / **`AuthController`（R4-2 で追加）** |
  | テストターゲットにリンクだけされている（1） | `ThumbnailController`（`TabsControllerTest` がタブ毎に構築するため必要。単体の検証はゼロ） |
  | テストターゲットに存在しない（調査時 4、R4-2 後 3） | `DownloadController` / `MenuActions` / `KeyboardState` |

- CI は無い（`.github` 不在）。linter も無い。サニタイザ構成も無い（`CMakePresets.json` は
  `msvc-debug` のみ。Release プリセットが無い件は既に節 5 の持ち越しにある）。

#### 確認された問題

**R4-1 [中] ✅対応済み 「`src/qml` は未テスト」という慣例は既に無効で、その残骸が 9 箇所にある**

- 慣例を**断言している**ヘッダが 2 箇所:
  - `src/qml/FolderNavigationController.h:42`「Untested by convention: src/qml is GUI glue」。
    当の `FolderNavigationControllerTest.cpp` が **1,190 行 / 49 ケース**で最大のテスト。
  - `src/qml/DownloadController.h:17-18`「Untested by convention, same as
    FolderNavigationController: src/qml is GUI glue, and **MegaExplorerTests only links
    MegaExplorerCore**」。後半は事実として誤り — `tests/CMakeLists.txt:33-56` は `src/qml` の
    **22 ファイル（11 クラス）**と `src/platform`・`src/app` を直接コンパイルしている。
- 慣例を**破ったことを弁解している**テストが 7 箇所: `AccountControllerTest.cpp:14`,
  `ClipboardControllerTest.cpp:10`, `FolderNavigationControllerTest.cpp:22-24`,
  `FolderTreeModelTest.cpp:11-12`, `QuickAccessModelTest.cpp:17-18`, `TabsControllerTest.cpp:16-18`,
  `UploadControllerTest.cpp:20-21`。いずれも「FileListModel/TabsController/QuickAccessModel が
  既に破っているのと同じ理由で破る」と互いを参照し合っており、**7 個の弁解が円環している**。
- 根本原因: `docs/ARCHITECTURE.md` の `Design: testability and dependency injection` 節の
  「Testing」箇条書きは `MockMegaClient` と `MegaSdkClient` に触れるだけで、**この慣例をどこにも
  書いていない**。規約の単一の置き場が無いので、コメントのコピーが 9 個に増えて個別に腐った。
- 実際に運用されている基準は、弁解文自体が言い当てている:「bookkeeping であって rendering では
  ない」。これを規約として書き直すのが実態に合う。案:
  > `src/qml` のうち**状態と分岐を持つもの（モデル・コントローラ）はテストする**。テストしないのは
  > (a) Qt/OS API の 1 行ラッパ（`KeyboardState`）と (b) QML エンジン無しでは意味を持たないもの
  > （`WindowAgentForeign`）だけ。
- 修正方針: (a) `docs/ARCHITECTURE.md` の Testing 箇条書きを上記の規約に書き換える、
  (b) テスト側 7 箇所の弁解コメントを削除（各ファイルに固有の情報 — `UploadControllerTest.cpp` の
  `checkUpload` トラップ、`AccountControllerTest.cpp` の `currentAccountIdentity` トラップ、
  `TabsControllerTest.cpp` の「未応答モックのまま返る」注記 — は残す）、(c) ヘッダ 2 箇所を直す。
  ドキュメントとコメントだけなので**コード変更ゼロ・テスト再実行のみ**。R4 の最初に置くのが安い。
- **対応（2026-08-07）**: (a)(b)(c) を方針どおり実施。決めた点:
  - **残骸は 9 箇所ではなく 10 箇所だった**。調査が数え漏らしていた 10 個目が
    `src/qml/ThumbnailController.h:22-24`。文面は `DownloadController.h` と同じ
    「`MegaExplorerTests` only links `MegaExplorerCore`」で、しかも当の `ThumbnailController` は
    `TabsControllerTest` のためにテストターゲットへリンク済み（前提節の「リンクだけ 1 クラス」）
    という、**最も分かりやすく事実に反する 1 箇所**。同じ文面のコピーだったので R4-1 に含めた。
  - **規約の置き場は `docs/ARCHITECTURE.md` の Testing 箇条書き**。ディレクトリではなく
    「状態と分岐を持つか」で決める、と書き、例外を `KeyboardState`（Qt/OS API の 1 行ラッパ）と
    `WindowAgentForeign`（QML エンジン無しでは無意味）の 2 種類だけに限定した。
    「per-file コメントに書き写すな、それで 9 個に腐った」を規約本文に明記してある。
  - **「まだテストが無い」と「テストしないと決めた」を分けて書いた**。前者
    （`AuthController` / `DownloadController` / `MenuActions`）は R4-2/R4-3 への参照付きで
    「gap であって decision ではない」と書く。これを書かないと、次に読む人が今回と同じ
    「未テスト＝方針」という誤読を再生産する。
  - **スレッドの既知の限界を独立した箇条書きにした**（成果物の (c)）。`MockMegaClient` が
    同期配達しかしない以上 `std::mutex` も `makeGuiOwned` のクロススレッド分岐も
    **消してもテストは緑のまま**、という具体形まで書いてある。Threading model 節の主張は
    レビューで支えられていてテストでは支えられていない、という対応関係を明示するのが狙い。
  - **テスト側 7 箇所は「弁解だけの塊」は全削除、固有情報は残す**。残したのは
    `AccountControllerTest`（`currentAccountIdentity` トラップ）、`TabsControllerTest`
    （未応答モックのまま返る注記）、`UploadControllerTest`（`QDesktopServices`→`QtGui` の事実、
    `QSignalSpy` 不使用の理由、`checkUpload` トラップ）。`FolderTreeModelTest` /
    `QuickAccessModelTest` の「real service, mocked SDK」は規約側に昇格したので削除した。
  - `FolderNavigationControllerTest` の「bent here for exactly one thing …
    Everything else in this class stays untested」は**現物が 1,190 行 / 49 ケース**で
    既に事実と反対だったため、丸ごと削除。
  - コード変更はゼロ（コメントのみ）。**ビルド不要・テスト不要**と判断した。

**R4-2 [中] ✅対応済み `AuthController` の `LoadingStage` 状態機械が最大の未テスト論理**

- 482 行（`.cpp` 322 + `.h` 160）。テストターゲットに入っていない。持っている状態は
  `AuthState` 7 値 × `LoadingStage` 5 値 + 世代カウンタ + ストールタイマ + 2FA 保留資格情報。
- テストする価値がある具体的な振る舞い（いずれも `.h` のコメントが「こう設計した」と主張している
  ものの、それを保証する検証が無い）:
  1. **世代による古い進捗の破棄** — `mLoadGeneration`（`.h:148`）。ログアウト→再ログイン後に
     前回の fetch の進捗イベントが届いても無視されること。
  2. **ストールタイマがラッチではないこと** — `.h:149-155` が「後続イベントで
     `DownloadingNodes` に戻る」と明記。`AuthController.cpp:21` の `kStallTimeoutMs = 8000` は
     **ハードコードされた定数**なので、そのままでは 1 ケース 8 秒かかる。
     コンストラクタ引数か protected setter で注入可能にする小さな製品側変更が要る
     （挙動は変わらないが、実施手順 3 の対象として一応提示する）。
  3. **2FA のキャンセル/成功で `mPendingEmail`/`mPendingPassword` が消えること**（`.h:156-159`）。
     消えていないと平文パスワードがプロセスに残るので、R1 の観点とも接続する。
  4. `classifyError` の 4 値マッピングと `default: → UnknownError`
     （`AuthController.cpp`、`MegaErrorCode::kENoEnt`/`kEBlocked`/`kETooMany`/`kEAgain`）。
     R3-1 で `kEInternal = -1` が入ったので、`-1` が `UnknownError` に落ちて生英文が出る経路が
     ここにある。
  5. `fetchProgress()` が総量未知のとき `0.0` を返すこと、`fetchProgressText()` が総量未知の間
     空であること（`.h:114-118`）。
- **コストは低い**: 依存は `core/AuthService` と QtCore（`QTimer`/`QString`）だけで、`QtGui` を
  引かない。既存の `MegaExplorerTests` に `AuthService` のモック（既存の `AuthServiceTest` と同じ
  `MockMegaClient` + `MockSessionStore`）でそのまま入る。`QTimer` を使うので `TestApp.h` の
  `testApp()` が必須。
- **対応（2026-08-07）**: `tests/AuthControllerTest.cpp` を新設。**32 ケース**、7 群
  （起動経路と状態ゲート / `LoadingStage` の導出 / 進捗表示値 / ストールタイマ / 世代ガード /
  2FA 保留資格情報 / エラー分類）。`tests/CMakeLists.txt` は 1 行追加のみ（R4-4 済みで
  `MegaExplorerQml` をリンク済みのため、追加ライブラリは不要）。決めた点:
  - **製品側の変更は 1 点のみ**: `kStallTimeoutMs` を `.cpp` の匿名名前空間から
    `AuthController.h` の `kDefaultStallTimeoutMs` へ移し、コンストラクタ第 2 引数
    （既定値付き、`parent` の手前）で注入可能にした。**8 秒の測定根拠コメントも定数に付けて移動**
    してある。構築箇所は `main.cpp:119` の 1 箇所だけで、既定値のまま無変更。挙動不変。
  - **`mStallTimer.stop()` は観測不能だと判明した**。`setLoadingStage` の `stop()` を
    コメントアウトしても 32 ケース全部が緑のまま — タイムアウトハンドラ自身が
    `mLoadingStage == DownloadingNodes` を再確認しているので、タイマが余分に発火しても no-op に
    なる。**二重に守られていて、片方だけを固定するテストは書けない**。該当ケースは
    `StaleStallTimerCannotFlipASignedInWindow` に改名し、「機構ではなく結果を固定している」旨を
    テスト内コメントに明記した。`stop()` 自体は防御として残す。
  - **世代ガードは変異テストで裏を取った**。`generation != mLoadGeneration` の early-return を
    潰すと `StaleProgressAfterReloginIsIgnored` と
    `StaleProgressFromAbandonedRestoreIsIgnored` の 2 本が落ちる。対になる
    `CurrentGenerationProgressIsStillHonoured` は緑のまま — つまり「そもそも何も配達されて
    いないから緑」ではないことも同時に担保できている。
  - **2FA 資格情報は間接検証に留めた（既知の限界）**。`mPendingEmail`/`mPendingPassword` は
    private で観測できないので、`MockMegaClient::multiFactorAuthLogin` に届く email/password で
    「後続の 2FA 送信が前回の資格情報を運ばない」ことだけを固定した（cancel 後・成功後の 2 経路
    ＋「PIN 誤りの再送信では消してはいけない」という逆側）。**平文パスワードがプロセスメモリに
    残らないことは検証できていない** — 項目 3 が挙げていた R1 の観点はテストでは担保されない。
  - **`kEInternal(-1) → UnknownError` は現状のまま固定し、節 5 に持ち越した**。テストは
    「`-1` は `UnknownError` になり `rawErrorMessage` に SDK の生英文が入る」をそのまま
    アサートしている。UX 上の是非は別件として切り離した。
  - **`signals` は変数名に使えない**（Qt が `#define signals public` するため、最初のビルドが
    `error C2059: 構文エラー: 'public'` で落ちた）。`stateSignals` に改名済み。
  - 実行は **exe 一括（32/32）と `ctest` の 1 ケース 1 プロセス（32/32）の両方で緑**。
    本テストは `QTimer`/`QEventLoop` を使うので R4-8 の「`QCoreApplication` の有無がフィルタ
    依存」の罠に正面から乗るが、`SetUp()` の `testApp()` がそれを閉じている。全体は
    **419/419**（従来 387 ＋ 新規 32）、`/W4` 新規警告ゼロ。

**R4-3 [中] ✅対応済み `DownloadController` にテストが無い（節 5 の持ち越しを回収）**

- 持ち越しの前提が **R1-1 で既に半分解消している**: 「`computeDestinationPath` にパス検証が
  入るならテスト可能な形に出すべき」という指摘に対し、R1-1 は
  `DownloadService::safeLocalFileName`（`src/core/DownloadService.h:69`）として `src/core` の
  静的純関数に出し、`tests/DownloadServiceTest.cpp` が **19 アサーション**で検証済み。
  `computeDestinationPath` の残りは `QStandardPaths::writableLocation` +
  `QDir::toNativeSeparators` だけで、テストしても OS を確認するだけになる。
- **残っている未テストの論理はそこではない**:
  - `downloadFile()` の**重複抑止**（`.h:45` の「No-ops if handle is already queued or active」）。
    連打で同じファイルが二重にキューされないこと。
  - `downloadFinished` の**フィールド構成**（`.h:56-72`）— 成功時の `fileName` が要求名ではなく
    `localPath` の basename であること（SDK のリネームで `photo (1).jpg` になった場合に元の名前を
    返すと「上書きされた」と読める、という理由が書いてある）、`alreadyPresent` の真偽、
    失敗時は要求名のままであること。
  - `refreshActiveJob()` の `downloadActive`/`activeFileName`/`activeProgress` 更新、
    `activeProgress` が総量未知のとき `0.0`（`.h:34`）。
- **障害は 1 つだけ**: `DownloadController.cpp:9` の `QDesktopServices` が `QtGui` を引く
  （`tests/UploadControllerTest.cpp:17-19` が「だから Download 側は入っていない」と説明している
  唯一の理由）。`QDesktopServices` は `openFile()` からしか使われないので、選択肢は
  (a) テストターゲットに `Qt6::Gui` を足す（`openFile` は呼ばない。リンクするだけなら GUI は不要）、
  (b) `openFile()` の 1 行を `src/platform` の小さなポートに出す。
  **(a) を推す** — (b) は 1 行のためにポートを 1 本増やすことになり、R5 の判断を先取りしてしまう。
  → **R4-4(b) の採用（2026-08-07）でこの論点自体が消えた**。`MegaExplorerQml` をリンクすれば
  `Qt6::Gui` は `Qt6::Quick` 経由で推移的に入る。`Qt6Gui.dll` は既に `PATH` に要る Qt の
  `bin` にあるので追加要求も無い。**R4-3 は R4-4 の後に回し、テストを書くだけの作業にする**。
- **対応（2026-08-07）**: `tests/DownloadControllerTest.cpp` を新設。**18 ケース**、6 群
  （重複抑止 / `downloadFinished` のフィールド構成 / アクティブジョブと `Q_PROPERTY` /
  宛先パスの結線 / `openFile` / デストラクタ）。`tests/CMakeLists.txt` は 1 行追加のみ。
  決めた点:
  - **製品側の変更はゼロ**。R4-2 の stall timeout のような注入口は不要だった
    （`DownloadService` を実物で組み、`MockMegaClient::download` のコールバックを
    `Invoke` で捕まえて、テスト側が完了/進捗を発火するだけで全経路に届く）。
  - **配送経路が最大の罠**。サービス→コントローラの 2 経路は `invokeOnGuiThread`
    （`Qt::QueuedConnection`）を通るので、モックが同期に `onDone` を呼んでも
    `downloadFinished` はその場では飛ばない。これ自体を 1 ケース
    （`FinishedIsNotEmittedUntilTheQueuedInvokeIsDelivered`）で明示的に固定したうえで、
    他ケースは `flushQueuedEvents()` 2 回のヘルパ経由にした。一方 `downloadFile()` 末尾の
    `refreshActiveJob()` は直接呼びで、こちらも別ケースで同期であることを固定してある。
  - **`onDone` はコピーしてから発火しないと自己代入になる**。`DownloadService` は
    完了ハンドラの中から次のジョブの `download()` を呼ぶので、フィクスチャのメンバに
    保持した `std::function` を直接 `onDone(...)` すると、実行中の関数オブジェクトが
    自分の呼び出し中に上書きされる。ヘルパでローカルにコピーしてから呼ぶ形に統一した。
  - **3 つの変異テストで裏を取った**（いずれも実施後に復元済み）:
    (1) `hasJobForHandle` の early-return を潰す → 重複抑止 2 ケースが落ち、
    逆側の `DifferentHandlesWithTheSameNameBothEnqueue` は緑のまま（過剰抑止でないことも
    同時に担保）。(2) 完了時の `displayName` を `job.name` 固定にする →
    `SuccessReportsTheSavedLeafNameNotTheRequestedName` のみ落ちる。
    (3) `activeProgress` の `totalBytes == 0` ガードを外す →
    `ActiveProgressIsZeroWhileTotalIsUnknown` が `-nan`/`inf` で落ちる。
  - **`openFile` は `QDesktopServices::setUrlHandler("file", ...)` で横取りしてテストした**。
    ハンドラを登録すると `openUrl` は外部アプリを起動せずハンドラを呼ぶだけになるので、
    テストが実機のアプリを起動する事故が無い。**既知の限界**: 登録中は `openUrl` が
    false を返せないため、失敗 → `qCWarning` + `notifyError("openFile")` の経路は
    検証できていない。moc が要るので `FileUrlHandler` はファイルスコープに置き、
    末尾で `#include "DownloadControllerTest.moc"` している。
  - **宛先パスは「結線」だけを 1 ケースで固定**。`safeLocalFileName` の規則自体は
    `DownloadServiceTest` の 19 アサーションが持っているので、ここでは
    `..\..\evil.exe` が Downloads 直下の `evil.exe` になり `'/'` を含まないことだけを見る。
  - **デストラクタは結果を固定するスモーク**。破棄後に捕まえておいた `onDone` を発火して
    「何も届かず、サービス側のキューは正常に空になる」ことを見る。オブザーバ解除を消すと
    破棄済み `this` への呼び出し（UB）になるが、**必ず落ちる保証は無い**旨をテスト内
    コメントに書いた（R4-2 の `StaleStallTimerCannotFlipASignedInWindow` と同じ書き方）。
  - 実行は **exe 一括（437/437）と `ctest` の 1 ケース 1 プロセス（437/437）の両方で緑**
    （従来 419 ＋ 新規 18）。`/W4` 新規警告ゼロ。
  - 副産物として陳腐化コメントを 2 箇所削除（成果物参照）。なお
    `docs/ARCHITECTURE.md` の `qml/` の項にある「`qt_add_qml_module` は
    `appMegaExplorer` にぶら下がるので URI をインポートできない」は R4-4 で前提が変わって
    いるが、**インポート可否の結論自体は R4-5 の調査事項**なので今回は触っていない。

**R4-4 [中] ✅対応済み ビルド定義が `src/qml` を 3 箇所で列挙している**

- 種は「`tests/CMakeLists.txt` が 20 ファイル手書き」としていたが、実際は**ルート側にも 2 分割が
  あり、三重管理**になっている:

  | 場所 | 対象 | ファイル数 |
  | --- | --- | --- |
  | `CMakeLists.txt:106-124`（`qt_add_executable`） | QML 登録の無い素の `QObject` | 19 |
  | `CMakeLists.txt:171-182`（`qt_add_qml_module` の `SOURCES`） | `QML_ELEMENT` を持つ型 | 12 |
  | `tests/CMakeLists.txt:33-56` | 上記のうちテストに要る分 | 22（＋ platform/app 2） |

- **ルート側の 2 分割それ自体は正しい**。確認したところ `SOURCES` 側の 6 クラスは全て
  `QML_ELEMENT`（`AccountController`/`AuthController`/`LicenseModel`/`KeyboardState`/`MenuActions`/
  `NotificationController`）＋ `QML_FOREIGN` の `WindowAgentForeign` で、型登録の生成に
  `SOURCES` が要る。素の `QObject` を `SOURCES` に入れると無意味な登録コードが増える。
  **問題は 3 番目の `tests/` が 1・2 の部分集合を手で写していること**。
- 実害: `src/qml` にクラスを 1 つ足すと 2〜3 箇所を直す必要があり、`tests/` 側を忘れると
  「テストは書いたのにリンクエラー」になる。逆に不要になったファイルが `tests/` 側に残っても
  誰も気づかない（現に `ThumbnailController` は専用テストが無いままリンクされ続けている）。
- 検討した 2 案:
  - **(a) 変数に括るだけ** — ルートで `set(MEGAEXPLORER_QML_PLAIN_SOURCES ...)` /
    `set(MEGAEXPLORER_QML_REGISTERED_SOURCES ...)` を定義し、`tests/` は前者＋必要分を参照する。
    安い。ただし「テストに要る分だけ」の選択は手で書いたまま残る。
  - **(b) `qt_add_qml_module` をライブラリ target に移す** — Qt の推奨形（モジュールを持つ
    ライブラリ + それをリンクする実行ファイル）。
  - 種の「`MegaExplorerCore` に寄せる」は**不可能**（下の「種が不正確だった」参照）。
- **決定: (b) を採用（2026-08-07、承認済み）**。理由は「コスト回避より Qt の推奨形に寄せる」。
  (a) は列挙の重複を隠すだけで三重管理の根を残し、R4-5 も R5 送りになる。

**R4-4 の実施設計（(b) の具体形）**

- **target は 1 本にする**（`MegaExplorerQml`、STATIC）。`src/qml` の 31 ファイル全部をこれに入れ、
  `qt_add_qml_module` をこの target に付ける。`appMegaExplorer` は `main.cpp` +
  `src/app`/`src/mega`/`src/platform` だけになり、`MegaExplorerQml` をリンクする。
  - **2 本に割る案（Qml 登録型とそれ以外）は却下**。テストが要る 11 クラスのうち
    `AccountController`/`LicenseModel`/`NotificationController` が `QML_ELEMENT` 側にあるので、
    割ってもテストは結局両方をリンクすることになり、列挙が 2 本に戻るだけ。
  - ルート側の `qt_add_executable` / `SOURCES` の 2 分割は**そのまま `MegaExplorerQml` 内の
    2 分割として残す**（`SOURCES` は型登録の生成に要る、という R4-4 冒頭の確認は有効）。
    消えるのは 3 番目の `tests/` 側 22 行。
- **`qt_add_qml_module` はルートの `CMakeLists.txt` に置いたままにする**（`src/qml/CMakeLists.txt`
  を新設しない）。理由 2 つ:
  1. `set_source_files_properties(qml/Theme.qml qml/ActionCatalog.qml PROPERTIES
     QT_QML_SINGLETON_TYPE TRUE)`（`CMakeLists.txt:135-136`）は**同一ディレクトリスコープでしか
     効かない**。`.qml` は `qml/` にあるので、モジュール定義をサブディレクトリへ移すとここが
     黙って無効化され、`qmldir` から `singleton` 行が落ちる（＝ `Theme`/`ActionCatalog` が
     シングルトンでなくなる、実行時まで気づかない壊れ方）。
  2. モジュールの出力先は target のバイナリディレクトリ基準。現状
     `build/msvc-debug/MegaExplorer/`（`qmldir` の `prefer :/qt/qml/MegaExplorer/` を確認済み）に
     出ており、ルートに置いたままなら**この配置が変わらない**。`main.cpp:171` の
     `engine.loadFromModule("MegaExplorer", "Main")` も無変更で済む。
- **`MegaExplorerQml` が要るリンク**: `Qt6::Quick`（`.qml` と QML 型登録）、`MegaExplorerCore`、
  `QWindowKit::Quick`（`src/qml/WindowAgentForeign.h` の `QML_FOREIGN(QWK::QuickWindowAgent)`）。
  `crypt32` は `src/platform` 側なので `appMegaExplorer` に残る。
- **`MegaExplorerTests` への波及（これが (b) の実質的な代償）**: 現在 `Qt6::Core` + `Qt6::Qml`
  だけの test target が、`MegaExplorerQml` 経由で `Qt6::Quick`／`Qt6::Gui`／`QWindowKit::Quick` を
  引く。評価:
  - **DLL の追加要求は無い**。`Qt6Qml.dll` は既に必要で、`Qt6Quick.dll`/`Qt6Gui.dll` は同じ
    `C:/Qt/6.11.1/msvc2022_64/bin` にある（確認済み）。`PATH` の指定は現状のままでよい。
    QWindowKit は `QWINDOWKIT_BUILD_STATIC ON`（`CMakeLists.txt:27`）なので DLL を増やさない。
  - `gtest_discover_tests` はビルド時に exe を起動して列挙するので、そこが通ることは
    ビルド 1 回で確認できる。
  - **副産物: R4-3 の唯一の障害が消える**。`DownloadController` の `QDesktopServices` が要求する
    `Qt6::Gui` が推移的に入るので、R4-3 は「`Qt6::Gui` を足す判断」ではなく素直にテストを書く
    作業になる。**R4-4 を R4-3 より先にやる理由がここにもある**。
- **`/W4` を落とさないこと（R4-9 参照）**。`/W4` は `appMegaExplorer` に target 単位で付いている
  （`CMakeLists.txt:216`）ので、`src/qml` の 5,529 行を新 target へ移すと**そのまま `/W4` の
  監視対象から外れる**。`target_compile_options(MegaExplorerQml PRIVATE /W4)` を**同じコミットで**
  入れる。これを忘れると `CLAUDE.md` の「`src/` を触ったら警告を確認」が静かに嘘になる。
- **既知の落とし穴**: AOT の `qmlcache_loader.cpp` アグリゲータは target 名を含むので、
  **再 configure（`cmake --preset msvc-debug`）が必須**（`CLAUDE.md` Build 節の Phase 9 の件と
  同じ機構）。`CMakeLists.txt:203-211` の「`qmltyperegistrations.cpp` が `<AuthController.h>` を
  裸のファイル名で include する」ための `target_include_directories(... src/qml)` も
  `MegaExplorerQml` 側へ移す。
- **検証**: `MegaExplorerTests` 387 ケースが緑、`/W4` 警告ゼロ、そして**アプリが実際に起動して
  ウィンドウが出ること**（`ui-style` スキルの `ui_shot.py cycle` でスクショ 1 枚）。
  静的ライブラリに入った QML リソースの初期化漏れは**ビルドもテストも通ったうえで起動時に
  `module "MegaExplorer" is not installed` で落ちる**タイプなので、起動確認を省略できない。

- **対応（2026-08-07、R4-9 と同一コミット）**: (b) を実施設計どおり実施。`MegaExplorerQml`
  （STATIC）に `src/qml` 31 ファイルと `qt_add_qml_module` を移し、`appMegaExplorer` は
  `main.cpp` + `src/app`/`src/mega`/`src/platform` の 11 ファイルに、`tests/CMakeLists.txt` は
  手写し 22 行が消えて `MegaExplorerQml` のリンク 1 行になった。決めた点と、**実施設計が
  外していた点 3 つ**:
  - **`OUTPUT_DIRECTORY` の明示が必須だった（実施設計の誤り）**。設計は「ルートに置いたままなら
    配置は変わらない」としていたが、Qt のドキュメントは *backing target が実行ファイルのときだけ*
    出力先に target path が付く、と規定している。ライブラリでは `CMAKE_CURRENT_BINARY_DIR`
    直下になり、`qmldir` がビルドルートに落ちて `<importpath>/MegaExplorer/qmldir` を探す
    import path 解決（qmllint / Qt Creator）が壊れる。
    `OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MegaExplorer` で従来の配置に固定した。
    **リソースパスは無関係**（`:/qt/qml/MegaExplorer/` のまま）なので `loadFromModule` は無変更。
  - **`NO_PLUGIN` は罠。プラグインのリンクと `Q_IMPORT_QML_PLUGIN` の両方が要る**。
    実施設計は「起動時に落ちたら (a) `Q_IMPORT_QML_PLUGIN` か (b) `NO_PLUGIN`」と書いていたが、
    **(b) は誤り**。実際に 2 段階で踏んだ:
    1. 既定構成のまま起動 → `module "MegaExplorer" plugin "MegaExplorerQmlplugin" not found`。
       STATIC backing target が生成する静的プラグインを誰もリンクしないため、engine が
       `optional plugin` 行を*動的*ロードにフォールバックして失敗する。
    2. `NO_PLUGIN` を付けるとモジュール自体は import できるようになるが、今度は
       `WindowAgent is not a type` で落ちる。**型登録が丸ごと効いていない**。
       `dumpbin` で確認: `qml_register_types_MegaExplorer` は `MegaExplorerQml.lib` に存在し、
       `appMegaExplorer.exe` には無い。登録は main.cpp が名前で参照しない生成 TU にあるので、
       リンカが静的ライブラリからその .obj を落とす — Qt のドキュメントが `NO_PLUGIN` の注意書きで
       「リンカが未使用と判断したライブラリを保持する保証は無い」と書いているそのものだった。
    3. 結論: `NO_PLUGIN` を外して `target_link_libraries(appMegaExplorer PRIVATE
       MegaExplorerQmlplugin)` ＋ main.cpp に `Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)`。
       **片方だけでは動かない**。`qt_import_qml_plugins()` は非静的 Qt では no-op なので代替に
       ならない。**R4-5 で Qt Quick Test の target を足すときも同じ 2 点セットが要る**。
  - **「コード変更ゼロ」は達成できなかった**。上記のとおり `main.cpp` に
    `Q_IMPORT_QML_PLUGIN` の 1 行（＋ include）が入る。製品の振る舞いは不変。
  - **`SOURCES` と素の `QObject` の 2 分割は維持したが、根拠は無効**。Qt のドキュメント上
    `SOURCES` は `target_sources()` と等価な便宜機能で、型登録は AUTOMOC を通った backing target
    の全ソースを走査して生成される。つまり「`SOURCES` は型登録の生成に要る」（調査時の確認）は
    **不正確**で、2 分割は登録結果に影響しない。今回は差分を小さく保つため現状維持にした。
    **統合するかは R7 の判断材料**（片方に寄せれば列挙が 1 ブロックになる）。
  - 検証: 387 ケース緑（増減なし）、自前 4 target の警告ゼロ、`qmldir` は従来の
    `build/msvc-debug/MegaExplorer/` に `singleton` 2 行を保ったまま出力、
    アプリ起動＆ログイン画面の描画をスクショで確認。configure 時に出る QWindowKit の
    `CorePrivate/GuiPrivate/QuickPrivate ... not declared` 警告は
    `_qt_internal_finalize_executable` 由来の**既存**のもので、R4-4 とは無関係。

**R4-5 [中] ✅対応済み `qml/` 7,356 行が全面ノーテストで、かつ現在のビルド構成ではテスト用に import できない**

- 種は「対象を絞って `qt-development-skills:qt-qml-test` を使う」としており方向は正しいが、
  **先に構成上の障害がある**: `qt_add_qml_module` は `appMegaExplorer`（`WIN32_EXECUTABLE` な
  実行ファイル）に付いている（`CMakeLists.txt:138`）ので、URI `MegaExplorer` は exe の中にしか
  存在せず、`qmltestrunner` / Qt Quick Test の target から import できない。
  → **R4-4(b) が前提。R4-4 の完了（2026-08-07）でこの障害は解消済み**
  （`MegaExplorerQml` を Quick Test の target からリンクすれば URI `MegaExplorer` が import 可能に
  なる）。R4-5 は R4 の範囲内に確定。**ただし R4-4 の対応ログのとおり、リンクするだけでは足りず
  `MegaExplorerQmlplugin` のリンク ＋ `Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)` の 2 点セットが
  Quick Test の target 側にも要る**。
- 環境側は揃っている: `C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6QuickTest` が存在し、
  `find_package(Qt6 COMPONENTS QuickTest)` は通る。現状 `CMakeLists.txt`/`CMakePresets.json` に
  `QuickTest`/`qmltestrunner`/`qmllint` の記述は 1 つも無い。
- 対象の絞り込み（関数を実測して選定）。**純粋な入力→出力の関数だけを狙う**:
  - `qml/components/ToastStack.qml` — `describeReason(clause, reason, rawMessage)`（:162）、
    `showError(context, reason, rawMessage)`（:185）、`showOperation(context, succeeded, failed)`
    （:98）、`showDownload(...)`（:82）。**R3-4/R3-5 で C++ から enum（context + reason）だけを
    受け取る形にしたばかりの文面合成器**で、分岐が最も多く、壊れても静かに間違った文が出るだけ。
    R4 で最も費用対効果が高い。
  - `qml/ActionCatalog.qml` — `label` / `icon` / `isEnabled` / `trigger`（:143-167）。
    引数は呼び出し側が組み立てる `ctx` オブジェクト 1 個だけなので、偽の `ctx` を渡せば足りる
    （:16 のコメントが「Every entry is a function of one `ctx` object」と設計を明示している）。
  - `qml/components/DragProxy.qml` — `canDropOn(handle, isRoot)`（:179）と
    `sampleCopyMode()`（:58）。後者は `KeyboardState` に触るので R4 では `canDropOn` だけでよい。
- **対象外にすると決めておくもの**: `FileTableView.qml`（1,060 行）/ `FileGridView.qml`（766 行）/
  `TabStrip.qml`（537 行）。描画とジェスチャが本体で、Qt Quick Test で書くと壊れやすいテストに
  なる。R6 の安全網としては上記 3 ファイルの純関数で足りる、と割り切る。
- **対応（2026-08-07）**: `tests/qml/` に `MegaExplorerQmlTests`（Qt Quick Test）を新設し、
  **105 ケース**（ToastStack 47 / ActionCatalog 46 / DragProxy 12、`initTestCase` 等を除く実数）。
  ctest は `.qml` ファイル毎に 1 エントリで **455 → 458**。着手前に決めた 3 点と、
  **実施設計が外していた点 3 つ**:
  - **ToastStack は `describe*` を切り出した**（承認済みの選択肢）。`showError`/`showOperation`/
    `showDownload` は戻り値が無く、唯一の観測点 `toastModel` は `id` なのでテストから届かない。
    文字列を返す `describeError` / `describeOperation` / `describeDownload` を分離し、`show*` は
    `push(describe*(...))` になった（挙動不変）。これで**シーンも ListModel も delegate も
    Timer も生成せずに 14 の error context・4 の operation context を全分岐 assert できる**。
    `model` の alias 露出案を採らなかったのは、全ケースで FluentWinUI3 の delegate と 6 秒
    Timer が立ち上がる代償に見合うのが `push()` のトリムだけだったため（→ 下の取りこぼし）。
  - **`-o -,tap` は飾りではなく必須だった（実施設計の見落とし・今回の最大の落とし穴）**。
    最初 `add_test` を素で登録したところ ctest は緑になったが、**わざと失敗させても
    `--output-on-failure` が空**だった。原因は QtTest の既定 plain logger で、Windows では
    プロセスにコンソールが付いていないとき出力を `OutputDebugStringA` に振り替えて stdout に
    何も書かない —— ctest 経由も Git Bash からの手動実行も、まさにその条件に当たる。
    `-o -,txt` でも同じ（stream が stdout である限り同じ分岐）。**TAP logger にはこの分岐が
    無い**ので `-o -,tap` に固定した。失敗時に `.qml` のファイル名と行番号まで出る。
    これを踏まないと「将来の失敗が全部デバッグ不能」という形で静かに効き続けるところだった。
  - **`src/app/Logging.cpp` のコンパイルが要った（実施設計の漏れ）**。`MegaExplorerQml` の中の
    `src/qml` 各クラスが `lcNavigation()` 等を呼ぶが、これらはライブラリではなく
    `src/app/Logging.cpp` に定義があるため、リンクだけでは 10 個の未解決シンボルになる。
    `tests/CMakeLists.txt` が同じ理由で同じファイルを足しているのを見落としていた。
  - **`add_executable` であって `qt_add_executable` ではない**（実施設計どおりだが理由を明記）。
    後者は Windows で `WIN32_EXECUTABLE` を付けるので、テストランナーの出力先が消える。
  - **`.qml` を直したら再ビルドしないとテストは古い実装を見る**。`ToastStack` はソースツリー
    からではなく `:/qt/qml/MegaExplorer/` のリソースから読まれるので、`describe*` を足した直後に
    テストを回して 35 件が `is not a function` で落ちた。テスト側 `.qml` は `-input` で
    ソースツリーから直に読まれる**のに製品側 `.qml` はそうではない**、という非対称が原因で、
    ここを取り違えると「テストが間違っている」と誤診する。
  - 「387 → 390」という実施設計の見積もりは**基準値が古かった**。387 は R4 調査時点の数で、
    R4-2/R4-3/R4-7 が足した分が反映されていない。実際の基準は 455。
- **意図的に取りこぼしたもの**（いずれも gap であって「テストしない決定」ではない。
  `docs/ARCHITECTURE.md` の Testing 規約にも同じ 3 件を並べてある）:
  - **`ActionCatalog` の `trigger` 11 個中 5 個**（`download` / `openInNewTab` / `togglePin` /
    `cut` / `copy`）。`downloadController` 等のコンテキストプロパティを直接名指しし、それを
    設定するのは `main.cpp` だけなので、埋めるには `QUICK_TEST_MAIN_WITH_SETUP` +
    C++ のテストダブルが要る。今回は「C++ を増やさない」を優先した。残る 6 個（`ctx` の
    コールバックと `ctx.navController` に流れるもの）は**カバー済み**。
  - **`ToastStack.push()` の `maxVisible` トリム**。上記のとおり `toastModel` を露出させない
    判断の裏返し。`show*` は `nextSeq` の増分で「押し込んだ／押し込まなかった」だけ見ている。
  - **`DragProxy.sampleCopyMode()`**。`KeyboardState.modifiers()` が実 OS のキー状態を読むので
    QML から決定的にできない。`begin()`/`moveTo()`/`finish()` も内部でこれを呼ぶため、
    まとめてテストから外した（調査時の判断どおり）。
- 検証: 458 ケース緑、自前 5 target を中間生成物ごと消してのフルリビルドで**警告ゼロ**、
  アプリ起動＆Cloud Drive 描画をスクショで確認（`ToastStack` は `Main.qml` が無条件に生成する
  ので、起動できた時点で改造後の QML が読めていることの担保になる）。

**R4-6 [中〜大] ✅対応済み スレッド起因の欠陥を構造的に検出できない（R2-19 / 節 5 の持ち越しを回収）**

- 事実確認（R2-19 の再確認）: `tests/MockMegaClient.h` は純 gmock で、完了は全て
  `testing::InvokeArgument<N>` によりテスト自身のスレッドで同期に配達される。
  `std::thread` / `QThread` / `std::atomic` は `src/` `tests/` ともにゼロヒット。
- 帰結: `DownloadService` / `ThumbnailService` の `std::mutex` は一度も競合せず、
  **ロックを削除する変更もテストが通る**。R2-1 の TOCTOU と R2-5 の `makeGuiOwned` の
  クロススレッド分岐（`src/qml/GuiThread.h`）は**現行の枠組みでは到達不能**。
- R4 で決めるべきは「やる/やらない」であって、やるなら 3 段階ある。**段階ごとに独立して価値が
  あるので、途中で止めてよい**:
  1. **同期のまま取れる分を取る** — R2-19 自身が「多段再帰（R2-2）はスレッドと無関係なので
     単一スレッドで再現できる」と結論している。「同期失敗を N 件返すモック」を足すだけ。安い。
  2. **`MockMegaClient` にワーカスレッド配達モードを足す** — `InvokeArgument` の代わりに
     `std::thread` でコールバックを撃つオプション。`makeGuiOwned` のクロススレッド分岐と
     `invokeOnGuiThread` の実配達が初めて走る。`testApp()` の `QCoreApplication` が
     全プロセスで要る（→ R4-8 と衝突するので順序に注意）。
  3. **サニタイザ構成** — MSVC は `/fsanitize=address` を持つが、**ThreadSanitizer は無い**
     （clang-cl でも Windows 版 TSan は未サポート）。つまり本命のデータ競合検出器は
     この toolchain では使えない。ASan で取れるのは UAF（R2-5 の系列）まで。
     `docs/investigations/CROSS_PLATFORM_INVESTIGATION.md` の結論（Linux 化の障害は `WindowsSessionStore` だけ）と
     合わせると、「TSan のためだけに Linux ビルドを起こす」は R4 の範囲を超える。**3 は見送りを
     推奨**し、ASan 付きプリセットの追加だけを Release プリセット整備（節 5 の持ち越し）と
     一緒に扱う。
- **1 と 2 のどちらまでやるかは着手前に承認が要る**（2 は `MockMegaClient` の全 16 利用箇所に
  影響しうる設計変更）。
- **承認された範囲（2026-08-07）**: 段階 1 の残ギャップ ＋ 段階 2 を opt-in 限定。段階 3 は上記
  のとおり見送り確定。
- **対応（2026-08-07）**: 製品コードの変更はゼロ。464 ケース緑（+9）、ctest 467/467。決めた点:
  - **着手時点で段階 1 の前提が古くなっていた**。「同期失敗を N 件返すモックを足すだけ」の本体は
    R2-2 の修正時に既に書かれている（`DownloadServiceTest.cpp` の
    `SynchronousFailuresDrainTheQueueWithoutRecursing` ほか 3 サービス分）。**上の段階 1 の記述は
    調査時点のもので、実施内容とは違う**。代わりに埋めたのは、①成功と失敗が混在するキュー
    （17 ケース中ゼロだった）、②`if (mQueue.empty()) return;` の早期 return 経路の 2 件。
  - **UploadService には失敗の出口が 2 つあり、混在キューでしか差が出ない**。`onDone` 経由
    （トランポリンが受ける）と `checkUpload` 拒否の `continue` 経由（**自分で `mAdvancing` を
    クリアしなければならない側**）で、既存の 2 つのドレインテストはどちらも均質なキューなので
    後者を取り違えても緑になる。`CheckUploadRejectionMidQueueDoesNotStopTheJobBehindIt` はその
    差を突く唯一のケース。
  - **段階 2 は `MockMegaClient` に触らずに実現した**。配達方法はモック本体ではなく**呼び出し側の
    gmock アクション**が決めているので、専用モードを足す必要が無い。既存 18 ファイル・199 箇所の
    `InvokeArgument` は 1 行も変えていない — これが opt-in であることの構造的な担保になる
    （`tests/WorkerDelivery.h` の `deliver()` にコールバックを渡すだけ。専用アクション
    テンプレートも作っていない）。
  - **最初に書いた 4a/4b は無効なテストだった**。逆確認（`invokeOnGuiThread` を
    `Qt::DirectConnection` に差し替える）で発覚: **auto-connection のせいで、ワーカで emit しても
    Qt 自身が受信側スレッドへキューし直す**ため、観測ラムダは常に GUI スレッドを報告する。
    製品側の hop を消しても緑のままだった。観測側を明示的に `Qt::DirectConnection` にして修正。
    この罠は次に書く人が同じ穴に落ちるので `docs/ARCHITECTURE.md` にも書いた。
  - **`flushDeferredDeletes()` が要る**。`deleteLater` の `DeferredDelete` を `processEvents()` が
    配達するかはイベントループのネスト段数に依存し、テストは `exec()` の外で走る。型を明示した
    `sendPostedEvents` は無条件なので、そちらに寄せた。
  - **逆確認は 2 通り効くことを確認**。`makeGuiOwned` のデリータを素の `delete` にすると
    テスト 1 だけが落ち、`invokeOnGuiThread` を `Qt::DirectConnection` にすると残る 4 つだけが
    落ちる（直交している）。flaky チェックは `--gtest_repeat=50` × 2 回で 0 件。
  - **担保できたのは経路の実行であって、データ競合の検出ではない**。TSan がこの toolchain に
    無い以上、3 サービスの mutex は依然として消しても緑になる。この限界の明記が R4 の成果物 (c)。

#### 種が不正確だった / 判断が要るもの

- **「`MegaExplorerCore` に寄せる」は不可能**。`MegaExplorerCore` は `CMakeLists.txt:46-89` の
  とおり **Qt を一切リンクしていない**静的ライブラリで、`target_compile_features(... cxx_std_17)`
  だけが付いている（:86-89 のコメントが「links no Qt at all」と明記）。`src/qml` は全て
  `QObject` 派生で moc が要るので、寄せた瞬間に `MegaExplorerCore` の「SDK-free かつ Qt-free な
  ドメイン層」という性質が壊れる。**R4-4 の (a) か (b) のどちらかしか無い**。
- **「20 ファイル手書き」→ 実数は 22**（`src/qml` の 11 クラス × 2）。加えて
  `src/platform/WindowsSessionStore.cpp` と `src/app/Logging.cpp` の 2 行があり、こちらには
  `tests/CMakeLists.txt:3-9` に**理由がきちんと書いてある**ので手書きのままでよい。
- **「未テストの `src/qml`: AuthController, DownloadController, ThumbnailController, MenuActions,
  KeyboardState」→ 3 件は要修正**:
  - `ThumbnailController` は**未テストではなくテストターゲットにリンク済み**
    （`tests/CMakeLists.txt:41-42`）。`TabsControllerTest` がタブ毎に構築するために必要。
    実体は 125 行（`.cpp` 64 + `.h` 61）で分岐がほぼ無く、専用テストの価値は低い。
  - `MenuActions` は 82 行で、中身は `MenuActions::Site` → `MenuSite` の 2 値 switch と
    `resolveMenuActions` の呼び出しだけ（`MenuActions.cpp`）。肝心の `MenuActionResolver` は
    **47 ケースで検証済み**。「未知の site で空リストを返す」1 ケースで足り、優先度は低い。
  - `KeyboardState` は `QGuiApplication::queryKeyboardModifiers()` を 1 行返すだけ
    （`KeyboardState.h:33-36`）。**テスト不要**と明記して閉じる側。
- **「`AuthController` は `LoadingStage` 状態機械を持つのでテスト価値が高い」→ 正しい**。R4-2 で
  そのまま採用。ただし 8 秒のストールタイマがハードコードされている点は種に無かった追加条件。

#### 問題なしと確認できた種

- **`src/core` にカバレッジの穴は無い**。11 サービス全てに対応する `*ServiceTest.cpp` / 
  `MenuActionResolverTest.cpp` があり、行数と密度も釣り合っている（最大の
  `DownloadService.cpp` 241 行に 17 ケース、`FileOperationService.cpp` 149 行に 37 ケース）。
- **モックの重複が無い**。`IMegaClient` の偽物は `tests/MockMegaClient.h` 1 つだけで、
  16 テストが共有している。テスト内でローカルに `class Fake...: public IMegaClient` を定義して
  いる箇所はゼロ。`ISessionStore`/`IPinnedFolderStore` も同様に 1 つずつ。
- **無効化されたテストが無い**。`DISABLED_` / `GTEST_SKIP` ともにゼロヒット。
- **`FileEntry` ビルダの重複は 3 箇所だが、寄せる価値は低い**。`FileListModelTest.cpp:12,20` の
  `makeEntry`/`makeFolderEntry`、`FolderNavigationControllerTest.cpp:58` の `entry`、
  `UploadControllerTest.cpp:41` の `entry` — シグネチャが 3 者で違い（フォルダ判定の扱いが違う）、
  共通化すると呼び出し側が長くなる。**現状維持でよい**と判断する。

#### 新規発見

**R4-7 [低] ✅対応済み `QSettingsPinnedFolderStore` にアダプタテストが無い**

- `tests/CMakeLists.txt:3-9` は `WindowsSessionStore` を実アダプタとしてテストする理由を
  「`MegaSdkClient` と違って完全にオフラインでテスト可能だから」と書いている。
  `QSettingsPinnedFolderStore`（97 行）は**まったく同じ基準を満たすのにテストが無い**。
- テストする価値のある振る舞いは `.h` のコメントが自分で挙げている:
  - **アカウント毎のキー入れ子**（Phase 11a）。フラットキー時代の実バグは「アカウントを
    切り替えると前のアカウントのピンを読んで上書きする」で、**これはまさに回帰テストが要る型**。
  - **JSON 単一文字列にした理由**（`beginWriteArray` だとリストを縮めたとき古い index が残る）。
    つまり「5 件 → 2 件に減らして読み直すと 2 件」が壊れやすい点として自己申告されている。
  - `Result` の失敗経路（R3-3 で `[[nodiscard]]` と失敗報告が入った側）。
- コストは低い: `QSettings` に明示パス（`QSettings(tempFile, QSettings::IniFormat)`）を渡すか
  `QCoreApplication::setOrganizationName` をテスト用に振れば、レジストリを汚さずに済む。
  ただし現状のコンストラクタは引数を取らないので、**パス注入の口を開ける小さな製品側変更が要る**。
- **対応（2026-08-07）**: `tests/QSettingsPinnedFolderStoreTest.cpp` を新設（**17 ケース**）。
  決めた点:
  - **注入は「既定値付きの ini パス引数」**（`explicit QSettingsPinnedFolderStore(std::string
    iniFilePath = {})`）。空＝既定の `QSettings`（＝本番のレジストリ経路）なので **`main.cpp` は
    無変更・保存先も挙動も不変**、非空＝その ini だけを見る。`QSettings` はコピー/ムーブ不可なので
    `.cpp` 側は `std::unique_ptr<QSettings>` を返す生成ヘルパ 1 つで両経路を吸収した。
    `QSettings::setDefaultFormat`/`setPath` をテスト側で振る案（製品側変更ゼロ）は、プロセス全体の
    グローバル状態に依存し、将来 `QSettings` を使うテストが増えると順序依存になるので採らなかった。
  - **`QCoreApplication` は不要**。明示パス + `IniFormat` の `QSettings` は organization/application
    名にもアプリインスタンスにも依存しない。よって `TestApp.h` は include していない
    （R4-8 の論点をこのファイルに持ち込まないため）。
  - **一時 ini は 1 ケース 1 個・テスト名埋め込み**（`WindowsSessionStoreTest.cpp` と同じ流儀）。
    ここではパスの一意性が単なる作法以上の意味を持つ: `QSettings` はファイルパスをキーに
    プロセス内キャッシュを共有するので、パスを使い回すとケース間で互いの未書き出し状態が見える。
  - **`save` の失敗経路も入った**（調査時は「決定論的に失敗させにくい」と見ていた）。
    「親ディレクトリの位置に既存の通常ファイルがある」パスを渡すと `QSettings` はディレクトリも
    ファイルも作れず `status() != NoError` になる。`--gtest_repeat=5` で安定を確認済み。
  - カバーした振る舞い: 往復と順序、48bit ハンドルの厳密往復（`double` 経由の主張の検証）、
    UTF-8 とカンマ/引用符入りの名前、**5 件 → 2 件の縮小**と空リスト化、**アカウント毎の入れ子**
    （読み側・書き側の両方＝Phase 11a の実バグの回帰）、壊れた保存値と配列でない JSON の
    `kEInternal` 失敗、非オブジェクト要素・`handle == 0`・handle 欠落のスキップ、name 欠落時の
    ハンドル保持。

**R4-8 [低] ✅対応済み 387 ケースが 387 プロセスに分かれ、`QCoreApplication` の有無がフィルタ依存になっている**

- `tests/CMakeLists.txt:83` の `gtest_discover_tests` は **1 gtest ケース = 1 ctest テスト**を
  生成するので、`ctest` は exe を 387 回起動する（`--gtest_filter` 付き）。
- `TestApp.h` の `testApp()` は関数内 static なので、**呼ばれたプロセスにしか
  `QCoreApplication` が存在しない**。呼んでいるのは 22 テスト中 **7 つ**だけ。
- 具体的なずれ: `TabsControllerTest.cpp` は `qml/GuiThread.h` を include し
  `makeGuiOwned`/`invokeOnGuiThread` の経路を通るが、`TestApp.h` を include していない。
  そのケースだけを走らせるプロセスには `QCoreApplication` もイベントループも無いので、
  キューされた呼び出しは**一度も配達されずに `~QObject` で捨てられる**。
  **今は無害**（同ファイル :16-25 が自ら「モックに `EXPECT_CALL` が無いので未応答のまま返る、
  どのアサーションも fetch の完了に依存しない」と宣言している）。
- しかし: 将来 `TabsControllerTest` にキュー済みコールバックを検証するケースを 1 つ足すと、
  **exe 全体で走らせれば通り、ctest の単体フィルタでは落ちる**（あるいはその逆）という
  再現性の無い失敗になる。R4-6 の段階 2（ワーカスレッド配達モード）を入れると全テストが
  この経路に乗るので、**先に閉じておく必要がある**。
- 修正方針: `::testing::AddGlobalTestEnvironment` で `QCoreApplication` をプロセス毎に 1 回
  構築する（`TestApp.h` の「1 プロセス 1 インスタンス」制約はそのまま満たせる）。
  全 fixture の `SetUp` に `testApp()` を書き足すより漏れが無い。
- **対応（2026-08-07）**: `tests/TestMain.cpp` を新設し、`QCoreApplication` を `main` のローカルに
  した。`testApp()` は削除、`TestApp.h` は `flushQueuedEvents()` だけの薄いヘッダとして名前ごと
  残した（include 行を 7 ファイルで書き換える価値が無いため）。決めた点:
  - **`AddGlobalTestEnvironment` ではなく独自 `main` にした**（調査時の方針からの変更）。
    環境登録も**結局 `.cpp` の新設が要る** — `TestApp.h` に静的初期化で書くと include している
    10 TU から 10 回登録されるので、ファイル数は同じ。同じコストなら
    (a) `QCoreApplication` は `argc` への**参照**を保持するため `main` のローカルなら寿命が
    構造的に正しい（旧 `testApp()` が `argc`/`argv` まで関数内 static にしていたのはこの回避）、
    (b) 静的デストラクタより**前**に破棄される、という 2 点で独自 `main` が上。(b) は
    R4-6 段階 2 でワーカスレッドが絡んだときに「誰が先に死ぬか」を読めるかどうかの差になる。
  - **`GTest::gtest_main` → `GTest::gtest` は必須**（`main` の二重定義になる）。ついでに
    `InitGoogleMock` を呼ぶ形にした。`gtest_main` の `main` は `InitGoogleTest` しか呼ばないので、
    **`GTest::gmock` をリンクしているのに `--gmock_*` フラグは一度もパースされていなかった**。
    リークしたモックの報告は `MockObjectRegistry` のデストラクタ由来で init とは無関係なので、
    既存 454 ケースの挙動は変わっていない（実測でも変化なし）。
  - **不変条件そのものの回帰テストを `TestMain.cpp` に 1 ケース置いた**
    （`TestProcessEnvironment.QueuedGuiThreadCallIsDelivered`）。`QCoreApplication::instance()`
    の非 null だけでなく `invokeOnGuiThread` → `flushQueuedEvents` の**実配達**まで見る。
    守りたいのは「インスタンスがある」ではなく「キューした呼び出しが届く」方なので。
    不変条件を作る側のファイルがその検証も持つ形にした。
  - **`gtest_discover_tests` の 1 ケース 1 プロセスは維持**。プロセス数を減らせば同じ問題は
    消えるが、失敗ケースが ctest 上で名指しされる利点を失うので採らなかった。
- 実測: **454 → 455 ケース**、`exe` 一括と `ctest`（1 ケース 1 プロセス）の**両方で緑**。
  `ctest` 全体 27.9 秒 / 1 ケース 0.05〜0.06 秒で、全プロセスで `QCoreApplication` を建てる
  オーバーヘッドは計測ノイズに埋もれた。`/W4` 新規警告ゼロ。
- 調査時「22 ファイル中 7 つが `testApp()` を呼ぶ」だったのが、実施時には **25 ファイル中 10**
  になっていた。間に入った R4-2/R4-3/R4-7 の 3 件がそのまま増分で、**fixture 側に書く方式は
  放っておくと書き忘れの母数が増え続ける**ことの実例になっている。
- `QSettingsPinnedFolderStoreTest` は R4-7 で意図的に `QCoreApplication` 非依存にしてあるが、
  明示パス + `IniFormat` は organization/application 名を見ないので影響なし（`ctest` で確認）。

**R4-9 [中] ✅対応済み `/W4` は `appMegaExplorer` にしか掛かっておらず、`src/core` は既に監視外**

- `CLAUDE.md` の Build 節は「`main.cpp`/`src/` を触ったら `/W4` の新規警告を潰す」と指示するが、
  `target_compile_options(... /W4)` は `CMakeLists.txt:216` の `appMegaExplorer` **1 target
  だけ**に付いている。生成された `.vcxproj` で実測:

  | target | `WarningLevel` | 対象 |
  | --- | --- | --- |
  | `appMegaExplorer` | `Level4` | `main.cpp` / `src/app` / `src/mega` / `src/platform` / `src/qml` |
  | `MegaExplorerCore` | **指定なし（＝ MSVC 既定の `/W1`）** | `src/core` 1,379 行 |
  | `MegaExplorerTests` | **指定なし（＝ `/W1`）** | `tests/` 7,919 行 |

- つまり `src/core` の `.cpp` は**一度も `/W4` で見られていない**。`appMegaExplorer` は
  `MegaExplorerCore` をリンクするだけで再コンパイルしないので、ヘッダ経由で漏れてくる分しか
  出てこない。R3 で `src/core` を広範に触ったが、その警告確認は実質 `MegaExplorerCore` を
  素通りしていたことになる。
- **R4-4(b) はこれを悪化させる**: `src/qml` の 5,529 行が `appMegaExplorer` から
  `MegaExplorerQml` へ移るので、放置すると `/W4` の対象が `main.cpp` + 3 ディレクトリだけに縮む。
- 修正方針: `/W4` を target 毎に書き足すのではなく、**自前の 3 target に一括で掛ける**
  （ルートで一度 `foreach` するか、`MEGAEXPLORER_WARNINGS` インターフェース target を 1 つ作って
  3 つが `target_link_libraries` する）。`third_party`/`SDKlib` に波及しないことが要件なので、
  `CMAKE_CXX_FLAGS` へのグローバル追加は**採ってはいけない**（`CMakeLists.txt:213-215` が
  target 単位にした理由をそう書いている）。
- **`src/core`/`tests` を `/W4` にした初回は既存警告が出る前提**。件数次第で「R4-9 で潰す」か
  「R7 に送る」かを決める。`MegaExplorerQml` への `/W4` 付与（現状維持のため）だけは
  R4-4 と同じコミットで必ず入れる。
- **対応（2026-08-07、R4-4 と同一コミット）**: `MegaExplorerWarnings` という INTERFACE target を
  1 つ作り、自前 4 target（`MegaExplorerCore` / `MegaExplorerQml` / `appMegaExplorer` /
  `MegaExplorerTests`）が `PRIVATE` でリンクする形にした。`PRIVATE` なので `third_party`/
  `SDKlib`/`QWindowKit` には一切伝播しない。決めた点:
  - **`src/core` と `tests/` の既存警告はゼロだった**。判断ゲート（少数なら潰す／多数なら R7 送り）
    は不要で、R7 への持ち越しは無し。
  - **`/W4` だけでは足りず、`/external:W0` と `/wd4702` が要る**。これは調査時に見えていなかった:
    - CMake は Qt/GTest の include を `/external:I` で渡している（imported target は既定で
      SYSTEM 扱い）が、**MSVC は `/external:W<n>` を指定するまでその指定を無視する**。
      `/external:W0` を足すまで、生成コード（moc / qmlcachegen / 型登録）から実体化された
      Qt ヘッダの警告が自前コードの警告と同列に出ていた。
    - `/wd4702`（到達不能コード）は `/external:W0` では消せない。**バックエンドが出す警告で、
      external ヘッダの概念を持たない**ため。フルリビルドで 51 件、すべて
      `qjsengine.h`/`qvariant.h`/`qjsprimitivevalue.h` の中（qmlcachegen の AOT 出力からの
      実体化）で、`src/` 由来はゼロ。放置するとフルリビルドが常に 51 警告で終わり、
      R4-9 が作ろうとしている監視自体が無意味になるので抑止した。
  - **なぜ今まで気づかれなかったか**: 生成ソースは増分ビルドで再コンパイルされないので、
    `CLAUDE.md` の手順（`main.cpp`/`src/` を触った後にビルドして確認）では**一度も現れない**。
    R4-4 の target 移動が初めてフルリビルドを強制して露出した。この点は `CLAUDE.md` と
    `docs/BUILD.md` の両方に注記した。

#### 推奨実施順（各項目 1 セッション）

**R4-4 は (b) で確定（2026-08-07）**。これで R4-5 が範囲内に入り、R4-3 が安くなり、
R4-9 が R4-4 と同時実施の必須項目になった。

```
R4-1  慣例の書き直し（ARCHITECTURE.md + コメント 10 箇所）✅ … コード変更ゼロ。認識を先に揃える
R4-4  MegaExplorerQml への分離（(b) 確定）                ✅ … ★R4 の背骨。R4-9 の /W4 維持を同梱
      + R4-9  /W4 を自前 4 target へ                     ✅ … 既存警告ゼロ。R7 送りは発生せず
R4-2  AuthController のテスト                             ✅ … 32 ケース。stall timeout のみ注入
R4-3  DownloadController のテスト                         ✅ … 18 ケース。製品側の変更ゼロ
R4-7  QSettingsPinnedFolderStore のテスト                 ✅ … 17 ケース。既定値付き ini パスのみ注入
R4-8  QCoreApplication のプロセス毎 1 回化                 ✅ … 独自 main。R4-6 段階 2 の前提
R4-5  QML テスト（ToastStack / ActionCatalog / DragProxy） ✅ … 105 ケース。ToastStack のみ改造
R4-6  スレッドモデルの検証（段階 1 →判断→段階 2）         ✅ … 9 ケース。段階 2 は opt-in 限定
```

順序の要点:

- **R4-1 を最初に**: 以降の全項目が「何をテストする方針なのか」に依存する。ドキュメントとコメント
  だけなので空振りが無い。
- **R4-4 を 2 番目に、かつ R4-9 と同一セッションで**: `/W4` の維持を分けると、その間に入った
  `src/qml` の警告を誰も見ない窓ができる。R4-3 の障害解消も R4-5 の前提充足もここで起きるので、
  **後続 3 項目がこのセッションの成否に乗っている**。失敗したら R4-4 だけ切り戻して
  R4-2/R4-7/R4-8 を先に進める（いずれも R4-4 非依存）。
- **R4-2/R4-7/R4-8 は R4-4 と独立**。R4-4 が長引くなら並行に回してよい。
- **R4-8 → R4-6 の順は固定**。逆順だと段階 2 の失敗が「スレッドのせい」か
  「`QCoreApplication` が無いせい」か切り分けられない。
- **R4-6 だけが依然 R4 の可変部分**。R5 の着手を急ぐなら R4-6 を R5 の後ろへ送ってよい
  （R5 は C++ 構造の整理で、担保は C++ 側の 387 ケース。R4-6 が本当に効くのは
  R2-8 のキャンセル実装や Phase 16 の実時間反映を入れるとき）。

#### 成果物

- ✅ `docs/ARCHITECTURE.md` の `Design: testability and dependency injection` 節の「Testing」
  箇条書きを、R4-1 で決めたテスト方針の規約に書き換える。R1 の「サーバ由来文字列の信頼境界」、
  R2 の「スレッドモデル」、R3 の「エラー表現の規約」と同じ位置づけの、**単一の置き場**にする。
  最低限含める内容: (a) `src/qml` のうち何をテストし何をしないかの基準、(b) `src/mega` を
  テストしない理由（既存）、(c) R4-6 で決めた「スレッド起因の欠陥はテストで担保していない」という
  **既知の限界の明記**（これを書かないと、次に読む人が R2-19 を再発見することになる）。
- ✅ `tests/` 側のコメント 7 箇所の削除と `src/qml` ヘッダ **3** 箇所の訂正（R4-1。ヘッダは調査時の
  2 箇所に `ThumbnailController.h` を加えた 3 箇所）。
- ✅ `docs/ARCHITECTURE.md` の `Directory overview` に target 構成の変更を反映。4 target の表
  （何を持ち／なぜ別target か）を `CMake targets` 小節として足し、`MegaExplorerQml` には
  `MegaExplorerCore` と同じ形で「なぜ実行ファイルではなくライブラリがモジュールを持つのか」を
  書いた。併せて `OUTPUT_DIRECTORY` 明示とプラグイン 2 点セットの理由も、ビルド定義を触る前に
  読む場所として同節に置いた。`qml/` の項の「`appMegaExplorer` is itself the QML module's
  backing target」も訂正（ついでに、存在しない `qml/dialogs/` と削除済みの
  `DownloadSnackbar.qml` を実態に合わせた）。
- ✅ `CLAUDE.md` に `qmlcache_loader` の再 configure が要る条件を追記（「`QML_FILES` を
  増減したとき」に加えて **target を移したときも**。アグリゲータのシンボル名が target 名を
  含むため）。`docs/BUILD.md` 側には `/W4` の適用範囲と 2 つの抑止フラグの根拠を書いた。
- ✅ `CLAUDE.md` の Build 節の `/W4` の記述を R4-9 の結果に合わせて更新（4 target が対象、
  経路は `MegaExplorerWarnings`）。併せて「生成ソースの警告は**フル**リビルドでしか出ない」を
  注記（増分ビルドのクリーンさを無警告の証拠と読まないため）。
- ✅ `tests/AuthControllerTest.cpp`（R4-2、32 ケース）と `tests/CMakeLists.txt` の 1 行追加。
  製品側は `src/qml/AuthController.h`/`.cpp` の stall timeout 注入のみ（挙動不変）。
- ✅ `docs/ARCHITECTURE.md` の「規約は言うがテストがまだ無いクラス」一覧から `AuthController`
  を削除（残りは `DownloadController` / `MenuActions` の 2 つ、参照先も R4-3 のみ）。
  → R4-3 で `DownloadController` も削除し、残りは `MenuActions` 1 つ。「82 行で
  `MenuActionResolver`（47 ケース済み）に流すだけ」という R4 の評価もその場に書いた。
- ✅ `tests/DownloadControllerTest.cpp`（R4-3、18 ケース）と `tests/CMakeLists.txt` の 1 行追加。
  **製品側の変更はゼロ**。
- ✅ `tests/QSettingsPinnedFolderStoreTest.cpp`（R4-7、17 ケース）と `tests/CMakeLists.txt`
  への 2 行追加（テスト本体と `src/platform/QSettingsPinnedFolderStore.cpp`。`WindowsSessionStore`
  と同じ扱い）。冒頭のコメントも「`WindowsSessionStore` だけがオフラインでテストできる」から
  「`src/platform` の 2 アダプタとも」に書き換えた —— R4-7 の指摘の出どころそのもの。
  製品側は既定値付き ini パス引数の追加のみ（挙動不変）。併せて `docs/ARCHITECTURE.md` の
  `src/platform` の項（「テストが無い、`QSettings` が実レジストリに書くから」）を訂正し、
  Testing 規約の箇条書きに `src/platform` の行（**実アダプタをテストする／保存先はコンストラクタ
  注入**）を追加。
- ✅ `tests/qml/`（R4-5）— `CMakeLists.txt`（`MegaExplorerQmlTests` と ctest 3 エントリ）、
  `QmlTestMain.cpp`、`tst_ToastStack.qml` / `tst_ActionCatalog.qml` / `tst_DragProxy.qml`。
  ルート `CMakeLists.txt` は `find_package` の `QuickTest` 追加のみ、`tests/CMakeLists.txt` は
  `add_subdirectory(qml)` の 1 行のみ、`CMakePresets.json` の `targets` に 1 語追加。
  製品側は `qml/components/ToastStack.qml` の `describe*` 分離だけ（挙動不変）。
- ✅ `docs/ARCHITECTURE.md` の Testing 規約の `qml/` の行を書き換え（R4-5）。R4-4 完了で
  「`appMegaExplorer` に付いているので import できない」が事実として誤りになっていた箇所。
  併せて `MenuActions` 1 件だった「gap であって decision ではない」一覧を 3 件に拡張
  （`ActionCatalog` の 5 trigger、`ToastStack.push` のトリムを追加）、`CMake targets` 表に
  `MegaExplorerQmlTests` の行と「なぜ `MegaExplorerTests` と別バイナリか」を追記。
- ✅ `CLAUDE.md` の Build 節にテスト target 2 つと **`-o -,tap` が要る理由**を明記（R4-5）。
  同節のバイナリパス（`build/msvc-debug/tests/Debug/...`）が実態と違っていたのも併せて訂正。
  `/W4` の対象は `docs/BUILD.md` ともども 4 → **5 target**。
- ✅ R4-4 で前提が消えた陳腐化コメントを 2 箇所訂正（R4-3 と同一コミット）:
  `src/core/DownloadService.h` の `safeLocalFileName` の配置理由（「`DownloadController` は
  テストターゲットに入っていないから」→「Qt 非依存の文字列規則だから `src/core`」）と、
  `tests/UploadControllerTest.cpp` 冒頭の「`DownloadController` と違ってこちらは入っている」
  （削除）。R4-1 が意図的に残した箇所だが、`Qt6::Gui` が入った時点で事実として誤りになった。

### R5 — 調査済み / R5-2・R5-3・R5-5・R5-6 対応済み（調査 2026-08-07）

計画の「種」5 件 +「持ち越し」の [R5] 3 件 + R2/R3 が明示的に R5 送りにした 5 件を現物で検証した
結果。**確認 10 件 / 種の誤り・判断が要るもの 4 件 / 問題なしと確認 3 件**。R1〜R4 と同じく、
以下はそのまま plan mode の作業単位として使える粒度で書いてある。**調査セッションではコードを
一切変更していない**。

#### 着手前に決めた事項（2026-08-07）

各項目の前提を動かすので、項目を単独で読まないこと。

| 決定 | 内容 | 影響する項目 |
| --- | --- | --- |
| **分割は QML API 面まで通す** | `TabContext`/`TabsController` が新コントローラを別プロパティで公開し、QML を `tab.mutations.renameEntry(...)` 等に書き換える。`Q_INVOKABLE` を実数で 19 → 8 + 11 にする。C++ 内部だけ割って薄い委譲を残す案は**採らない** — 行数は減っても「1 クラスが QML API 面の 3 割」という当の負債が残るため | R5-1（+ R5-2 の数合わせ） |
| **busy は委譲で隠す** | 独立クラスに切り出すが QML への露出は `navigation.busy` 1 プロパティのまま。`TabsController.cpp:59` は無変更 | R5-3, R5-4 |
| **ミューテーションは 1 クラスに留める** | 単発（rename/createFolder）と bulk fan-out を別クラスに割る 3 分割案は採らない。クラス 4 つは `tabFactory` のキャプチャを 7 前後まで押し上げ、R5 の最後の再点検項目を自分で悪化させる | R5-1, R5-4 |
| **`NodeRef` は `std::string`** | `src/core` の既存慣例（`FileEntry`/`NodeInfo`/`PathSegment`）に揃え、`src/core` の Qt 非依存を保つ。`QString`→`std::string` 変換は `toEntries()` の中に閉じるので実質 1 箇所 | R5-5 |
| **R5-9 は契約の明文化で閉じる（製品コード変更ゼロ）** | `DownloadController`/`UploadController` は**アプリ寿命 1 個**が設計であって、R5-1 の分割対象は `FolderNavigationController` のみ。誤った安全論証（null チェック）を実際の根拠（`main.cpp:192` の `shutdown()` が SDK スレッドを join してからスタック破棄が走る）に置き換え、`ThreadedDeliveryTest` に 1 ケース足して固定する。**「この 2 つをタブ単位化するなら `weak_ptr` 化が要る」を契約本文に書く**のが要点 | R5-9 |
| **R5-10 は固定文に倒す** | R3-4 が `openFile` でやったのと同じ処理。`ToastStack.qml:82` を `Couldn't download %1` に、`AuthController.cpp:135/182` の `UnknownError` 時の生文字列を `QString()` に。生の英文と `errorCode` は `qCWarning` に残す。`ErrorReason` を `DownloadJob` に通す案は R5-8 と同一セッションを強要して分量が R3-4 相当になるため見送り | R5-10 |

#### 前提: 「最大の負債」の実体は行数ではなく、1 クラスに同居した 3 つの状態機械

節 0 の表は 2026-08-06 のもので、R3/R4 の修正後は下記が正しい。R5 の判断はこちらに乗る。

| ファイル | 節 0 | 現在 | 差の理由 |
| --- | --- | --- | --- |
| `src/mega/MegaSdkClient.cpp` | 905 | **1,106** | R3-1 の `errorCode` 付与が全メソッドに入った |
| `src/qml/FolderNavigationController.{cpp,h}` | 822 + 384 | **832 + 396** | ほぼ横ばい |
| `src/qml/FileListModel.cpp` | 472 | 472 | 変化なし |
| `main.cpp` | — | 194 | — |

`FolderNavigationController.cpp` 832 行を責務で切ると、**ミューテーション群だけで 398 行（48%）**:

| 責務 | 行範囲 | 行数 |
| --- | --- | --- |
| ナビゲーション（back スタック / breadcrumb / reset） | 51-65, 96-236, 271-280, 813-832 | 191 |
| busy カウンタ + 遅延タイマ | 38-48, 66-95 | 41 |
| 検索 | 237-270 | 34 |
| ソート | 281-344 | 64 |
| **ミューテーション（rename / rubbish / createFolder / move / paste / copy）** | **345-742** | **398** |
| refresh | 743-784 | 42 |
| bulk 会計 | 785-812 | 28 |

この 3 つ（ナビゲーション状態 / ミューテーション実行 / busy 表示）が**別々の状態機械**で、
互いに触るのは「ミューテーションが終わったら refresh する」「ミューテーション中は busy」の
2 本だけ。種の「少なくとも 3 つに割れる」は行数の上でも裏付けが取れた。

#### 確認された問題

**R5-1 [高] `Q_INVOKABLE` は 18 ではなく 19。次点の `FileListModel` の 13 を大きく引き離す**

- `FolderNavigationController.h` の `Q_INVOKABLE` は grep で 23 ヒットするが、うち 4 件
  （:23, :39, :95, :119）はコメント本文。実宣言は **19 個**。
- 全 `src/qml` の分布: 19 / 13（`FileListModel`）/ 7（`QuickAccessModel`）/ 6 / 6 / 5 / …。
  **1 クラスが QML API 面の 3 割を持っている**。
- 分割の切れ目は上表の「ミューテーション 398 行」。ここを別クラス（`FileMutationController` 等）に
  移すと 19 → 8（navigation 7 + refresh 2 のうち QML 呼び出しのあるもの）まで落ちる。
  QML 側の呼び出し箇所は移動対象 11 個で計 14 箇所しかないので、置換のコストは小さい。

**R5-2 [低] ✅対応済み `loadRoot()` のコメントは「Not Q_INVOKABLE」と書いているのに `Q_INVOKABLE` が付いている**

- `FolderNavigationController.h:119-121`。「main.cpp の合成ルートから 1 回呼ばれるだけで QML からは
  呼ばれない」という説明自体は正しく、**`qml/` 全体で `loadRoot` の呼び出しは 0 件**（19 個の
  `Q_INVOKABLE` でこれだけ）。宣言のほうが事実に追いついていない。
- R5-1 の分割前に外しておくと、QML API 面の実数が 18 になり種の数字と一致する。単独では 1 行。
- **対応（2026-08-07）**: `Q_INVOKABLE` を外し、`Q_INVOKABLE` の実数を **19 → 18** にした。
  併せて分かったこと:
  - **コメントは二重に腐っていた**。「`main.cpp` の合成ルートから 1 回呼ばれる」は Phase 22 の
    タブ化以前の記述で、現在の呼び出し元は `TabsController` の 3 経路
    （`loadRootAll` `:210` / `addTab` `:111` / `addTabAt` `:125`）。`main.cpp` は
    `loadRoot` を直接は呼ばない。QML 側の入口は `Main.qml:1027` の
    `tabsController.loadRootAll()` で、`FolderNavigationController::loadRoot` は
    **タブ単位の内部エントリポイント**に降格している。この事実に合わせて本文も差し替えた
    （「QML は `loadRootAll()` 経由で届く」）。**「main.cpp から呼ばれる」を残したまま
    `Q_INVOKABLE` だけ外すと、R5-1 の分割時に呼び出し元を探す人を誤誘導する**。
  - `AuthController.h:116-117` の `restoreSession` が「same convention as
    FolderNavigationController::loadRoot」と参照しているが、こちらは実際に `main.cpp` から
    `app.exec()` 前に 1 回呼ばれる素の非 `Q_INVOKABLE` メソッドで、記述も正しい。参照先が
    「QML から呼ばれない public メソッド」という点は変わらないので**無変更**。
  - ビルド警告なし、`ctest` 467 件全通過。テストは `loadRoot()` を C++ から直接呼んでいるだけ
    （`FolderNavigationControllerTest` の 43 箇所）なので、`Q_INVOKABLE` の有無に依存しない。

**R5-3 [高] ✅対応済み busy 機構は「begin/end の 2 箇所だけ」と書いてあるが、`reset()` が 3 番目の書き手**

- ヘッダ :258-262 が「`mBusyCount` が変わってよいのはこの 2 つだけ」と明言。実際には
  `reset()`（`.cpp:824-827`）が `mBusyCount = 0` を直接代入し、タイマを止め、`busyChanged` を
  自前で出している。**begin/end のペア規約の外側にある唯一の経路**で、しかもここだけ
  「カウントを 0 に落とす」という不変条件違反を意図的にやっている。
- 独立クラス化のコストは低い: 外に出ている API は `busy` プロパティ 1 個 + `busyChanged` だけで、
  `TabsController.cpp:59` が `navigation->busy()` をアップロード分と OR しているのが唯一の読者。
  内部の呼び出しは begin/end の **8 対**（:347/353, :379/389, :434/439, :474/480, :577/584,
  :622/631, :687/694, :755/758）。
- **R5-4 の前に切ること**。逆順だと bulk runner が busy を触るために親への逆参照を持つ。
- **対応（2026-08-07）**: `src/qml/BusyState.{h,cpp}` を新設（`QObject` + `QTimer`、`QML_ELEMENT`
  なし → `add_library(MegaExplorerQml)` 側）。`visible()` / `begin()` / `end()` / `abandonAll()` +
  `changed()` の 5 面のみ。`FolderNavigationController` は値メンバ `BusyState mBusy` を持ち、
  ctor で `changed` → `busyChanged` を signal-to-signal で中継するだけになった。`Q_PROPERTY` /
  `busy()` / `busyChanged` は無変更、`TabsController` と `qml/` は 1 行も触っていない。
  併せて決めたこと・分かったこと:
  - **3 番目の書き手は「消す」のではなく `abandonAll()` として正式化した**。ログアウト時に
    カウントを 0 に落とすのは意図的な不変条件違反であって、バグではない。`end()` の 0 クランプは
    **`abandonAll()` が存在するからこそ要る**（放置された in-flight のコールバックがそのまま
    `end()` に届く）ので、両者は 1 クラスに閉じて初めて対で読める。**規約違反を潰す方向に倒すと
    クランプの理由が説明できなくなる**のが、そうしなかった理由。
  - **所有は値メンバ。`shared_ptr` にはしなかった**。`BusyState` は `QTimer` を持つので、
    `shared_ptr` を SDK コールバックに捕獲させると `~QTimer` が SDK スレッドで走る R2-5 の
    ハザードを新クラスにも背負わせることになり、`makeGuiOwned` 相当の生成規約が今から要る。
    値メンバなら「コントローラと一緒に GUI スレッドで死ぬ」という現行の寿命規約がそのまま効く。
    **昇格条件**: R5-1 で navigation / mutation の 2 コントローラが同じ busy を共有する形に
    なったら、その時点で `shared_ptr` + `makeGuiOwned` にして `tabFactory` から注入する。
    R5-4 の `BulkOperationRunner` は同じコントローラが所有するので、参照 1 本で足りるはず。
  - **薄いラッパ (`beginBusyOperation`/`endBusyOperation`) は残さなかった**。8 対 16 箇所を
    `mBusy.begin()` / `mBusy.end()` に直したのは、R5-4 が**オブジェクトそのもの**を受け取れる
    ことが切り出しの目的だから。委譲で隠すのは QML 面（`navigation.busy`）だけで、C++ 内部まで
    隠すと R5-4 が結局 `this` 経由に戻る。
  - **`reset()` の emit 順が変わったが無害**。以前は `canGoBackChanged` → `breadcrumbChanged` →
    `busyChanged` の順だったのが、`abandonAll()` が先に `busyChanged` を出すようになった。
    `mBusy.abandonAll()` を呼ぶ時点で他の状態は全て更新済みなので、どの受け手も一貫した状態を
    見る。**重要なのは順序ではなく「emit 前に全フィールドが確定していること」**で、それは元の
    実装が `wasBusy` を先に控えていた理由でもある。
  - `FolderNavigationController.cpp` の匿名 namespace は `kBusyIndicatorDelayMs` 1 個だけだった
    ので、定数の移動と同時に**ブロックごと消えた**（R5-7 が触ろうとしている匿名 namespace の
    1 つが先に消えた形）。
  - **テストは「出ない側」を初めて固定した**。既存の 3 件（`BusyClearsOnlyAfterTheLastCallback…`
    ほか）は全て「スピナが出た後どう消えるか」しか見ておらず、**遅延内に終わった操作が
    スピナを出さない**という遅延の存在理由そのものは無検証だった。`tests/BusyStateTest.cpp` の
    `EndBeforeTheDelayNeverShows` がそれ。既存 3 件は無変更で通っており、それが委譲で挙動が
    変わっていないことの証拠になっている。`ctest` 472 件（467 + 新規 5）全通過、警告なし。

**R5-4 [高] `BulkOperationBatch` の切り出しは成立するが、busy と絡んでいるぶん単独では出せない**

- `BulkOperationBatch` を作るのは 3 箇所（`:428` moveToRubbish / `:464` moveHandlesFrom /
  `:665` startCopyBatch）で、`accountForBulkOutcome` の呼び出しも同じ 3 経路。種の
  「移動/コピー/ゴミ箱の 3 経路が共有している」は正しい。
- ただし 3 経路とも fan-out ループの中で `beginBusyOperation()`／コールバック先頭で
  `endBusyOperation()` を挟む。`BulkOperationRunner` に出すなら busy をコールバックとして
  注入する形になり、それが自然にできるのは R5-3 で busy が独立オブジェクトになった後。
- `refresh` / `onComplete` の 2 つの `std::function` フックは既にあるので、Runner の
  インタフェースはほぼ現状の `BulkOperationBatch` そのままでよい。**`QuickAccessModel::Sweep` と
  同型**（ヘッダが自分でそう書いている）なので、統合できるかは切り出し後に判断する。

**R5-5 [中] ✅対応済み `ClipboardController.h` の実インクルードは種のとおり。`Entry` を `src/core` に降ろせば消える**

- `FolderNavigationController.h:6-9` が自ら理由を書いている。使用箇所は `.cpp` 6 + `.h` 2。
- `Entry` は `{quint64 handle, QString name, bool isFolder}` の純値型で `QObject` 非依存。
  `src/core/NodeRef.h` 相当へ降ろせば `ClipboardController` は前方宣言に戻せる。
- **唯一の判断点**: `static std::vector<Entry> toEntries(const QVariantList&)` は `QVariant` 依存
  なので `src/core` には置けない。(a) 変換だけ `ClipboardController` に残す、(b) `src/qml` の
  自由関数にする、のどちらか。`QString` を `src/core` に持ち込むこと自体は既に
  `FileEntry`/`PathSegment` が前例……を確認したが、実際は `FileEntry` は `std::string` なので
  **`NodeRef` も `std::string` にするなら QML 境界での変換が 1 段増える**。
  → **決定: `std::string` + 変換は `toEntries()` に残す**（上の決定表）。
- **対応（2026-08-07）**: `src/core/NodeRef.h`（`{std::string name, std::uint64_t handle,
  bool isFolder}`）を新設し、`ClipboardController::Entry` を廃止。`FolderNavigationController.h` の
  実インクルードは前方宣言 + `core/NodeRef.h` になった。変換は `ClipboardController` に静的関数の
  まま残したが、指す型が消えたので **`toEntries` → `toNodeRefs`** に改名（呼び出しは 2 箇所）。
  併せて分かったこと:
  - **`NodeInfo` が `{name, handle, isFolder, inCloud}` で `NodeRef` の上位互換に見えるが、統合
    しなかった**。`NodeInfo` は `IMegaClient::getNodeInfo` が**ハンドルから今解決した実体**で、
    `inCloud`（ゴミ箱/Vault の判別）を持つのがその存在理由。`NodeRef` は**選択された時点の
    スナップショット**で、解決も検証もしない — ペースト時に古い名前を使うのは既知かつ許容、と
    `ClipboardController.h` が元から書いている性質。同じフィールドでも保証が違うので、
    その区別を `NodeRef.h` のコメントに明記した。**フィールドが一致しているという理由だけで
    後から寄せると、`inCloud` の意味が「未検証だから常に false」に化ける**。
  - **`quint64` への明示キャストが 2 箇所必要になった** — `ClipboardController::cutHandles()` と
    `FolderNavigationController::paste()` の cut 分岐、どちらも `QVariant::fromValue(handle)` で
    QML に返す経路。MSVC では `std::uint64_t` == `quint64` なので今は無害だが、`std::uint64_t` が
    `unsigned long` になる環境では metatype が `ULongLong` → `ULong` に変わる。QML 側は
    `cutHandles.indexOf(cell.handle)`（`FileGridView.qml:573` / `FileTableView.qml:860`）で
    JS の厳密比較に晒しているので、**ゴーストが黙って効かなくなる**類の壊れ方をする。
    `CROSS_PLATFORM_INVESTIGATION.md` の射程に入る話なので、キャストの理由をコード側に書いた。
  - **決定表の「変換は実質 1 箇所」は当たっていた**が、消えたのは
    `FolderNavigationController.cpp:680` の `entry.name.toStdString()` **1 行だけ**で、代わりに
    上のキャスト 2 箇所が増えている。正味の行数はほぼ横ばいで、得たのはヘッダ依存 1 本と
    `src/qml` から `src/core` への値型の移動そのもの。**行数を期待して読むと成果が無いように
    見える項目**（R5-1 の分割面を単純にするのが目的）。
  - ついでに冗長化した `static_cast<std::uint64_t>(entry.handle)` を 2 箇所（`canCopy` / `copy`）
    外した。`currentHandle()`/`target` 側は `quint64` のままなのでキャストは残る。
  - ビルド警告なし、`ctest` 467 件全通過。テスト修正は `ClipboardControllerTest.cpp:60` の
    `QStringLiteral("b")` → `"b"` の 1 行のみ。`FolderNavigationControllerTest` /
    `TabsControllerTest` は `QVariantList` 越しにしか触っていないので無変更。

**R5-6 [中] ✅対応済み `IMegaClient` の同期例外は確かに 7 個。位置依存の規約も現存するが、解消はファイル分割ではない**

- `Result<T>` を直接返す（＝同期シグネチャ）のは 7 個: `currentSessionToken` / `currentUserHandle` /
  `checkMove` / `checkUpload` / `findChildFiles` / `hasSubfolders` / `currentAccountIdentity`。
- 序数がコメントに埋まっている（`:348`「the sixth exception here」、`:366`「the seventh」）うえ、
  `:355-361` が「上に挿すと既存 4 つの doc コメントを振り直すことになるので末尾に足した」と
  **規約が並び順を人質に取っていることを自白している**。
- ただし解消は簡単で、**R2-2 が冒頭に入れた 3 モードの表（`:17-42`）が既に「どれが同期か」を
  1 箇所で列挙している**。序数を消して「同期例外は冒頭の表を見よ」に寄せるだけでよく、型や
  ファイルを割る必要はない。種の「型か命名で分離」は過大。
- **混同注意**: 「モード 2（コールバック形だが常に同期）」の 5 メソッド（`getRootChildren` /
  `getChildren` / `search` / `getPath` / `getNodeInfo`）は別軸。これを足して数えると 7 でなくなる。
  `FolderNavigationService` のロックフリー設計が寄りかかっているのは**モード 2 のほう**。
- **対応（2026-08-07）**: 序数を全廃し、7 個を冒頭で 1 回だけ列挙する形にした。ただし
  **調査の想定どおりには進まなかった**:
  - **「冒頭の表が既にどれが同期かを列挙している」は誤り**。R2-2 の表は 1 行目が
    「How the **callback-shaped** methods below actually deliver」で、コールバック形だけを
    対象にしている。`Result<T>` を直接返す 7 個は表に**一度も出てこない**。序数を消して
    「冒頭の表を見よ」に寄せるだけだと、**その方法自体が存在しない参照先を指す**。
  - なので冒頭を「shape A（直接返し・7 個を実名で列挙）/ shape B（コールバック形・従来の
    3 モード）」の 2 段に組み替え、shape A の列挙をこのファイル唯一の一覧にした。**8 個目が
    増えたときの編集はこの列挙 1 箇所**、というのが序数規約の置き換えになっている。
  - **モード 1/2/3 の番号はそのまま維持**。`DownloadService.h:48`/`:122` と
    `FolderNavigationService.h:20` が「delivery mode N」と番号で参照しており、shape A/B を
    足したついでに振り直すと 3 ファイルが道連れになる。番号の付け替えは利得ゼロ。
  - 調査の「混同注意」（モード 2 の 5 個を足して数えるな）を**コード側に明文化**した。
    合算すると 12 になり、それがこの 2 段組みが防ごうとしている間違いだと書いてある。
  - 消したのは序数だけで、**「checkUpload と同じ理由」式の理由参照は残した**。位置に依存せず、
    かつ列挙表には書けない情報（なぜ同期でなければならないか）を運んでいるため。
    `currentSessionToken` の「Synchronous, **unlike every other method here**」も、序数ではないが
    同種の嘘（他に 6 個ある）なので同時に落とした。
  - `--- Account-level reads ---` の「上に挿すと 4 つの doc コメントを振り直すことになる」という
    自白も削除。位置が自由になったので、グループとしてまとまっている以上の理由はもう要らない。
- **R2-22（`AccountIdentity` の裂けた値）はここで決着 — スナップショット化しない**。
  `currentAccountIdentity` の唯一の消費者は `AccountController::loadProfile`（`:73-86`）で、
  用途は email / avatarColor / イニシャルの**表示のみ**。裂けるのは logout が読みの途中に入った
  ときで、その logout 自身が同じアカウントパネルを畳むので、最悪でも 1 フレーム古いイニシャルが
  出るだけ。**判断材料にする呼び出し側が現れたらスナップショット版が要る**、という反転条件のほうを
  doc コメントに書いた（実害が出るのはそのときで、それまでは検証する対象がない）。
- **R2-9（SDK の `sdkMutex` によるハング）には触っていない** — 調査で「R5 では触らないことを
  明示的に決める」とした項目。同期であること自体がインタフェースの定義なので、shape A の
  列挙はハングの可能性を減らしも増やしもしない。
- 変更は `src/core/IMegaClient.h` のコメントのみ（製品コード変更ゼロ）。`appMegaExplorer` ビルド
  警告なし。コメント専用の変更なので `ctest` は回していない。

**R5-7 [低] `MegaSdkClient.cpp` の匿名 namespace は 355 行だが、切り出しには internal linkage の放棄が要る**

- `:65-419` が匿名 namespace（1,106 行の **32%**）。内訳はリスナ 7 クラス（`SimpleResultListener`
  `:96` / `FetchNodesListener` `:128` / `AttributeFileListener` `:172` / `TextResultListener` `:200` /
  `AccountDetailsListener` `:227` / `DownloadListener` `:323` / `UploadListener` `:379`）と
  変換ヘルパ 4 個（`toMegaUserAttribute` / `toMegaOrder` / `nodeToEntry` / `nodeListToEntries`）。
- リスナを別ファイルに出すと本体は約 750 行になる。ただし**匿名 namespace のままでは出せない** —
  名前付き namespace（`megaexplorer::sdk::detail` 等）に変えることになり、internal linkage を
  失う。得られるのは行数だけで、境界が増えるわけではない。**R5-1〜R5-4 より優先度は下**。

**R5-8 [中] 完了/進捗コールバックがジョブ id を照合していない（持ち越し + R2-8 の合流先）**

- 現物確認: 進捗・完了とも `if (mQueue.empty()) return;` の後に無条件で `mQueue.front()` へ書く。
  `DownloadService.cpp:185-198`（progress）/ `:199-222`（finish）、`UploadService.cpp:147-151` /
  `:162-176`。**`UploadService` には `checkUpload` 失敗を書く 3 つ目の front() 経路もある**
  （`:121-129`）。
- `enqueue()` は既に id を返す（`DownloadService.h:82` / `UploadService.h:71`）が、コールバックには
  渡っていない。R2-8 が挙げた「今日成立している 3 つの偶然」は現在も全て成立（`cancel(jobId)` 未実装）。
- 方針は R2-8 の結論をそのまま採る: `std::optional<Job> mActive` ＋待ち行列の分離、加えて
  コールバックへの id 引き渡し。`ThumbnailService` がハンドル keyed で構造的に免疫という良い前例あり。
- **R4-6 があえてテストで固定しなかった箇所**なので、先にテストを書くと書き直しになる。修正と
  同時にテストを入れる。

**R5-9 [中] `~DownloadController` / `~UploadController` の安全論証が事実と違う（持ち越し）**

- `DownloadController.cpp:58-64` は「サービスがロック下でコピーし null チェックしてから呼ぶので
  mid-flight でも安全」と書くが、`DownloadService.cpp:185-197` は**ロック下でコピー → ロック解放
  → null チェック → 呼び出し**。null チェックが見ているのはコピー**後**の値なので、コピーと
  デストラクタの間の窓は塞げていない。`UploadController.cpp:85-91` も同文で同じ穴。
- 現状到達不能なのは `main.cpp:192` の `client->shutdown()`（SDK スレッド join 済み）が両
  コントローラのスタック破棄より前にあるため。**R5-1 の分割でコントローラをタブ単位化した瞬間に踏む**。
- 選択肢は (a) コピーも呼び出しもロック下（サービス→コントローラ→サービスの再入で
  デッドロックしうる）、(b) observer を `weak_ptr` 化、(c)「`shutdown()` より前に破棄されない」を
  明示的な契約として書き、`ThreadedDeliveryTest` で固定する。
  → **決定: (c)**（2026-08-07）。この 2 つはアプリ寿命 1 個が設計で、R5-1 の分割対象は
  `FolderNavigationController` のみ。誤った論証（null チェック）を実際の根拠（`shutdown()` の
  停止点）に差し替え、**「タブ単位化するなら (b) が要る」を契約本文に書く**。製品コード変更ゼロ。

**R5-10 [低] 生 `errorMessage` の UI 露出が 2 経路残っている（持ち越し 2 件の統合）**

- (a) `qml/components/ToastStack.qml:82` `describeDownload` の `%2`。`DownloadJob`（R3-11 の
  「3 つ目のエラー表現」）経由なので、R3-4 が作った `NotificationController::ErrorReason` に
  乗っていない。
- (b) `AuthController.cpp:135` / `:182` の `kind == UnknownError ? errorMessage : QString()`。
  R3-1 で `kEInternal(-1)` が「分類できない失敗」の既定になった結果、`src/platform` と
  `MegaSdkClient` の**該当 8 箇所が全部この経路に落ちる**。
- 同じ族だが入口が違う（前者は `DownloadJob`、後者は `AuthErrorKind`）。`ErrorReason` を
  `DownloadJob` 側にも通すのが自然な畳み方。
- 変更すると `tests/AuthControllerTest.cpp` の `LoginErrorsMapToErrorKinds` の `kEInternal` 行
  （R4-2 が現状挙動をそのまま固定した）も一緒に直す必要がある。

#### 種が不正確だった / 判断が要るもの

- **`main.cpp` の `tabFactory` は現時点で問題ではない** — キャプチャは種のとおり 5 個
  （`client` / `thumbnailService` / `fileOperationService` / `&notifications` / `&clipboard`）だが、
  本体は 16 行（`:143-158`）、合成ルート全体でも 194 行。R5-1 でコントローラを割れば
  ここのキャプチャは**増える**（新コントローラの分）ので、**R5 の最後に再点検する項目**であって
  着手対象ではない。なお `engine.rootContext()->setContextProperty` が 9 個並んでおり、
  分割後に 10 個を超えるようならそちらのほうが先に問題になる。
- **`FolderNavigationController` のコンストラクタが public のまま（R2-14 が R5 送りにした件）** —
  正しい生成経路は `makeGuiOwned` だけだが、実際の生成箇所は `main.cpp:150` と
  `tests/FolderNavigationControllerTest.cpp:73` / `tests/TabsControllerTest.cpp:49` の 3 つで
  **全て `makeGuiOwned` を通っている**。`static create()` 化は不変条件を型で強制するが、
  違反者はゼロなので**利得は将来分だけ**。R5-1 で新クラスを足すときに同じ形を要求するかを
  含めて判断する。
- **`AccountIdentity` の裂けた値（R2-22）** — `MegaSdkClient::currentAccountIdentity`
  （`:1013-1040`）が 2 つの別ロック下の読みを合成する件。実害のある呼び出し側は R2 調査でも
  今回も見つからず、`AccountIdentity` を単位として検証するかは未決。**R5-6 で同じメソッドの
  doc コメントを触るので、そのついでに結論を出す**のが安い。
- **SDK の `sdkMutex` によるハング（R2-9）** — `docs/ARCHITECTURE.md:209-229` に恒久記録済み。
  「その場で答える」がインタフェースの定義なので R5-6 の同期例外整理でも解消しない。R5 では
  **触らないことを明示的に決める**項目。

#### 問題なしと確認できた種

- **`FolderNavigationService` / `QuickAccessService` の mutex 無しの根拠は既に自ファイルにある** —
  持ち越し節の [R5] 項目は **R2-2 で解消済み**だった。`FolderNavigationService.h:18-23` が
  「`DownloadService`/`UploadService`/… と違って mutex を持たない、`IMegaClient` のモード 2 が
  同期だから」と自分で書いており、`QuickAccessService.h:19-25` も同様。**持ち越し節から落としてよい**。
- **`FileListModel`（472 行）は割る必要がない** — 種は「選択モデル + バンド選択セッション + ロール」と
  3 責務のように書くが、バンド選択は 5 メソッド（`beginBandSelection` / `updateBandSelection` /
  `updateBandSelectionGrid` / `endBandSelection` / `cancelBandSelection`）+ `applyBandSelection` で、
  状態は `mBand*` の 4 フィールドに閉じている。`Q_INVOKABLE` 13 個は 2 番目に多いが、全て
  「1 つのリストの選択」という単一の話題。**R5 の対象から外す**。
- **`FolderNavigationController` の非同期経路の `shared_from_this()` 捕獲** — R2-14 で確認済みの
  17 箇所は今回も全て健全。R5-1 の分割で新クラスへ移す際に**この形ごと移すこと**が制約になる
  （新クラスも `enable_shared_from_this` + `makeGuiOwned` が必要）。問題ではないが、分割設計の
  入力として記録する。

#### 推奨実施順（各項目 1 セッション）

```
R5-2  loadRoot の Q_INVOKABLE 除去              … ✅済。1 行。R5-1 の前に数字を合わせた
  ↓
R5-6  IMegaClient の同期例外を位置非依存に      … ✅済。doc コメントのみ。R2-22 もここで決着
  ↓
R5-5  ClipboardController::Entry を src/core へ … ✅済。NodeRef.h 新設、実インクルードが消えた
  ↓
R5-3  busy 機構を独立クラスへ                   … ✅済。BusyState 新設。3 番目の書き手は abandonAll() に
  ↓
R5-4  BulkOperationRunner を切り出し            … R5-3 の後でないと親への逆参照が要る
  ↓
R5-1  ミューテーション群を別コントローラへ      … 本丸。ここまでで 398 行の依存が整理済み
  ↓
R5-9  観測者解除の競合                          ★製品挙動の変更を含む。R5-1 の直後（踏む直前）
  ↓
R5-8  ジョブ id 照合 + optional<Job> mActive    … R2-8 の合流先。テストは修正と同時
  ↓
R5-10 生 errorMessage の 2 経路                 … ErrorReason を DownloadJob へ。R4-2 のテストも直す
  ↓
R5-7  MegaSdkClient のリスナ切り出し            … 行数のみの利得。時間が余ったら
```

**★ の 2 件は 2026-08-07 に方針決定済み**（上の決定表）。R5-9 は製品コード変更ゼロの契約明文化に
落ちたので ★ は実質 R5-10 の文言 1 件のみ。R5-1 の分割の形も同表で確定しており、実施手順の
ステップ 3 で改めて止まる必要があるのは**各項目の具体的な diff の形**だけ。

未決のまま残しているのは、いずれも「実施中に判断すればよく、前もって決めても情報が増えない」もの:

- **R5-7 をやるか**（匿名 namespace → 名前付き namespace）。internal linkage を捨てて得るのが
  行数だけなので、**推奨は見送り**。R5-1〜R5-6 を終えて時間が余ったときに再考する。
- **`static create()` 化**（R2-14 の R5 送り分）。違反者ゼロなので利得は将来分だけ。R5-1 で
  新コントローラを足すとき、そちらに同じ形を要求するかと合わせて判断する。
- ~~**`AccountIdentity` を単位として検証するか**（R2-22）~~ → **R5-6 で決定: しない**。
  裂けても表示 1 フレーム分にしかならないため。反転条件は R5-6 の対応欄。

### R6 — 未着手
### R7 — 未着手

## 5. 持ち越し（スコープ外で見つかった事項）

（担当スコープが来るまでここに置く）

- **[R3] SDK の英語 `errorMessage` がそのまま UI に出ている** — `qml/components/ToastStack.qml:85`
  の `qsTr("Failed to download %1: %2").arg(fileName).arg(errorMessage)`。`errorMessage` は
  `Result::fail()` の生文字列。`AuthController` は `classifyError` で構造化してから渡しているのに
  （`src/qml/AuthController.cpp:155-159`）、ダウンロードだけ生のまま。R3 の「`fail()` の
  `errorMessage` がそのまま UI に出る経路がないか」の答えの 1 つ。（R1 調査中に発見）
  → **R3 調査で回収済み（2026-08-06）**。同型が `showError` にも 8 文脈あり、R3-4 に統合した。
  → **`showError` 側は R3-4 で解消、この `showDownload:85` 自体は R5 へ（2026-08-07）**。`%1` の
  `fileName` は意味が通り、問題は `%2` の `errorMessage` だけ。ただしダウンロードは `Result` ではなく
  `DownloadJob`（R3-11 の「3 つ目のエラー表現」）に乗っているので、`notifyError` の enum 化とは
  別の入口が要る。R3-11 が既に R5 のサービス整理に合流させると決めており、そこで一緒に畳む。
  → **R5 調査で回収（2026-08-07）、R5-10 に統合**。下の `kEInternal` の件と同族なので 1 項目にした。
- **[R4] `DownloadController` にテストが無い** — `tests/UploadControllerTest.cpp:19` が理由を
  「`QDesktopServices` が QtGui を引く」と説明している。R1-1 の修正で `computeDestinationPath` に
  検証ロジックが入るなら、テスト可能な形（`src/core` の純関数）に出すのが望ましい。（R1 調査中に発見）
  → **R4 調査で回収（2026-08-07）、R4-3 に統合**。後半の要望は R1-1 が既に果たしていた
  （`DownloadService::safeLocalFileName` として `src/core` に出され、19 アサーションで検証済み）。
  残る未テスト論理は `computeDestinationPath` ではなく重複抑止と `downloadFinished` の
  フィールド構成の側。`QtGui` の件は「テストターゲットに `Qt6::Gui` を足す」で閉じる方針。
  → **R4-3 で解消（2026-08-07）**。`tests/DownloadControllerTest.cpp` の 18 ケース。
  `QtGui` は R4-4 が `MegaExplorerQml` 経由で解決済みだったので、製品側の変更はゼロ。
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
  → **R4 調査で回収（2026-08-07）、R4-6 に統合**。3 段階に分解し、段階 3（サニタイザ）は
  **見送りを推奨**する結論になった — MSVC/clang-cl には Windows 版 ThreadSanitizer が無く、
  本命のデータ競合検出器がこの toolchain では使えないため。ASan で取れる UAF までに限る。
  段階 2 の前提として R4-8（`QCoreApplication` のプロセス毎 1 回化）が要る → **R4-8 は
  2026-08-07 に完了**（`tests/TestMain.cpp`）。段階 2 は前提を気にせず着手してよい。
  → **R4-6 で決着（2026-08-07）**。段階 1 の残ギャップ 4 ケースと、段階 2 の
  `tests/ThreadedDeliveryTest.cpp` 5 ケース（`tests/WorkerDelivery.h` 経由の opt-in）。
  **クロススレッドの配達経路は担保されたが、データ競合そのものは依然として担保外** — 3 サービスの
  mutex は消してもテストが通る。TSan がこの toolchain に無いのが理由で、これは R4-6 の
  対応ログと `docs/ARCHITECTURE.md` の「Threads in the suite」に既知の限界として明記した。
- **[未割当] `kEInternal(-1)` がログイン画面に SDK の生英文を出す** — `classifyError`
  （`src/qml/AuthController.cpp`）は `kENoEnt`/`kEBlocked`/`kETooMany`/`kEAgain` の 4 値しか
  畳まず、`default:` は `UnknownError` ＋ `rawErrorMessage` の素通しになる。R3-1 で
  「分類できない失敗には `kEInternal = -1` を入れる」と決めたので、**その全部がこの経路に
  落ちる**。`LoginView.qml` の `describeError()` は `UnknownError` のときだけ生文字列を出すので、
  日本語 UI に英語が 1 文だけ混ざる。R4-2 のテストは**現状の挙動をそのまま固定してある**ので、
  変えるときはそのケース（`LoginErrorsMapToErrorKinds` の `kEInternal` 行）も一緒に直す。
  上の `[R3] ToastStack.qml:85` と同じ「生 `errorMessage` の露出」族なので、R5 でまとめて
  畳むのが自然。（R4-2 実施中に確認、2026-08-07）
  → **R5 調査で回収（2026-08-07）、R5-10 に統合**。`kEInternal` を返す箇所は現時点で
  `src/platform` + `MegaSdkClient` の 8 箇所。
- **[R5] `FolderNavigationService` / `QuickAccessService` が mutex 無しで安全な根拠が別ファイルにある** —
  根拠（`MegaSdkClient` の `getChildren`/`getNodeInfo` が同期であること）は
  `src/core/DownloadService.h:49-51` に書かれており、当の 2 ファイルには何も無い。R2-2 の契約書き直しで
  一部は移すが、サービス整理そのものは R5。（R2 調査中に発見）
  → **解消済み。R2-2 が全部持っていっていた（R5 調査で確認、2026-08-07）**。
  `FolderNavigationService.h:18-23` と `QuickAccessService.h:19-25` が現在それぞれ自分の根拠を
  持っている。R5 でやることは残っていない。
- **[R5] 完了/進捗ラムダが `mQueue.front()` 決め打ちで、ジョブ id を照合していない** —
  `DownloadService.cpp` と `UploadService.cpp` の両コールバックは、届いた結果がどのジョブのものか
  検証せずに先頭ジョブへ書き込む。`onTransferFinish` の後に `onTransferUpdate` が来ないという SDK の
  暗黙の保証に寄りかかった設計で、破れると前ジョブの進捗が次ジョブの
  `transferredBytes`/`totalBytes` を上書きする（クラッシュではなく表示の乱れ）。**R4-6 では
  あえてテストで固定しなかった** — 現行挙動を固定すると、id 照合を入れるときにそのテストごと
  書き換えることになるため。`enqueue()` が返す id は既にあるので、直すなら渡すだけ。
  （R4-6 実施中に確認、2026-08-07）
  → **R5 調査で回収（2026-08-07）、R5-8 に統合**。R2-8 の `optional<Job> mActive` 化と同じ項目に
  まとめた。`UploadService` には `checkUpload` 失敗を書く 3 つ目の `front()` 経路もある。
- **[R5] `~DownloadController` / `~UploadController` の `setOnProgress(nullptr)` は競合を防げていない** —
  `DownloadController.cpp:57-67` のコメントは「サービスがロック下でコピーし null チェックしてから
  呼ぶので mid-flight でも安全」と書くが、実際の `DownloadService.cpp:186-198` は**ロック下で
  コピーし、ロックを解放してから呼ぶ**。SDK スレッドがコピーを取った直後に GUI スレッドが
  デストラクタを走り切れば、解放済み `this` に対して `invokeOnGuiThread` が呼ばれる。null チェックは
  コピー**前**の値を見ているので無力。現状は両コントローラが `main.cpp` のスタックローカルで、
  破棄が `client->shutdown()`（SDK スレッド join 済み）より後なので到達不能だが、
  **コントローラをタブ単位化した瞬間に踏む**。製品側の修正が要るので R5。（R4-6 調査中に発見、2026-08-07）
  → **R5 調査で回収（2026-08-07）、R5-9 に統合**。`UploadController.cpp:85-91` も同文で同じ穴。
  実施順は R5-1（分割）の直後 — 踏む直前に置く。
