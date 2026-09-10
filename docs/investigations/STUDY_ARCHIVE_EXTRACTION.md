# 書庫からエントリ1つを取り出す — 実装前調査（zip）

> **状態: 未着手。設計判断は確定済み、最大の未確認事項だった「チャンクの順序保証」は SDK ソースで解決した（§2）。**
> 対象は zip のみ。一覧表示は `evolve/080` で出荷済みで、本書はその上に
> **「中のフォルダを行き来する独立ビューア」と「選んだエントリ1つの取り出し」** を載せる話。
> 前提となる一覧側の調査は `STUDY_ARCHIVE_PREVIEW.md`（あちらは「一覧のみ、展開は対象外」と
> 宣言しているので、本書がその続きにあたる）。
> 結論は §0、確定済みの判断は §1、経路の形は §4、罠は §5、着手順は §6、未計測は §7。
> 2026-09-10 時点のコード（`485a1f2`）に対する調査。**実測値は §7 に挙げたとおり一つも無い。**

---

## 0. 結論（先に要約）

1. **特定エントリだけの取り出しは zip のメタデータだけで成立する。** Central Directory が
   エントリごとに local header の位置・圧縮後サイズ・圧縮方式・CRC-32 を持っているので、
   **レンジ取得 2 回**（local header 30 バイト／データ本体）で 1 エントリが取れる。転送量は
   そのエントリの圧縮後サイズだけで、書庫全体の大きさに依らない。
2. **逐次展開は「レンジ API の作り直し」ではなく「受け皿の差し替え」で済む。**
   既存の `StreamingContentListener::onTransferData` は既にチャンク単位で呼ばれていて、
   バッファに足し込んでいるだけ（`src/mega/MegaSdkListeners.h:368`）。ここを
   「inflate してファイルへ書く」に替えれば逐次展開になり、**`return false` が転送中止なので
   キャンセルも同時に手に入る**。
3. **②の結果、「小さければメモリ内、大きければ逐次」の 2 経路は不要。** 一本の逐次経路が
   両方を扱う。閾値という決定事項を持ち込むわりに得るものが無いので、**畳む**。
   `STUDY_ARCHIVE_PREVIEW.md` §5 の「小さければ丸ごと落とす」逃げ道は、取り出しについては
   採らないという判断。
4. **`onTransferData` は順序保証つき・単一スレッド。** 逐次 inflate の前提が崩れないことを
   SDK ソースで確認した（§2）。ここが唯一の設計上の生死を分ける点だった。
5. **`DownloadService` が `IMegaClient::download` 以外のジョブを扱えるようにするリファクタが要る**
   （§4.4）。プレリリース特権を使う対象として、判断は取得済み。

---

## 1. 確定済みの判断（2026-09-10、口頭で決定）

| # | 論点 | 決定 | 理由 |
| --- | --- | --- | --- |
| A1 | 一覧を出す場所 | **ダブルクリックで独立した `ArchiveViewer` を開く。プレビューペインは現状の平坦インデントのまま据え置き** | ペインは「ちら見」の面。移動・選択・取り出しには面積と焦点が要る。画像/動画/音声/PDF と同じ型に乗れる |
| A2 | 中の移動の見せ方 | **フォルダ単位（パンくず＋「上へ」）。展開ツリーにはしない** | 要望そのもの。副次的に `STUDY_ARCHIVE_PREVIEW.md` §8.3 の「数千エントリを `QVariantList` で丸ごと QML へ」懸念が消える — 1 フォルダ分しか渡らない |
| A3 | 一覧のモデル | **専用の小さいモデルを起こす。`FileListModel` は使い回さない** | `FileListModel` は `FileEntry`＝MEGA ハンドル前提。書庫エントリを通すと「偽ノード」概念が `src/core` に入る |
| A4 | 書庫の中身をその場で開く | **今回は対象外** | 一時ファイルの寿命・ビューアへの受け渡し・キャッシュ有無を全部決める必要があり、決定事項が倍になる。次の一手として §8 に残す |
| A5 | 入れ子の zip | **非対応** | 同上 |
| A6 | 複数選択・「すべて展開」 | **非対応（単体のみ）** | 一括はディレクトリ構造の再現＝Zip Slip を正面から解く必要が出る（§5.6）。単体なら葉の名前だけで済む |
| C4 | 進捗とキャンセルの置き場所 | **`DownloadService` に相乗り** | 転送を探す場所が 2 つあるのは避けたい。TransferFlyout の行とキャンセルボタンがそのまま効く。**リファクタが要る**ことは織り込み済み（§4.4） |
| C5 | 保存先 | **既存のダウンロードと同じ Downloads フォルダ。衝突は同じ `(1)` サフィックス** | `DownloadController::downloadFile` と完全に同じ扱い。新規 UI ゼロ。Save-As は別要望として切り出せる |

---

## 2. `onTransferData` の順序保証（本調査の主目的）

**結論: 順序どおり・隙間なし・単一スレッド。逐次 inflate の前提は満たされる。**

`megaapi.h:9532` のドキュメントコメントは順序について何も言っていないので、SDK の実装を辿った。

**① 配送は常に「次の 1 個」だけ。** `DirectReadSlot::processAnyOutputPieces`
（`third_party/sdk/src/transfer.cpp:1584`）は

```cpp
while (continueDirectRead && (outputPiece = mDr->drbuf.getAsyncOutputBufferPointer(0)))
{
    ...
    continueDirectRead = mDr->onData(outputPiece->buf.datastart(), len, mPos, ...);
    mDr->drbuf.bufferWriteCompleted(0, true);
    if (continueDirectRead) { mPos += len; ... }
}
```

**添字は常に `0`** — 複数コネクション（RAID を含む）が並行に取ってきた断片は
`TransferBufferManager` 側で組み立てられ、**先頭から連続した分だけが出力ピースになる**。
スロット側は単調増加のカーソル `mPos` を 1 本持つだけ。したがって
「後ろのチャンクが先に届く」経路が構造上存在しない。

**② コールバックは SDK 自身の単一スレッド上。** `MegaApiImpl::fireOnTransferData`
（`megaapi_impl.cpp:18277`）の先頭が `assert(threadId == std::this_thread::get_id());`。
`IMegaClient.h` が今「SDK-internal thread」と書いているのは正確には
**「SDK の唯一のスレッド」** で、`StreamingContentListener` の「SDK は 1 転送のコールバックを
直列化する」という既存コメントより強い保証がある。

**③ ただしオフセットは我々の層に届かない。** `pread_data` は第 3 引数でオフセットを
受け取っているのに `MegaApiImpl::pread_data`（`megaapi_impl.cpp:14238`）が名前を付けずに
捨てているため、`onTransferData` の引数からは自分の位置が分からない。
**自衛策**: `pread_data` は `fireOnTransferData` の**前**に
`setTransferredBytes(getTransferredBytes() + len)` を済ませているので、リスナ側で
「自分が受け取った累計 == `transfer->getTransferredBytes()`」を検算できる。安いので入れる。

**残る不安**: 実績があるのは 20KB の zip（＝数チャンク）だけ。§7-3 で数 MB 以上を実測する。

---

## 3. zip 側に足すもの

### 3.1 `ZipEntry` の追加フィールド

Central Directory ヘッダから読めるのに、今は捨てているもの:

| フィールド | 位置 | 用途 |
| --- | --- | --- |
| `compressionMethod` | `at + 10`（u16） | 0 / 8 以外を弾く |
| `crc32` | `at + 16`（u32） | 展開後の検算 |
| `localHeaderOffset` | `at + 42`（u32） | データの探し方の起点 |

### 3.2 ZIP64 の offset は**未対応**なので足す

`parseZipDirectory` の extra field `0x0001` の処理（`ZipListing.cpp:227` 付近）は
**サイズ 2 つしか読んでいない**。ZIP64 extra の並びは

```
uncompressedSize(8) / compressedSize(8) / localHeaderOffset(8) / diskStart(4)
```

で、**32bit 側が 0xFFFFFFFF に飽和したフィールドだけが、この順で詰めて置かれる**。
既存コードはこの「飽和したものだけ・順序は固定」という規則を既に正しく実装しているので、
`localHeaderOffset == 0xFFFFFFFFu` の場合の `if` を 1 つ後ろに足すだけで済む。
4GB 超の書庫こそ「丸ごと落とさない」価値が最大なので、ここは今回対応する。

### 3.3 inflate は `ZipListing` に入れない

`src/core/ZipExtract.{h,cpp}` を別に起こす。`ZipListing` の
**「外部依存ゼロ・純関数・バイト列を食わせるだけで全部テストできる」**性質は資産で、
zlib はこの新しいファイル 1 つからだけ入る。zlib は vcpkg に既にある
（`vcpkg_installed/x64-windows-mega/include/zlib.h`）が、**SDK の推移的依存として入っているだけ**
なので、寄りかからずに明示的に宣言すること。

---

## 4. 経路の形

### 4.1 `IMegaClient` に足す口

```cpp
// 既存の readFileRange と併存。onChunk は SDK の単一スレッドから、
// 先頭から順に隙間なく呼ばれる（STUDY_ARCHIVE_EXTRACTION.md §2）。
// false を返すと転送が中止される＝これがキャンセル。
virtual void readFileRangeStreamed(std::uint64_t handle,
                                   std::uint64_t offset,
                                   std::uint64_t length,
                                   std::function<bool(const char*, std::size_t)> onChunk,
                                   std::function<void(Result<void>)> onDone) = 0;
```

`MegaSdkClient` 側は `readFileRange` の実装をほぼそのまま使い回せる
（`setStreamingMinimumRate(0)` の罠の対処も含めて）。リスナだけ
`StreamingContentListener` の隣に新しく起こす。

### 4.2 1 エントリを取り出す流れ

```
1. CD の localHeaderOffset から 30 バイト  ← readFileRange（既存）
     → nameLen / extraLen を読む（★ CD の extra 長とは別物、§5.1）
     → dataOffset = localHeaderOffset + 30 + nameLen + extraLen
2. dataOffset から compressedSize バイト   ← readFileRangeStreamed（新規）
     → チャンクごとに inflate（method 8 は raw deflate = windowBits -15、method 0 は素通し）
     → 一時ファイルへ書く／CRC-32 を回す
3. 完了時に CRC を照合 → 一致なら本名へ rename、不一致なら一時ファイルを消して失敗
```

**転送は 2 回**。投機的に 1 回で済ませる案（`30 + nameLen + 4096 + compressedSize` を
まとめて取り、extra が溢れたら読み直す）は分岐が増えるだけなので採らない。ただし
レンジ 1 回の実時間は未計測なので、§7-1 の結果次第では再考の余地がある。

### 4.3 inflate と書き込みをどのスレッドで

**SDK の単一スレッド上でそのまま**。zlib も `fwrite` も純 C で Qt オブジェクトに触らないので、
スレッドを跨ぐ必要がない。**既知のリスク**: ディスク書き込みで SDK の唯一のスレッドを塞ぐ。
遅いディスク／大きいエントリで他の転送やコールバックが詰まる可能性があり、
これは §7-2 で測るまで机上の懸念のまま。

### 4.4 `DownloadService` 相乗りのために要るリファクタ

`DownloadService` は今 `IMegaClient::download` を直接呼ぶ前提で組まれていて、
`DownloadJob` も `handle` と `destinationPath` しか持たない。**「ジョブ = 実行方法を知っている
何か」に一段抽象化する**必要がある。`kMaxConcurrent` のスロット管理・キャンセル・
`jobChanged` の発火はそのまま使い回せるので、変わるのは「起動のしかた」だけのはず。

**なぜ相乗りにするか**: 進行中の転送を見る場所がアプリ内に 2 つできるのを避けるため。
TransferFlyout の行とキャンセルボタンがそのまま効く。

---

## 5. 罠

1. **local header の extra field 長は Central Directory の extra field 長と一致しない。**
   別々に書かれるフィールドで、一致する保証はどこにもない。**CD の値から
   `localHeaderOffset + 30 + nameLen + extraLen` を計算してはいけない** — local header を
   実際に読むこと。これが本件で最も踏みやすい罠。
2. **data descriptor（汎用フラグ bit 3）**では local header のサイズ欄が 0 になっているが、
   **CD の値は常に正しい**ので影響しない。CD を起点にする設計の副産物。
3. **対応する圧縮方式は 0（無圧縮）と 8（deflate）のみ。** 12(bzip2) / 14(LZMA) / 93(zstd) /
   99(WinZip AES) は**行は出すがダウンロードを無効化し、理由を出す**。実在書庫のほぼ全部は
   0 か 8。bzip2/lzma/zstd のヘッダは vcpkg に既にあるので後から足すのは安い。
4. **暗号化エントリは拒否**（`ZipEntry::encrypted` は既に取れている）。ZipCrypto は設計上
   壊れており、WinZip AES は実質別形式。パスワード UI は独立した機能。
5. **CRC-32 は必ず検証する。** 不一致なら一時ファイルを消して失敗を報告する。zip は検算値を
   タダでくれる唯一の形式で、黙って壊れたファイルが Downloads に残るほうが害が大きい。
6. **Zip Slip。** エントリ名は `../` や絶対パスや `\` を含みうる。本件は
   **葉の名前だけを使い、サニタイズし、書庫内のディレクトリ構造をディスク上に再現しない**
   と決めたので実質無害化されるが、**明文化しておかないと後で「すべて展開」を足す人が踏む**。
7. **中断・失敗・CRC 不一致時に本名の壊れたファイルを残さない。** 一時名で書いて成功時に
   rename する。
8. **目録サイズの上限。** ROADMAP の見送り行「目録が巨大な zip を選ぶと目録を丸ごとメモリへ
   読み込む」は、この周辺を触るついでに閉じる。**32 MiB 程度**で打ち切り、
   「書庫の目録が大きすぎます」を出す。

---

## 6. 着手順

| | 内容 | 規模 | 備考 |
| --- | --- | --- | --- |
| 1 | `ArchiveViewer` — フォルダ単位の移動、パンくず、「上へ」 | **S** | 新規 I/O ゼロ。既存の一覧結果を畳み直すだけ。`AudioViewer.qml` と同じ型で起こせる。**先に出すと 2 の「どのエントリを選ぶか」の UI がそのまま手に入る** |
| 2 | `ZipEntry` 拡張＋local header 解析＋`ZipExtract`（zlib） | **M** | §3。純関数なのでユニットテストはバイト列を食わせるだけで全部書ける |
| 3 | `readFileRangeStreamed`＋逐次 inflate | **M** | §4.1 / §4.3 |
| 4 | `DownloadService` のジョブ抽象化＋相乗り | **M** | §4.4。プレリリース特権を使う対象 |
| 5 | 目録サイズ上限（見送り行を閉じる） | **S** | §5.8 |

1 → 2 → 3 → 4 の順。1 は単独で価値があり、単独で出荷できる。

---

## 7. 測っていないこと

1. **レンジ取得 1 回の実時間。** `STUDY_ARCHIVE_PREVIEW.md` §7 が未計測のまま残したもの。
   §4.2 の「転送 2 回」が体感に響くかはこれ次第で、響くなら投機読みを再考する。
2. **逐次展開の実効速度 vs 同じバイト数の通常 `download`。** 「大きいエントリなら書庫ごと
   落としたほうが速い」境界が実在するかどうか。§4.3 の SDK スレッド占有もここで見る。
3. **数 MB 以上でのチャンク順序の実測。** §2 はソース読みによる結論で、実績は 20KB のみ。
4. **エントリ数の多い書庫での挙動。** A2（フォルダ単位）で緩和されるはずだが未確認。

---

## 8. 今回やらないと決めたこと（次の一手の候補）

- 書庫の中の画像／テキスト／PDF をその場で開く（A4）。取り出し経路ができれば技術的には
  一時ファイル 1 つ分の距離だが、寿命管理とキャッシュの判断が新たに要る。
- 複数選択・「すべて展開」（A6）。§5.6 を正面から解く必要がある。
- 7z / rar。`STUDY_ARCHIVE_PREVIEW.md` §2.2 / §2.3 と ROADMAP の見送り行のまま。
- 保存先を選ぶ／設定する（C5 の別案）。通常のダウンロードの保存先も同じ設定に従うべきか、
  という別の議論を呼ぶので切り離した。
