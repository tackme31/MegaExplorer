# 調査メモ: ログイン〜fetchNodes中の進捗表示は実現できるか（2026-07-30）

ログイン直後、ファイル一覧画面に遷移するまでのローディングが（特にノード数の多いアカウントで）
かなり長くなるケースがある。現状ローディング表示自体が未実装なので、いずれ実装する必要があり、
その際に進捗（%表示など）を出せるかを調べた記録。**コード変更は未実施（調査のみ）**。

> **2026-08-04 追記あり。** ローディング画面に「今なにをしているか」を出す方針で再調査した結果、
> 下記「重要な限界」節の前提（ダウンロードとパースが順番に走る）は SDK 10.17 では成り立たない
> ことが判明した。段階の切り分けに使える公開 API も追加で見つかっている。
> [追記（2026-08-04）](#追記2026-08-04-段階表示のための再調査) 以降を先に読むこと。

## 結論

- `fetchNodes` 専用の進捗コールバックは SDK に存在する（`MegaRequestListener::onRequestUpdate`）。
  現状のコードでは未配線。
- ただしこの進捗は**「fetchNodes APIレスポンス本体のHTTPダウンロード済みバイト数 / 総バイト数」**
  であり、「処理済みノード数 / 総ノード数」ではない。ダウンロード完了後に行われるノードツリー構築
  （復号・パース）フェーズには進捗コールバックが一切なく、SDKの公開APIではそこを細かく可視化する
  手段がない。
- 実際に手元のログ（60万ノード規模のアカウント）を追ったところ、ローディングが長時間止まって見え
  る箇所は、まさにこの「進捗コールバックが飛んでくるはずの `f` コマンド本体」の待ちだった（詳細は
  下記「ログ調査」）。つまり実装すれば体感改善が見込める可能性はあるが、ダウンロードとパースのど
  ちらが支配的なボトルネックかまでは今回の調査では切り分けられていない（SDKログを`VERBOSE`まで上
  げれば診断できる、下記参照）。

## 進捗API: `onRequestUpdate`

```cpp
// third_party/sdk/include/megaapi.h:9274 (MegaRequestListener)
// 同一シグネチャが MegaListener 側にも重複定義: megaapi.h:9883
virtual void onRequestUpdate(MegaApi* api, MegaRequest* request);
// ドキュメント (megaapi.h:9261):
//   "Currently, this callback is only used for fetchNodes
//    (MegaRequest::TYPE_FETCH_NODES) requests"
// -> request->getTransferredBytes() / request->getTotalBytes() で取得
```

発火元を `third_party/sdk/src/megaapi_impl.cpp` / `megaclient.cpp` で追うと:

```cpp
// megaapi_impl.cpp:16138
void MegaApiImpl::request_response_progress(m_off_t currentProgress, m_off_t totalProgress)
{
    LOG_verbose << "Request response progress: current progress = " << currentProgress
                << ", total progress = " << totalProgress;
    if (!client->isFetchingNodesPendingCS()) return;
    for (...) // requestMap を走査
    {
        if (request->getType() == MegaRequest::TYPE_FETCH_NODES)
        {
            request->setTransferredBytes(currentProgress);
            if (totalProgress != -1) request->setTotalBytes(totalProgress);
            fireOnRequestUpdate(request);
        }
    }
}
```

呼び出し元（`megaclient.cpp:2697`, `2705` など）は `pendingcs->bufpos` /
`pendingcs->contentlength`。すなわち **`f`（fetchnodes）APIレスポンス本体のHTTPダウンロード進捗**
（Web版が出しているプログレスバーと同種のもの）。

## 現状の実装（未配線）

- `src/mega/MegaSdkClient.cpp:39` の `SimpleResultListener`（`fetchNodes()` もこれ経由、
  `MegaSdkClient.cpp:250`）は `onRequestFinish` のみをオーバーライドしており、`onRequestUpdate` は
  実装していない。
- `src/core/IMegaClient.h:53` の `fetchNodes(std::function<void(Result<void>)> onDone)` も完了コー
  ルバックのみで、進捗コールバックの受け皿がない。
- 配線するなら: `SimpleResultListener`（または新規リスナー）に `onRequestUpdate` を実装 →
  `getTransferredBytes()`/`getTotalBytes()` を進捗コールバックとして `IMegaClient::fetchNodes` の
  シグネチャに追加 → `AuthService`/`AuthController` 経由でQML側に% として上げる、という流れになる
  見込み。既存の完了コールバックの型を変えずに追加パラメータで済むはずで、実装自体の難易度はそこ
  まで高くなさそう。

## 重要な限界: カバーしているのは「ダウンロード区間」だけ

> **この節の結論は 2026-08-04 の再調査で覆っている**（`f` はチャンク処理され、パースはダウンロード
> と並行して進む）。以下は当時の記録としてそのまま残す。
> → [追記（2026-08-04）](#追記2026-08-04-段階表示のための再調査)

ダウンロード完了後、`third_party/sdk/src/megaclient.cpp:10561` の `readnodes()` がレスポンス
JSONを一括パースしてノードツリー（ノードキーの復号含む）をメモリ上に構築する処理が走るが、この
区間には**進捗コールバックが一切ない**（1回のループで全ノードを処理し、アプリ側への途中経過通知
はしない）。60万ノード規模だとこのパース/ツリー構築フェーズも無視できない時間がかかっている可能
性があり、その間は `onRequestUpdate` の進捗が100%付近で張り付いて見える
（＝`onRequestFinish` が来るまで実際には終わっていない）はずである。

- ダウンロードが支配的なボトルネックなら → バイト進捗バーはそこそこ意味のある体感改善になる。
- パース/ツリー構築が支配的なボトルネックなら → 進捗が途中で止まる「見せかけ」の表示になる。

ノード件数ベースの、より粒度の細かい進捗APIはSDKの公開ヘッダには見当たらなかった。

## ログ調査: 実際にどこで止まっていたか

ユーザーから提供された、60万ノード規模アカウントでのログイン時ログ（`megaexplorer.sdk`
カテゴリ、`LOG_LEVEL_INFO`）:

```
[info]    ...Request (FETCH_NODES) starting                       (megaapi_impl.cpp:17968)
[warning] ...[MegaClient::handleSetThrottleResult] Ignoring...     (megaclient.cpp:18197)
[info]    ...File versioning is enabled                            (commands.cpp:5153)
[info]    ...CallKit is enabled [noCallKit.size() == 0]             (commands.cpp:5167)
[info]    ...S4 has been disabled                                   (commands.cpp:5443)
[info]    ...Keypairs and signatures loaded successfully            (megaclient.cpp:16525)
```
ここで数分単位で止まる。

`third_party/sdk/src/megaclient.cpp` の `MegaClient::fetchnodes()`（15990行〜）を実際の呼び出し順
に対応させると:

1. `Request (FETCH_NODES) starting` — `MegaApiImpl` レベルで `fetchNodes()` が呼ばれた合図
   （`AuthService.cpp:97` からの呼び出し）。まだネットワークは動いていない。
2. throttle警告 / File versioning / CallKit / S4 のログ — `fetchnodes()` がまず送る
   **`ug`（ユーザーデータ取得）コマンド**のレスポンス処理（`megaclient.cpp:16249`
   `getuserdata(0, getUserDataCompletion)`、レスポンス解釈は`commands.cpp`側）。まだノードツリー
   本体とは無関係。
3. `Keypairs and signatures loaded successfully`（`megaclient.cpp:16525`） — `ug` 完了後の
   `getUserDataCompletion` ラムダ（`megaclient.cpp:16214`）が `initializekeys()`
   （`megaclient.cpp:16235`）を実行し終えた合図。

**重要な点**: `getUserDataCompletion` はこのログの直後、同じ関数内で同期的に
`startFetchNodes(API_OK)` を呼び、そこで実際の `CommandFetchNodes`（＝ノードツリー本体を取得する
`f` コマンド、`megaclient.cpp:16172`）を queue している（`startFetchNodes` は
`megaclient.cpp:16154`）。非同期の隙間はなく、次にINFOレベルのログが出るとすれば基本的に
`fetchnodes_result()`（`f` コマンド完了後）まで何も出ない。

つまり **ログが `Keypairs and signatures loaded successfully` で止まっている = ちょうど本題の
「`f` コマンド（ノードツリー本体のダウンロード＋パース）」を投げた直後で待っている状態**。それ以
前の `ug` 取得・鍵初期化は前置きの軽い処理（実測でも1〜2秒程度で完了）で、体感で長く感じている待
ち時間の正体はこの後の `f` コマンドである。

### なぜそれ以降ログが一切出ないのか

`onRequestUpdate` の元になる `request_response_progress` は `LOG_verbose` で出力される
（`megaapi_impl.cpp:16140`）。一方 `src/app/Logging.cpp:15` のコメントにある通り
`MegaApi::setLogLevel` はデフォルトの `LOG_LEVEL_INFO` のまま引き上げていない
（設定箇所は本プロジェクトのどこにも見当たらなかった）。そのためSDK側でVERBOSE/DEBUGログはそもそ
も生成されておらず（Qt側のフィルタ以前の話）、`f` コマンドの送受信中は完了するまで文字通り無音に
なる。

診断目的で一時的に `MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_VERBOSE)` を上げれば
`request_response_progress` のバイト進捗ログが見えるようになり、「今の待ちがダウンロード律速なの
かパース律速なのか」を切り分けられる（今回は調査のみの依頼だったため未実施）。

## 今後実装する場合の見積もり（メモ）

- 配線コスト自体は小さい: `onRequestUpdate` を実装したリスナーを `fetchNodes()` に渡し、
  `getTransferredBytes()/getTotalBytes()` を `IMegaClient` 経由で上に伝搬するだけ。
- ただし「バイト進捗」を出しても、パースフェーズで進捗が張り付く可能性があるため、UI文言は
  「XX% 読み込み中」のような厳密な数値表示より、「読み込み中... (概算)」+ 進捗バー、くらいの緩い
  見せ方にしておく方が誤解を招きにくいかもしれない。
- パースフェーズ自体の進捗をSDK側から取る手段は見当たらないため、そこは素直に不確定ローディング
  （スピナー等）で埋めるしかなさそう。
- 実装前に、上記VERBOSEログでダウンロード/パースの時間配分を実測しておくと、進捗バーがどこまで
  「意味のある」表示になるか判断しやすい。

## 追記（2026-08-04）: 段階表示のための再調査

「%を出せるか」ではなく **「今なにをしているかを文言で出せるか」**（`ログイン中` / `ファイル取得中`
など）を主眼に再調査した。結論から言うと、**公開 API だけでログイン〜Cloud Drive 表示を 4 段階に
割れる**。以下すべて実機未検証（コードリーディングのみ）、コード変更も未実施。

### 段階と検出手段

| # | 区間 | 検出手段（公開 API） | 進捗値 |
|---|---|---|---|
| 1 | 認証（`us0` prelogin → `us` → 鍵導出 PBKDF2） | 自前（`AuthService::login` 呼び出し〜そのコールバック） | なし |
| 2 | アカウント情報取得（`ug` + `initializekeys`） | fetchNodes 開始〜`EVENT_MISC_FLAGS_READY` | なし（実測 1〜2 秒） |
| 3 | ノードツリー取得（`f` コマンド） | `onRequestUpdate` のバイト進捗 + `getNumNodes()` の件数 | **あり** |
| 4 | 仕上げ（キー適用・DB コミット） | `onRequestFinish` まで | なし |
| (5) | `sc` 追いつき完了 | `EVENT_NODES_CURRENT` | なし（fetchNodes 完了後） |

2→3 の境界が取れるのが今回の新発見。`ug` のレスポンス処理 `MegaApiImpl::userdata_result`
（`megaapi_impl.cpp:16331`）が `EVENT_MISC_FLAGS_READY` を発火する。ただし同イベントは `gmf`
完了でも飛ぶ（`megaapi_impl.cpp:14609`）ので、「今 fetchNodes 実行中か」でゲートする必要がある。

なお 1 の `TYPE_LOGIN` には進捗コールバックが存在しない（`onRequestUpdate` は仕様上 fetchNodes
専用、`megaapi.h:9261`）ので、ここは 1 メッセージ固定。

### 訂正: SDK 10.17 では `f` はチャンク処理される

`megaclient.cpp:2992` に
`// Currently only fetchnodes requests can take advantage of chunked processing` とあり、
**fetchnodes のレスポンスに限り** `pendingcs->mChunked = true` になる（`megaclient.cpp:2994`）。
チャンクは受信のたびに `RequestDispatcher::serverChunk`（`request.cpp:538`、実体は
`Request::serverChunk` `request.cpp:180`）が逐次 JSON パースして `mJsonSplitter` に流す。

つまり **ダウンロードとノードツリー構築は並行して進む**。上の「重要な限界」節が想定していた
「ダウンロード完了 → 無進捗のパース区間が丸ごと残る（＝100%で張り付く）」という形にはならず、
残るのは末尾の仕上げ（キー適用・DB コミット）分だけになる。バイト進捗は当初の想定より素直に
「全体の進み具合」の代理になっている可能性が高い。**ただし末尾の仕上げが何秒かかるかは未実測**で、
そこが支配的なら結局張り付いて見えるため、実測が必要（後述）。

### 件数表示: `MegaApi::getNumNodes()` は poll してよい

チャンク処理の副産物として、**読み込み済みノード件数がロードの途中で増えていくのが観測できる**。

- `MegaApiImpl::getNumNodes()`（`megaapi_impl.cpp:27509`）の実体は
  `client->totalNodes.load()` だけ。`std::atomic_ullong`（`megaclient.h:2379`）なので
  **SdkMutex を取らない** → GUI スレッドから `QTimer` で叩いてもブロックしない。
- 値は `notifypurge()` の末尾で更新され（`megaclient.cpp:8960`）、`notifypurge()` は
  `MegaClient::exec()` のメインループから毎周回呼ばれる（`megaclient.cpp:3537`）。
  チャンクを消化しながら回っている間ずっと更新される。

総数が事前に分からないので % にはできないが、「123,456 件のファイルを読み込みました」という
*生きている感*の表示には向く。バイト % との併用が現実的。

### 使えなかった: `EVENT_REQSTAT_PROGRESS`

per-mille（0〜1000）の進捗を返す `EVENT_REQSTAT_PROGRESS`（`megaapi.h:6517` / 説明は `:6637`、
`MegaApi::enableRequestStatusMonitor(true)` `megaapi.h:24599` で有効化）は名前だけ見ると使えそうだ
が、中身は **サーバー側で走っている長時間ロック操作** の進捗である。`procreqstat()`
（`megaclient.cpp:5825`）が `reqstat` エンドポイントのバイナリを読み、operation の識別子は
生の API コマンド頭文字（`p` putnodes / `d` delete / `m` move、`formatReqstatOpcode`
`megaclient.cpp:5808`）。fetchnodes の進捗ではない。

ただし「別デバイスで大量移動中のためログインが待たされている」ケースをそのまま説明できるので、
**例外時メッセージ**（例: `サーバー側で処理中… 45%`）としては価値がある。採用するなら
`enableRequestStatusMonitor(true)` の明示呼び出しが要る（既定は無効）。

### 実装時の注意（未確認事項）

- `onRequestUpdate` の発火は `pendingcs->contentlength > 0` が前提（`megaclient.cpp:2672`）。
  MEGA API は gzip 時に `Original-Content-Length` ヘッダで非圧縮長を返す（`posix/net.cpp:1857`、
  同 `:1812` のコメント）ので取れる見込みだが、**取れなければ進捗イベントが 1 回も来ない**。
  「% が来ない前提」のフォールバック（不定スピナー＋件数表示）を必ず持たせること。
- `MegaEvent` 系は現状まったく受け取っていない。`MegaSdkClient` に `mApi->addListener()`
  （または `addGlobalListener()`）の配線が新規に必要。`MegaGlobalListener`/`MegaListener` の
  どちらにも `onEvent` がある（`megaapi.h:9803`, `:10305`）。
- コールバックはすべて SDK スレッド。既存の `IMegaClient` の作法どおり GUI スレッドへ移すこと。
- UI 側の現状: `LoginView.qml` はローディング表示を `authState === Restoring`（セッション復元）に
  しか出していない。フォーム submit 後の `LoggingIn` / `VerifyingTwoFactor` では
  **入力欄が disabled になるだけで何も出ない**。ここが今回埋めたい穴。

### 別件（ローディング時間そのものに効く）: SDK の状態キャッシュ DB が CWD に作られている

`main.cpp:63` は `MegaSdkClient()` をデフォルト引数で構築しており、`basePath` は `"."`
（`MegaSdkClient.h:21`）。SDK は basePath に **状態キャッシュ DB を必ず作る**
（`megaapi_impl.cpp:7113`、`LocalPath::fromAbsolutePath(basePath)`）。

この DB が有効なら 2 回目以降の fetchNodes は `f` を丸ごとスキップしてローカル DB から読む
（`megaclient.cpp:16048` の `MODE_DB`、`:16058` の `"Session loaded from local cache"`）。
逆に言うと現状は **起動方法によって CWD が変わるたびにキャッシュが外れ、毎回フルの `f` を引いて
いる可能性がある**（Qt Creator からの起動と exe 直叩きで CWD が異なる。相対パス `"."` を
`fromAbsolutePath` に渡している点も怪しい）。`session.dat` と同じ `AppLocalDataLocation` 配下に
固定すれば、多くの場合ログインの待ち時間自体が消える。

ローディング UI の設計上も重要で、**DB ヒット時はバイト進捗が一切来ない**（段階 3 が存在しない）。
段階表示はこの分岐を前提に組む必要がある。

### 次アクション

1. 60万ノード規模のアカウントで、`MegaApi::setLogLevel(LOG_LEVEL_VERBOSE)` を一時的に有効にして
   実測する。見たいのは (a) `request_response_progress` が実際に飛ぶか＝`Original-Content-Length`
   が来ているか、(b) 最後のバイト進捗から `onRequestFinish` までの「仕上げ」に何秒かかるか、
   (c) `getNumNodes()` がロード中に増えるか、(d) `MODE_DB` に入っているかどうか。
2. その結果を見てから、進捗を % で出すか件数だけにするかを決める。
3. `basePath` の修正は上とは独立に実施してよい。

---

## 参考ファイル:行

| 内容 | 場所 |
|---|---|
| `onRequestUpdate` 宣言（`MegaRequestListener`） | `third_party/sdk/include/megaapi.h:9274` |
| `onRequestUpdate` 宣言（`MegaListener`） | `third_party/sdk/include/megaapi.h:9883` |
| `getTransferredBytes()`/`getTotalBytes()` | `third_party/sdk/include/megaapi.h:5951`, `5962` |
| `request_response_progress`（バイト進捗の発火元） | `third_party/sdk/src/megaapi_impl.cpp:16138` |
| 呼び出し元（HTTPダウンロード進捗） | `third_party/sdk/src/megaclient.cpp:2697`, `2705` |
| `MegaClient::fetchnodes()` 本体 | `third_party/sdk/src/megaclient.cpp:15990` |
| `ug` 完了後の鍵初期化・`f` 送出ラムダ | `third_party/sdk/src/megaclient.cpp:16214`（`getUserDataCompletion`）, `16154`（`startFetchNodes`） |
| ノードツリー構築（進捗コールバックなし） | `third_party/sdk/src/megaclient.cpp:10561`（`readnodes()`） |
| 現状の完了専用リスナー（未配線） | `src/mega/MegaSdkClient.cpp:39`（`SimpleResultListener`）, `:250`（`fetchNodes()`） |
| `IMegaClient::fetchNodes` シグネチャ | `src/core/IMegaClient.h:53` |
| SDKログレベルに関する既存コメント | `src/app/Logging.cpp:15` |

2026-08-04 の追記分:

| 内容 | 場所 |
|---|---|
| fetchnodes だけチャンク処理される旨のコメント / `mChunked` 設定 | `third_party/sdk/src/megaclient.cpp:2992`, `:2994` |
| チャンクの逐次 JSON パース | `third_party/sdk/src/request.cpp:538`（`RequestDispatcher::serverChunk`）, `:180`（`Request::serverChunk`） |
| バイト進捗の発火条件（`contentlength > 0`） | `third_party/sdk/src/megaclient.cpp:2672` |
| `Original-Content-Length` ヘッダの解釈 | `third_party/sdk/src/posix/net.cpp:1857`（コメントは `:1812`） |
| `ug` 完了 → `EVENT_MISC_FLAGS_READY` | `third_party/sdk/src/megaapi_impl.cpp:16331`（`gmf` 経由は `:14609`） |
| `EVENT_NODES_CURRENT` の発火 | `third_party/sdk/src/megaapi_impl.cpp:17220`、呼び出しは `megaclient.cpp:5467` |
| `getNumNodes()` の実体（atomic 読み） | `third_party/sdk/src/megaapi_impl.cpp:27509`、`include/mega/megaclient.h:2379` |
| `totalNodes` の更新箇所 / 毎周回の呼び出し | `third_party/sdk/src/megaclient.cpp:8960`（`notifypurge()` 末尾）, `:3537`（`exec()` から） |
| `EVENT_REQSTAT_PROGRESS`（サーバー側ロック操作の進捗） | `megaapi.h:6517`（enum）, `:6637`（説明）, `:24599`（有効化）, `megaclient.cpp:5825`（`procreqstat`）, `:5808`（opcode） |
| `onEvent` を持つリスナー | `third_party/sdk/include/megaapi.h:9803`（`MegaGlobalListener`）, `:10305`（`MegaListener`） |
| 状態キャッシュ DB の生成（`basePath`） | `third_party/sdk/src/megaapi_impl.cpp:7113`、渡し元は `main.cpp:63` / `src/mega/MegaSdkClient.h:21` |
| ローカル DB から読む分岐（`MODE_DB`） | `third_party/sdk/src/megaclient.cpp:16048`, `:16058` |
| ローディング表示が `Restoring` にしか出ていない | `qml/views/LoginView.qml:23`, `:52-60` |
