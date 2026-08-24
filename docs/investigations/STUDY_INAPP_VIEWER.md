# アプリ内ビューア（画像 / 動画 / PDF） — 実現可能性調査

> **状態: 未着手の机上調査。** 2026-08-25 時点のコード（`db8357e`）に対する調査で、コード変更・
> 実測は一切していない。ROADMAP にも載せていない。
> 結論は §0、SDK 側の配信手段は §1、ビルド前提は §2、形式ごとの表示手段は §3、
> Qt WebEngine を入れる場合のコストは §4、着手順の案は §7。

想定している機能は「一覧でファイルを選ぶ／ダブルクリックすると、アプリ内で中身が見られる」。
出発点の問いは二つ:

1. MEGA SDK は**一時フォルダへ落とさずに**中身を配信できるのか
2. そのために **Qt WebEngine** が要るのか

---

## 0. 結論

**どちらも「否」寄り。** ダウンロードは不要 —— SDK にローカル HTTP サーバが内蔵されており、
`http://127.0.0.1:<port>/<handle>/<name>` という URL を Qt 側に渡すだけで済む。Range リクエストに
対応しているのでシークも効く。復号は SDK 内で行われ、**平文はディスクに落ちない**。

WebEngine も大半の用途では要らない。画像は Qt Quick の `Image` が、動画は Qt Multimedia の
`MediaPlayer` が、その URL をそのまま食える。**唯一 PDF だけが素直にいかない**（§3-3）。

代わりに前提条件が一つある: **その HTTP サーバの API は今のビルドに入っていない。**
`#ifdef HAVE_LIBUV` の中にあり、`USE_LIBUV` は Windows では既定 OFF（§2）。

---

## 1. SDK 側の配信手段

### A. 内蔵 HTTP プロキシサーバ（本命）

`megaapi.h:21916` からの `#ifdef HAVE_LIBUV` ブロック。

- `httpServerStart(localOnly=true, port=4443, useTLS=false, ...)` (`megaapi.h:21974`) —
  初期化は同期。`true` が返れば接続受付可。`port=0` で空きポート自動選択。
- `httpServerGetLocalLink(MegaNode*)` (`megaapi.h:22210`) →
  `http://127.0.0.1:<port>/<NodeHandle>/<NodeName>`。戻り値は `delete[]` で解放する所有権付き。
- 既定の制限モードは `TCP_SERVER_ALLOW_CREATED_LOCAL_LINKS` — **自分が発行したリンクのノードしか
  配信しない**。フォルダノードの配信は既定 OFF、ファイルは ON。
- `httpServerSetMaxBufferSize` (`megaapi.h:22296`) / `httpServerSetMaxOutputSize`
  (`megaapi.h:22332`) でバッファ調整。字幕サポートは `httpServerEnableSubtitlesSupport`。

**Range 対応は実装を読んで確認済み**（ここが動画シークの可否を決める）:

- リクエストの `Range:` ヘッダを解析 → `httpctx->rangeStart` (`megaapi_impl.cpp:35878`)
- 応答は `HTTP/1.1 206 Partial Content` + `Content-Range: bytes <start>-<end>/<total>`
  (`megaapi_impl.cpp:37410-37411`)、`Accept-Ranges: bytes` (`同 37397, 37422`)

つまり HTTP としては普通のシーク可能なストリームで、クライアント側に特別な実装は要らない。

### B. `startStreaming`

`megaapi.h:17623`。`startStreaming(node, startPos, size, listener)` でチャンクが
`MegaTransferListener::onTransferUpdate`（`getLastBytes` / `getDeltaSize`）と `onTransferData`
に届く。libuv 非依存なのが利点だが、**シーク・先読み・バッファリングを自前で書く**ことになり、
実質 `QIODevice` 実装 1 本。A が使えるなら選ぶ理由は薄い。

ただし PDF のようにランダムアクセスが必要で、かつ HTTP 経由では扱えない形式（§3-3）については、
B を土台にした `QIODevice` が選択肢に戻ってくる可能性はある。

### 共通の落とし穴: `setStreamingMinimumRate`

`megaapi.h:17634` 付近。ストリーミング転送は数秒の猶予のあと平均速度を監視し、**既定の最低速度を
下回ると `API_EAGAIN` で失敗する**（既定値は「音声/動画に妥当なレート」とだけ書かれている）。
回線が細い環境で「勝手に止まる」形の不具合になりうるので、0（チェック無効）を明示的に入れるか
どうかは実装時に決める必要がある。内蔵 HTTP サーバも内部ではストリーミング転送を使うため、
同じ監視が効く見込み（未確認 — §6）。

---

## 2. ビルド上の前提: `USE_LIBUV`

**現状 OFF。** 有効化しないと §1-A の API はヘッダにすら現れない。

- `third_party/sdk/cmake/modules/sdklib_options.cmake:21` — デスクトップ系では
  `option(USE_LIBUV ... OFF)`（iOS/Android では :13 で ON）。
- vcpkg 側の feature は `third_party/sdk/vcpkg.json:43` の `use-libuv` → ポートは `libuv`。
  overlay ports には無いので通常のレジストリから来る（pin 済み baseline に存在: `libuv` あり）。
- `HAVE_LIBUV` の定義は `sdklib_target.cmake:384`、リンクする Win32 ライブラリの追加は同 `:429`
  （`Kernel32/Iphlpapi/Userenv/Psapi`）。

**罠**: SDK の `vcpkg_management.cmake:99` は `USE_LIBUV` が ON なら feature を足してくれるが、
それは SDK が最上位プロジェクトのときの経路。うちは `CMakePresets.json:22` で
`VCPKG_MANIFEST_FEATURES` を明示しており、vcpkg ツールチェーンは `project()` 時点、つまり
`add_subdirectory(third_party/sdk)` (`CMakeLists.txt:34`) より前に走る。したがって
**プリセット側に自分で両方書く**必要がある:

- `VCPKG_MANIFEST_FEATURES` に `use-libuv` を追加
- `USE_LIBUV=ON` を cacheVariables に追加（既定 OFF なので明示が要る。openssl/freeimage/ffmpeg/
  pdfium が明示されていないのは、あちらは既定 ON だから）

feature を増やすと vcpkg の再インストールが走るので、初回は相応の時間を見ておくこと。

---

## 3. 表示側 — 形式ごとの手段

### 3-1. 画像

- 縮小版なら既存の `PreviewService`（MEGA の preview 属性）でそもそも足りる場面が多い。
  原寸が要るときだけ §1-A の URL を `Image.source` に渡す。Qt Quick は http URL を
  そのまま読める（内部の `QNetworkAccessManager` 経由）。
- Qt Network は Qt Quick が既に引いているので、**新規の Qt モジュール依存は増えない**。

### 3-2. 動画

- Qt Multimedia の `MediaPlayer.source` に URL を渡すだけ。§1-A が Range 対応なのでシーク可。
- **Qt Multimedia はインストール済み**（`C:/Qt/6.11.1/msvc2022_64` に `Qt6Multimedia.dll`、
  plugins/multimedia に `ffmpegmediaplugin.dll` と `windowsmediaplugin.dll`）。
- FFmpeg の DLL 衝突は起きない見込み: `CMakeLists.txt:36` のコメントのとおり SDKlib の FFmpeg
  探索は既に **Qt 側の FindFFmpeg / Qt が配る avcodec-61 等**に解決されている。Qt Multimedia の
  ffmpeg バックエンドも同じものを使う。
- 配布サイズは Qt の FFmpeg DLL 群のぶん増えるが、これは SDK が既に要求している DLL と同一。

### 3-3. PDF — ここだけ素直にいかない

Qt PDF（`QtQuick.Pdf` / `QPdfDocument`）は `QIODevice` に**ランダムアクセス**を要求するため、
http URL をそのまま渡せない。取りうる案は三つ:

1. **全部メモリに取ってから渡す** — `QBuffer` に載せて `QPdfDocument::load(QIODevice*)`。
   一時ファイルすら作らない。PDF は動画ほど大きくないので現実的。**現時点の推し。**
2. **`startStreaming` ベースの `QIODevice` を書く** — 遅延読み込みできるが実装コストが跳ねる。
   数百 MB の PDF を想定しないなら割に合わない。
3. **WebEngine の内蔵 PDF ビューア（pdf.js）に URL を渡す** — URL のまま扱えて、Range も
   ビューア側が勝手に使う。ただし §4 のコストが丸ごと乗る。

**なお Qt PDF は現在のインストールに入っていない**（`C:/Qt/6.11.1/msvc2022_64/lib/cmake` に
`Qt6Pdf*` が無い）。Maintenance Tool での追加が要る。pdfium 自体は vcpkg 経由で SDK が既に
ビルドしているが、それは SDK 内部のサムネイル生成用で、こちらから叩ける API ではない。

---

## 4. Qt WebEngine を入れる場合のコスト

ライセンス上は問題ない（WebEngine は LGPLv3、Chromium は BSD）。MIT アプリ + LGPL Qt という
`CLAUDE.md` の構成は崩れない。効いてくるのは実務コストのほう:

- **未インストール**（`lib/cmake` に `Qt6WebEngine*` が無い）。Maintenance Tool で追加が要る。
- 配布サイズが 150MB 前後増える。`QtWebEngineProcess.exe` という別プロセスも付く。
- `QtWebEngineQuick::initialize()` を `QGuiApplication` の**生成前**に呼ぶ必要があり、
  `main.cpp` の合成ルートの初期化順序に制約が入る。
- QWindowKit（フレームレス窓）との同居は未検証。WebEngine ビューは別プロセスで合成される
  ネイティブ寄りの面なので、ヒットテストや角丸との相性は実際に見るまで分からない。

**PDF 1 形式のために丸ごと入れるのは割に合わない**というのが現時点の評価。分岐点は
「HTML / Office 文書 / テキストもアプリ内で見たいか」。そこまで行くなら WebEngine 一択になる。

---

## 5. 既存コードへの影響

- `IMegaClient` に「ノードのローカル URL を得る」1 メソッドが増える。SDK 直叩き禁止の原則
  （`CLAUDE.md` の Architecture 節）どおり、URL 生成もこのポートの内側に閉じる。
- **サーバのライフサイクルをどこが持つか**が設計上の唯一の争点。案は「ログイン後に一度
  `httpServerStart`、ログアウトで stop」。ビューアを開くたびの start/stop は、初期化が同期とはいえ
  無駄が多い。`MegaSdkClient` の寿命に合わせるのが素直。
- ポートは固定 4443 にせず `port=0`（自動選択）にして、返ってきたポートを URL 生成に使う。
  4443 が他プロセスに取られている環境で沈黙して失敗するのを避けるため。
- セキュリティ: `localOnly=true` + 既定の制限モードでも、**同一マシンの他プロセスは URL さえ
  知っていれば取れる**。URL にノードハンドルが入るので推測は現実的でないが、ログに URL を
  出さないことは決めておく。TLS は `useTLS` で可能だが証明書の用意が要るので、localhost 限定なら
  見合わない。

---

## 6. 未確認事項

- 内蔵 HTTP サーバにも `setStreamingMinimumRate` の速度監視が効くのか（効くなら低速回線で
  再生が落ちる）。実測が要る。
- libuv を有効にしたときのビルド時間・バイナリサイズの増分。
- Qt Multimedia の ffmpeg バックエンドが、Qt が配る DLL と vcpkg の FFmpeg のどちらを実行時に
  掴むか。`CMakeLists.txt:36` の記述からは Qt 側で一致するはずだが、`PATH` の順序次第。
- QWindowKit + WebEngine の同居（§4 を選ぶ場合のみ）。

---

## 7. 着手順の案

1. **ビルドの PoC を先に**。`USE_LIBUV=ON` + `use-libuv` を通し、`megatool` にローカル URL を
   吐くサブコマンドを足して、curl で Range 取得できるところまで確認する。ここが一番リスクが
   高い（vcpkg 再インストール、リンク周り）ので先に潰す。
2. **画像**。`IMegaClient` に URL 取得を足し、プレビューペインの原寸表示から。
3. **動画**。Qt Multimedia を依存に追加、`MediaPlayer` + シークの実機確認。
4. **PDF**。§3-3 の案 1（全部メモリ）で入れる。Qt PDF の追加インストールが要る。
5. WebEngine は「HTML/Office も見たい」となった時点で再検討。この調査書の §4 が出発点。
