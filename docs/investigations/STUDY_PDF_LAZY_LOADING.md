# PDF ビューアのページ単位取得（遅延読み込み） — 実現可能性調査

> **状態: 机上調査のみ。実装未着手、実測値ゼロ。着手には §7 の計測が前提条件。**
> 2026-09-07 時点のコード（`9beb43d`）に対する調査。どのフェーズにも入っていない。
> 現行の PDF ビューアは**ファイル全体をメモリに載せる**実装で、サイズガードは一切ない
> （`src/qml/PdfPageItem.cpp`）。本書はそれをページ単位の遅延取得に置き換えられるかを見たもの。
> 結論は §0、「zip と同じ手が使えない理由」は §1、成立の根拠は §2、
> 残る争点は §5、サイズガードとの関係は §8。
> `docs/investigations/STUDY_INAPP_VIEWER.md` §3-3 が同じ論点を扱っていて、そこでは
> 「案 2（遅延読み込み `QIODevice`）は割に合わない」と結論している。**その判断の前提は
> その後変わった** — §3 参照。

---

## 0. 結論（先に要約）

1. **可能。ただし「ページの開始位置を先に確定する」形にはならない。** PDF に「ページ N は
   X〜Y バイト」に相当する情報は存在しない（§1）。zip の目録先読みの相似形は作れない。
2. **正しい形は「必要なところだけ読む `QIODevice` を pdfium に渡す」。** そして**その受け口は
   Qt PDF に既にある** — `QPdfDocument::load(QIODevice*)` は非 sequential なデバイスに対して
   `seek()` + `read()` のオンデマンド読みを行う（§2）。現行実装はその受け口を使っていないだけ。
3. **SDK 側の土台も揃っている。** `readFileRange()` が zip プレビューのために実装済みで、
   内蔵 HTTP サーバも Range に応答する（§3）。STUDY_INAPP_VIEWER §3-3 が案 2 を退けた時点では
   前者が存在しなかった。
4. **最大の争点は非同期／同期の不整合。** pdfium の読み出しは同期呼び出しで、こちらの
   レンジ取得はコールバック非同期。書庫プレビューで libarchive を検討したときと**同じ形の
   争点**（`STUDY_ARCHIVE_PREVIEW.md` §4.1）で、答えも同じくワーカースレッド（§4, §5-1）。
5. **着手前に計測が要る。** 「1 ページ表示に実際何バイト・何リクエスト必要か」は PDF の
   作られ方で桁が変わり、ここが分からないと価値が判定できない。ローカルファイル相手の
   使い捨てプロトタイプで測れる（§7）。

---

## 1. zip 方式（目録先読み）がそのまま効かない理由

zip は末尾の EOCD → セントラルディレクトリに「各エントリの開始オフセットと長さ」が並ぶので、
末尾数十 KB を 1 回読めばインデックスが完成する（`PreviewController::requestArchive`）。

PDF も**末尾起点**なのは同じで、`startxref` → xref テーブル／ストリーム、という道筋は取れる。
違うのはそこから先で:

- **xref が返すのはオブジェクト番号 → バイトオフセット**であって、ページ単位の範囲ではない。
- **1 ページを描くのに必要なバイトは連続していない。** ページ辞書、コンテンツストリーム、
  フォント、画像 XObject が別々の位置に散らばる。しかもフォントや画像は複数ページで共有される
  ので、「このページ専用の範囲」という区切り自体が存在しない。
- **PDF 1.5 以降は xref 自体が圧縮ストリーム**（`/Type /XRef`）で、オブジェクトが
  オブジェクトストリーム（`ObjStm`）の中に入れ子になる。目録を読むだけで
  Flate 展開器と PDF オブジェクトパーサが要る。

つまり自前解析でやろうとすると、実質 PDF パーサを 1 本書くことになる。**その解析をやる本体
（pdfium）に、必要なところだけ読むデバイスを渡す**のが本件の正しい形で、以降はその話。

---

## 2. Qt PDF は遅延読み込みの受け口をすでに持っている（成立の根拠）

`C:/Qt/6.11.1/msvc2022_64/include/QtPdf/6.11.1/QtPdf/private/qpdfdocument_p.h` で、
`QPdfDocumentPrivate` は `FPDF_FILEACCESS` / `FX_FILEAVAIL` / `FX_DOWNLOADHINTS` を継承し、
`FPDF_AVAIL avail` と `checkPageComplete(int page)` を持つ。実装（qtwebengine 6.11
`src/pdf/qpdfdocument.cpp`）の要点:

```cpp
int QPdfDocumentPrivate::fpdf_GetBlock(void *param, unsigned long position,
                                       unsigned char *pBuf, unsigned long size)
{
    QPdfDocumentPrivate *d = ...;
    d->device->seek(position);                        // 毎回シークして
    return qMax(qint64(0), d->device->read(...));     // 必要な分だけ読む
}
```

- `QPdfDocumentPrivate::load()` は **sequential かどうかで分岐する**。非 sequential
  （＝ランダムアクセス可能）なら `initiateAsyncLoadWithTotalSizeKnown(device->size())` に進み、
  以後の読み出しは上記 `fpdf_GetBlock` 経由の**オンデマンド**になる。
- `fpdf_IsDataAvail` は「要求範囲が `device->size()` に収まるか」を見るだけ。`size()` が
  ノード全長を返すデバイスなら常に true になり、pdfium は素直にブロックを取りに来る。
- **`fpdf_AddSegment`（先読みヒント）は空実装**。pdfium が「次にどこが要るか」を教えてくる
  経路は使えない。先読みを入れるなら `GetBlock` の履歴から推測するしかない。
- `loadAsync()`（sequential デバイス用の逐次ロード）は private で、公開 API からは呼べない。
  リニアライズ済み PDF の progressive 表示はこの経路の話で、**本件では使えない**。

**要するに、`QBuffer` を差し替えるだけで pdfium 側は勝手に必要な分しか読まなくなる。**
現行 `PdfPageItem` は `mBytes` に全部載せて `QBuffer` を渡しており（`PdfPageItem.cpp` の
`onFetchFinished` / `mDocument.load(&mBuffer)`）、この受け口を使っていない。

---

## 3. SDK 側の土台

- **`IMegaClient::readFileRange(handle, offset, length, onDone)`** — 「a real range request」と
  明記された範囲読み（`src/core/IMegaClient.h`）。zip プレビューが実運用している。
  **STUDY_INAPP_VIEWER §3-3 が案 2 を「割に合わない」と退けた 2026-08 時点では、これが
  存在しなかった**（`readFileContent` の `startStreaming(node, 0, size)` 固定だけだった）。
  前提が変わったのはここ。
- **内蔵 HTTP サーバは Range に応答する** — `sourceUrl()` が返すローカル URL は
  「the server answers Range requests, so seeking works」と `IMegaClient.h` が明言している。
  `QNetworkAccessManager` に Range ヘッダを付けて叩く経路も選べる。

どちらを土台にするかは §4 の選択肢 A / B。

---

## 4. 設計案

共通形: **ブロックキャッシュ付きランダムアクセス `QIODevice`** を書き、
`isSequential() == false`・`size()` はノード全長を返し、`readData()` で
「要求位置を含むブロックがキャッシュになければ取得して埋める」。ブロックは 256KB 程度の
固定境界に丸める（pdfium の読みは数百バイト単位で散るので、丸めないとリクエストが爆発する）。

取得経路の選択肢:

- **A. `readFileRange` を直に叩く。** 依存が増えず、`IMegaClient` の抽象の内側に留まる
  （テストでモックできる）。欠点は **1 回のレンジ取得ごとに `startStreaming` の転送が 1 本
  立つ**こと（`MegaSdkClient::readFileRange`）。転送の立ち上げコストがそのままレイテンシに乗る。
- **B. ローカル HTTP サーバに Range リクエストを投げる。** 接続を使い回せる可能性があり、
  サーバ側が既に持っているバッファリングに乗れる。欠点は `IMegaClient` を迂回すること
  （`PdfPageItem` が今すでに `QNetworkAccessManager` を直に使っているので、そこは現状追認）と、
  サーバの Range 実装の挙動を実測でしか確かめられないこと。

**A を第一候補**とする。抽象の内側に留まる利点が大きく、リクエスト数の問題はブロック
サイズと先読みで殴れる余地がある。ただし §7 の計測で A のレイテンシが致命的だと出たら B。

---

## 5. 難所

1. **pdfium の読み出しは同期ブロッキング。** `fpdf_GetBlock` は戻り値を待つので、
   `readData()` はデータが届くまで返せない。現行は GUI スレッドで `load` / `renderPage` して
   いる（`PdfPageItem` の「Renders on the GUI thread」コメント）ので、そのままだと
   **ブロック取得のたびに UI が固まる**。`QPdfDocument` ごとワーカースレッドに移し、
   デバイス側は条件変数で待ち、描画結果の `QImage` だけ GUI に戻す形になる。
   `QPdfPageRenderer` を使う手もあるが、`load()` 自体は呼び出しスレッドで走る点は変わらない。
2. **キャンセルできない。** `readFileRange` は「There is deliberately no way to cancel」と
   明記されている。ビューアを閉じても飛んでいるリクエストは止まらないので、待ち側に
   タイムアウトが要り、遅れて届いたコールバックが死んだデバイスを触らない配線も要る。
   現行の `reset()` が `QNetworkReply::abort()` でやっている後始末が、そのままでは効かなくなる。
3. **xref が壊れた PDF。** pdfium は `RebuildCrossRef()` でファイル全体を先頭から走査する。
   遅延デバイス越しにこれをやると**全体を細切れに取る**ことになり、一括ダウンロードより
   確実に遅い。取得済みバイト数がファイルの一定割合（例: 40%）を超えたら**一括取得に
   フォールバック**する脱出口が要る。
4. **スキャン PDF は 1 ページが重い。** 1 ページ＝大きな画像なので、そのページ分は数 MB 取る。
   これは避けようがなく、避けるべきでもない（見たいページそのもの）。遅延読み込みの利得は
   「開いた瞬間に全ページ分を取らない」ことであって、1 ページの重さは減らない。
5. **ページ移動のたびに取りに行く。** pdfium は開いた文書内にパース済みオブジェクトを
   キャッシュするが、まだ触っていないページのコンテンツストリームは当然読みに行く。
   ページ送りの体感は「次ページで一瞬待つ」になる。先読み（隣接ページを投機取得）を
   入れるかは、§7 の実測でページあたりのバイト数が出てから決める。

---

## 6. 触ることになる場所

- `src/qml/PdfPageItem.{h,cpp}` — 中身はほぼ書き換え。`mBytes` / `mBuffer` /
  `QNetworkAccessManager` が消え、デバイス＋ワーカースレッドに変わる。公開プロパティ
  （`source` / `pageCount` / `currentPage` / `status`）は**据え置ける**ので、
  `qml/components/PdfViewer.qml` は無変更で済む見込み。
- 新規: レンジ取得デバイス本体。`src/core` に置いて `IMegaClient` 依存にすればテスト可能
  （モック `IMegaClient` に対してブロック境界・クランプ・再入を単体テストできる）。
  `PdfPageItem` から `QNetworkAccessManager` 直叩きを外せるなら、そのほうが層としても正しい。
- `ViewerController::sourceUrl()` — 案 A ならビューアが URL ではなくハンドルを受け取る形に
  なるので、この経路自体が PDF では不要になる（画像・動画・音声は現状のまま URL）。

---

## 7. 着手前に測ること（プロトタイプ手順）

やる価値があるかは「**1 ページ表示に実際何バイト・何リクエスト要るか**」次第で、これは
PDF の作られ方で桁が変わる。ネットワークを一切使わずに測れる:

1. `QFile` をラップし、`seek()` / `read()` の位置と長さをログするだけの `QIODevice` を書く
   （使い捨て。`src/` には入れない）。
2. それを `QPdfDocument::load()` に渡し、(a) 1 ページ目、(b) 中ほどのページ、(c) 最終ページ、
   を `render()` する。
3. 各段階で「読んだ総バイト数 / ファイルサイズ」と「256KB ブロックに丸めた場合の
   ユニークブロック数」を出す。後者がそのままレンジ取得の回数になる。

測る対象は最低 4 パターン: リニアライズ済み／非リニアライズ、テキスト主体／スキャン画像。

判定の目安:

- 100MB クラスの 1 ページ目が**数百 KB・十数リクエスト**で出るなら、実装の価値は明確。
- **3 割以上読む**なら、A 案のリクエスト立ち上げコストを考えると一括ダウンロードに
  勝てない可能性が高く、見送り（＋ §8 のサイズガードだけ入れる）が妥当。

---

## 8. サイズガードとの関係

現行はサイズによるガードが**どこにもない**。`viewerKind()` は拡張子だけで判定し
（`ViewerController::viewerKind`）、`Main.qml` の `openViewer()` も `PdfViewer.qml` も
`sizeBytes` を見ない。`readAll()` の瞬間はファイルサイズの 2 倍がピークになる。

遅延読み込みが入ると:

- **メモリ面のガードは概ね不要になる。** ピークがブロックキャッシュ＋pdfium のパース構造＋
  描画済み 1 ページ分に収まり、ファイルサイズに比例しなくなる。
- **ただし消えるのではなく移る。** §5-3 の一括フォールバックが残る限り、そこには依然として
  「これ以上は取らない」の線が要る。ガードはビューアの入口から**フォールバック経路の
  ゲート**に移動する。

したがって、いま入れるガードは遅延読み込みが入っても完全な捨て仕事にはならない。ただし
「開く前に確認ダイアログ」という **UI としての形は捨てることになる**（遅延読み込みが効けば
大きい PDF も普通に開けるので、確認を出す理由がなくなる）。

---

## 9. 未確認事項

- pdfium が 1 ページに要求する実バイト数・読み回数（§7 で測る。本書の全判断がここに乗る）。
- `readFileRange` 1 回あたりの実レイテンシ。`startStreaming` の立ち上げコストが支配的なら
  ブロックサイズを大きく取る必要がある。
- ローカル HTTP サーバの Range 実装の実挙動（案 B を採るなら必須）。ヘッダ上は対応と
  書かれているが、部分応答の挙動は未確認。
- `QPdfDocument` をワーカースレッドで使うことの可否。pdfium 呼び出しは Qt 側が
  グローバルな再帰ミューテックス（`QPdfMutexLocker`）で直列化しているので原理的には
  問題ないはずだが、未検証。
- 暗号化 PDF（パスワード付き）の挙動。`QPdfDocument::passwordRequired` は現行ビューアでも
  未対応で、遅延読み込みとは独立の穴。
