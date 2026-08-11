# 調査メモ: ログイン〜fetchNodes中の進捗表示は実現できるか（2026-07-30）

> **状態: Phase 18（ログイン中ローディング画面）として実装済み。** 60万ノードでの実測値は
> 追記2にあり、そこが本書の一番の資産。

ログイン直後、ファイル一覧画面に遷移するまでのローディングが（特にノード数の多いアカウントで）
かなり長くなるケースがある。現状ローディング表示自体が未実装なので、いずれ実装する必要があり、
その際に進捗（%表示など）を出せるかを調べた記録。**コード変更は未実施（調査のみ）**。

> **2026-08-04 追記あり。実測済み。** ローディング画面に「今なにをしているか」を出す方針で
> 再調査し、60万ノードのアカウントで実測した。**結論から言うと、下記「重要な限界」節の懸念
> （ダウンロード後に進捗のない区間が長く残る）は実測で裏付けられた**。再調査時にコードリー
> ディングだけで立てた「チャンク処理なので並行に進むはず」という反証は、実測では成り立って
> いない。
> 読む順序: [追記1（設計調査）](#追記12026-08-04-段階表示のための再調査) →
> [追記2（実測結果）](#追記22026-08-04-60万ノードでの実測結果) 。結論だけ必要なら追記2 だけでよい。

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

> **この節の懸念は 2026-08-04 の実測で裏付けられた。** 実測では「ダウンロード 162 秒 / その後
> 218 秒」で、進捗のない後半のほうが長かった。途中、追記1 でコードリーディングだけから
> 「チャンク処理なので並行のはず」と反証しかけたが、それは誤り。
> → [追記2](#追記22026-08-04-60万ノードでの実測結果)

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

## 追記1（2026-08-04）: 段階表示のための再調査

「%を出せるか」ではなく **「今なにをしているかを文言で出せるか」**（`ログイン中` / `ファイル取得中`
など）を主眼に再調査した。結論から言うと、**公開 API だけでログイン〜Cloud Drive 表示を 4 段階に
割れる**。以下すべて実機未検証（コードリーディングのみ）、コード変更も未実施。

> **この節の予測のうち 3 つは同日の実測で外れた**（tail は短くない / 件数は育たない /
> `EVENT_MISC_FLAGS_READY` は来ない）。当たったのは「バイト進捗は取れる」だけ。突き合わせ表は
> [追記2](#追記22026-08-04-60万ノードでの実測結果)。以下は当時の推論としてそのまま残す。

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

→ 1 は同日実施済み（追記2）。SDK ログは VERBOSE には上げず、`MegaSdkClient.cpp` に一時的な計測
リスナーを仕込んで INFO で測った。VERBOSE は 60万ノードでは JSON チャンクの中身まで出て測定自体
を歪めるため。計測コードは実測後に撤去済み（この文書の数値がその成果物）。

---

## 追記2（2026-08-04）: 60万ノードでの実測結果

追記1 の (a)〜(d) を実機で確認した。計測は `MegaSdkClient.cpp` に一時的に入れた
`FetchNodesProbe`（`onRequestUpdate` のバイト進捗、500ms ごとの `getNumNodes()` サンプリング、
完了時の tail 計測）と `EventProbe`（`addGlobalListener` で `MegaEvent` を全部ログ）による。
撤去済み。

**条件**: 639,472 ノードのアカウント、2FA あり、Debug ビルド、リポジトリルートから起動。
Run1 = サインアウト後の新規ログイン、Run2 = その直後に再起動してのセッション復元。

### Run1（新規ログイン）: fetchNodes 合計 **384.8 秒**

| 区間 | 経過 | 所要 | 割合 |
|---|---|---|---|
| `ug` + 鍵初期化 | 0 → 1.1s | 1.1s | 0.3% |
| 最初のバイト到達まで | → 4.1s | 3.0s | 0.8% |
| `f` ダウンロード | 4.1 → 166.5s | 162.4s | **42%** |
| 仕上げ（復号・ツリー構築・DB 書き込み） | 166.5 → 384.8s | **218.4s** | **57%** |

レスポンス総サイズ 192,187,977 B（183 MiB）、`onRequestUpdate` は 141 回。

### Run2（セッション復元、同一アカウント）: fetchNodes **619 ms**

`Session loaded from local cache. SCSN: ...`（`megaclient.cpp:16058`）の **MODE_DB 経路**。
`updates = 0`（バイト進捗は 1 回も来ない）、開始 30ms 後には既に 639,472 ノードが揃っている。

**385 秒 → 0.6 秒。** SDK の状態キャッシュ DB は劇的に効く。裏を返すと、6 分半かかるのは
**初回ログインとサインアウト後の再ログインだけ**。`MegaApi::logout` はこの DB を破棄するので、
**サインアウト 1 回のコストが 6 分半**という副次的な発見もある（UX 上の別件）。

### 予測（追記1）と実測の突き合わせ

| | 予測 | 実測 | |
|---|---|---|---|
| (a) | バイト進捗は取れる | 取れた。総サイズ既知、141 回 | ○ |
| (b) | チャンク処理なので tail は短いはず | **218 秒 = 全体の 57%**。バイト % は 166 秒で 100% に達したあと **3 分 38 秒** 動かない | ✗ |
| (c) | `getNumNodes()` が育つのが見える | ダウンロード中ずっと `0`。338 秒地点で `0 → 638,965` に一段でジャンプ | ✗ |
| (d) | `EVENT_MISC_FLAGS_READY` が `ug`→`f` 境界になる | 一度も来ない | ✗ |

- **(c) の理由**: `NodeManager::getNodeCount()`（`nodemanager.cpp:700`）は**ルートノードの
  `NodeCounter` を合計する**実装。ルートが確定する終盤まで構造的に 0 のままで、進捗指標には
  使えない。
- **(d) の理由**: fetchnodes 内の `getuserdata` は完了ラムダ経由で呼ばれ、
  `app->userdata_result()` を通らないためと思われる。代わりに `ug` 完了とほぼ同時刻（1109ms、
  `Keypairs and signatures loaded successfully` の 2ms 前）に `EVENT_STORAGE`(5) が来ている。
  ただしこの区間は 1.1 秒しかなく、専用の文言を出す価値自体がない。
- **`EVENT_REQSTAT_PROGRESS` はゼロ**。`reqstat` リクエスト自体が失敗していた
  （`Failed reqstat request. Retrying`、`megaclient.cpp:3418`）。この線は実質使えないので
  `enableRequestStatusMonitor` は無効のままでよい。
- **UI の生存性**: 500ms サンプラは最大ギャップ 510ms で回り続けた。218 秒の間もプロセスは
  固まらないので、アニメーションするローディング表示は問題なく成立する。

### 結論: 2 フェーズ表示（ダウンロードは確定バー、その後は不定）

Web 版 MEGA と同じ「ダウンロードの % を出し、その後は復号中の表示に切り替える」形にする。

| フェーズ | 表示 | 遷移条件 |
|---|---|---|
| 認証 | 「サインインしています…」+ 不定 | `login` 呼び出し〜そのコールバック |
| 準備 | 「アカウント情報を取得しています…」+ 不定 | fetchNodes 開始〜**最初の**進捗イベント（実測 4.1 秒） |
| ダウンロード | 「ファイル一覧をダウンロードしています」+ **確定バー** + `62 MB / 183 MB` | 進捗イベント受信中 |
| 復号 | 「受信したデータを復号しています。時間がかかることがあります」+ 不定 + **経過時間** | `transferred >= total` または無更新 N 秒 → `onRequestFinish` |

（**経過時間は Phase 18 で一度実装したあと、実機で触ったユーザーの判断で削除した**。
不定インジケータが動いていれば「生きている」ことは伝わるため。）

成立させるための条件が 3 つある。

1. **バーのラベルを「ダウンロード」に限定する**。全体の 42% を 100% と表示する以上、バーが
   100% に達しても作業は終わらない。ラベルが「読み込み中 100%」だとそこから 3 分 38 秒待たせる
   ことになり、今と同じ裏切りになる。何の進捗かを明示すれば 100% → 次フェーズの遷移は自然になる。
2. **42:57 という比率を固定値にしない**。ダウンロードは回線律速、復号は CPU 律速なので環境で
   動く。「バーを 0.42 倍にスケールして全体進捗に見せる」方向に踏み込むと破綻する。
3. **「ダウンロード完了」の検出にフォールバックを置く**。判定は `transferred >= total` だが、
   実測でログに残った最後の値は 99.44%（191,102,976 / 192,187,977）だった。SDK は `REQ_SUCCESS`
   時にもう一度 `bufpos/contentlength` を投げる（`megaclient.cpp:2705`）ので 141 回目が 100%
   だったはずだが、ログの間引きで確証がない。**一定時間 `onRequestUpdate` が来なければ次フェーズ
   へ進む**保険を入れれば、100% ちょうどが来なくてもバーが固まったままにはならない。

なお **MODE_DB では進捗イベントが一切来ない**（Run2、`updates = 0`）ので、「準備」フェーズのまま
一瞬で終わる。初回のみ遅いことが実測で裏付けられたので、復号フェーズに「初回のみ数分かかります。
次回以降は数秒で開きます」を添える根拠もある。

### 実装タスク

Phase 18 として `docs/PROGRESS.md` のロードマップに登録済み。やること一覧はそちら。

**2026-08-04 追記: Phase 18 として実装済み**（`docs/PROGRESS.md` の実装ログ参照）。実装時に
この文書の結論から 1 点だけ変えている: **4 段階のうち「準備」を落として 3 段階にした**。
`(0,0)` を「fetchNodes 開始」の合図に使う設計だったが、SDK は総バイト数が未知のうちは
`setTotalBytes` を呼ばない（`megaapi_impl.cpp:16150`）ため **本物の進捗イベントも `(0,0)` で
飛びうる**ことが分かり、合図として一意でなかった。上の「実装時の注意」でこの区間を
「1.1 秒しかなく専用の文言を出す価値自体がない」と書いていたので、段階ごと落とす判断にした。

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
