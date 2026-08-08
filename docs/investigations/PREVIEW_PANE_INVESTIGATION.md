# アプリ内プレビュー（右ペイン） — 実装前調査

2026-08-08。Phase 15 の設計に入る前の机上調査。実装・計測はまだ行っていない。

想定している仕様（依頼時点）:

- ステータスバーの表示形式トグルの右に区切りの縦棒を 1 本挟み、その右にプレビュー ON/OFF ボタン
- ON でウィンドウ右側にサイズ変更可能なペインが出て、選択中のファイルを表示する
- 対応していない種別は「プレビューできません」相当の文言
- **プレビュー内容はキャッシュしない**

---

## 結論（先に要約）

1. **画像・動画・PDF は「サーバ側プレビュー（1000×1000 の JPEG）」1 本でほぼ賄える。**
   MEGA SDK は `MegaApi::getPreview()` を持ち、既存の `getThumbnail()` とまったく同じ形
   （`MegaNode` + 出力先パス + `MegaRequestListener`）なので、`IMegaClient`/`ThumbnailService`/
   `ThumbnailController` の三層をそのまま横に 1 本増やすだけで済む。**ここが実装の本命。**
2. **ただし「動画を再生する」「PDF をページ送りする」は別物で、今回のスコープに入れるべきではない。**
   - 動画再生には SDK 内蔵 HTTP サーバ（`httpServerGetLocalLink`）が要るが、**このビルドでは
     コンパイルされていない**（`USE_LIBUV` が OFF、後述 §1.4）。有効化はビルド構成の変更を伴う。
   - PDF ページ送りには Qt PDF が要るが、**このマシンの Qt 6.11.1 には Qt PDF が入っていない**
     （後述 §5）。
   - どちらも「先頭フレーム / 1 ページ目の静止画」ならサーバ側プレビューで無料で出る。
3. **テキストは唯一「実データを読む」種別で、ここだけ設計が違う。** `MegaApi::startStreaming()`
   を使えば *ファイルを作らずに* メモリへ読める。Explorer 本家に倣い **50 KB 以下だけ全取得、
   超過は通信せず拒否**とする（§4.5 — 先頭を切り出す案より優れている理由もそこに）。
   「キャッシュしない」要件と最も相性が良い一方、`IMegaClient` に新しい形のメソッド
   （バイト列を渡すコールバック）が要る。
4. **「キャッシュしない」を額面どおり実装すると QML の `Image` キャッシュが牙を剥く。**
   `Image` は source URL でキャッシュするので、同じパスに違う中身を上書きすると古い絵が残る。
   対策は「リクエストごとに別名の一時ファイル + 直前のファイルを削除」。§4.1 に詳述。
5. **ユーザ向けダウンロード（`DownloadService`）は絶対に流用しない。** あれは直列キューで
   ステータスバーに進捗を出す設計なので、プレビュー取得が「ダウンロード中」表示を占拠する。
   `ThumbnailService` 側（並列 4・ハンドルキー）が正しい参照実装。

**決定したスコープ（2026-08-08）**: 「サーバ側プレビュー JPEG の表示」＋「50 KB 以下の
テキスト全文表示」＋「それ以外は『プレビューできません』」（§7 の A+B+C）。
動画再生・PDF ビューアは Phase 15 の外。
表示 ON/OFF は**タブ単位**、ペイン幅は**ウィンドウ単位**（§6.2）。

---

## 1. MEGA SDK 側にあるもの / ないもの

### 1.1 `getPreview` — 既存 `getThumbnail` と同型

`third_party/sdk/include/megaapi.h:15001`:

```cpp
void getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
```

- `getThumbnail` と完全に同じ呼び出し規約。`dstFilePath` が `/` `\` で終わると
  ディレクトリ扱いになり SDK 側が `Base64ハンドル + "1.jpg"` を作る、という罠まで同じ
  （サムネイルは `"0.jpg"`）。`IMegaClient::getThumbnail` のコメントにある「末尾にセパレータを
  置くな」はそのままプレビューにも当てはまる。
- **プレビューが無いノードでは `API_ENOENT` で失敗する。** `getThumbnail` と同じ。
- 実装は `MegaSdkClient::getThumbnail`（`src/mega/MegaSdkClient.cpp:396-418`）を
  `mApi->getPreview(...)` に差し替えただけのものになる。リスナも既存の
  `megasdk::AttributeFileListener` をそのまま使える。

### 1.2 サイズ — プレビューは 1000×1000 の JPEG

`third_party/sdk/src/gfx.cpp:32-35`:

```cpp
const std::vector<GfxDimension> GfxProc::DIMENSIONS = {
    { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
    { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
};
```

- サムネイルは 200px の**正方形切り抜き**、プレビューは 1000px 枠内に**アスペクト比維持で縮小**。
- 右ペインの実寸（240〜600px 程度）に対して 1000px は十分。**原本をダウンロードする理由はない。**
- 元画像が 1000px 未満なら拡大はしない（`gfx.cpp:264-269`）。
- **解像度は要求できない。** `DIMENSIONS` はコンパイル時定数で、しかも実体は
  *アップロード時に生成してサーバへ保存済み*の添付属性。`getThumbnail`/`getPreview` は
  それを取ってくるだけなので、API に解像度パラメータは無く、選べるのはこの 2 種類だけ。

### 1.3 `hasPreview()` — フラグは取れるが、当てにしきってはいけない

`megaapi.h:1600` に `MegaNode::hasPreview()` がある。`FileEntry::hasThumbnail` と同じ要領で
`FileEntry::hasPreview` を足せばモデルまで運べる（`MegaSdkClient.cpp:107` の `nodeToEntry`）。

ただし **プレビューは「アップロードしたクライアントが生成したときにだけ存在する」**。
生成は SDK のビルド構成に依存する（`third_party/sdk/src/gfx/freeimage.cpp`）:

| 生成器 | 対象拡張子（抜粋） | 参照 |
| --- | --- | --- |
| FreeImage | `.jpg .png .bmp .gif .webp .tga .tiff` ほか、RAW 系（`.cr2 .nef .arw .dng` …） | `freeimage.cpp:919-940` |
| FFmpeg | `.mp4 .mkv .mov .avi .webm .wmv .ts .m4v` ほか | `freeimage.cpp:496-503` |
| PDFium | `.pdf` のみ | `freeimage.cpp:857-860` |

本アプリのビルドは `use-freeimage;use-ffmpeg;use-pdfium` を渡している
（`CMakePresets.json:22`）ので、**本アプリからアップロードしたファイルは上表どおり生成される**。
一方、他のクライアント（gfx を持たない megacmd など）で上げたファイルには無いことがある。

→ **`hasPreview` は「あれば速い」ヒントに留め、実際には失敗を正常系として扱う**設計にする。
フォールバック段階は §3.3。

### 1.4 動画再生用のローカル HTTP サーバは**このビルドに存在しない**

`megaapi.h:21916` 以降の `httpServerStart` / `httpServerGetLocalLink`（:21974, :22210）は
すべて `#ifdef HAVE_LIBUV` の中にある。そして:

- `third_party/sdk/cmake/modules/sdklib_options.cmake:21` — デスクトップでは
  `option(USE_LIBUV ... OFF)`
- `CMakePresets.json:22` の `VCPKG_MANIFEST_FEATURES` に `use-libuv` は**入っていない**

つまり `HAVE_LIBUV` は未定義で、**これらの API はコンパイルすらされていない**。
有効化するには vcpkg の feature 追加 + SDK 再ビルド（依存に libuv が増える）が要る。
Phase 15 で踏み込む判断をするなら、それ自体が独立した作業項目。

### 1.5 `startStreaming` — テキストプレビューの本命

`megaapi.h:17623`:

```cpp
void startStreaming(MegaNode* node, int64_t startPos, int64_t size, MegaTransferListener *listener);
```

- **ローカルファイルを作らずに**、指定レンジのバイト列を
  `MegaTransferListener::onTransferData(api, transfer, char* buffer, size_t size)`（`megaapi.h:9531`）
  へ渡す。`onTransferData` が `false` を返すと**その場で転送を打ち切れる**（= キャンセル手段）。
- レンジ指定できるが、テキストプレビューは**サイズ上限以下なら全取得**の方針とした（§4.5）ので、
  実際の呼び出しは `startPos=0, size=sizeBytes` になる。
- 「キャッシュしない」要件との相性が最も良い（そもそもディスクに何も置かない）。
- 難点: `IMegaClient` の既存メソッドはどれも「パスに書く」形なので、**バイト列を返す新しい形の
  メソッドが 1 本増える**。`tests/MockMegaClient.h` にも 1 行追加が要る（軽微）。

### 1.6 その他

- `MegaApi::getMimeType(const char* extension)`（`megaapi.h:22644`、static）が MIME 文字列を返す。
  種別判定に使えるが、`char*` の所有権を受け取る（`delete[]` 必須）ので、**自前の拡張子テーブルの
  ほうが素直**（`src/core` は Qt 非依存を保っており、テーブルもそこに置ける）。
- `IMegaClient::download` には**キャンセルが公開されていない**。`megasdk::DownloadListener`
  （`src/mega/MegaSdkListeners.h:207-256`）は `MegaCancelToken` を保持しているが、
  「将来 `cancel(jobId)` が呼ぶためのフック」とコメントされたまま誰も呼んでいない。
  プレビューで原本ダウンロードに踏み込むなら、この公開が前提作業になる。

---

## 2. 種別ごとの実現可否（まとめ表）

| 種別 | 推奨手段 | 追加コスト | 備考 |
| --- | --- | --- | --- |
| 画像（jpg/png/gif/bmp/webp/tiff/RAW…） | `getPreview` の JPEG を `Image` で表示 | 小 | RAW は Qt が読めないが、サーバ側 JPEG なので無関係。**原本を落とす必要がない** |
| 動画 | `getPreview`（静止画 1 枚）＋「再生は非対応」 | 小 | Explorer と同じ絵面。**先頭フレームではなく再生時間の 20% 地点** — `freeimage.cpp:650-652` が `videoStream->duration / 5` へシークする（真っ黒な先頭を避けるため、コメントアウトされた旧「5 秒地点」実装が隣に残っている）。フレーム位置は指定できない。再生は §1.4 のとおり別作業 |
| PDF | `getPreview`（1 ページ目の静止画） | 小 | ページ送りは Qt PDF が必要、§5 |
| テキスト/コード | 50 KB 以下なら `startStreaming` で全取得 → `TextArea` | 中 | 上限超は通信せず拒否。エンコーディング判定が要る、§4.5 |
| 音声 | 非対応（文言表示） | — | `USE_MEDIAINFO` は無効なのでカバーアートも出ない |
| その他 | 非対応（文言表示） | — | |

「アニメーション GIF を動かす」は `AnimatedImage` で可能だが、サーバ側プレビューは静止 JPEG に
なるため**原本ダウンロードが要る**。今回のスコープ外に置くのが妥当。

---

## 3. 既存コードのどこに何が増えるか

### 3.1 C++ 側（`ThumbnailService` 系統をそのまま踏襲できる）

| 層 | 追加物 | 参照する既存実装 |
| --- | --- | --- |
| `src/core/IMegaClient.h` | `getPreview(handle, dstPath, onDone)` | 既存 `getThumbnail` の宣言をコピー |
| `src/mega/MegaSdkClient.cpp` | 同名メソッド（`mApi->getPreview`） | `MegaSdkClient.cpp:396-418` |
| `tests/MockMegaClient.h` | `MOCK_METHOD` 1 行 | — |
| `src/core/FileEntry.h` | `bool hasPreview` | 末尾に足せば既存の位置指定初期化（`tests/FileListModelTest.cpp:17` 等）は壊れない |
| `src/core/PreviewService.*` | 取得の直列化・重複排除 | `src/core/ThumbnailService.h` |
| `src/qml/PreviewController.*` | 出力先パス決定・GUI スレッド復帰・状態プロパティ | `src/qml/ThumbnailController.cpp:18-53` |
| `src/qml/FileListModel.h` | `HasPreviewRole`（必要なら） | `FileListModel.h:35-46` |

**ただし `PreviewService` は `ThumbnailService` の丸写しにはならない。** サムネイルは
「ハンドルごとに 1 枚、キャッシュして使い回す」だが、プレビューは
「**同時に 1 枚だけ、最新の選択が勝つ、キャッシュしない**」。共有できるのは
再入トランポリン（`mAdvancing`/`mAdvanceRequested`）の作法くらいで、
`mCache` は持たない。むしろ `ThumbnailService` より単純になる。

### 3.2 状態機械（`PreviewController` が QML に見せるもの）

QML 側が分岐できるよう、少なくともこの 4 状態が要る:

```
Empty      … 選択なし / 複数選択 / フォルダ選択
Loading    … 取得中（スピナー）
Ready      … 表示できる（imageSource もしくは text がセット済み）
Unsupported… 「プレビューできません」（理由テキストを添えてもよい）
```

`Ready` を種別で割る（`ReadyImage` / `ReadyText`）か、別プロパティ `kind` を持つかは設計判断。

### 3.3 フォールバック段階（`hasPreview` が当てにならないことへの対策）

```
1. hasPreview → getPreview          （成功すればこれ）
2. 失敗 or hasPreview=false
   ├ hasThumbnail → 既存 ThumbnailService の 200×200 を暫定表示（粗いが「何もない」よりまし）
   └ なし → Unsupported
```

段階 2 の是非（粗い絵を出すか、素直に諦めるか）は要判断。**200×200 を引き伸ばすと明らかに
ぼやける**ので、「出さない」も十分ありうる選択。

---

## 4. 懸念点（実装前に決めておくべきこと）

### 4.1 「キャッシュしない」と QML `Image` のキャッシュが衝突する ★最重要

`getPreview` は **ローカルパスにしか書けない**（メモリに返す API はない）ので、
「キャッシュしない」は実際には「*同じ場所を上書きして使い捨てる*」になる。ところが:

- QML の `Image` は **source URL をキーにピクスマップをキャッシュする**。
  同じ `file:///.../preview.jpg` に別の中身を書いても、**古い絵がそのまま出る**。
- `cache: false` を付けても「このアイテムがキャッシュに*入れない*」だけで、
  「同じ URL を*読み直す*」保証にはならない。

**対策**: リクエストごとに**別名**の一時ファイルを作る（単調増加カウンタ or ハンドル＋連番）。
そして次を出す時に前のファイルを `QFile::remove` する。これで
「URL が毎回変わる」＝キャッシュが効かない＋「ディスクに 1 枚しか残らない」＝キャッシュしない、
の両方を同時に満たせる。`ThumbnailController::computeDestinationPath`
（`src/qml/ThumbnailController.cpp:44-53`）が「ハンドルごとに固定名」なのと**逆の設計**になる点に注意。

### 4.2 「最新が勝つ」— 世代カウンタが要る

サムネイルは結果をハンドルごとにモデルへ書き戻すので競合しないが、プレビューは
**表示先が 1 つ**。矢印キーで一覧を高速に流されると、古い取得が後から着弾して
「今選んでいないファイル」が表示されうる。

→ `PreviewController` にリクエスト世代（`quint64 mGeneration`）を持ち、
コールバック内で世代不一致なら**結果を捨てて一時ファイルも消す**。
SDK 側のリクエスト自体はキャンセルできない（`getThumbnail`/`getPreview` は
`MegaCancelToken` を取らない）ので、**「捨てる」しか手がない**。
テキスト側（`startStreaming`）だけは `onTransferData` の戻り値で本当に打ち切れる。

### 4.3 `DownloadService` を経由してはいけない

`src/core/DownloadService.h:55-117` は「アクティブ 1 本 + `mPending` の直列キュー」で、
`DownloadController` 経由でステータスバーの進捗バーとファイル名を占有する
（`qml/components/StatusBar.qml:83-98`）。プレビュー取得をここに流すと、
選択を動かすたびにユーザのダウンロード表示が奪われる。**別系統にする。**

### 4.4 エラーをトーストしてはいけない

`ThumbnailController` は失敗時に `mNotifications->notifyError(...)` を呼ぶ
（`ThumbnailController.cpp:33-37`）。プレビューでは
**「プレビューが無い」が正常系**なので、これを踏襲するとテキストファイルを選ぶたびに
トーストが飛ぶ。`IMegaClient::getMyAvatar` のコメントが書いている
「失敗は例外的ではない」の扱いに寄せ、**失敗はペイン内の文言に落とし、通知は出さない**。

### 4.5 テキストは「上限以下なら全取得、超えたら拒否」

Explorer 本家がテキストプレビューを 50 KB で打ち切っているのに倣い、**先頭 N KB を切り出すのでは
なく、上限以下のファイルだけを丸ごと読む**方針とする。切り出し案より優れている点が 3 つある:

- **上限判定に通信が要らない。** `FileEntry::sizeBytes` は一覧取得の時点でモデルに載っているので、
  100 MB のログを選んでも**リクエストを一度も出さずに**「大きすぎます」を表示できる。
- **UTF-8 の途中でバイトを切る問題が消える。** N バイトで打ち切ると 3 バイト文字を分断して
  末尾が化けるため境界処理が要ったが、全取得ならその論点自体が無い。
  エンコーディング判定も完全なバイト列に対して行うほうが確実。
- **「全部見えているのか途中までなのか」が曖昧にならない。** 表示できたものは常に完全なファイル。

50 KB なら `startStreaming(node, 0, sizeBytes)` 一発、`onTransferData` も数回で終わる。
ディスクに何も書かないので §4.1 のキャッシュ問題ともそもそも無縁。

残る論点:

- **拡張子ホワイトリスト**が要る（`.txt .md .json .log .xml .csv .ini` ＋ソースコード系）。
  「テキストかどうか」を拡張子以外から事前に知る手段は無いため。
- **保険の NUL チェック**: ホワイトリストに合致しても中身がバイナリなら `Unsupported` に倒す。
  50 KB の全バイト走査は一瞬なので、先頭ブロックだけに限る必要はない。
- **エンコーディング**: UTF-8 決め打ちだと日本語の Shift_JIS ファイルが化ける。
  `QStringDecoder` + BOM 判定 + 「UTF-8 として不正なら CP932 で読み直す」程度の簡易判定が現実解。
  `src/core` は Qt 非依存の方針なので、**この判定は `src/qml` 側（Qt が使える層）に置く**。
- 上限値（50 KB）は `src/core` 側の名前付き定数に置き、QML には出さない。

### 4.6 一時ファイルの掃除 — 既存の穴が広がる

現状、`%TEMP%/MegaExplorerThumbnails`（`ThumbnailController.cpp:50`）と
`%TEMP%/MegaExplorerAvatars`（`AccountController.cpp:182`）は
**アプリ終了時に誰も消していない**。プレビューが 3 つ目になる。
プレビューは「使い捨て」なので、少なくとも**プレビュー用ディレクトリだけは
起動時 or 終了時に丸ごと消す**べき。既存 2 つも一緒に片付けるかは別途判断
（プレビューの実装ついでに直すなら、`CLAUDE.md` の「触ったファイルは同じ規則で掃除」に沿う）。

### 4.7 選択取得のコスト

`FileListModel::selectedEntries()`（`src/qml/FileListModel.cpp:202-217`）は
**`mEntries` を全走査する**。60 万行のフォルダで選択が変わるたびに呼ぶと重い。
プレビューが欲しいのは「単一選択のときの 1 件」だけなので、
`selectedHandles.length === 1` を先に見て、`cursorRow()` + `entryAt(row)`
（`FileListModel.cpp:219-230`、O(1)）で取るか、専用のアクセサを 1 本足すのが良い。
なお `entryAt` は現状 `sizeBytes` を返さないので、必要なら足す。

### 4.8 その他

- **ゴミ箱・共有フォルダ**: `getPreview` はノードが解決できれば通る。特別扱いは不要そうだが未確認。
- **オーバークォータ**: `API_EOVERQUOTA` も単に取得失敗として文言に落ちる。
- **フォーカス**: ペインを増やしても、`TabContentPane` の
  `StackLayout.onIsCurrentItemChanged` によるフォーカス受け渡し（`TabContentPane.qml:59-71`）を
  壊さないこと。プレビューペインはフォーカスを取りに行かない（`focusPolicy: Qt.NoFocus` 相当）。

---

## 5. Qt モジュールを増やす場合（ライセンス・在庫確認）

| モジュール | ライセンス | このマシンの Qt 6.11.1 msvc2022_64 に入っているか |
| --- | --- | --- |
| Qt Multimedia | LGPLv3 可 → **OK** | **入っている**（`lib/cmake/Qt6Multimedia`、`plugins/multimedia` に ffmpeg/windows プラグイン） |
| Qt PDF | LGPLv3 可 → **OK** | **入っていない**（`lib/cmake` に `Qt6Pdf` なし）。Qt Maintenance Tool での追加インストールが要る |
| Qt Svg | LGPLv3 可 → OK | 入っている（`qsvg.dll` イメージプラグインも） |
| Qt WebEngine | **GPLv3 のみ** → **使用不可** | — |

`CLAUDE.md` の「GPL 専用 Qt モジュールを入れると MIT + LGPL の建て付けが壊れる」に照らすと、
**WebEngine だけが明確に禁止**。PDF と Multimedia はライセンス上は問題ない。

利用可能な画像フォーマットプラグイン（`plugins/imageformats`）:
`gif / icns / ico / jpeg / svg / tga / tiff / wbmp / webp`（＋ Qt 内蔵の png / bmp / ppm / xbm / xpm）。
**RAW は無い**が、§1.2 のとおりサーバ側 JPEG を使うので影響しない。

**PDFium の二重リンクについて**: 本アプリは SDK 経由で
`build/msvc-debug/vcpkg_installed/x64-windows-mega/lib/pdfium.lib` を既に静的リンクしている。
Qt PDF も内部に PDFium を抱えるが、そちらは `Qt6Pdf.dll` という別 DLL に閉じており
`FPDF_*` を公開しないため、**リンク時のシンボル衝突は起きない見込み**（未検証）。
実害はバイナリサイズ程度。ただし前提として Qt PDF のインストール自体が未了。

---

## 6. QML 側の構造

### 6.1 ペインの置き場所 — `Main.qml` の `SplitView` の 3 枚目

`qml/Main.qml:243-371` の `SplitView` は現在 `SidePanel` + `StackLayout`（タブごとのペイン）の
2 枚構成。**その右に 3 枚目として `PreviewPane` を足す**のが素直。

- 幅の永続化は `treePanel` と同じ作法をコピーできる: `Component.onCompleted` で
  `SplitView.preferredWidth` に一発代入し、`onResizingChanged` で書き戻す
  （`Main.qml:284-291, 369-370`）。**ライブバインディングにすると SplitView の書き戻しと喧嘩する**、
  という既存コメントの理由がそのまま当てはまる。
- 表示 ON/OFF は `visible` で切る（`SplitView` は非表示の子を無視する）。

### 6.2 ウィンドウ単位か、タブ単位か → **決定: 表示 ON/OFF はタブ単位、幅はウィンドウ単位**

表示形式（`viewMode`）が **タブごと**（`TabContentPane.qml:48`）＋ `window.viewMode` は
「新規タブの初期値」でしかない、という既存の作りに**プレビュー ON/OFF も揃える**。

**ペインの実体は `SplitView` の 3 枚目に 1 個だけ**置き、可視性だけをタブに追従させる:

```
visible: window.currentPane?.previewVisible ?? false
```

タブ切替や選択変更のたびに取得し直す、という割り切りで押し切れる。追加物は
`viewMode` の三点セット（`previewVisible` / `initialPreviewVisible` /
`previewVisibleWriteBack`）を丸ごと写すだけで、実績のあるパターンをなぞる形になる。

**この選択のほうが良い点**: ステータスバーのプレビューボタンが隣の表示形式トグルと同じく
`root.currentPane` を読むことになり、**同じ行のボタンで参照先が食い違わない**
（ウィンドウ単位にすると、この行だけ `window` を直接読むボタンが混ざる）。

**受け入れる制約 — 幅はタブごとにできない。** `SplitView.preferredWidth` は
ペイン実体に付く attached property なので、実体が 1 個なら幅は全タブ共有になる。
タブごとの幅を得るにはペインを N 個立てることになり、`PreviewController` も一時ファイルも
N 系統に増える。割に合わない。既存の `treePanelWidth` も同じくウィンドウ単位なので、
**「表示するかはタブごと、どれだけ広いかはウィンドウごと」**で作法は揃う。

**受け入れる制約 — タブを行き来するたびに再取得が走る。** 「キャッシュしない」要件の
直接の帰結。プレビュー JPEG は 100〜300 KB 程度、テキストは 50 KB 以下なので許容範囲とする。
ただし**取得は `previewVisible` かつ現在タブのときだけ**に絞ること
（プレビュー OFF のタブは通信ゼロ）。

**注意点 2 つ**:

- `SplitView` は既定で**最後の「可視の」子**を fill 項目にする（Qt docs）。3 枚目を足すと
  fill がプレビュー側へ移りかねないが、`Main.qml:295` が `StackLayout` に
  `SplitView.fillWidth: true` を**明示済み**なので回避されている。この明示を消さないこと。
- 高速なタブ連打（Ctrl+Tab 長押し）は高速な選択移動と同じ競合を生む。§4.2 の世代カウンタは
  「選択が変わったとき」だけでなく**「現在タブが変わったとき」も進める**必要がある。

なお Qt には `SplitView.saveState()`/`restoreState()` という幅の永続化 API もあるが、
本リポジトリは `treePanelWidth` で「`Component.onCompleted` で一発代入 + `onResizingChanged` で
書き戻し」という手書きの作法を既に採っている（`Main.qml:284-291, 369-370`）。
プレビュー幅もそちらに揃える。既存分の作り直しは今回のスコープ外。

### 6.3 選択の受け渡し

ステータスバーが件数を取っているのと同じ経路
（`window.currentPane?.navController?.fileListModel`、`StatusBar.qml:66-67`）を
プレビューペインも使えば、タブ切り替えに自動で追従する。
更新の起点は `FileListModel::selectionChanged`。

### 6.4 ステータスバーのボタンと縦棒

- `StatusIconButton` は `StatusBar.qml:176-224` のインライン component。プレビューボタンは
  これをそのまま使える（`checked` は表示専用バインディング、`checkable: false` のまま、という
  既存の作法を守ること — 理由は `StatusBar.qml:136-143` のコメント）。
- **縦棒（セパレータ）は既存にない。** `Rectangle { Layout.preferredWidth: Theme.border.thin;
  Layout.preferredHeight: ~16; color: Theme.color.stroke }` 相当を足すことになる。
  高さ・上下マージンは実測で決める案件＝`docs/DESIGN_IMPROVEMENT.md` 側の仕事。
- **アイコンのグリフが未定。** Segoe Fluent Icons なら ``(ShowBcc) 等ではなく
  Explorer 本家のプレビューウィンドウに近いのは **`` View**。
  `Theme.qml:127-135` の作法どおり、(1) Segoe MDL2 Assets の cmap にも在ることの確認、
  (2) 実際に描画して COLR カラーグリフでないことの確認（`viewGrid` の E80A 失敗例）
  の 2 段を通してから採用すること。

### 6.5 CMake

新規 `.qml` を `qt_add_qml_module` の `QML_FILES` に足したら
**必ず `cmake --preset msvc-debug` で再構成**（`CLAUDE.md` の AOT `qmlcache_loader.cpp` の件）。

---

## 7. 段階案

| 段 | 内容 | 主な追加物 |
| --- | --- | --- |
| A | ペインの器 + ON/OFF + 「プレビューできません」だけ | `PreviewPane.qml`、`StatusBar` のボタンと縦棒、`Settings` の幅/表示状態 |
| B | 画像・動画・PDF（＝サーバ側プレビュー JPEG） | `IMegaClient::getPreview`、`PreviewService`、`PreviewController`、`FileEntry::hasPreview` |
| C | テキスト表示（50 KB 以下は全取得、超過は拒否） | `IMegaClient` にストリーミング取得を追加、拡張子ホワイトリスト、エンコーディング判定 |
| D | （スコープ外候補）動画再生 / PDF ページ送り | `USE_LIBUV` 有効化 or Qt PDF 導入 |

**決定（2026-08-08）: Phase 15 のスコープは A + B + C。** D は独立した phase に切る
（`USE_LIBUV` 有効化にせよ Qt PDF 導入にせよ、ビルド構成の変更を伴うため）。

---

## 8. この調査で確認していないこと

**実装後の追記（2026-08-09）**: Phase 15 は A+B+C まで実装済み。下記のうち 2 件は
「実測で確認」ではなく**設計で論点ごと消した**形で決着しているので、先に書いておく
（経緯は `docs/PROGRESS.md` の Phase 15 エントリ）。

- `SplitView` の子を `visible: false → true` に戻したときの `preferredWidth` は、
  **`onVisibleChanged` で毎回入れ直す**ことにしたので保たれるかどうかを問わない。
- `startStreaming` のスレッド境界は、**キャンセル機構を一切持たない**と決めたことで
  論点が消えた。`onTransferData` の `return false` は maxBytes 超過専用。
- なお §4.1（`Image` の URL キャッシュ）と §4.2 の一時ファイル削除も、
  **SDK が書いた JPEG を即読み込んで即削除しメモリで持つ**方式にしたため両方とも不要になった。
  §4.6 の一時ディレクトリ掃除は、プレビュー分についてはゴミが出なくなったので見送り
  （既存の Thumbnails/Avatars の蓄積は未解決のまま）。

- `getPreview` の実測レイテンシ（`getThumbnail` と同等と推測しているだけ）。
- `SplitView` の子を `visible: false` → `true` に戻したとき `SplitView.preferredWidth` が
  保たれるか。attached property の値なので機構上は残るはずだが、実機未確認（§6.2）。
  タブ切替のたびに幅が初期値へ戻るなら、`treePanelWidth` と同じ一発代入で復元する必要がある。
- 共有フォルダ／ゴミ箱内ノードでの `getPreview` の挙動。
- Qt PDF を実際に入れた場合の PDFium 二重リンクの実挙動（§5 は「衝突しない見込み」止まり）。
- `startStreaming` を `IMegaClient` に載せたときのスレッド境界（`onTransferData` が
  SDK 内部スレッドで来ることは確実だが、打ち切り (`return false`) と GUI 側の
  破棄タイミングの競合は要設計）。
