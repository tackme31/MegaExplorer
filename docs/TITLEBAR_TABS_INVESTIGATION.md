# 調査メモ: タブをウィンドウタイトルバーに統合できるか（2026-07-30）

Windows Explorer や Chrome のように、タブ帯をOSネイティブのタイトルバー位置に重ねて表示するUIを
Qt/Qt Quickで実現できるかの調査記録。**コード変更は未実施**（調査のみ）。

## 結論

実現は可能だが、Qt Quick/QMLに標準機能としては存在しない。フレームレス化＋独自描画＋（本物の
見た目を狙うなら）Win32ネイティブ連携が必要になる、それなりの規模の作業。

## 現状

`qml/Main.qml:80-99` の `ApplicationWindow` はネイティブタイトルバーをそのまま使い、`header:` の
`Loader` → `TabStrip` + `ToolBar` はその**下**に表示されているだけ（Explorer 10以前寄りのレイアウ
ト）。Chrome/Explorer 11のような「タブがタイトルバーと同じ行に乗る」統合はまだ入っていない。

## 実現に必要な要素

| 要素 | Qt側の対応 |
|---|---|
| ネイティブタイトルバーを消してQML側に描画領域を広げる | `flags: Qt.FramelessWindowHint` |
| タイトルバー領域をドラッグしてウィンドウ移動 | `Window.startSystemMove()` — **Qt 6.8以降**（本プロジェクトはQt 6.10/6.11なので利用可） |
| 端をドラッグしてリサイズ | `Window.startSystemResize(edges)` — 同じくQt 6.8以降 |
| 最小化/最大化/閉じるボタン | 自前のQMLボタン + `window.showMinimized()`等 |
| Windows 11のスナップレイアウト（最大化ボタンにホバーで出るフライアウト） | Qt公開APIには存在しない |
| DWMの最小化/最大化アニメーション、ドロップシャドウ、角丸 | Qt公開APIには存在しない |
| タイトルバー相当部分のみをOSに「ここはキャプション」と認識させる（`WM_NCHITTEST`） | Qt公開APIには存在しない — Win32ネイティブコードが必要 |

「フレームレス＋自前描画」だけなら着手は容易だが、それだけではAero Snapのフライアウト・DWMアニ
メーション・影・角丸が失われ、素朴な自作アプリの見た目になる。Explorer/Chrome相当の“本物”の統合
には、`HWND`を取得して`DwmExtendFrameIntoClientArea`でタイトルバー領域へクライアント領域を拡張
し、`QAbstractNativeEventFilter`で`WM_NCCALCSIZE`/`WM_NCHITTEST`をフックしてどのピクセル領域が
「キャプション（タブ帯）」でどこが「クライアント」かをOSに教える、というWin32レベルの実装が必要
（Qt公開APIの範囲外）。

## 実現パス（3通り）

1. **DIY・QMLのみ（Qt標準APIの範囲内）**
   `Qt.FramelessWindowHint` + 独自タイトルバー行（`TabStrip`をそのまま流用可）+
   `startSystemMove`/`startSystemResize`。実装コストは低いが、スナップレイアウトのフライアウト・
   DWMアニメーション・影・角丸が失われる。Qtフォーラムでも「最小化/最大化時にDWMアニメーション
   が出ない」「端のリサイズが不安定」という報告が複数ある。

2. **DIY・Win32ネイティブ連携込み**
   フレームは残したまま`DwmExtendFrameIntoClientArea`でタイトルバー領域をクライアント側に拡張し、
   `QAbstractNativeEventFilter`で`WM_NCCALCSIZE`/`WM_NCHITTEST`を処理。Explorer/Chromeと同等の見
   た目・挙動が得られる本命ルートだが、実装・保守コストが高く、Windows専用コードになる（本プロジ
   ェクトは既に`WindowsSessionStore`のようなWindows専用クラスがあり、CMakeもMSVC/vcpkg前提なので
   方向性としては矛盾しない）。

3. **サードパーティ: [QWindowKit](https://github.com/stdware/qwindowkit)（Apache-2.0）**
   上記2をライブラリ化したもの。`QWindowKit::Quick`モジュールがQMLの`Window`/`ApplicationWindow`
   向けAPIを提供し、スナップレイアウト・Mica/Acrylicブラー・ダークモード連携まで面倒を見てくれる。
   最も少ない自前コードで“本物”の統合が狙えるが、Qtのprivate APIに依存しており「Qt 5.15.2+ /
   6.6.2+ 推奨」との注記あり — 本プロジェクトのQt 6.10/6.11での安定動作は要検証。追加の外部依存
   （サブモジュール等でのベンダリング）も増える。

## このプロジェクトへの影響（見積もり）

- `header:`/`footer:`の`Loader`パターン（`qml/Main.qml:80-88`）自体は維持できるが、
  `headerComponent`内の`TabStrip` + `ToolBar`構造（`qml/Main.qml:90-168`）を「タイトルバー行（ド
  ラッグ領域＋最小化/最大化/閉じるボタン＋タブ）」として再設計する必要がある。
- `FluentWinUI3`スタイル（`qml/Main.qml:4`）はコントロールの描画スタイルであり、ウィンドウフレー
  ム自体のMica背景・角丸を自動では提供しないため、フレームレス化すると見た目の統一感を保つ工夫が
  別途必要。
- 現状クロスプラットフォーム前提のコードではない（MSVC専用ビルド、Windows専用のセッションストア）
  ため、Win32ネイティブ実装（パス2/3）を選んでもアーキテクチャ上の矛盾は少ない。

## 推奨

- 見た目の完成度を優先するなら **QWindowKit の採用検討**（実装コストが最小で本物に近い挙動）。
  ただし本プロジェクトのQtバージョンでの動作検証が前提。
- 依存を増やしたくない/妥協できるなら **パス1（DIYフレームレス＋QML標準API）** から着手し、スナ
  ップレイアウト等の欠落は許容する。

## プラットフォーム別の機能出し分け（Windows/macOSのみタブ統合、Linuxはフォールバック）

QWindowKitはWindows/macOS/Linuxの3プラットフォームに対応しているが、対応の実装度合いに差がある
（詳細は上の「実現パス（3通り）」のQWindowKit項目を参照）。

- **Windows**: `DwmExtendFrameIntoClientArea` + `WM_NCHITTEST`等のフックによるネイティブ本格対応。
  Snap Layoutフライアウト・DWMアニメーション込みで“本物”に近い。
- **macOS**: `NSWindow`レベルのネイティブ対応。システムの信号機ボタン（traffic light）位置調整＋
  ブラー/ガラス効果に対応。
- **Linux**: ライブラリ自身が「フォールバック実装、部分的なネイティブシステムメニュー対応のみ」と
  明記。X11/Waylandはウィンドウ管理・装飾（CSD）がWM/コンポジタごとに異なり、単一実装で
  Windows/macOS相当の統合感を再現するのは原理的に難しい。

この非対称性を踏まえ、**「Windows/macOSではタイトルバー統合タブを有効化し、Linuxは現行レイアウト
（ネイティブタイトルバー＋`header:`の`TabStrip`+`ToolBar`）のまま」というプラットフォーム条件分岐**
が現実的な落としどころとして有力候補に挙がる。

- 判定手段: QML側`Qt.platform.os`、C++側`Q_OS_WIN`/`Q_OS_MACOS`/`Q_OS_LINUX`（コンパイル時）また
  は`QOperatingSystemVersion`（実行時）。
- 既存の`header:`の`Loader`パターン（`qml/Main.qml:80-99`）にそのまま乗る形で、プラットフォームに
  応じてロードするheaderコンポーネントを切り替えるだけで済む。
- Linux向けの分岐は「新規追加コンポーネントを使わない＝現行コードパスに手を入れない」で済むため、
  既存Linuxパス（といっても本プロジェクトは今のところWindows専用ビルドだが）への影響がなく安全。
- QWindowKit側も`WindowAgent::setup()`をプラットフォームごとに呼ぶかどうか選べる作りなので、
  「Linuxでは統合を諦める」という運用はライブラリの想定範囲内。
- 追加コストの見積もり: C++側にプラットフォーム分岐、QML側に統合タイトルバー用headerコンポーネン
  トを別途追加・保守、Windows/macOS/Linuxそれぞれでの見た目検証が必要（現状macOS/Linuxの検証環境
  自体がない点に注意）。

**注記**: この方向性で進めることを決定したわけではない。あくまで将来的なクロスプラットフォーム化
を見据えた場合の有力な選択肢の一つとして記録している。

## レイアウト案の比較: タブをどこに置くか（2026-07-30 追記）

タイトルバーに何を乗せるかで2案ある。

1. **タイトルバーにタブ**、その下にパンくず/検索/戻る（Explorer 11 / Chrome / Edge 型）
2. **タイトルバーにパンくず/検索/戻る**、その下にタブ（VS Code 型）

**結論: 実装難易度は案2の方がやや低い（1〜2割程度）が、案の選択を覆すほどの差ではない。UX上の理由
から案1を採用する。**

### 共通コスト（全体の7〜8割、どちらを選んでも同じ）

| 項目 | 備考 |
|---|---|
| QWindowKit導入 or DIY Win32実装 | サブモジュール追加＋CMake統合 |
| `header:`の`Loader`構造の再設計 | `qml/Main.qml:80-99` |
| 最小/最大/閉じるボタンの自作＋`setSystemButton()`登録 | Snap Layoutフライアウトに必須 |
| 最大化時の上端パディング対応 | Windowsは最大化でフレーム分食い込む |
| `FluentWinUI3`とキャプション背景（Mica）の見た目合わせ | |
| ログイン画面でのウィンドウ移動 | 下記のとおり見落としやすい新規作業 |

最後の項目が要注意: 現状`authState !== LoggedIn`ではheaderがまったく生成されない
（`qml/Main.qml:81`）ため、フレームレス化するとLoginView表示中にドラッグ領域が消えてウィンドウを
動かせなくなる。両案共通で、独立した対応が必要。

### 案1固有の追加コスト

- `setHitTestVisible()`の対象が**Repeater由来の動的リスト**になる。`qml/components/TabStrip.qml:
  31-74`のTabButton・×ボタン・`+`ボタンが生成/破棄されるたびに登録/解除が要る（delegateに
  `Component.onCompleted`/`onDestruction`を足す形）。
- ドラッグ可能領域＝「最後のタブより右の余白」で、タブ数に応じて動的に変わる。現状`TabBar`は
  `Layout.fillWidth: true`（`qml/components/TabStrip.qml:27`）なので**そのままだとドラッグ領域が
  ゼロ**。タブに最大幅を設け、システムボタンとの間に必ず余白を残す設計が必要。
- 空き領域のダブルクリック＝最大化/復元、右クリック＝システムメニュー、を自前で担保。
- **将来のタブD&D並べ替え/タブを引きちぎって別ウィンドウ**が、キャプションドラッグ（ウィンドウ移
  動）と真っ向から競合する。Chrome/Explorerが最も手をかけている箇所。現状未実装なので即座には効か
  ないが、実装するならここが山場。

### 案2固有の追加コスト

- `setHitTestVisible()`の対象はBack/Breadcrumb/TextField/≡の**4つで静的**。一度登録すれば終わり。
  **ここが両案の最大の差**。
- ただし現在のToolBar行はBreadcrumb（fillWidth 7）+ TextField（fillWidth 3）で横幅を使い切ってい
  る（`qml/Main.qml:133-150`）。このままだと**ドラッグ余白がゼロ**なので、検索ボックスに最大幅を
  設けるなどのレイアウト再設計が要る（VS Codeがコマンドセンターを中央に置いて左右に余白を残してい
  るのと同じ理由）。
- `qml/components/Breadcrumb.qml`の`relayout()`は`root.width`基準なので、幅が減るだけでロジック変
  更は不要。
- `qml/components/TabStrip.qml`は**完全に無改造**。将来のタブD&Dもキャプションと無関係な純QMLで完
  結する。

### 案1を採る理由（実装難易度とは別軸）

パンくず・検索・戻るは**アクティブタブ固有の状態**。案2ではそれがタブ帯より**上**に描画され、「タ
ブに属する情報がタブより親の位置にある」という階層の逆転が起きる。VS Codeのタイトルバーにあるコマ
ンドセンターはワークスペース全体のものでタブ固有ではないため破綻しないが、本プロジェクトの構成
（タブごとに独立したナビゲーション/検索状態、Phase 9）では違和感が出る。Explorer 11 / Chrome /
Edgeがいずれも案1なのも同じ理由。

### 着手順（案1のリスク低減）

1. まず**タブ帯はキャプションに乗せないまま**、フレームレス化＋システムボタン＋ドラッグ＋ログイン
   画面対応を通す（＝共通コストの消化）。
2. その後`TabStrip`をキャプション行へ移し、hit-test登録とドラッグ余白の確保を行う。

こうすると案1固有のリスク（動的hit-test・ドラッグ競合）を後半に隔離でき、途中で案2へ切り替える判
断余地も残せる。

## 参考

- [Window QML Type — Qt 6.11](https://doc.qt.io/qt-6/qml-qtquick-window.html)
  （`startSystemMove`/`startSystemResize`がQt 6.8以降であることを確認済み）
- [QWindowKit (GitHub)](https://github.com/stdware/qwindowkit)
- [Qt Forum: Custom TitleBar (2026)](https://forum.qt.io/topic/164518/custom-titlebar) —
  DWMアニメーション/リサイズの実践的な問題点
- [Qt Forum: Modern titlebars in Qt widget based desktop applications](https://forum.qt.io/topic/160298/modern-titlebars-in-qt-widget-based-desktop-applications)

## ステータス

**実装済み**（Phase 17a / 17b）。レイアウトは**案1（タイトルバーにタブ）**、実装方針は
**パス3（QWindowKit をベンダリング、タグ 1.5.0 に固定）**を採用した。17a でフレームレス化と自前
キャプション行（`qml/components/CaptionBar.qml`）、17b でタブ帯のキャプション行への移動を行って
いる。本メモに残っていた未決事項はこれで全て解消済み。

このメモは調査時点の比較検討の記録として残す。実際に何を作ったか・何にハマったかは
`docs/PROGRESS.md` の Phase 17a / 17b の実装ログを参照（本メモの想定と実装が食い違った点も
そちらに書いてある）。
