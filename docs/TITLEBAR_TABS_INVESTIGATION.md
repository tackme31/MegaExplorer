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

## 参考

- [Window QML Type — Qt 6.11](https://doc.qt.io/qt-6/qml-qtquick-window.html)
  （`startSystemMove`/`startSystemResize`がQt 6.8以降であることを確認済み）
- [QWindowKit (GitHub)](https://github.com/stdware/qwindowkit)
- [Qt Forum: Custom TitleBar (2026)](https://forum.qt.io/topic/164518/custom-titlebar) —
  DWMアニメーション/リサイズの実践的な問題点
- [Qt Forum: Modern titlebars in Qt widget based desktop applications](https://forum.qt.io/topic/160298/modern-titlebars-in-qt-widget-based-desktop-applications)

## ステータス

未着手。実装方針（パス1〜3のどれか）を決めてから着手する。ロードマップ（`docs/PROGRESS.md`）には
まだ載せていない — 着手を決めたらそちらにフェーズとして追加する。
