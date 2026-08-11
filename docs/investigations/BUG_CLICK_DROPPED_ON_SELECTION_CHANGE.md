# プレビューペイン導入後のクリック取りこぼし — 調査結果

2026-08-09。Phase 15（アプリ内プレビューペイン）実装直後にユーザが見つけた不具合の調査。
**原因特定・修正済み。** ただし §5 に残課題が1件ある。

**結論**: プレビューは無関係だった。真犯人は選択が変わるたびに右クリックメニューの
`MenuItem` を全部作り直していた既存のバインディングで、GUI スレッドが 1 クリックあたり
最大 154ms 止まっていた。プレビューは「クリック連打」という操作をユーザにさせた誘因にすぎない。

---

## 1. 報告された現象

> グリッド表示で、プレビューが読み込まれる前に別のアイテムをクリックすると、選択するアイテムが
> ずれる現象を確認しました。というより、プレビュー待たずに色んな画像をクリックして回ると
> 選択できなかったり反応したりします。

---

## 2. 外れた仮説（先に潰した順に）

計測ログをユーザに再現してもらって全部棄却した。**同じ道を二度通らないために残す。**

- **SDK の mutex 競合**（`resolveNode()` = `getNodeByHandle()` がグローバル mutex を取る）。
  最有力に見えたが、`mService->request()` 全体が常に 2ms 未満。棄却。
- **`QDir().mkpath()` を選択のたびに実行**。同じく 2ms 未満。棄却。
- **一時 JPEG の読み+削除**。read 6〜11ms / remove 0ms。無視できる。棄却。
- **`mipmap: true` の 1000×1000 テクスチャ破棄**（`publish(Loading)` の中）。
  計測点を `publish` を含む位置に動かして再測定したが 2ms 未満。棄却。
- **デリゲートの `DragHandler` が閾値超えでタップを潰している**。調査書 `STUDY_PREVIEW_PANE.md`
  時点での本命だったが、**不具合発生時の `DragHandler` 発火は 0 回**。バンド選択も 0 回。
  そもそも「タップが届いていない」という前提が誤りで、タップは毎回届き `selectRow` も呼ばれていた。
- **`dataChanged`（全行）が重い**。`emit selectionChanged()` と分けて測ったら 0ms。棄却。

---

## 3. 真因

`emit selectionChanged()` の中で **154ms** 消えていた。内訳は 77ms × 2。

```
05.904  tap
05.905  ActionMenu の actionIds 変化（グリッド用 FileContextMenu）
        ← 77ms
05.982  ActionMenu の actionIds 変化（テーブル用 FileContextMenu）
        ← 76ms
06.058  SDK リクエスト発行（＝プレビュー要求はこの 154ms の「最後」に走る）
06.060  notifySelectionChanged selectionChanged=154ms dataChanged=0ms
```

`FileContextMenu.qml` が `actionIds` を `fileListModel.availableActions` にライブバインドしており、
この Q_PROPERTY の NOTIFY は `selectionChanged` である。`ActionMenu.qml` の `Instantiator` は
リストの**内容が変わると全 `MenuItem` を破棄・再生成**する。FluentWinUI3 の `MenuItem` は
背景・インジケータ・contentItem を持つフル `Control` で、Debug ビルドで 5 個 ≈ 77ms。
`FileViewInput` はグリッド用とテーブル用に 1 つずつあるので、タブあたり 2 メニューが再構築される。

**なぜ 10 回に 1 回だったか**: `availableActions` の**値が実際に変わったとき**だけ
`Instantiator` が作り直す。画像→画像のクリックでは同じ 5 個なので再構築されない。
変わるのは主に「空 ⇄ 非空」、つまり**空白クリックで選択解除した直後**。

裏付けとして、最初の不具合ログの `[STALL]` **6回すべてが `row= -1`（空白クリック）の直後のタップ**
で発生していた。6/6。

### 「選択がずれる」がこれで説明できる理由

タップ座標→行の解決は最後まで正しかった。グリッドの実寸（`cellWidth=120` / `cellHeight=146` /
`topMargin=4` / 列数 6）を当てはめると、正常時のログは -1 を返した座標まで含めて全タップが
`contentY = -4` で説明できる。不具合時の 3 タップだけが `contentY ≈ +72` でないと説明できない。

つまり **ビューが実際に約 72px スクロールしていた**。そこに 154ms のフリーズが重なると、
画面が更新されないままユーザは**古い絵**を見てクリックする → 1 行下が選択される。
`row= -1` になった回は新レイアウトのタイル間ギャップに当たっており、`clearSelection` される
＝「反応しない」に見える。**両方の症状が 1 つの原因**。

---

## 4. 修正

`qml/components/FileContextMenu.qml` — `actionIds` のライブバインディングを廃止し、
`popup()` 直前のサンプリングに変更した。`context` が既に従っていた
「開く直前にまとめて代入し、開いている最中は変えない」という `ActionMenu.qml` の規約に
`actionIds` だけが従っていなかった、という不整合の解消でもある。

`onAboutToShow` ではなく `popup()` の手前に置いたのは、項目の増減がメニューのサイズと
表示位置の計算に絡むため、Popup の開閉シーケンスの外で済ませたかったから。
`context` の文言差し替えとは影響範囲が違う。

呼び出しは `FileViewInput.popupContextMenu()` の 1 箇所だけで、**呼び忘れると全メニューが
無効な "None" 1 行になる**。両ファイルにその旨のコメントを置いてある。メニューは Popup で
ウィンドウを要求するため QML テストの射程外（`tst_FileViewInput.qml` 冒頭の「Not covered」に
元から挙がっている領域）で、ここは手動確認に頼っている。

検証: 空白クリックと画像クリックを交互に打つ 100% 再現手順で `[STALL]` が 1 回も出なくなった。
`actionIds` の変化は 66 回 → 11 回（右クリックで実際にメニューを開いた回数のみ）。
右クリックメニューの内容・有効無効・幅・位置は手動でリグレッションなしを確認済み。

### 残っているコスト（許容と判断）

メニューを開いた瞬間に 100ms 前後かかる（初回は約 490ms、FluentWinUI3 のコンポーネント
初回コンパイル込み）。これはメニュー構築の本来のコストで、**以前は「選択のたび」に払っていたものが
「開いたときだけ」に移った**形。Debug ビルドの数字であり、Release ではかなり縮む。
気になるなら `MenuItem` を使い回して `enabled` だけ更新する方向になるが、`actionIds` は
個数自体が変わる（4 個 / 5 個）ので単純ではない。今回のスコープ外とした。

---

## 5. 残課題: `contentY` が動いた引き金は未解明

`contentY` が -4 → 約 +72 に動いたこと自体は数値で確定しているが、**何が動かしたかは不明**。

`FileGridView` は `acceptedButtons: Qt.NoButton` でドラッグ/フリックを殺してあり
（Phase 14a 以降、左ドラッグは move D&D）、`keyNavigationEnabled: false`。
`contentY` を書くのは `DragAutoScroller`（ドラッグ中のみ / 当該時刻は未発火）と
`positionViewAtIndex`（`revealRow` 経由＝キー操作とリネーム開始のみ）だけ。
ホイール 1 ノッチ相当という線が残っている（72px はほぼその大きさ）。

フリーズが直った今、スクロールが起きても画面が即座に追従するので**症状としては解消**している。
再発したら以下を仕込めば一発で分かる（今回使ったものと同じ）:

- `FileViewInput.qml` の `Connections { target: root.view }` に
  `onContentYChanged` で `contentY` / `moving` / `flicking` / `verticalVelocity` を `console.warn`
- `handleLeftTap` 先頭で `pos` / 解決された row / `contentY` / `contentHeight` / `view.width` / モデル行数
- 同 `Connections` にモデルの `onRowsInserted` / `onRowsRemoved` / `onModelReset`（スクロールと
  モデル側の行ずれを切り分けるため）

`moving`/`flicking`/`vy` が付いていればホイールかフリック、付いていなければプログラムからの書き込み。

### 計測手法のメモ

GUI スレッドの停止検出は `main.cpp` に interval 16ms の `QTimer` を置き、`QElapsedTimer` で
前回発火からの実経過を測って 100ms 超で warning、が非常に有効だった。
「そもそも GUI スレッドが止まっているのか」が一発で分かり、以降の切り分けの土台になる。
ログは `%LOCALAPPDATA%\MegaExplorer\MegaExplorer\MegaExplorer.log`（`MegaExplorer` が 2 段ネスト）、
前回分は `.log.1` にローテートされる。

---

## 6. 触るときの注意（Phase 15 の設計上の前提）

原因追及で書き換える前に、下記は意図的にそうなっている点なので壊さないこと。
詳細は `docs/PROGRESS.md` の Phase 15 エントリ。

- **一時 JPEG は読んだ直後に消す**（`onImageFetched` は世代チェックより前に読み+削除する）。
  これでディスクにゴミが残らず、`Image` の URL キャッシュ問題も消えている。
- **世代カウンタ（`mGeneration`）は捨てられない**。`getPreview` はキャンセル不能なので、
  見捨てた結果を落とす唯一の手段。タブ切替でも進む必要がある。
- **`PreviewController` はウィンドウ単位**（`main.cpp` のスタックローカル + context property）。
  タブ単位にすると `TabsController` の 5 箇所改修が戻ってくる。
- **通知（トースト）は出さない**。コンストラクタが `NotificationController` を取らないことで
  構造的に禁じてある。
- **`PreviewService` の待ち枠は 1 つだけ**で、押し出された側は `kPreviewSuperseded`(3) で終わる。
