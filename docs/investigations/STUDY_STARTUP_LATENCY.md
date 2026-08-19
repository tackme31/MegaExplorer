# 調査メモ: セッション再利用時の起動が数秒かかるのは何をしているからか（2026-08-19）

> **状態: 調査のみ。コード変更なし。** 常駐化（アプリをプロセスとして生かし続ける）を検討する
> 前提として、起動のどこに時間が溶けているかを実測した。**結論は「6.5秒のうち MEGA 由来は
> 1.5秒だけで、残り4.8秒は QML のオブジェクト生成」**。どのロードマップ項目にも紐づいていない。

## 結論

Debug ビルド・ウォーム起動・セッション復元で、プロセス生成からファイル一覧が出るまで **約6.5秒**。
内訳の支配項は以下の3つで、いずれも MEGA SDK ではない。

| | 所要 | 何 |
| --- | ---: | --- |
| 1 | **3.0 s** | `loadFromModule()` の QML 型解決〜`LoginView` 完成まで |
| 2 | **1.1 s** | `authState=LoggedIn` を受けてログイン後 UI を同期構築する区間 |
| 3 | **0.7 s** | `Main.qml` が無条件に実体化している 9 個のダイアログ |

MEGA 側は `fastLogin` 1.0秒 + `fetchNodes` 0.5秒 の計1.5秒しかなく、しかも `fetchNodes` の0.5秒は
ローカル state cache 経路（`STUDY_FETCHNODES_PROGRESS_UI.md` 追記2の619msと一致）。**セッション
再利用時の待ち時間は MEGA の問題ではない。**

加えて構造上の発見がひとつ: **QML ロードと MEGA ログインが完全に直列**になっている。
`main.cpp` は `engine.loadFromModule()`（3.75秒）が返ってから `authController.restoreSession()`
を呼ぶので、1.5秒の MEGA 処理がまるごと後ろに積まれている。

## 実測タイムライン

プロセス生成時刻からの経過ms。2回連続で測って再現性を確認済み（下表は2回目）。

| 経過 | 区間の所要 | 内容 |
| ---: | ---: | --- |
| 24 | 24 ms | DLL ロード + 静的初期化（`main()` 到達まで） |
| 63 | 39 ms | `QGuiApplication` ctor |
| 89 | 26 ms | `installLogging()` + **`MegaApi` ctor（22ms）** |
| 118 | 28 ms | 全サービス/コントローラ生成 + `QQmlApplicationEngine` + contextProperty |
| 3124 | **3006 ms** | `loadFromModule()`: QML 型解決〜`LoginView` の `Component.onCompleted` |
| 3825 | **700 ms** | 9 ダイアログ + `ToastStack` + `CaptionBar` の生成 |
| 3867 | 42 ms | `loadFromModule()` 復帰 → `restoreSession()` 呼び出し |
| 3883 | 16 ms | **最初のフレーム描画（ここで初めてウィンドウが見える）** |
| 4864 | **994 ms** | `fastLogin`。うち約925msが `folder_locker` の排他オープン + FileService 初期化 |
| 5395 | **530 ms** | `fetchNodes`（`Session loaded from local cache` 経路） |
| 5398 | 3 ms | `dumpSession` + `saveSession` + GUI スレッドへ post |
| 6477 | **1079 ms** | `authState=LoggedIn` 発火 → `Main.qml` の `Connections` がログイン後 UI を構築 |

読み方の注意が2点ある。

**`MegaApi` の構築は22msで、疑う必要がない。** 起動が遅いと聞いて最初に疑うのはここだが、
SDK の重い仕事は ctor ではなく `fastLogin` に入っている。その `fastLogin` の1秒も内訳の9割が
state-cache フォルダの排他オープンと FileService 初期化で、ネットワークではなくローカル I/O。

**最後の1.08秒はログイン後 UI の構築であって、MEGA の待ちではない。** `fetchNodes` のコール
バックは5.4秒時点で GUI スレッドに届いている。そこから `setState(LoggedIn)` が
`authStateChanged` を出し、`Main.qml` の `Connections`（`tabsController.loadRootAll()` /
`folderTreeModel.reload()` / `quickAccessModel.reload()`）と、`Loader` の
`mainContentComponent` への切り替え（SplitView・SidePanel・TabStrip・FileView 一式の同期生成）
が走る。この1.08秒はまるごと QML。

## 計測方法（再現手順）

外から測れるのは「プロセス生成→ウィンドウ可視」までで、その3.8秒の内訳は取れない。`QT_LOGGING_RULES`
で Qt 内部カテゴリを開けても、最初に出るのは `qt.scenegraph.general` の "threaded render loop" で、
これは既に QML ルート生成時点。**内訳を割るには一時的な計測ログを入れるしかない**、というのが今回
の手順上の結論。入れて測って戻した。

- `main.cpp`: `GetProcessTimes()` でプロセス生成時刻を取り、そこからの経過msを `qInfo` で刻む
  マクロを置く。`installLogging()` より前の値は変数に控えて後からまとめて出す（それ以前の
  `qInfo` はファイルに届かない。WIN32_EXECUTABLE なので stderr は消える）。
- 刻む位置: `main()` 冒頭 / `QGuiApplication` 後 / `installLogging()` 後 / `MegaSdkClient` ctor 後 /
  全サービス構築後 / `loadFromModule()` 前後 / `restoreSession()` 前。加えて root の
  `QQuickWindow::frameSwapped` を一度だけ拾って初回描画、`authStateChanged` を繋いで状態遷移。
- QML 側は各ファイルのルートに `Component.onCompleted: console.info(...)` を1行。`onCompleted` は
  子から親へ順に発火するので、兄弟間のタイムスタンプ差がそのまま各サブツリーの構築時間になる。
  `Main.qml` は既に `Component.onCompleted` を持っているので二重定義でコンパイルエラーになる。
- **`src/core` に `qInfo` は入らない。** Qt にリンクしていないので `QtGlobal` すら開けない。
  `AuthService` を測ろうとして詰まったので、隣接する `src/mega`（`MegaSdkClient`）と
  `src/platform`（`WindowsSessionStore`）と `src/qml`（`AuthController`）から挟んで区間を出した。

計測ログは `qSetMessagePattern` のおかげでミリ秒付きで `MegaExplorer.log` に落ちるので、
SDK のログ行と同じ時間軸で並べられる。これが今回いちばん効いた。

## 常駐化を検討するときの材料

常駐化すれば6.5秒すべてが消え、2回目以降の「起動」はウィンドウ表示だけになる。効果は最大。
ただし常駐化しなくても効く手が3つあり、合計で約3.2秒（半分）は削れる見込み。

- **`restoreSession()` を `loadFromModule()` の前に出す（約1.5秒）。** MEGA の処理を QML ロードの
  裏に隠すだけで、増える複雑さは小さい。ただし `Main.qml` の `onAuthStateChanged` は「QML 構築前に
  LoggedIn になった場合」を拾えない（変化した瞬間にしか発火しない）ので、初期状態を読む経路が要る。
- **9 ダイアログの遅延生成（約0.7秒）。** `Loader` に落とすだけ。
- **ログイン後 UI の構築（約1.1秒）を非同期 `Loader` にするか、ログイン待ちの裏で先に組む。**

残る約3.0秒は QML の型解決そのもので、ここは常駐化かビルド構成でしか動かない。

## 未確認

- **Release ビルドで測っていない。** 数字はすべて Debug。QML 生成コストは Release で相当下がる
  はずなので、常駐化の要否を判断する前に測り直す価値がある。今は Release プリセットが無い。
- 3.0秒の `loadFromModule` の内訳（FluentWinUI3 のインポート解決 / 自モジュールの型登録 /
  `Theme.qml` などのシングルトン / qwindowkit）は割れていない。ここを割るなら `qmlprofiler`。
- コールドスタート（OS のファイルキャッシュが冷えた状態）は1回だけ観測しており、DLL ロードが
  24ms → 283ms に伸びた。それ以外の区間は変わらなかった。
