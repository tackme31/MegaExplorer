# MEGA Explorer-like Desktop Client

Windowsエクスプローラー風のUIを持つ、MEGA向けの独自デスクトップクライアント。
公式デスクトップアプリ(MEGAsync)にはないサムネイル表示・検索・ファイル起動を実装する。

具体的な設計・実装はClaude Codeとの対話の中で詰めていく前提とし、本READMEでは**機能の列挙**と**開発ロードマップ**を主題とする。

## 目的・スコープ

- 公式MEGAデスクトップアプリの不足機能(サムネイル、検索、ダブルクリックで開く)を補う独自クライアント
- 常時バックグラウンド監視・双方向同期は行わない。フォルダを開いた際に、その場でバックグラウンド更新を1回走らせる方式とする
- ローカルフォルダとの双方向ミラーリング(公式アプリ相当のフル同期)は対象外。当面の検討対象からも外す
- 対象OS: Windows(将来的にLinux/macOSへの展開余地は残すが、第一段階はWindows専用でよい)

## 使用SDK・ライブラリ

| レイヤー | 採用 | 備考 |
|---|---|---|
| MEGAアクセス | **MEGA C++ SDK** (`meganz/sdk`) | BSD-2-Clauseライセンス。要app key(mega.io/developers) |
| UIフレームワーク | **Qt 6 (Qt Quick / QML)** | Qt Quick Controls 2 でエクスプローラー風UIを構築 |
| ビルドシステム | **CMake + vcpkg** | MEGA SDK公式ビルド手順に準拠 |
| サムネイル/プレビュー生成 | **FreeImage** または **Qt自前デコード**、動画は**FFmpeg** | サーバー側にサムネイルが無い既存ファイル用 |
| ローカルキャッシュ | **SQLite** | ノードツリー・サムネイルキャッシュの永続化 |

## 機能一覧

1. **ファイル一覧・階層表示** — MEGA上のフォルダ構造をエクスプローラー風に表示
2. **サムネイル表示** — 画像・動画・各種ファイルのサムネイル/プレビューをグリッド表示
3. **検索** — ファイル名等によるインクリメンタル検索。MVPでは画面上部の検索ボックスで開いているフォルダ配下を再帰検索、将来的に検索ボックス脇の詳細フィルタボタンからカテゴリ/日時/お気に入り等・アカウント全体検索を追加
4. **タップでファイルを開く** — Downloadsフォルダにダウンロードし、既定アプリで開く
5. **アップロード** — ドラッグ&ドロップ等によるファイルアップロード
6. **フォルダオープン時のバックグラウンド更新** — フォルダを開いたタイミングで1回だけバックグラウンド更新(常時監視・手動更新ボタンなし)
7. **【将来拡張】リモート変更のリアルタイム反映** — 他デバイスでの変更をSDKのプッシュ通知経由で受け取り、開いている一覧に自動反映(MVP完成後に追加。ローカルとの双方向ミラーリングとは別物)

## ライセンス・利用規約に関する留意点

- MEGA SDK本体(`meganz/sdk`)は **BSD 2-Clause**。組み込み・改変・再配布は同ライセンス条件を満たせば可能
- MEGAsync本体のソース(`meganz/MEGAsync`)は別の制限的ライセンス(Code Review Licence)であり、参考にする場合もコピー&ペーストせず、あくまでSDKの使い方の参考程度に留める
- MEGAの利用規約(ToS)に従う必要があり、appKeyの取得・レート制限順守が必須

## 既知の技術的懸念事項

- **APIレート制限**: サムネイル一括取得・検索を並列化しすぎるとAPIエラーになりうる
- **サムネイル未生成ファイルの処理コスト**: DL→デコード→リサイズのパイプライン設計が必要
- **大規模アカウントの初回fetchNodes**: ノード数が多い場合の初回ロード時間・メモリ使用量は要検証

## 要検討機能
- カラム幅を記録しておく
- テーブルの幅を横いっぱいにする

## ロードマップ(ボトムアップ開発)

各フェーズは「その時点で単体で動作確認できる状態」を目指し、下位レイヤーから積み上げる。MVP(フェーズ0〜6)を安定させてから、フェーズ7以降の拡張に着手する。
サムネイル/プレビューはMVP必須ではない(無くてもアプリとして使える)と判断し、優先度を下げて検索・DL/アップロードより後ろに移動した(2026-07-24)。

1. **フェーズ0**: MEGA SDKのビルド確認、ログイン・fetchNodesのCLI疎通確認 ← 完了
2. **フェーズ1**: 最小限のファイル一覧表示(サムネイル無し、ルート直下のみ) ← 完了
3. **フェーズ2**: フォルダナビゲーション — サブフォルダのダブルクリックで中に入れるようにする(`FolderNavigationService`/`FolderNavigationController`、戻るボタン付き) ← 完了
4. **フェーズ3**: 検索機能 — MVPスコープ決定(2026-07-25)。画面上部に検索ボックスを配置し、デフォルトは開いているフォルダ配下を再帰検索(`MegaApi::search()` + `MegaSearchFilter::byName`+`byLocationHandle`)。検索ボックスの脇に詳細フィルタボタンを配置し、押すとアカウント全体検索(`byLocation(SEARCH_TARGET_ALL)`)やカテゴリ・日時・お気に入り等の詳細条件を指定できるようにする(後回し) ← 完了
5. **フェーズ4**: ダウンロード→開く — MVPスコープ決定(2026-07-25、ハイブリッド案)。ダブルクリック(ファイル)とコンテキストメニュー「ダウンロード」のどちらから始めても同じDLフローに合流させる。DL開始と同時に進捗表示、完了時はスナックバー通知に「開く」ボタンを出し、押されるまで自動では既定アプリを起動しない(検索のEnter確定と同じく「意図しないタイミングで何かが起動する」ことを避ける方針)。保存先は当初tempフォルダの予定だったが、実装中にDownloadsフォルダ直下(ブラウザのダウンロードと同じ振る舞い)へ変更(2026-07-25)。形式別のアプリ内プレビュー(`getPreview`/`startStreaming`)・アップロードは今回のスコープ外 ← 完了
6. **フェーズ5**: サムネイル取得・グリッド表示(旧フェーズ2。優先度を下げて検索・DL/アップロードの後ろに移動)。スコープ確定(2026-07-25、ハイブリッド案): MEGAサーバー側に生成済みのサムネイルのみ取得(`MegaNode::hasThumbnail()`+`MegaApi::getThumbnail()`)、未生成ファイルはローカル生成せず拡張子ベースの汎用アイコンで代替(FreeImage/FFmpegによるローカル生成は別フェーズへ先送り)。取得結果はメモリ上セッションキャッシュのみ(永続化はフェーズ6のSQLiteまで先送り)。リスト/グリッド表示切り替えは`QtCore`の`Settings`型(`Qt.labs.settings`は非推奨のため不採用)で永続化 ← 完了
7. **フェーズ6**: ローカルキャッシュ、フォルダオープン時のバックグラウンド更新、エラーハンドリング全般 ← **ここまででMVP**。
   このうち「エラーハンドリング全般」は**フェーズ6a**として先行着手・完了(2026-07-28)。
   `QLoggingCategory`+`qInstallMessageHandler`によるカテゴリ別ログのファイル出力(MEGA SDK自体の
   `MegaLogger`フックも統合)、および従来UIに何も出なかったフォルダ移動・検索・サムネイル取得の
   失敗+ダウンロードの「開く」失敗に汎用エラートーストを追加。ローカルキャッシュ・バックグラウンド
   更新は未着手
8. **フェーズ6b**: 一覧表示時のファイル属性表示(サイズ・更新日時)+ソート機能。フェーズ6aと同様、
   フェーズ6本体(ローカルキャッシュ・バックグラウンド更新)より前に先行着手する差し込み枠 ← **完了
   (2026-07-28)**。
   仕様決定(2026-07-28):
   - リスト表示はWindowsエクスプローラーの詳細表示相当に変更 — カラムヘッダーを追加し、ヘッダークリック
     でソート(再クリックで昇順/降順トグル)
   - 表示カラムは以下の3つ(2026-07-28、種類列は下記の理由で不採用に変更):
     - ファイル名(拡張子含む) — `MegaNode::getName()`、`FileEntry::name`
     - 更新日時(ローカル日時表示) — `MegaNode::getModificationTime()`、`FileEntry::modificationTime`/
       `FileListModel::ModificationTimeRole`。Unix秒なのでQML側は`new Date(t * 1000)`
     - ファイルサイズ(人が読みやすい単位に整形。KB/MB等) — `MegaNode::getSize()`、
       `FileEntry::sizeBytes`/`FileListModel::FormattedSizeRole`(`QLocale::formattedDataSize`で整形)
     - **種類列は不採用**: 当初4列目として検討したが、ソートをSDK委譲する方針(下記)と衝突すると判明
       (`MegaApi::getChildren`/`search`の`order`に拡張子ベースの種類に対応する値が存在しない)。
       種類列専用に用意していた`FileKind.h/.cpp`(`fileExtensionUppercased`)と
       `FileListModel::ExtensionRole`は削除
   - ソート時はソートキーによらずフォルダを常に先頭に固定 — SDK自身のドキュメントに "the nodes are
     always sorted by type, being folders always first" と明記されており、クライアント側ロジック不要
     (2026-07-28確認)
   - グリッド(サムネイル)表示側は変更しない — 名前+サムネイルのみのまま
   - 検索結果一覧にも現在選択中のソート列/方向を適用する(`IMegaClient::search`にも`order`を渡す)
   ソートはアプリ側(メモリ上)ではなくMEGA SDKの`MegaApi::getChildren`/`search`の`order`引数で行う方針
   (2026-07-28調査・実装) — 1フォルダ数万ファイル規模でのパフォーマンスを考慮。`IMegaClient`/
   `MegaSdkClient`(`src/core`/`src/mega`)に`SortOrder`(新規`src/core/SortOrder.h`、`SortKey::
   {Name,Size,ModificationTime}` + `ascending`)を渡す引数を追加し、`MegaSdkClient`側で
   `MegaApi::ORDER_DEFAULT_ASC/DESC`・`ORDER_SIZE_ASC/DESC`・`ORDER_MODIFICATION_ASC/DESC`に変換して
   渡す。`FolderNavigationController::setSortOrder(column, ascending)`がヘッダークリックの入口で、
   検索中かどうかで検索結果の再取得/現在フォルダの再取得(`FolderNavigationService::refreshCurrent`、
   back-stackは変更しない)を使い分ける。ソート列/方向は`FileTableView.qml`側で`Settings`(QtCore)に
   永続化。

   テーブル実装方式も決定(2026-07-28、Qt公式ドキュメント調査済み): **`TableView`+`HorizontalHeaderView`
   への移行**(`import QtQuick.Controls`、`HorizontalHeaderView`はQt Quick Controls所属)。
   - 現状の一覧表示は`ListView`+`QAbstractListModel`(`FileListModel`、1行=1ファイルでロールが列相当の
     単一カラムモデル)。`TableView`は行×列の2次元モデルが前提のため、`FileListModel`を
     `QAbstractTableModel`ベースに書き換え、`columnCount()`を3返すようにして`data(index, role)`が
     `index.column()`で出し分ける形にする変更が必要(グリッド表示側は`ListView`のままでよく、
     `FileListModel`を両ビューで共用するなら列に依存しない形を保つか、ビュー別にラッパーを挟むかは
     実装時に要検討)
   - カラムのドラッグリサイズは`TableView.resizableColumns: true`(Qt 6.5〜)で標準サポートあり、
     `HorizontalHeaderView`はデフォルトで`resizableColumns: true`。列の並び替え(`movableColumns`)も
     Qt 6.8〜標準サポート(今回は使う予定なし)
   - **ヘッダーの幅をアプリ終了後も維持する**(2026-07-28追加)。ソート列/方向と同様に`FileTableView.qml`
     側で`Settings`(QtCore)へ各列幅を保存し、起動時に復元する想定
   - **ヘッダー境界のダブルクリックでコンテンツ幅へ自動調整する挙動**は上記の幅維持より優先度を下げる
     (2026-07-28)。Qt標準コンポーネントに組み込みでは無い(ドキュメント確認済み、2026-07-28)。自前実装が
     必要だが材料はQtが提供している: `TableView.implicitColumnWidth(column)`(現在ロード済みの行の中で
     最大implicitWidthを返す)と`TableView.setColumnWidth(column, size)`を、ヘッダー境界の
     `TapHandler.onDoubleTapped`(`Main.qml`の他のダブルタップ処理と同じパターン)から呼べば実現可能。
     ただし`implicitColumnWidth()`は現在ビューにロードされている行のみを見る制約があり、スクロール位置で
     結果が多少ブレる(数万件フォルダでは特に)。フォルダ全件を見て厳密に幅を決めたい場合は、モデルの
     文字列長からフォントメトリクスで自前計算する必要がある(文字列測定なのでサムネイル取得ほど重くはない)
9. **フェーズ7(将来拡張)**: リモート変更のリアルタイム反映(機能一覧 7. を参照)。フォルダオープン時更新の上に追加するだけなので、既存構造への影響は小さい
10. **フェーズ8以降(未確定)**: 必要になれば都度検討。現時点ではローカルとの双方向フル同期は対象外
