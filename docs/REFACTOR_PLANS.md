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

### R1 — 調査済み / 修正未着手（2026-08-06）

計画の「種」5 件を現物で検証した結果。**確認 3 件 / 種の誤り 1 件 / 問題なし 3 件 / 新規 3 件**。
以下はそのまま plan mode の作業単位として使える粒度で書いてある。

#### 確認された問題

**R1-1 [高] ダウンロード先パスにサーバ由来のノード名を無検証で連結（パストラバーサル）**

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

**R1-2 [中] `install()` に `LICENSE` / `THIRD-PARTY-NOTICES.txt` が入っていない（Phase 20b の宿題）**

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

### R2 — 未着手
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
  （R1 調査中に発見）
