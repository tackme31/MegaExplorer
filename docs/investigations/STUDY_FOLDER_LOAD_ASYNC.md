# フォルダ移動の非同期化 — 実現可能性調査

対象: 「フォルダを開くと一覧取得が終わるまで UI スレッドが固まる」挙動を、

1. 移動は即座に成立（パンくず・タブ名がすぐ切り替わる）
2. 取得中はタブにローディング表示、内容領域は空 or「取得中」
3. 取得でき次第、描画

に変える案。2026-08-10 時点のコードに対する調査で、実装は未着手。

## 結論

**難易度は中程度。1 フェーズ相当。** C++ の API 形状はすでに全部コールバック非同期で、
タブのスピナー UI も既存なので、新規に作る配線はほとんどない。大変なのは、
`IMegaClient` が明文化している「一部メソッドは呼び出しスレッドで同期実行」という契約を
壊す変更になる点で、その契約に乗っている 3 箇所を同時に直す必要がある。

ただし **着手前に計測が要る**。固まり時間の内訳次第では、スレッド化しても体感が変わらない
可能性がある（§4）。

## 1. 現状の仕組み

固まる正体は `MegaSdkClient::listChildren`（`src/mega/MegaSdkClient.cpp:873-888`）。
`mApi->getChildren()` → `nodeListToEntries()` → `onDone()` を、呼び出しスレッド
（= GUI スレッド）で同期実行している。`mApi->getChildren()` は SDK 内部ミューテックスを
取ってからメモリ上のノードツリーをソートするので、SDK スレッドが長い処理を持っていれば
そのぶん GUI が待たされ、加えて件数ぶんのソートと `FileEntry` 変換が乗る。

これは事故ではなく意図的な設計で、2 箇所に明記されている:

- `src/core/IMegaClient.h:27-30` — 「`getRootChildren`/`getChildren`/`search`/`getPath`/
  `getNodeInfo` は常に呼び出しスレッドで同期実行。FolderNavigationService の lock-free 設計が
  これに依存」
- `src/core/FolderNavigationService.h:10-14` — 「このクラスはミューテックスを持たない。それは
  借り物の保証であって、上記 3 つのいずれかが SDK スレッドから返すようになったら
  ミューテックスが要る」

呼び出し経路自体はすでに非同期形:
`FolderNavigationController::openFolder`（`src/qml/FolderNavigationController.cpp:95-104`）
→ `FolderNavigationService::openFolder` → `navigateTo` → `runAndCommit` → `mClient->getChildren`。
コールバックは `invokeOnGuiThread` で GUI スレッドへ post されるので、`applyResult` 自体は
次のイベントループ周回で走る。つまり**形は非同期、中身が同期**という状態。

なお「フォルダを開いたときのバックグラウンド更新」（`syncWithServer` → `syncPendingChanges`）は
自動では走らない。`refresh()` は F5 とツールバーの更新ボタン経由のみ（`qml/Main.qml:153`,
`qml/components/AddressToolBar.qml:112`）なので、今回の固まりとは無関係。

## 2. UI 側の受け皿はほぼ既にある

- `BusyState`（`src/qml/BusyState.h`）— 件数カウンタ + スピナー表示までの遅延タイマー。
- `TabsController` の `BusyRole`（`src/qml/TabsController.cpp:62,76,241`）が
  `navigation->busy()` を各タブへ配る。
- `qml/components/TabStrip.qml:355-366` — busy のときアイコンを隠して `BusyIndicator` を回す。

つまり「タブのぐるぐる」は**すでに動くものが存在する**。ただし `openFolder` が
`mBusy->begin()/end()` を呼んでいないので、現状これが回るのは `refresh()` のときだけ
（`FolderNavigationController.cpp:362,365`）。

不足しているのは内容領域の「取得中」表示だけで、`qml/views/TabContentPane.qml` に
空状態のプレースホルダが無い。

## 3. 変更が必要な箇所

### 3.1 lock-free 前提の崩壊（最大の作業）

`listChildren` をワーカースレッドへ出すと、`FolderNavigationService` の `mCurrent` /
`mBackStack` が SDK スレッド側から触られ、GUI スレッドの `currentLocation()` /
`canGoBack()` / `resolveCurrentPath()` と競合する。

同じ同期前提に乗っている他の呼び出し元:

- `FolderTreeService`（`src/core/FolderTreeService.cpp:30,32`）— ツリーの `hasChildren` /
  `fetchMore` 系がその場で答える必要があり、コールバックを置く場所が無い。
- `search` / `getPath` / `getNodeInfo` — 同じ契約に含まれる。

→ **`getChildren`/`getRootChildren` だけを非同期化し、残りは同期のまま据え置く**という
切り分けが現実的。`IMegaClient.h` の契約コメントも、非同期になったものだけ移す形に更新する。
`FolderTreeService` は同期の別経路（あるいは同期版メソッド）を使い続ける必要がある。

### 3.2 「即座に移動」= 楽観コミット

`FolderNavigationService::runAndCommit`（`src/core/FolderNavigationService.cpp:8-18`）は
位置の確定（`mBackStack.push_back` + `mCurrent` 更新）を**成功コールバックの中**でやっている。
同期実行だから結果的に即時なだけで、非同期化するとむしろ「取得が終わるまでパンくずが
変わらない」という逆方向の症状になる。

→ 先にコミットし、失敗したらロールバックする形へ反転させる。`runAndCommit` の名前と
シグネチャごと変わる。

### 3.3 レース対策（現状は存在しない）

非同期化すると、連打・戻る操作・ツリークリックで複数の取得が同時に飛ぶ。古い結果が後着して
新しい画面を上書きする経路ができるので、世代カウンタ（navigation token）を持って古い結果を
捨てる必要がある。同期だった今までは原理的に起こり得なかったので、この仕組みは無い。

### 3.4 付随

- `MegaSdkClient::shutdown()` の停止点に、飛んでいるワーカーの join を追加。
- テストのフェイククライアントが同期返却前提なので書き換え。
- `openFolder` に `mBusy->begin()/end()` を追加し、モデルを即クリア。
- `TabContentPane.qml` に「取得中」プレースホルダ。

## 4. 着手前に計測すべきこと

固まり時間の内訳は 2 つある:

1. `mApi->getChildren()` + `nodeListToEntries()` — ワーカースレッドへ出せる。
2. `applyResult` 以降 — `FileListModel::setEntries` の `beginResetModel`/`endResetModel`
   （`src/qml/FileListModel.cpp:84-90`）と、それに続く QML デリゲート生成。
   **GUI スレッドからは動かせない。**

数千件のフォルダで体感の固まりが 2 主体だった場合、本改修だけでは解決しない
（その場合は別軸 — デリゲートの遅延生成やモデルの差分更新 — の話になる）。

→ `openFolder` の 1 区間と 2 区間に `QElapsedTimer` を入れて内訳を出すのを先にやる。半日程度。

## 5. 推奨する順序

1. **計測** — §4 の内訳を取る。ここで 2 が支配的なら計画を組み直す。
2. **本体** — `getChildren`/`getRootChildren` のみワーカー化（§3.1）+ 楽観コミット（§3.2）
   + 世代トークン（§3.3）。
3. **表示** — `mBusy` 連携とプレースホルダ（§3.4）。2 と同時に入れないと、GUI が固まっている
   間はスピナー自体が回らないので、単独では意味がない。

規模: C++ 5〜6 ファイル（`MegaSdkClient`, `IMegaClient`, `FolderNavigationService`,
`FolderNavigationController`, テスト）+ QML 2 ファイル。

## 6. Phase 16 との関係

次に予定されている Phase 16（リモート変更のリアルタイム反映）は、同じ層
（`MegaSdkClient` のコールバック配送と `FolderNavigationController` の再読み込み経路）を
触る。特に §3.3 の世代トークンは、外部起因の再読み込みが割り込む Phase 16 でも同じものが
必要になるため、まとめて 1 フェーズとして扱う選択肢がある。
