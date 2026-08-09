# ビューのヒットテストがスクロール量だけ下にずれる件 — 調査と修正方針

作成: 2026-08-10 / 対象: `qml/components/FileViewInput.qml`, `qml/components/BandSelector.qml`

## 結論（先に）

**ビュー直下のポインタハンドラに書いている `parent: root.view` は、Qt によって黙って
`root.view.contentItem` に付け替えられる。** その結果 `point.position` /
`centroid.position` は最初から**コンテンツ座標**で届いているのに、受け手
（`indexAtViewportPos()` / `rowAt()` / `BandSelector` の `pointerContent`）が
「ビューポート座標だ」と思って `contentY` をもう一度足している。**`contentY` の二重計上**
なので、ズレ量はスクロール量そのものになる（＝スクロールするほど大きくなる）。

Qt 6.11.1 で実測して確定。ドキュメントには書かれていない挙動である。

修正は **ハンドラの位置を受け取った直後に一度だけビューポート座標へ正規化する**
（`view.mapFromItem(handler.parent, pos)`）。ハンドラの設置場所は変えない。

## 1. 症状

- グリッド／リストの両方で、スクロール後にマウスオーバー・クリックすると、
  **ポインタより下のアイテム**がハイライト／選択される。
- ズレ量はスクロール量に比例して増える。スクロール位置が先頭（`contentY == 0`）なら正しい。
- 一方 **ドラッグ＆ドロップのドロップ先判定はずれない**（後述 §4）。この非対称が手掛かりだった。

## 2. 実測（Qt 6.11.1 / `qmltestrunner` の最小再現）

`GridView`（`cellHeight: 50`）を `contentY = 200` までスクロールし、ビューポート y=100
（＝コンテンツ y=300、正しい index は 24）にマウスを置いたときの値：

| 観測項目 | 実測値 |
| --- | --- |
| `handler.parent === grid` | **false** |
| `handler.parent === grid.contentItem` | **true** |
| `handler.point.position.y` | **300**（＝コンテンツ座標。ビューポートなら 100） |
| 現行コード `indexAt(contentItem.mapFromItem(grid, pos))` | **16**（＝4 行ぶん下） |
| 正解 `indexAt(50, 300)` | 24 |

同じことが `TableView` でも起きる（`contentY = 210`、ビューポート y=90 →
現行コードは `row = -1`、正解は `row = 10`）。

派生して分かったこと：

- **素の `Flickable` の中に宣言した `Item` は `contentItem` の子になるが、`GridView` の中に
  宣言した `Item` はビュー自身の子のまま**（`layer.parent === grid` が true）。
  `FileViewInput.qml` 冒頭のコメントが前提にしている「Flickable の中に置くと contentItem に
  入る」は、`Item` については GridView では成立していない。ただし今回の原因はそちらではなく、
  **ハンドラの `parent:` の付け替え**のほうである。
- `parent:` に素の `Item` を渡した場合は素直にその `Item` に付く（`handler.parent === plain`）。
  **付け替えが起きるのは Flickable を渡したときだけ**。
- `contentItem` に付いたハンドラでも、**コンテンツ下端より下（＝コンテンツの外）のタップは
  届く**（`contentHeight = 50` の状態でビューポート y=250 のクリックが発火した）。
  つまり `parent: view` を書いた本来の目的（最終行より下のタップを拾う）は、
  付け替えられた後も満たされている。**ハンドラを動かす必要はない**。

## 3. 影響を受けている箇所（7 つ）

| 位置 | 何が壊れるか |
| --- | --- |
| `FileViewInput.qml:265,266,274` `viewHover.point.position` | ホバー行（`hoverRow`）が下にずれる |
| `FileViewInput.qml:281` 左タップ `point.position` | 選択行が下にずれる／空白判定も誤る |
| `FileViewInput.qml:287` 右タップ `point.position` | 「行の上か空白か」の判定を誤り、行上でも背景メニューが出る |
| `BandSelector.qml:94` `centroid.pressPosition` | `originContent` が `contentY` ぶん過大 → 帯の始点がずれる。`isOnItem(press)` も誤判定 |
| `BandSelector.qml:110` `centroid.position` | `pointerView` が実はコンテンツ座標 → 帯の矩形が `contentY` ぶん下に描かれ、選択行もずれる。`autoScroller.track()` の端判定も誤る |

**影響を受けない箇所**（同じ `parent: root.view` を書いているが `Item` なので付け替えられない）：

- `FileViewDropArea.qml:66`（`DropArea` は `Item`）。`drag.x/y` は本当にビューポート座標なので、
  `rowAtPos` 側の `mapFromItem` と辻褄が合っている。**ドロップ判定が正しかった理由がこれ**。
- `BandSelector.qml:123` の帯の `Rectangle`（`Item`）。座標系自体は正しく、
  上流の `contentRect` が汚染されているだけ。

## 4. 経緯 — 以前の「ズレ対応」との関係

ユーザーの記憶にある過去対応は S8 / S6a（`docs/DESIGN_IMPROVEMENT.md` の 1438 行付近
「S8 が気にしていた『クリックとハイライトのズレ』は起きない」）だが、**あれはこの不具合の
原因ではない**。`git log -L` で確認したところ：

- S8（`eed7f29`）と S6a（`f960bdb`）は、3 箇所に散っていた
  `contentItem.mapFromItem()` → `indexAt()` の 2 行を `indexAtViewportPos()` / `rowAt()` に
  **抽出しただけ**で、変換自体はそれ以前から同じ形で存在していた。
- したがって二重計上は、ビュー直下のタップ／ホバーハンドラを導入した時点
  （Phase 6b の選択まわり）からの**元からのバグ**。ドロップ経路だけは当時から
  `DropArea` 基準だったため正しく、そちらが「動いている実績」になって疑われなかった。

S8 の「タップ・ホバー・ドロップが 1px もずれないよう判定を 1 本にする」という方針自体は正しい。
壊れていたのは、その 1 本に**座標系の違う入力が 2 種類流れ込んでいた**こと。

## 5. 修正方針

### 採用: 入口で一度だけビューポート座標に正規化する

`rowAtPos` の契約（「ビュー座標を渡す」）は 3 コンポーネントのコメントに書かれており、
`FileViewDropArea` はその契約どおりに動いている。契約は変えず、**契約を破っている側**
（ハンドラ）を入口で直す。

`FileViewInput.qml`：

```qml
    // ハンドラの point.position は「そのハンドラの parent item」の座標系。
    // parent: <Flickable> と書いても Qt は contentItem 側に付け替えるため（実測、
    // 未文書化）、届く時点でスクロール込みのコンテンツ座標になっている。ここで一度だけ
    // ビューポートへ戻し、rowAtPos の契約を守る。将来 Qt が parent: を額面どおり
    // 扱うようになっても mapFromItem は恒等変換になるので、この形なら壊れない。
    function viewPos(handler, pos) {
        return root.view.mapFromItem(handler.parent, pos);
    }
```

- `viewHover` の 3 箇所 → `root.resolveHover(viewHover.hovered, root.viewPos(viewHover, viewHover.point.position))`
- 左右の `TapHandler` に `id` を付け、`root.viewPos(leftTap, point.position)` を渡す
  （`onTapped` 内の `this` に頼らない）。

`BandSelector.qml`：

```qml
const press = root.view.mapFromItem(bandDrag.parent, bandDrag.centroid.pressPosition);
...
root.pointerView = root.view.mapFromItem(bandDrag.parent, bandDrag.centroid.position);
```

これで `originContent` / `pointerContent` / `autoScroller.track()` / `isOnItem()` が
すべて同時に直る。`BandSelector` 内の `contentX/contentY` を足す既存の式（47-52 行の
コメント込み）はそのままでよい。

あわせて、`FileViewInput.qml` 冒頭と `BandSelector.qml:77-80` の「Flickable の中に置くと
contentItem に入る」という説明コメントを、実測に合わせて直す（§2 の 2 つ目の箇条書き）。

### 採らない案

- **契約をコンテンツ座標側に寄せる**（両ビューの `mapFromItem` を消す）: `FileViewDropArea` に
  逆変換を足すことになり、さらに `BandSelector` は端の自動スクロール判定にビューポート座標が
  必要なので、結局どちらの座標も要る。移動する手間が増えるだけで単純化しない。
- **ハンドラを `parent: view` + `anchors.fill: view` の実 `Item` に載せ替える**:
  座標は素直になるが、その `Item` が `contentItem`（＝デリゲート）より**手前**に来るため、
  `FileViewInput.qml:18-22` が明示的に避けている「デリゲートの `DragHandler` が
  このレイヤの pending tap をキャンセルできる」順序が壊れる。選択済み行からのドラッグで
  選択が潰れる回帰を招く。

## 6. リグレッションテスト（実装済み）

現行の `tests/qml/tst_FileViewInput.qml` は `resolveHover(hovered, pos)` を**直接呼ぶ**ため、
配線側のこの不具合は原理的に検出できない。そこで実ウィンドウ + 実マウスイベントの経路を
`tests/qml/tst_FileViewInputPointer.qml` として別ファイルで追加した（別ファイルなのは、
既存側が「ウィンドウが無いので `hovered` は false」という前提のケースを持っているため）。

20 行ぶんスクロールした `Flickable` の上でビューポート y=50 に `mouseMove` / `mouseClick` し、
ホバー行と選択行がどちらも 22 行目になることを見る。修正前のビルドでは **42**（＝20 行ぶん下）
になり、実際に落ちることを確認済み。

なお `qml/` は QML モジュールとしてバイナリに焼き込まれるので、**QML を直しただけでは
テストに反映されない**（テストランナーがソースツリーから読むのは `tst_*.qml` だけ）。
この種の切り分けをするときは毎回リビルドすること。

## 7. 検証手順（手動）

1. 100 件以上あるフォルダを開き、グリッド／リストの両方で最下部までスクロールする。
2. 任意のタイル／行にポインタを置き、**ポインタの下のもの**がハイライトされること。
3. クリックしてその行が選択されること。最終行より下の空白をクリックして選択が解除されること。
4. 行の上で右クリックすると選択メニュー、空白で右クリックすると背景メニューが出ること。
5. スクロール後に空白からドラッグしてラバーバンドを引き、**帯がポインタに追従し**、
   帯が触れた行だけが選択されること。
6. スクロール後にドロップ先のフォルダをドラッグでホバーし、従来どおり正しい行が光ること
   （回帰していないことの確認）。
