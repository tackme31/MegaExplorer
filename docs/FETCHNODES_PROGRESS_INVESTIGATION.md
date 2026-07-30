# 調査メモ: ログイン〜fetchNodes中の進捗表示は実現できるか（2026-07-30）

ログイン直後、ファイル一覧画面に遷移するまでのローディングが（特にノード数の多いアカウントで）
かなり長くなるケースがある。現状ローディング表示自体が未実装なので、いずれ実装する必要があり、
その際に進捗（%表示など）を出せるかを調べた記録。**コード変更は未実施（調査のみ）**。

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
