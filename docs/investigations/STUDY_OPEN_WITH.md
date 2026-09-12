# 「Open with」— 外部プログラムにストリーミング URL を渡す仕組みの調査

> **状態: 方針調査。動く PoC を一度作ったが、確認後に破棄している（コミットしていない）。**
> 2026-09-12、`develop` の `d258e0b` 時点のコードに対する調査。どのフェーズにも入っておらず、
> `docs/REQUESTS.md` にも未投入。
> **結論: 実現可能で、しかも土台は全部揃っている。** SDK のローカル HTTP サーバーが返す URL
> （`ViewerController::sourceUrl()` が既に返しているもの）を外部プログラムの引数に渡すだけで動く。
> 本当の論点は実装量ではなく、(1) HTTP サーバーがアプリのプロセス内にいるので**閉じると止まる**
> こと（§4-1）、(2) 内蔵ビューアが守っていた「復号済みバイトをディスクに置かない」性質が
> 渡した先の判断に移ること（§4-3）、(3) この方式で開けるのは **URL を引数に取れるプログラムだけ**
> という境界（§4-4）。§5 はこの仕組みで他に何ができるかのアイデア置き場で、**WebDAV（§5-1）が
> (3) の境界そのものを消す可能性がある**。

---

## 0. 結論（先に要約）

1. **新規の配管はほぼ不要。** `MegaSdkClient::streamingUrl()`（`src/mega/MegaSdkClient.cpp:1013`）が
   `http://127.0.0.1:<port>/<Base64Handle>/<名前>` を返し、`ViewerController::sourceUrl()` が
   それを QML に渡している。内蔵ビューアの動画・画像・PDF はすべてこの URL を食っている。
   外部に渡すのは、同じ文字列を `QProcess::startDetached` に渡すだけ。
2. **Range 対応なのでシークが効く。** `megatool stream`（`tools/megatool.cpp` の `cmdStream`）が
   既に Range ヘッダを投げていて、サーバー側が応えることは確認済み。
3. **メニューへの載せ方も既存機構で足りる。** `Open as` が使っている submenu の `group` 機構
   （`qml/ActionCatalog.qml:343` の `groups`）にもう 1 グループ足すだけ（§3-1）。
4. **PDF は例外的に「難しそうに見えて一番簡単」。** 現行が全文メモリ載せなのは Qt PDF の都合
   であってサーバーの制約ではないので、外部経路ではその制約ごと消える（§2-3）。
5. 残る判断は全部 §4。**実装より先にそこを決めるべきで、特に §4-1 は決めないと形が決まらない。**

---

## 1. 現状 — ストリーミング URL は既に存在する

- `MegaSdkClient::streamingUrl(handle)`（`src/mega/MegaSdkClient.cpp:1013`）が SDK の
  ローカル HTTP プロキシを遅延起動し、ノードへの URL を返す。起動は
  `httpServerStart(true, 0, false)` = **localhost 限定・ポート自動・TLS なし**。
  ポートを 0 にしているのは SDK 既定の 4443 が他プロセスに取られていると起動が
  裸の `false` で失敗するため（同箇所のコメント）。
- 停止はシャットダウン時の `httpServerStop()`（`src/mega/MegaSdkClient.cpp:312`）。
  `~MegaApi` は libuv サーバーを止めないので、ここで明示的に止めている。
- QML 側の入口は `ViewerController::sourceUrl()` 一本で、
  `ImageViewer.qml:97` / `VideoViewer.qml:63` / `AudioViewer.qml:66` / `PdfViewer.qml:50`
  がいずれもこれを `source` に入れている。
- **外部プログラムを起動する前例も既にある。** `LocalFolderController` が
  `QDesktopServices::openUrl()` で関連付けプログラムを開き（`src/qml/LocalFolderController.cpp:78`）、
  `revealInExplorer()` が `QProcess` で explorer.exe を叩いている（同 `:27`）。
  つまり「外部を起動する」こと自体は新しい行為ではない。

## 2. PoC で確認したこと

`showViewer()`（`qml/Main.qml`）で `kind === "video" || kind === "pdf"` を Chrome に逃がす、
という最小の変更を一度入れて実際に動かした。変更は 3 ファイル・約 30 行。**確認後に破棄した**ので
ツリーには残っていない。

- **2-1. 動画。** Chrome がそのまま再生し、シークも効く。ただし Chrome が再生できるのは
  mp4/webm 系だけなので、mkv や avi は「ダウンロードするか」のバーになる。
  VLC・mpv ならここは問題にならない見込み（未確認）。
- **2-2. 起動方法。** Chrome も Edge も PATH に載っていないので、実行ファイルを直接指す必要がある。
  この機体では Chrome が `C:\Program Files\Google\Chrome\Application\chrome.exe`、
  Edge が `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`（Edge は x86 側なのが罠）。
  `QStandardPaths::findExecutable()` は既定インストールでは当たらないので、既知パスを先に見て
  それを後段のフォールバックにする形になった。
- **2-3. PDF が一番素直だった。** 現行の `PdfPageItem` はファイル全体をメモリに載せてから描く
  （`src/qml/PdfPageItem.h` のコメント）。これは Qt PDF がシーク可能な `QIODevice` を要求する
  ことへの妥協で、`STUDY_INAPP_VIEWER.md` §3-3 がその判断を、`STUDY_PDF_LAZY_LOADING.md` が
  その見直しを扱っている。**外部に URL を渡す経路はこの制約の外側にある** — ブラウザの PDF
  ビューアは http URL を受け取って自分で部分取得するので、こちらの全文取得より素直に出る。
  大きい PDF ほど初回表示までの差が出るはずで、ここは実測する価値がある。

## 3. 方針

### 3-1. メニュー — `Open with > Browser`

`Open as` と同じ作りでよい。必要なのは:

- `src/core/MenuAction.h` の enum に項目を追加（`OpenAs*` は `:16`〜`:20`）
- `src/core/MenuActionResolver.cpp` の適用表に行を追加（`OpenAs*` は `:85`〜`:128`、
  `FilesOnly` + `SingleOnly` + 全 `ViewKind`）と、`actionId` 文字列（同 `:333`〜`:342`）
- `qml/ActionCatalog.qml` にエントリを追加し、`"group": "openWith"` を付ける。
  グループは `:343` の `groups` に 1 行足すだけで submenu になる（`:359` の畳み込みが
  同じ group のエントリを自動でまとめ、位置はグループ先頭メンバーの位置になる）

**グレーアウトの扱いは要検討。** `Open as` は「拡張子が間違っている場合こそ使う」ので決して
グレーにならない（`MenuActionResolver.cpp:84` のコメント）。`Open with > Browser` も同じ理屈で
常時有効にするのが筋に見えるが、テキストファイルをブラウザに投げると普通に表示されてしまう点は
むしろ利点かもしれない（内蔵テキストビューアがまだない）。

### 3-2. 既定は Edge、設定で Chrome

Edge は Windows 同梱なので「必ずある」を前提にできる唯一のブラウザ。既定はこれでよい。
設定で Chrome に切り替えられると嬉しい、という要望。

実装としては §3-3 の一般形の**既定エントリ 1 つ**として持つのが素直で、「ブラウザ」を特別扱い
した専用設定を先に作ると、後で §3-3 を入れるときに二重になる。

### 3-3. カスタムプログラム — 「拡張子 → コマンド」の組

設定に拡張子とコマンドラインのテンプレートを登録できるようにする。URL を差し込む
プレースホルダ（`%U` など）を置く想定。

**保存先が既存の仕組みに素直に乗らない点は注意。** 今の設定は `Main.qml:161` の `Settings` に
`property alias` を並べる形で、スカラー値しか置けない。可変長のリストはこの形に入らないので、
C++ 側に置き場が要る。前例は `src/platform/QSettingsPinnedFolderStore` で、ピン留めフォルダの
リストを QSettings に入れている。同じ作りにするのが自然。

CLAUDE.md の「Persisted user data は互換性を持つ」に該当するので、キーの形は最初に決め切ること。

### 3-4. `Open local file` / `Open local location` を `Open with` に入れるか → **入れない**（実施済み）

「この 2 つを `Open with > Default app` / `Open with > Windows Explorer` に移す」という案が出たが、
**採らなかった。** 理由は 2 つ。

1. **指している対象が違う。** `Open with > Browser` が開くのは **MEGA のノード**（ストリーミング
   URL）だが、`Open local file` が開くのは**ローカルフォルダ側の別ファイル**で、名前が一致して
   いるだけのもの。2 つのツリーが一致しているかは何も検証していない（`LocalLinkService.h`）。
   同じサブメニューに並べると「同一操作のプログラム違い」に見えるが、実際には**別の中身を
   開きうる**。
2. **`Windows Explorer` はラベルが動作と合わない。** `openLocation` がやるのは
   `explorer.exe /select,"パス"`（`src/qml/LocalFolderController.cpp:27`）で、「開く」ではなく
   「選択して見せる」。加えてこれは `ActionTarget::Any`（`MenuActionResolver.cpp:154`）で
   フォルダにも出るので、他が `FilesOnly` になるサブメニューの中で 1 つだけ条件が違う。

代わりに**ローカル側を独立したグループにした**（2026-09-12 実施）。トップレベルの行数を減らす
という元の狙いは満たしつつ、参照先の違いが名前に出る。

- グループ名は `Local path`。単なる `Local` では何を指すか分からないため。
- 中身のラベルは `Open file` と `Show in Explorer` に変更。グループ名が「ローカル」を言うので
  `Open local file` は冗長になり、`Show` は上の理由 2 の動作と一致する。
- 実装は `qml/ActionCatalog.qml` のみ：2 エントリに `"group": "localPath"` を足し、`groups` に
  1 行追加。C++ 側（`MenuAction` / `MenuActionResolver`）は**無変更** — グループは表示の話で、
  メンバーと順序は依然リゾルバの担当という既存の分担どおり。

**結果として `Open with` は MEGA のノードだけを対象にするサブメニューになる**ので、§3-3 の
カスタムプログラムもその前提で設計してよい。

## 4. 未決事項（実装より先に決めるもの）

### 4-1. HTTP サーバーの寿命 — 最大の論点

サーバーはアプリのプロセス内にいて、終了時に止まる（`src/mega/MegaSdkClient.cpp:312`）。
**MegaExplorer を閉じた瞬間、外部プレーヤーの再生も PDF の表示も死ぬ。** 内蔵ビューアなら
「ウィンドウを閉じたら終わり」が自明だが、外部に渡すと**別アプリで再生中なのにこちらを閉じられる**
という、今までなかった状況が生まれる。選択肢:

- 何もしない（渡した時点で「アプリを開いたままにしてください」と 1 回通知する）
- 外部に渡した URL が生きている間は終了時に確認ダイアログを出す
- トレイに残る

「何もしない」が一番小さいが、ユーザーから見て最も分かりにくい壊れ方をする。ここは決め打ちで
進めず、明示的に選ぶべき。

### 4-2. URL は capability であること

`ViewerController.h` の `sourceUrl()` のコメントが「**結果を決してログに出すな — URL は
capability である**」と書いている通り、この URL を持っていればそのノードは誰でも取れる。
制限モードは SDK 既定の `ALLOW_CREATED_LOCAL_LINKS`（`megaapi.h:22077` 付近）なので、
一度リンクを作ったノードは**アプリが動いている間ローカルの他プロセスからも取得できる**
（localhost 限定・平文・認証なし）。

外部プログラムに渡すとその URL は**コマンドライン引数として他プロセスから見える**ことになる。
1 ユーザーのデスクトップアプリという前提では許容範囲だと思われるが、内蔵ビューアの時には
存在しなかった露出なので、意識的に許容する判断が要る。

ポートが毎回変わるので URL は起動ごとに別物。保存・共有の用途には使えない。

### 4-3. 「復号済みバイトをディスクに置かない」性質の扱い

`STUDY_INAPP_VIEWER.md` §3-3 が一時ファイル案を退けた理由がこれで、`PdfPageItem` が
全文メモリ載せという妥協をしてまで守っている性質でもある。**外部に渡すと、この性質は
渡した先の判断に移る** — ブラウザで「保存」すればディスクに残るし、未対応コンテナは
そもそもダウンロード扱いになる（§2-1）。

これは劣化ではなく**責任の移譲**だと整理できるが、そう整理したことをどこかに書いておかないと、
後から「なぜ §3-3 の判断を破ったのか」に見える。

### 4-4. この方式で開けるのは URL を取れるプログラムだけ

VLC・mpv・ffmpeg 系・ブラウザは問題ない。**パスしか受け取らないプログラム（多くのデスクトップ
アプリ）は対象外。** §3-3 の「カスタムプログラム」に期待しすぎると、ここで壁に当たる。
一時ファイルを経由すれば越えられるが、それは §4-3 を正面から捨てる話になるので別扱い。
§5-1 はこの壁を別の方向から消す案。

## 5. この仕組みで他にできそうなこと（アイデア、未検証）

### 5-1. WebDAV — §4-4 の壁を消す可能性

**同じ HTTP サーバーに `httpServerGetLocalWebDavLink()` がある**（`megaapi.h:22223`）。
関連 API も一式揃っている（`httpServerGetWebDavLinks` `:22235`、
`httpServerGetWebDavAllowedNodes` `:22247`、`httpServerRemoveWebDavAllowedNode` `:22255`）。
Windows は WebDAV URL をドライブレターに割り当てられるので、成立すれば
**「URL を取れるプログラム限定」という制約が丸ごと消えて、パスしか受け取らないアプリも対象に入る。**

ただし Windows の WebDAV リダイレクタ（WebClient サービス）は遅い・不安定という評判があり、
ファイルサイズ制限もレジストリ既定で存在するはず。**どこまで実用になるかは完全に未検証**で、
やるなら独立した STUDY が要る。なお、これはマウントであって双方向同期ではないので、
CLAUDE.md が scope 外と書いている「full bidirectional local sync」には当たらないと解釈できる。

### 5-2. 字幕の自動読み込み — 外部経路でしか価値が出ない

`httpServerEnableSubtitlesSupport(bool)`（`megaapi.h:22153`）。SDK のドキュメントが
「メディアプレーヤーは動画と同じ階層に字幕ファイルを探しに来る」と明記していて、この機能は
まさにそのリクエストを通すためのもの。有効にすると、MEGA 上で動画の隣に置いた `.srt` が
プレーヤー側で勝手に読まれる。

**内蔵ビューアでは一切意味がなく、外部プレーヤー経路でのみ効く。** 呼び出し 1 行。
Open with を入れるなら同時に入れる価値が高い。

### 5-3. フォルダ再生 — m3u を書いて渡す

**今このアプリにプレイリストの概念はない。** 画像だけが前後送りを持ち（`Main.qml` の
`imageSequence()`、`viewerKind() === "image"` で絞っている）、`AudioViewer.qml` には
次の曲へ進む手段すらない。

フォルダ内の動画・音声のストリーミング URL を並べた `.m3u` を一時生成してプレーヤーに渡せば、
「このフォルダを頭から順に再生」がプレーヤー側の機能として手に入る。自前でプレイリスト UI を
作るのに比べて労力が桁で違う。ただし m3u 自体は一時ファイルとしてディスクに落ちる（中身は
URL だけなので §4-3 には抵触しない）。

### 5-4. ffmpeg で実メタデータを読む

ffmpeg は http URL を直接開けるので、**ダウンロードせずに**尺・コーデック・解像度・ビットレートが
取れる。MEGA のノード属性はこれらを持っていないので、プロパティダイアログの情報量が上がる。
FFmpeg は SDK 経由で既にリンク済み（`VCPKG_MANIFEST_FEATURES` の `use-ffmpeg`）なので依存は増えない。
同じ理屈で「動画の任意の位置のフレームをサムネイルにする」もできる。

### 5-5. FTP サーバー（一応）

`ftpServerStart()`（`megaapi.h:22399`）と `ftpServerGetLocalLink()`（同 `:22520`）も存在する。
`ftp://` を取れるプレーヤーは一部あるが、HTTP で足りる場面ばかりなので**今のところ使い道は薄い**。
存在だけ記録しておく。

## 6. 着手順の案

1. **`Open with > Browser`（既定 Edge、設定で Chrome）** — §3-1 + §3-2。§4-1 の答えを先に決める。
2. **字幕サポートの有効化** — §5-2。1 行で、1 と組み合わせたときだけ効く。
3. **カスタムプログラム（拡張子 → コマンド）** — §3-3。保存先を `QSettingsPinnedFolderStore`
   と同じ作りにする。1 のブラウザはこの仕組みの既定エントリに寄せる。
4. 以降は独立したネタ。**WebDAV（§5-1）は別 STUDY が前提**、m3u（§5-3）と
   ffmpeg メタデータ（§5-4）はそれぞれ単独で成立する。
