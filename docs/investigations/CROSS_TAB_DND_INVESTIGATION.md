# タブ間ドラッグ＆ドロップ移動 — 実現可能性調査

2026-07-31。実装はまだ行っていない、机上調査のみのメモ。

> **2026-08-05 追記 — Phase 22b で実装済み。結果は本文と一部異なる。**
>
> - **3. の「本質的なリスク」は起きなかった。** タブを切り替えても元ペインの `DragHandler` は
>   grab を失わず、ドラッグはそのまま生き続ける（実機で確認）。grab の持ち主が item ではなく
>   handler オブジェクトである、という本文の但し書きのほうが正しかった。したがって
>   **対策 B（`StackLayout` → `Item` スタック）は実施していない**。`Main.qml` は無変更。
> - **1./2./4. は本文どおり。** ドロップ判定・実行は既存コードのまま成立し、spring-loaded は
>   `DropArea` + 600ms `Timer` で入った。4.（移動先タブの更新）も本文の見立てどおり必要で、
>   `FolderNavigationController::nodesMoved` + `TabsController` のファンアウトとして実装した。
> - 本文が触れていない実装上の争点は「並べ替え側」にあった（`TabBar` が `Container` である
>   ことに由来する挿入線の置き場所、Fluent の `contentItem` からの左ボタン剥奪など）。
>   詳細は `docs/PROGRESS.md` の Phase 22b 実装ログを参照。
>
> 以下は当時の調査内容そのまま。

## やりたいこと

1. タブ A のファイルをドラッグする
2. ドラッグしたままタブ B の **タブボタン** にホバーすると、タブ B に切り替わる（spring-loaded tab）
3. そのままタブ B のペイン内にドロップして移動する
4. タブ B のリスト内のフォルダにドロップすれば、そのフォルダへ移動する

## 結論（先に要約）

- **3. と 4. は追加実装ゼロで既に動く。** ドロップ側の判定・実行は Phase 14a で
  `dragProxy.sourceNav.canDropHandlesOn()` / `moveHandlesTo()` に集約されており、ドロップ先ペインが
  「別タブのペイン」であっても既存コードはそのまま成立する（後述）。
- **新規に要るのは 2.（ホバーでタブ切替）だけ**だが、ここに **1 つだけ本質的なリスク**がある。
  タブを切り替えると *ドラッグ元ペインが `visible: false` になり、掴んでいる `DragHandler` の
  grab が Qt に取り消されてドラッグ自体が中断する可能性が高い*。ここが可否を決める唯一の争点。
- 対策はある（下の「対策 B」）。ただし `StackLayout` をやめる小規模なリファクタを伴う。
- 加えて、移動後に **移動先タブの一覧が更新されない** 問題が付いてくる。Phase 14a の既知の制限
  （他タブ・フォルダツリーを更新しない）と同じ話だが、今回は「更新されない古い一覧が目の前に
  表示されている」状態になるため、実質的に無視できない。小さな追加実装が要る。

総じて **「難しくはないが、ノーリスクでもない」**。作業量の目安は下記「見積り」。

---

## 1. なぜ 3./4. が無改造で動くか

Phase 14a の設計上、ドラッグのペイロードとドロップの実行主体は **ドラッグ元タブ**に固定されている
（`qml/components/DragProxy.qml:26-32`）。

- `DragProxy` はウィンドウ全体で 1 個、`Overlay.overlay` 直下（`qml/Main.qml:389-392`）。
  タブやペインの階層には属していないので、タブが切り替わってもドラッグ状態
  （`Drag.active` / `handles` / `sourceNav`）は一切影響を受けない。
- 各ドロップ先（`FileGridView.qml:182`、`FileTableView.qml:442`、`FolderTreePanel.qml:102`、
  `QuickAccessSection.qml:112`、`Breadcrumb.qml:125`）は
  `root.dragProxy.sourceNav.canDropHandlesOn(root.dragProxy.handles, …)` を呼ぶだけで、
  自分のタブの選択状態には依存していない。

つまり「タブ B のペインにドロップする」は、実装から見れば「今のタブのペインにドロップする」と
まったく同じコードパスを通る。`checkMove` による可否判定（自分自身・子孫へのドロップ拒否など）も
そのまま効く。

## 2. 新規に必要なのはタブボタンの spring-loaded 切替

`qml/components/TabStrip.qml` の `TabButton` に `DropArea` を足すだけの、形としては小さい変更：

```qml
DropArea {
    anchors.fill: parent
    keys: ["application/x-megaexplorer-nodes"]
    onEntered: switchTimer.restart()
    onExited: switchTimer.stop()
}
Timer { id: switchTimer; interval: 600
        onTriggered: tabsController.currentIndex = tabButton.index }
```

留意点：

- **ホバー滞留のタイマーは必須**。通しでドラッグする途中にタブ上を横切っただけで切り替わると
  操作不能になる。Explorer / ブラウザ相当の 500–800ms。
- **内部ドラッグは「ドラッグアイテムが動いたとき」しかイベントを配送しない**。
  `FolderTreePanel.qml:48-51` に同じ制約のコメントがある。タブ切替直後、カーソルはまだタブ上に
  あるので新ペインの `DropArea` に `entered` は来ないが、ユーザーはそのまま下へ動かすので実害なし。
- タブボタン自体へのドロップ（＝そのタブのカレントフォルダへ移動）を許すかは任意。
  許すなら `canDropHandlesOn(handles, thatTabNav.currentHandle, thatTabNav.atRoot)` で判定できるが、
  `TabsController` は現状 `currentNavigation`（＝カレントタブ）しか公開しておらず
  （`src/qml/TabsController.h:78`）、任意 index の nav を取る口が要る。
  ※ モデルの `navigation` ロールは既に各タブの `FolderNavigationController*` を返しているので、
  `TabStrip` の `Repeater` デリゲートで `required property var navigation` を足すだけで届く。

## 3. 本質的なリスク：タブ切替でドラッグの grab が切れる

現在ドラッグを掴んでいるのは **ドラッグ元ビュー内の `DragHandler`**
（`FileGridView.qml:383-402` / `FileTableView.qml:610-624`）。マウス移動は
`root.dragProxy.moveTo(...)`、離した時に `finish()`、キャンセル時に `cancel()` を呼ぶ。

一方、タブの本体は `StackLayout`（`Main.qml:303-373`）で、**非カレントのペインは `visible: false`
になる**（`Main.qml:347-351` のフォーカス回避コメントがまさにその挙動を前提にしている）。

Qt は「不可視／無効になったアイテム」からポインタ grab を取り上げてキャンセルを配送する。
DragHandler の grabber は item ではなく handler オブジェクトなので *必ずそうなるとは断言できない*
が、そうなった場合は `onCanceled: dragProxy.cancel()` が走り、**タブが切り替わった瞬間に
ドラッグが消える**。これが可否を決める唯一の未確認点。

### 検証方法（実装前に 10 分で潰せる）

`TabStrip.qml` に上記 `DropArea` + `Timer` を仮で入れて、ドラッグ中にタブを切り替えたとき
`DragHandler.onCanceled` が発火するか（＝ゴーストが消えるか）を見るだけ。ここが白なら
以下の対策 B は不要で、作業はほぼ 2. だけになる。

### 対策 A（不採用）: ドラッグ中だけ元ペインを可視に保つ

`StackLayout` は子の `visible` を命令的に書くため、バインディングで抗うのは不可（Phase 9 で
`TabBar.currentIndex` について同種の問題を踏んでいる。`TabStrip.qml:9-20` のコメント参照）。

### 対策 B（本命）: `StackLayout` を素の `Item` スタックに置き換える

全ペインを `anchors.fill` で重ね、可視条件を自前で持つ：

- `visible: index === currentIndex || index === dragSourceIndex`
- `z: index === currentIndex ? 1 : 0`（カレントを最前面に）
- 非カレントは `enabled: false` にはしない（grab を失うため）。重なった `DropArea` は
  最前面が優先されるので、下敷きの元ペインが誤ってドロップを取ることはない。

影響範囲は `Main.qml` の当該ブロックのみ。ただし `StackLayout.isCurrentItem`
（`Main.qml:352-355` のフォーカス受け渡し）を等価な条件に書き換える必要がある。
Pre-release なので CLAUDE.md の方針上この程度のリファクタは許容範囲。

### 対策 C（保険）: ドラッグの grab をオーバーレイ側へ移す

`DragProxy` 側にウィンドウ全面の `DragHandler` を置き、ドラッグ開始時に grab を引き取る構成。
不可視化の影響を根本的に受けなくなるが、押下位置のヒットテストや `grabPermissions` の調整が必要で、
Phase 14a のジェスチャ開始ロジックを書き直すことになる。B が通るなら不要。

## 4. 付随して必要になる「移動先タブの更新」

`moveHandlesTo` は **ドラッグ元 nav** に対して呼ばれ、完了後に更新されるのも元 nav の一覧だけ
（`FolderNavigationController.h:134`、更新は private の `refreshCurrentFolder()`）。
タブ間移動では移動先タブが画面に出ているので、**移動したのにファイルが現れない**という
見た目上の不具合になる。最低限の対処：

- `FolderNavigationController` に「カレントフォルダを再取得する」公開口（`Q_INVOKABLE` 化 or
  移動完了シグナル）を追加
- `TabsController` に「このハンドルを表示しているタブを更新する」ファンアウトを追加

これは Phase 16（リモート変更の反映）の前倒し部分に相当する。逆に言えば、Phase 16 を先にやれば
自動的に解消するので、**Phase 16 の後に着手すればこの項目は不要**になる可能性が高い。
フォルダツリー側が更新されない点は Phase 14a の既知の制限のままで、今回悪化はしない。

## 5. 見積り

| 項目 | 規模 |
|---|---|
| 検証（grab が切れるか） | 数十分 |
| TabStrip の spring-loaded DropArea + Timer | 小（QML 20 行程度） |
| 対策 B（StackLayout → Item スタック） | 中（`Main.qml` 1 ブロック + フォーカス条件の書き換え） |
| 移動先タブの更新 | 小〜中（C++ 2 クラスに口を足す。Phase 16 後なら不要） |
| ドロップ判定・移動の実行 | **0（既存のまま動く）** |

## 6. 推奨

Phase 14b（アップロード D&D）や Phase 16 より前に割り込ませる必然性はない。
順当には **Phase 16 の後に小さなフェーズとして** 入れるのが安く済む（4. がまるごと消えるため）。
先にやる場合は、まず 3. の検証だけ単独で行い、結果次第でスコープ（対策 B の要否）を確定させてから
着手するのが良い。
