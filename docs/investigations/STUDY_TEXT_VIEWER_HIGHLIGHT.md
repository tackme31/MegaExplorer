# アプリ内テキストビューア + シンタックスハイライト — 実現可能性調査

> **状態: 調査のみ（2026-09-11、`e8406ba` 時点）。コードは変更していない。**
> **結論は §0** —— 難しくない。テキストを読む・文字コードを判定する・表示する部品はプレビュー
> ペイン用に揃っていて、ビューア側では `viewerKind()` が Text をわざと空文字にしているだけ。
> ハイライトは Qt 標準の `QSyntaxHighlighter` を `TextArea` の文書に付ければ済み、決めるべきは
> **言語ごとの規則をどこから持ってくるか**の 1 点（§3）。Qt 自体は規則を 1 言語も持たない。
> **推奨は §3-6 の軽量ライブラリ 2 本（どちらも MIT、QtGui のみ）** —— 自前で書くのは
> 車輪の再発明、KSyntaxHighlighting は **JSON の定義ファイルが GPL のみ**などライセンスの選別が要る
> （§3-3）うえに取り込みが重い。推奨の着手順は §4、未確定事項は §5。

依頼: ビルトインビューアでテキストを開けるようにし、拡張子から形式を判別してハイライトしたい。
`.md` はマークダウン、`.js` は JavaScript として色付きで。**マークダウンは HTML にレンダリング
するのではなく、VS Code のエディタで開いたときのように見出しに色が付く程度**（コピペしやすいので）。

---

## 0. 結論

1. **ビューア窓（ハイライトなし）は小さい。** 拡張子の判定（`PreviewKind::Text`、約 150 拡張子）、
   ファイルをディスクに書かずにメモリへ読む口（`IMegaClient::readFileContent`）、文字コード判定
   （`decodePreviewText`: BOM / UTF-8 / CP932 / バイナリ除外）はすべて既存。新しく要るのは
   `TextViewer.qml` と、それを開く `ViewerController` 側の口だけ。
2. **ハイライトの仕組みも Qt 標準で足りる。** `QSyntaxHighlighter` を `TextEdit.textDocument`
   （`QQuickTextDocument::textDocument()`）に付ける。付けた書式は**表示時にだけ合成され、文書本体は
   変わらない**（Qt 公式ドキュメント `QSyntaxHighlighter::setFormat`）ので、選択してコピーすれば素の
   テキストが出る —— 「コピペしやすい」要件はこの方式なら自動的に満たされる。Qt の
   `TextEdit.MarkdownText`（HTML 風レンダリング）は要件に反するので使わない。
3. **規則の出どころは三つ。**
   - **案 C: 軽量ライブラリ（推奨、§3-6）** —— qmarkdowntextedit の `MarkdownHighlighter`（.md 用）と
     QSourceHighlite（コード用）。どちらも MIT、QtGui だけに依存、ソース数ファイルを取り込むだけ。
     Markdown 側は Qt Quick の文書を直接受け取るプロパティまで持っている。M。
   - **案 B: 自前で書く（§3-4）** —— 案 C と同じ種類のもの（キーワード表 + 正規表現）を一から書く
     ことになり、車輪の再発明。案 C が使えなかったときの退路。M。
   - **案 A: KSyntaxHighlighting（§3-2）** —— 400 超の言語で精度も最上。ただし vcpkg ポートは使えず、
     定義ファイルのライセンス選別が要る（§3-3）。L。
4. **推奨: 土台 → 案 C の順で入れ、案 A は後から差し替え可能な形にしておく**（§4）。依頼の範囲
   （md の見出しに色、js に色）は案 C で届く。ハイライタを「QML から `document` と `fileName` を渡す
   C++ 要素」1 個に閉じ込めれば、案 A への乗り換えはその要素の中身だけで済む。

---

## 1. 既にあるもの

| 部品 | 場所 | 状態 |
| --- | --- | --- |
| 拡張子 → 種別 | `src/core/PreviewKind.cpp` `textExtensions()` | md / js / json / cpp / py / sh / yaml ほか約 150 |
| メモリへの全量読み込み | `IMegaClient::readFileContent(handle, maxBytes, …)` | キャンセル不可（数十 KB 前提の設計） |
| 範囲読み・キャンセル可 | `IMegaClient::readFileRangeStreamed` | zip 展開で使用中。`onChunk` が false で中断 |
| 文字コード判定 | `src/qml/TextPreviewDecoder.cpp` `decodePreviewText` | BOM・UTF-8・CP932、NUL を含めば非テキスト |
| テキスト表示 | `qml/components/PreviewPane.qml` の `TextArea` | Consolas、折り返し、50 KB 上限（`kMaxTextPreviewBytes`） |
| ビューアの振り分け | `ViewerController::viewerKind` / `Main.qml` `openViewer` | **Text は意図的に空文字** → 開かない |
| 右クリック「開く」 | `FileContextMenu.qml` | `viewerKind() !== ""` で有効化 —— "text" を返せば自動で付く |

つまりビューア側の追加は、既存の PdfViewer（198 行）/ ArchiveViewer（400 行）と同じ
「1 ファイル 1 ウィンドウ」の形に、もう 1 種類足す作業になる。

## 2. ビューア窓（ハイライト抜き）

- `viewerKind()` の `PreviewKind::Text` を `"text"` にし、`Main.qml` の `openViewer` に
  `textViewerComponent` を足す。
- 読み込みは ArchiveViewer と同形: `ViewerController::openText(handle, sizeBytes, owner)` が
  状態（Loading / Ready / TooLarge / NotText / Failed）と `text` を持つ小さな QObject を返し、窓が
  親になる。**PreviewService は通さない**（latest-wins なのでペインの選択移動で読み込みが捨てられる。
  `openArchive` のコメントと同じ理由）。
- **ビューアでは 50 KB 上限を撤廃する**（2026-09-11 決定。プレビューペインの 50 KB はそのまま）。
  そうすると `readFileContent` の「数十 KB なのでキャンセル不要」という前提が崩れるので、ビューアは
  `readFileRangeStreamed` で読み、窓を閉じたら `onChunk` で false を返して止めるのが筋。
  巨大ファイルでの挙動は §5-1。
- 表示は `TextArea`（読み取り専用、`selectByMouse`、等幅）。折り返しの ON/OFF と、あれば行番号。
  テーマは `Theme.qml` の色を使い、ライト / ダーク両方で確認する。

## 3. ハイライト

### 3-1. 取り付け方（案 A / B 共通）

C++ で `QSyntaxHighlighter` 派生を QML 要素として登録し、QML 側は

```qml
TextArea { id: body; textFormat: TextEdit.PlainText; ... }
SyntaxHighlighter { document: body.textDocument; fileName: root.fileName; light: Theme.isLight }
```

の形にする。`fileName` から言語を選ぶのは C++ 側。KDE の `org.kde.syntaxhighlighting` QML モジュール
（`KQuickSyntaxHighlighter`、`textEdit` / `definition` / `theme` プロパティ）がまさにこの形で、
方式としての実績はある。この要素の外側（QML・ViewerController）は案 A / B で変わらない。

### 3-2. 案 A: KSyntaxHighlighting を取り込む

- 利点: 定義 413 ファイル（400 超の言語）、Kate / Qt Creator 品質。Markdown 定義は見出し・強調・
  リンク・リスト・引用に加え、**コードフェンス内を言語別に色付け**する（VS Code の見え方に最も近い）。
  テーマ 30 本同梱（`github-light` / `github-dark` / `vscodium-dark` など、テーマは全て MIT）。
  ライブラリ本体（`src/lib`、53 ファイル）は MIT。
- **vcpkg ポート（`syntax-highlighting` 6.28.0）は使えない。** 依存に vcpkg 版 `qtbase` があり、
  `C:/Qt/6.11.1` を使うこのビルドに Qt をもう 1 本ソースから建てることになる。
- 上流の CMake をそのまま `add_subdirectory` する場合の要件: **ECM ≥ 6.30（extra-cmake-modules）**、
  **Perl（必須）**、Qt ≥ 6.9（6.11 なので可）、Qt6 Network / Test、ビルド中に
  `katehighlightingindexer` を建てて走らせる。Perl は PHP / HTML 系の生成定義のためだけに要る。
  サブモジュールが 2 本（本体 + ECM）増え、ECM の KDE 向けコンパイル設定がそのディレクトリに入る。
- **別案: ライブラリの `src/lib` だけを取り込み、CMake は自前、定義 XML はこちらで選んで独自 qrc に
  入れる。** `Repository` は index なしのプレーンな XML フォルダも読める（`loadSyntaxFolder`、
  `addCustomSearchPath`）ので indexer・Perl・ECM が全部不要になる。代わりに ECM が生成していた
  export / version / logging ヘッダ 3 本を自前で用意する。**上流に追従する手間はこちらが持つ**。

### 3-3. 案 A のライセンス（要選別）

アプリは MIT、Qt は LGPL で、GPL を持ち込まない方針（CLAUDE.md）。定義ファイルの `license` 属性を
上流 master で数えた結果:

| license | 本数 | 備考 |
| --- | --- | --- |
| MIT | 149 | typescript, rust, powershell ほか |
| LGPL 系 | 約 125 | cpp, xml, html, css, yaml, java, sql, bash, ini, cmake, toml … |
| GPL 系（GPL / GPLv2 / GPLv2+ / GPLv3+） | 38 | **json**, **go**, **r**, sed, roff, nasm … |
| 空欄・表記なし | 16 + α | **javascript**, **python**, csharp |
| その他（BSD, Public Domain, WTFPL, Artistic, FDL …） | 少数 | |

- **markdown.xml は "GPL,BSD"** —— ヘッダに「GPL と BSD のデュアルライセンス、2019 年以降の変更は
  MIT」。BSD 側を選べば使える。
- **json.xml は GPL のみ** → 同梱できない。`.json` は案 B の自前 JSON か、JavaScript 定義で代用。
- **javascript.xml / python.xml は表記なし** —— 上流の CONTRIBUTING は「貢献は MIT」だが、表記の
  ない古いファイルに何が適用されるかは要確認。依頼の例そのものなので、ここが曖昧なまま同梱はしない。
- LGPL の定義はバイナリに埋め込むことになるが、LibRaw を LGPL で静的リンクしソース公開している
  現状と同じ扱いで済む。取り込んだものは `scripts/gen_third_party_notices.py` に載せる。

→ 案 A は「全部入り」にはできず、**許可リスト方式で XML を選んで同梱**するのが前提になる。
§3-2 の「別案」（自前 CMake + 独自 qrc）はこの選別とも噛み合う。

### 3-4. 案 B: 自前のハイライタ（案 C が使えなかったときの退路）

§3-6 の軽量ライブラリがまさにこの形のものなので、一から書く理由は案 C が合わなかった場合に限る。

`QSyntaxHighlighter::highlightBlock` を行ごとの正規表現 + 行をまたぐ状態（`setCurrentBlockState`）で
書く。依頼の範囲なら以下で足りる:

- **Markdown**: 見出し（`#`〜`######`、レベルで色を変えても可）、`**太字**` / `*斜体*`、
  `` `code` ``、コードフェンス（開始〜終了を block state で追う）、リンク `[..](..)`、リスト記号、
  引用 `>`、水平線、表の `|`。VS Code のエディタ表示と同程度。フェンス内は同じエンジンの言語別
  ハイライトを呼べば言語付き着色もできる（任意）。
- **C 系ファミリ**（js / jsx / tsx / c / cpp / cs / java / kt / go / rs / swift / dart / qml …）:
  キーワード表を言語ごとに差し替え、文字列・コメント（`/* */` は block state）・数値は共通。
  js のテンプレート文字列は block state 1 本追加。
- **その他**: JSON（キー / 文字列 / 数値 / true・false・null）、`#` コメント系（py / sh / rb /
  yaml / toml / ini）、XML / HTML（タグ・属性・文字列・コメント）、SQL、diff。

限界: 正規表現リテラル（js の `/…/`）、HTML 内の `<script>` の入れ子、ヒアドキュメントなどは
取りこぼす。誤判定で「以降が全部文字列色」になる事故を避けるため、行をまたぐ状態は
コメント・フェンス・テンプレート文字列に限る。

### 3-5. 比較

| | 案 C 軽量ライブラリ | 案 B 自前 | 案 A KSyntaxHighlighting |
| --- | --- | --- | --- |
| 言語数 | 約 25（Markdown + コード） | 10 ファミリ前後 | 選別後でも 300 前後 |
| Markdown | 見出し・強調・リンク・表 + フェンス内の言語別着色 | 見出し・強調・コード・リンク | 案 C と同等 |
| 精度 | キーワード表 + 正規表現 | 同左 | 文脈を追う状態機械（最上） |
| 依存 | MIT ソース 4〜6 ファイル | なし | サブモジュール 1〜2 本、上流 CMake なら Perl / ECM |
| ライセンス作業 | notices に 2 件追加 | なし | 許可リスト作成、notices 更新 |
| サイズ | M | M | L（取り込み・CMake・選別で分割が要る） |

### 3-6. 案 C: 軽量ライブラリ（推奨）

どちらも Qt の `QSyntaxHighlighter` 派生で、QtWidgets には依存しない（`#include` を実物で確認）。
作者系統が同じで、QSourceHighlite は qmarkdowntextedit のコード着色部分を切り出したもの。

| | [qmarkdowntextedit](https://github.com/pbek/qmarkdowntextedit) の `MarkdownHighlighter` | [QSourceHighlite](https://github.com/Waqar144/QSourceHighlite) |
| --- | --- | --- |
| 用途 | `.md` | コードのファイル単体 |
| ライセンス | MIT | MIT |
| 取り込むもの | `markdownhighlighter.{h,cpp}` + `qownlanguagedata.{h,cpp}`（約 11,000 行、大半はキーワード表） | `qsourcehighliter.{h,cpp}` + `languagedata.{h,cpp}` + themes（約 7,600 行） |
| 保守 | 活発（最終コミット 2026-08-29、QOwnNotes の本体エディタ） | 停滞気味（最終コミット 2024-09） |
| QML | **`Q_PROPERTY(QQuickTextDocument* textDocument)` を持つ** —— `TextArea.textDocument` をそのまま渡せる | `QTextDocument*` を渡す（C++ で `textDocument()` を剥がす 1 行） |
| 言語 | フェンス内: bash, c, cpp, c#, cmake, css, go, html/xml, ini, java, js, json, make, nix, php, python, qml, r, rust, sql, ts, v, yaml, toml, forth, systemverilog ほか | Bash, C, C++, C#, CMake, CSS, Go, HTML, INI, Java, JavaScript, JSON, Make, PHP, Python, QML, Rhai, Rust, SQL, TypeScript, V, XML, YAML, Vex |

注意点:

- **見出しは既定で文字が大きくなる**（H1 = 1.6 倍 … H6 = 1.1 倍）。静的な `setTextFormats` で書式を
  差し替えられるので、「色だけ・大きさは同じ」の VS Code 風に設定し直す。記号を小さくして隠す書式
  （`setFontPointSize(0.01)`）もあるので、同じく無効化する。
- テーマは QSourceHighlite に Monokai（ダーク）1 本と既定の色しかない。ライト / ダークの色は
  `Theme.qml` 側で持ち、両ライブラリの書式に流し込む。
- 両者はキーワード表をそれぞれ持っていて重複する（qmarkdowntextedit 側が新しく言語も多い）。
  `MarkdownHighlighter` の保護メンバ `highlightSyntax()` はブロックの状態値で言語を選ぶので、派生
  クラスで状態を固定すれば .js 単体にも使え、QSourceHighlite を省ける可能性がある。**内部実装への
  依存なので未検証**。まずは 2 本並べる。
- 第三者コードは `/W4` の警告ゼロ方針に引っかかるので、`MegaExplorerWarnings` を付けない別の静的
  ライブラリターゲットにする。取り込み後は `scripts/gen_third_party_notices.py` を回す。
- 覚えている範囲で退けた候補: QScintilla（GPL）、GNU source-highlight（GPL）、Tree-sitter（MIT で
  高精度だが、言語ごとに巨大な生成 C ファイルとハイライト用クエリが要り、KSyntaxHighlighting より
  重い）、highlight.js を `QJSEngine` で走らせる案（BSD。出力が HTML の span なので行単位の書式に
  変換し直す手間がかかる）。

## 4. 推奨の着手順（ROADMAP に載せるなら）

1. **テキストビューア窓（プレーン表示）** — S〜M。§2。`viewerKind` → "text"、`TextViewer.qml`、
   `readFileRangeStreamed` によるキャンセル可能な読み込み、上限、折り返し。これ単体で価値がある。
2. **案 C の取り込み: `MarkdownHighlighter` + QSourceHighlite** — M。§3-1 の QML 要素と §3-6。
   third_party への配置と静的ライブラリ化、見出しを色だけにする書式設定、ライト / ダークの色を
   `Theme.qml` から流し込む、拡張子 → 言語の対応表、notices の再生成。
3. （任意）**プレビューペインにも同じハイライタを付ける** — S。`PreviewPane.qml` の `TextArea` に
   要素を 1 個足すだけ。
4. （任意・将来）**案 A への差し替え** — L。言語数が足りないと感じたら。§3-2 の別案 + §3-3 の
   許可リスト。2 の要素の中身だけを入れ替える。

## 5. 未確定事項・リスク

1. **巨大ファイルでの速度は未計測。** 上限を撤廃したので、数十 MB のログも全量をメモリに読み、
   Qt Quick の `TextEdit` がレイアウトし、`QSyntaxHighlighter` が文書を設定した時点で全ブロックを
   同期的に処理する（QSourceHighlite の公称は 10 万行で約 0.4 秒）。固まるようなら、一定サイズ以上は
   ハイライトだけ切るのが最小の手当て。Release ビルドで実測する（Debug で判断しないこと、CLAUDE.md）。
2. **`.ts` は TypeScript として開けない。** `videoExtensions()` が MPEG-TS として先に取るため
   （`textExtensions()` のコメントどおり）。中身を嗅いで振り分けるかは別件。
3. **拡張子なしのファイル**（`Makefile`, `Dockerfile`, `.gitignore`, `LICENSE`）は拡張子判定では
   開けないが、後続の「Open as...」（`STUDY_OPEN_AS.md`）で開ける予定なので、ここでは扱わない。
   その経路でも言語を選べるよう、ハイライタ要素は `fileName` とは別に言語を直接指定できる形にしておく。
4. **コピーで色が付かないこと**は Qt のドキュメント上は保証されている（§0-2）が、実機で
   Word 等への貼り付けを 1 回確認する。フォント名（Consolas）は HTML 側に乗る可能性がある。
5. zip の中のテキストをアーカイブビューアから直接開く件は範囲外（展開の口は別にある）。
