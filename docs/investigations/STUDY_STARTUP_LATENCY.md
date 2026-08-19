# 調査メモ: セッション再利用時の起動が数秒かかるのは何をしているからか（2026-08-19）

> **状態: 調査のみ。コード変更なし。** 常駐化（アプリをプロセスとして生かし続ける）を検討する
> 前提として、起動のどこに時間が溶けているかを実測した。**結論は「6.5秒のうち MEGA 由来は
> 1.5秒だけで、残り4.8秒は QML のオブジェクト生成」**。どのロードマップ項目にも紐づいていない。
>
> **2026-08-19 追記1 あり。以下の本文は Debug ビルド限定の数字**として読むこと。Release で測り直すと
> 初回描画まで0.82秒・ファイル一覧まで約1.5秒で、体感の「数秒」はほぼ全部ビルド構成由来だった。
> あわせて本文の「9ダイアログの生成に0.7秒」は誤りで、正体は `window.visible = true` の1行。
> → [追記1](#追記12026-08-19-release-で測り直した結果)

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

## 追記1（2026-08-19）: Release で測り直した結果

`restoreSession()` を `loadFromModule()` の前に出すコミット（c56f52d、本文の削減案の1つ目）を入れた
あとも「ウィンドウが開くまで数秒」の体感が残ったので、同じ手口で測り直した。本文が「未確認」に
挙げていた2点 — Release で測っていない / `loadFromModule` の3.0秒が割れていない — がどちらも潰れ、
結論が変わっている。

計測は本文の手順に2つ足しただけ。**`engine.loadFromModule()` を一時的に `QQmlComponent` の
`loadFromModule()` + `create()` に割る**と、型解決とオブジェクト生成が分かれる。その手前で
`setData()` の捨てコンポーネントを4つ作れば、`QtQuick` / `QtQuick.Controls` / `FluentWinUI3` /
`Layouts+QtCore` の各 import を単独で計れる。QML 側は45ファイル全部の root に
`Component.onCompleted` の1行をスクリプトで挿し、終わったら `git checkout` で戻した。

**Release は `CMakePresets.json` に手を入れずに取れる。** VS ジェネレータはマルチ構成なので
`cmake --build build/msvc-debug --config Release --target appMegaExplorer` の1コマンドで済み、vcpkg も
Qt も release 側が既に入っているため再構成もいらない（`build/msvc-debug/Release/` に出る）。実行時は
`PATH` に Qt の `bin` と vcpkg の `bin`（`debug/bin` ではない）を通す。

| 区間 | Debug | Release |
| --- | ---: | ---: |
| プロセス生成 → `main()` | 234 ms | 27 ms |
| `QGuiApplication` 〜 サービス／エンジン構築（`MegaApi` ctor 含む） | 93 ms | 51 ms |
| `import QtQuick` + `QtQuick.Controls` | 286 ms | 51 ms |
| `import QtQuick.Controls.FluentWinUI3` | 809 ms | 47 ms |
| 自モジュール45ファイルの型解決 | 692 ms | 56 ms |
| `Theme` / `LoginView` / `Main` 本体の生成 | 656 ms | 72 ms |
| **`window.visible = true`** | 約500 ms | **496 ms** |
| **初回フレーム描画まで（合計）** | **5047 ms** | **820 ms** |
| ログイン後 UI の構築 | 909 ms | 638 ms |
| ファイル一覧デリゲート（`FileIcon` ×52 ほか） | 762 ms | 上に含む |
| ファイル一覧が出るまで（合計） | 約6.5 s | 約1.5 s |

読み方が3点ある。

**体感の数秒は Debug ビルドのコストが支配項。** QML の型解決と import は Release で1桁縮む
（1.8秒 → 0.15秒）。常駐化を検討する前に、まず Release/RelWithDebInfo のプリセットを足すのが
コード変更ゼロで最も効く手になる。

**本文の「9ダイアログの生成に0.7秒」は撤回する。** ダイアログ9個の `Component.onCompleted` は実測で
3ms 以内に固まっており、その0.7秒の正体は `Main.qml` の `Component.onCompleted` にある
`window.visible = true` の1行（Release で496ms）だった。root の `onCompleted` が子より先に発火して
見えるため、直後に並ぶ子の完了ログをダイアログの生成コストと読み違えていた。**したがって
「ダイアログを `Loader` に落とす」削減案は効かない**。本文の「合計3.2秒削れる」も再計算が要る。

**`visible = true` の496msは Debug と Release でほぼ同値**、つまり QML ではなく OS 側の仕事
（HWND 生成、QWindowKit のフレーム乗っ取り、RHI/D3D11 の初期化）。ここは常駐化以外に動かす手がない。

MEGA 側は LOGIN 0.98秒 + FETCH_NODES 0.53秒で、どちらも QML の型解決中に終わっている。**c56f52d は
狙いどおり効いており、MEGA はもう律速ではない。**

副産物として、`FileIcon` が52個作られていることが分かった。ファイル26件に対して `FileTableView` と
`FileGridView` の両方がデリゲートを作っており、`StackLayout` が非表示側も生かしているため。Debug の
最後の0.76秒の相当部分がこれ。

### 追記1時点での未確認

- Release のコールドスタートは測っていない（Debug のコールドは本文のとおり DLL ロードのみ伸びる）。
- Release プリセットはまだ無い。この追記の数字は手動の `--config Release` で得たもの。
