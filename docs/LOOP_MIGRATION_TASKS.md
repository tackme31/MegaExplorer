# ループエンジニアリング導入タスク（作業用・完了後に削除）

> **この文書は一時的な作業リストです。** 対応が終わったら削除するので、`CLAUDE.md` や他の
> `docs/*.md` からリンクしないこと。恒久的に残す内容（ループの運用方法・ROADMAP の位置づけ）は
> タスク L12 で `CLAUDE.md` 側へ書き、この文書には残さない。

元ネタ: https://zenn.dev/green_tea/articles/e39e3726a449c9
参照実装: https://github.com/atsushi-green/vscode-Markdown-WYSIWYG の
`.claude/skills/evolve/SKILL.md` / `docs/ROADMAP.md` / `docs/roadmap-done.md`（2026-08-11 時点の
main を取得して確認済み）。ただし後述のとおり本リポには**そのまま移植できない差分が5つ**ある。

## 確定した方針（2026-08-11 のやりとりで決定）

1. `docs/FEATURE_IDEAS.md` は `docs/ROADMAP.md` へ**移行して廃止**する（併存させない）
2. `evolve/NNN` ブランチは**公開 origin へ push してよい**（master へのマージは人間）
3. 周期は **2時間**（`/loop 2h /evolve`）
4. **`/W4` 警告ゼロをハードゲート**にする
5. コードレビューは **`qt-development-skills:qt-cpp-review` / `qt-qml-review`** に任せる
6. **undo は out of scope を維持**する。ROADMAP の「見送り (blocked)」へ理由付きで置き、ループには
   絶対に着手させない。`CLAUDE.md` のスコープ宣言はそのまま
7. GUI は **`launch` / `shot` まではループに許可**する（起動時クラッシュや真っ白画面の検知に使う。
   見た目の良し悪しの判断は人間）
8. **`drive`（実マウス・キーボードの乗っ取り）は2層構成で制御**する。層1 = `PreToolUse` フックに
   よる時間帯判定、層2 = ntfy のスマホ push（詳細は L8。RC を使わない理由は方針15）
9. **`megatool`（MEGA 操作 CLI）を作る**（L6）
10. 環境変数は **`.claude/settings.local.json` の `env` ブロック**に置く。ただし**パスワードなど
    機密性の高いものは Windows のユーザー環境変数**としてユーザーが直接設定する
11. **ループは Qt Creator（`qtcreator` MCP）に一切依存しない。** IDE がメモリを食うため、ループを
    回したまま他の作業（ゲーム等）ができる状態を保つ。確認済み: `loop_verify.sh` が使う
    cmake/ctest は CLI のみ、`ui_shot.py` も `cmake.exe` を直接叩いており（`UI_SHOT_CMAKE` で
    上書き可）、`qt-cpp-review`/`qt-qml-review` も MCP を呼ばない。**唯一の MCP 依存は Serena**
    （実測 298MB。ダッシュボードを開かなければこの程度）
12. **Serena は使う。ただし落ちていたら無条件に使わない。** サイクル冒頭で死活確認し、死んで
    いたら復旧を試みず**縮退モード（Grep/Read）で続行**する。再接続はモデル側から不可能で、
    人間がデスクに戻るまで解決しないため、待っても止まる時間が延びるだけ（詳細は L9 の 0. と 3.）
13. **GitHub issue は使わない。** バグ・要望の受け口は `ROADMAP.md` の「バグ修正」セクション。
    長い診断が要るものは既存の `docs/investigations/BUG_*.md` の慣習に乗せる。`docs/issues/` の
    ような新しいフォルダは作らない——キューが二重になり同期のコストが増えるだけで、ROADMAP の
    表がそのまま issue トラッカーとして機能する。これにより SKILL から issue 同期節がまるごと
    消え、参照実装で最も込み入っていた重複判定ロジック（`/issues/N` の URL 突き合わせ）も不要になる。
    外出先で気づいたことは手元にメモしておき、デスクに戻ってから ROADMAP へ書く（方針15）
14. **MEGA はテストアカウントを常用する。** 本番へ戻す仕組みは用意しない（資格情報を保管して
    おけば、必要になったとき UI からログインし直すだけで済む）。副次的な利点として、
    `.screenshots/` に本番のファイル名が写らなくなり、`.gitignore` が警告している漏洩リスクが
    そもそも消える。`megatool` は環境変数で独立にログインするのでアプリ側のセッションと無関係
15. **Remote Control は使わない。** モバイル push が「送信要求は通るが配信されない」状態にある。
    2026-08-11 に5回連続で不達を確認（接続中・ターミナル非フォーカス・`No mobile registered` 無し
    の条件下）。既知の未修正バグで、claude-code issues [#85168](https://github.com/anthropics/claude-code/issues/85168)
    ／[#84488](https://github.com/anthropics/claude-code/issues/84488) がいずれも open・公式回答なし
    （#85168 は「13回中3回しか届かない」と定量報告）。**通知は `scripts/ntfy-send.sh`（ntfy）へ
    置き換える。** 内蔵の `PushNotification` はループから呼ばない——成功を返しても届かないため、
    「通知したから人間が気づいたはず」という前提が置けない。RC の対話操作（スマホからセッションに
    話しかける）自体は動くが、通知が来ない以上「気づいて開く」導線が成立しないので依存しない

## 参照実装との差分（本リポ固有で、SKILL.md に必ず入れる必要があるもの）

| # | 差分 | 対応タスク |
|---|---|---|
| D1 | 全 git ライトを `scripts/git_unlock.sh` 経由にする必要がある（参照実装には該当工程が無い） | L9 |
| D2 | `npm run` 4本に相当するものが無い。ビルドは VS ジェネレータ + プリセットで、`QML_FILES` 変更時は reconfigure 必須、警告はフルリビルドでしか出ないものがある | L5 |
| D3 | ドキュメントが巨大（`PROGRESS.md` 68k tokens 等）。参照実装のように「毎サイクル changelog と README を更新」をそのまま真似すると1サイクルで破綻する | L4 / L9 |
| D4 | GUI アプリなので見た目・操作感の検証がループ内でできない | L7 / L8 / L9 |
| D5 | 組み込み `/code-review` はユーザー起動専用の可能性が高い。代わりに allow 済みの Qt レビュースキルを使う | L11 |

## 測定済みの事実（2026-08-11 / commit 18befa4）

- 差分なしビルド **7.7秒** / 3ターゲット全ビルド **3分02秒** / `ctest` **573件 66秒 全 pass**
  → 1サイクルの機械検証は **2〜5分**。2時間周期に対して十分軽い
- **`/W4` 警告は0件**（全ターゲットのオブジェクトを消して強制フル再コンパイルして確認済み）。
  方針4のハードゲートはそのまま採用できる
- **アプリを起動したままビルドすると `LNK1104: appMegaExplorer.exe を開くことができません` で
  失敗する**（上記の確認中に実際に踏んだ）。無人ループを確実に止めるので L5 で対策する
- `git push` は Git Credential Manager で認証済み（`--dry-run` 確認済み）。open issue は現在0件、
  `gh` は `tackme31` で認証済み
- `.gitignore` には既に `env.local.ps1`（"local debug credentials (MEGA_EMAIL/MEGA_PWD etc.)"）と
  `.claude/settings.local.json` の両方が入っている。どちらも commit される心配はない

---

# フェーズ0 — 着手前にユーザーがやること（チェックリスト）

**ここが埋まっていないと後続の自動作業が止まる。** 上から順に。

- [x] **H1. テスト用 MEGA アカウントでアプリにログインし直す。**
      アプリは保存済みセッションで自動ログインするので、ここを切り替えないとループが本番アカウント
      を開く。以後は**テストアカウントを常用**する（方針14）。本番の資格情報だけはパスワード
      マネージャ等に保管しておくこと。確認方法: アプリを起動してアカウント表示がテスト用になっている
- [x] **H2. テストアカウントのメールアドレスを共有する（または自分で書く）。**
      置き場は `.claude/settings.local.json` の `env` ブロック、キー名は
      `MEGAEXPLORER_TEST_ACCOUNT`。値さえもらえばこちらで書き込める
- [x] **H3. テストアカウントのパスワードを Windows のユーザー環境変数に設定する。**
      `setx MEGAEXPLORER_TEST_PASSWORD "..."`。キー名はこれで固定する（L6 の megatool が読む）。
      **設定後、Claude Code のセッションを開き直さないと見えない**ので、H3 は試運転（L13）より前に
      済ませること
- [x] **H4. ntfy でスマホ通知を受け取れるようにする（層2）。** 2026-08-11 完了・疎通確認済み。
      1) スマホに ntfy アプリを入れ、`.claude/settings.local.json` の `env.NTFY_TOPIC` と同じ
      トピックを Subscribe する
      2) 送信は `bash scripts/ntfy-send.sh <本文> [タイトル] [優先度1-5]`
      **トピック名はこのチャンネル唯一の秘密**（知っていれば誰でも購読も偽装もできる）なので、
      gitignore 済みの `settings.local.json` にだけ置く。`scripts/ntfy-send.sh` は公開リポジトリに
      入るため、スクリプト本体にも本文書にも書かない
- [ ] **H5. `drive` 切り替え用 bat のショートカットをデスクトップに置く。**
      bat とフックは L8 でこちらが作る（6h / 8h / 12h / off の4枚）。既定は OFF。
      **席を離れるときは画面をロックしないこと**——ロック中は `drive` の入力がアプリに届かない
- [x] **H6. PC のスリープ／休止を無効にする。** ディスプレイのオフは問題ない。
      `/loop` はセッションローカルなので、セッションが死ぬとループも止まる。
      なお2時間ごとに**3分前後 MSBuild が全コアを使う**（+ `ctest` が1分）。裏でゲーム等をする場合
      その間だけ重くなる。Qt Creator は起動不要（方針11）なので常駐メモリは Claude Code と Serena
      のぶんだけで済む
- [x] **H7. Pro アカウント2つの運用方針を決める。**
      `/loop` はセッションに紐づくため、**アカウントを切り替えるにはセッションを開き直す＝ループを
      張り直す**必要がある。上限に当たったときに手動で張り直すのか、片方だけで回すのかを決めておく
      => 上限に来たら自動で貼り直すので、特に対応は不要です。
- [x] **H8.（不要になった）** GitHub issue は使わない（方針13）。バグ・要望は `ROADMAP.md` の
      「バグ修正」セクションに直接書く（外出先からの追記手段は用意しない。方針15）
      => 不要とのことなので完了
- [x] **H9.（初回のみ）アプリを終了しておく。** 起動したままだとビルドが LNK1104 で失敗する。
      2回目以降は L5 のスクリプトが自動で閉じる

---

# フェーズA — ドキュメント構造の移行

## L1. `docs/ROADMAP.md` を新設し、FEATURE_IDEAS.md を移行する

**目的**: ループの外部記憶＝実行キューを作る。ループが毎サイクル最初に読む唯一のバックログ。

**フォーマット**（参照実装に合わせる）:

- 冒頭に運用ルール（上から順に着手／完了したら `roadmap-done.md` へ移す／`blocked` の使い方／
  GitHub issue 取り込みの表記規約／このリポで実装しないもの）
- 凡例: **S**（1時間相当）/ **M**（半日相当）/ **L**（要分割・そのまま着手禁止）
- セクション: 「バグ修正（最優先）」→「優先度: 高」→「中」→「低」→「見送り (blocked)」
- 各セクションは `| 状態 | 項目 | サイズ | メモ |` の表

**移行作業**: 現 `FEATURE_IDEAS.md` の **見出し項目25件 + 「細かい改善」6箇条 = 31件**を表へ移す。
散文の説明はメモ欄へ落とす（情報を捨てない）。移行時の判断:

- **サイズ付けの実態**: S 相当は「細かい改善」の6件がほぼ全部。残りは M〜L で、ゴミ箱・アルバム・
  詳細検索・プラグイン対応などは画面1つ分以上＝ **L**。つまり **初期状態で着手可能な todo が
  6件程度しかない**。ループを回し始める前に、L のうち優先度が高いものを2〜3件だけ先に S/M へ
  分割しておくこと（さもないと最初の数サイクルが「分割するだけのサイクル」で埋まり、試運転の
  判断材料が得られない）
- **undo は「見送り (blocked)」へ**（方針6）。`FEATURE_IDEAS.md` の「「もとに戻す」アクション」は
  todo にしない。理由（MEGA にネイティブ undo が無く、全操作に逆操作の手当てが要る）をメモ欄に
  書き、`PROGRESS.md` のフェーズ20a–23 前文へリンクする
- 「ウィンドウの外観と状態」は無関係な3件（Mica/Acrylic・最大化状態の保存・タイトルバーアイコン）
  の寄せ集めなので、移行時に3行へ割る
- 各項目のメモ欄に、関連する `docs/investigations/STUDY_*.md` があればリンクを張る
  （L 分割時にループがそれを読めるようにするため）

**完了条件**: `ROADMAP.md` だけを読めば次に何を実装すべきか一意に決まる。todo の先頭5件が
すべて S または M である。

## L2. `docs/roadmap-done.md` を新設する

`| 完了日 | 項目 | サイズ | コミット | メモ |` の表のみ。ループはここに **1行だけ**書く。
「実機確認が残っている」旨もこのメモ欄に書かせる（人間が後で確認する際の入口になる）。

## L3. `docs/FEATURE_IDEAS.md` を廃止する

`docs/archive/` へ移すのではなく**削除**でよい（内容は全件 ROADMAP へ移り、履歴からも辿れる。
`archive/` は「リンクが解決し続けるため」の場所であって、まだどこからもリンクされていない
2026-08-10 追加のこのファイルには当てはまらない）。参照元の書き換えが2箇所:

- `CLAUDE.md` 冒頭の companion docs リスト
- `docs/PROGRESS.md` 冒頭（「unimplemented feature ideas live in docs/FEATURE_IDEAS.md」）

## L4. `PROGRESS.md` / `DESIGN_IMPROVEMENT.md` の役割を再定義する

**問題**: `PROGRESS.md` は現在「ロードマップの単一情報源」かつフェーズ単位・1エントリ100行・
68k tokens。1サイクル1機能のループとは粒度もフェーズ番号体系も合わず、毎サイクル追記させると
ファイルが破綻する。

**決めること（そして CLAUDE.md へ書くこと）**:

- **実行キューの単一情報源は `ROADMAP.md`** に移る。`PROGRESS.md` の Roadmap 節は
  「これまでのフェーズの記録」に降格し、今後の予定は持たない
- **ループは `PROGRESS.md` に書かない。** 書くのは `roadmap-done.md` の1行だけ
- `PROGRESS.md` への100行エントリは、**人間が切った「フェーズ」だけ**のもの。ループのサイクルが
  たまたま大きな設計判断を含んだ場合は、`roadmap-done.md` のメモにその旨を書き、人間が後から
  フェーズとしてまとめるか `docs/investigations/` の STUDY にするかを決める
- **`DESIGN_IMPROVEMENT.md`（見た目の作業）はループのスコープ外**と明記する
- **`docs/investigations/` への新規 STUDY/SPEC 作成もループにはさせない**（1サイクルに収まらない）。
  ただし L 項目の分割時に既存の STUDY を**読む**のは必須

---

# フェーズB — 検証基盤とテストアカウント

## L5. `scripts/loop_verify.sh` を作る

参照実装の `npm run check-types && lint && test:unit && compile` に相当する単一エントリポイント。
SKILL.md に長い cmake コマンドを4本ベタ書きしないため、かつ毎サイクルの出力トークンを抑えるため。

**やること（順序が意味を持つ）**:

1. **起動中の `appMegaExplorer.exe` を閉じる。** 起動したままだとリンクが `LNK1104` で落ちる
   （測定中に実際に踏んだ）。閉じられなければ即エラーで抜け、報告に理由を書かせる
2. `--reconfigure` 指定時、または `CMakeLists.txt` の `QML_FILES` に差分があるときは
   `C:/Qt/Tools/CMake_64/bin/cmake.exe --preset msvc-debug` を先に走らせる
3. `cmake --build --preset msvc-debug`（3ターゲット）
4. 出力から `warning C` を抽出し `third_party` を除外。**1件でも残ったら exit 1**（方針4）。
   併せて `LNK[0-9]+` も拾い、LNK1104 のときは「アプリが起動していないか」を促す文言を出す
5. `ctest --preset msvc-debug`
6. 出力は「各ステップの成否 + 失敗時のみ該当行」に圧縮する。成功時は数行で終わること

**注意**: `cmake` はフルパス必須（PATH 上のものは Strawberry Perl の 3.29 で、`CMakeCache.txt` を
壊してから失敗する）。**Qt Creator も `qtcreator` MCP も使わない**（方針11）——
`CLAUDE.md` は警告チェックに MCP を「preferred」と書いているが、ループはこのスクリプトだけを使う。生成ソース（moc/qmlcachegen/型登録）の警告はフルリビルドでしか出ないので
`--full` オプションを用意する（各ターゲットの `*.dir` を消してから建て直す。全消しすると SDK まで
巻き込んで数十分かかる）。

## L6. `megatool`（MEGA 操作 CLI）と `mega-cli` スキル

**目的**: 動作確認用のフィクスチャ（フォルダ・ファイル・お気に入り・ゴミ箱の中身）を GUI 操作
なしで用意する。ループにとっても人間にとっても効く。

**形**: `MegaExplorerCore` を link する `add_executable(megatool ...)` を追加し、`IMegaClient`
（既に login / fetchNodes / createFolder / startUpload / remove を持つ）を叩く。サブコマンド案:
`whoami` / `ls <path>` / `mkdir <path>` / `put <local> <path>` / `rm <path>` /
`fixture reset`（テスト用ツリーを既知の状態へ作り直す）。

**認証**: `MEGAEXPLORER_TEST_ACCOUNT`（メール、`settings.local.json`）と
`MEGAEXPLORER_TEST_PASSWORD`（Windows ユーザー環境変数）でログインする。アプリの保存セッションに
依存しないので、アプリが本番アカウントを開いていても CLI 側は必ずテストアカウントを見る。

**注意**: 認証情報をリポジトリに一切書かない。`megatool` はパスワードをログにも出さないこと
（`MegaSdkLogger` のログレベルに注意）。

## L7. テストアカウントのガード

**問題**: `ui_shot.py launch` は保存済みセッションで**自動ログインする**。そのセッションが本番
アカウントのままだと、ループが破壊的操作を本番で試しうる。

**やること**:

1. `megatool whoami` が「アプリの保存セッションが今どのアカウントか」を答えられるようにする
   （`AccountService::identity()` が `email` を持っている）
2. ループがアプリを起動する前、および MEGA を変更する検証を行う前に `whoami` を実行し、
   `MEGAEXPLORER_TEST_ACCOUNT` と一致しなければ**そのサイクルを中止して報告**する
3. 一致しなかった場合の復旧手順（テストアカウントでログインし直す）を報告文に含めさせる

## L8. `drive` の2層ガード

**層1 — 明示的な ON/OFF スイッチ + `PreToolUse` フック**

時間帯による自動判定ではなく、**ユーザーが席を立つときに明示的に ON にする**方式。

- **環境変数は使わない。** `setx` は既に起動しているプロセスに届かず、Claude Code のセッションと
  その子プロセスであるフックは**古い値を見続ける**（効かせるにはセッションを開き直す＝ループを
  張り直すことになる）。フックが `reg query HKCU\Environment` を直接読めば回避できるが遠回り
- 代わりに**フラグファイル**を使う。どのプロセスからも即座に見える。Claude Code 自身の
  `CLAUDE_CLIENT_PRESENCE_FILE` と同じ作法
- 置き場は **`%LOCALAPPDATA%\MegaExplorerLoop\drive-allowed`**。リポジトリ内に置くと
  ループの `git status` チェックと紛らわしいので外に出す
- **期限付きにする。** ON のまま消し忘れ、翌朝デスクで作業中にマウスを奪われるのが最悪の事故。
  ファイルには許可の期限（epoch 秒）を書き、フックが期限切れを拒否する

```
scripts/drive_on_6h.bat    -- 期限を書いたフラグファイルを作る（6時間）
scripts/drive_on_8h.bat    -- 同（8時間）
scripts/drive_on_12h.bat   -- 同（12時間）
scripts/drive_off.bat      -- フラグファイルを消す
```

実体は共通スクリプト1本（時間を引数で受ける）＋薄いラッパ3枚にする。ダブルクリック／ショート
カットだけで切り替えられるようにするのが目的なので、ラッパ側に引数は要らない。

**フックの判定**（`.claude/settings.local.json` の `hooks.PreToolUse`、matcher は `Bash`）:
コマンドに `ui_shot.py drive` を含むときだけ判定し、**フラグファイルがあり、かつ期限内**なら
`{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"}}`。
それ以外は **`deny` ＋理由**（`scripts/drive_on.bat` で有効化できる旨を書く）。**既定は OFF**。

**画面ロックとの関係（重要）**: `drive` は `SendInput` / `SetCursorPos` を使うため、**ワーク
ステーションがロックされている間は入力がセキュアデスクトップへ行き、アプリに届かない**。
つまり「ロックしたら自動 ON」は *drive が効かないときだけ ON にする* ことになるので採用しない。
席を離れるときは **ロックせず、ディスプレイのオフだけ**にすること（`shot` は `PrintWindow` なので
ディスプレイオフでは撮れる）。

> **`ask` にしてはいけない。** permission prompt と `AskUserQuestion` は答えるまで開いたままで、
> 5分で閉じる `dialogExpiry` の対象外。無人時に `ask` を返すとサイクルが無限に止まる。`deny` なら
> ループは「drive をスキップした」と報告して次へ進める。

**層2 — ntfy のスマホ push**（H4 で設定済み）: 想定外のプロンプトが出たときの保険。
`bash scripts/ntfy-send.sh` をループから叩く。内蔵の `PushNotification` は使わない（方針15）。

**SKILL.md 側**: `drive` が拒否されたら**失敗ではなくスキップ**として扱い、「人間に確認して
ほしい観点」に回す、と書く（L9）。

---

# フェーズC — ループ本体

## L9. `.claude/skills/evolve/SKILL.md` を書く

参照実装（172行）を土台に、以下を本リポ向けに差し替える。**節構成は参照実装に合わせる**
（0.事前チェック → 1.issue 取り込み → 2.選択 → 3.実装 → 4.テスト → 5.レビュー → 6.ドキュメント →
7.commit&push → 8.報告 → 禁止事項）。

- **0.事前チェック**: `evolve/NNN` の連番算出はほぼそのまま流用可（既存のマージ済みローカル
  ブランチ `about-this-app` 等は3桁完全一致に引っかからないので影響なし）。**master 上で作業しない**。
  加えて **L7 のアカウント確認**と、下記の **Serena 死活確認**をここに入れる
- **0の Serena 死活確認（方針12）**: `claude mcp list` を実行し `serena: ... ✔ Connected` を確認する
  （数秒で終わり、サーバーごとに Connected/Failed を返す。`qtcreator` が Failed でも無視してよい）。
  Failed だった場合は**復旧を試みず、その周は Serena を使わない**:
  1. **モデル側から再接続する手段は無い。** Serena は stdio 型で Claude Code の子プロセスとして
     起動されており、復旧手段の `/mcp reconnect` は対話コマンド。`claude mcp` CLI にも reconnect は
     無い（`add`/`get`/`list`/`remove` などの設定管理のみ）。**プロセスを kill してはいけない**——
     生きている接続を殺すだけで、自動で建て直される保証がない
  2. **復旧を促す通知は焚かない。** 直せるのは人間がデスクに戻ったときだけで、それまでの間は
     縮退したまま進めば実害が無い。サイクルごとに鳴らしても待ち時間が延びるだけ
  3. **サイクルは止めず、Grep/Read の縮退モードで続行する。** ただしトークン効率が落ちるので、
     この周は **S サイズの項目か、ドキュメントのみで完結する項目**を選ぶ。縮退した事実は 8.報告 に
     書き、サイクル完了通知に1行混ぜる（そのための単独 push はしない）
- **全 git ライトの前に `bash scripts/git_unlock.sh && ...` を必ず挟む**（D1）
- **1.issue 取り込みの節は作らない**（方針13）。参照実装で最も込み入っていた工程がまるごと消える。
  バグ・要望は `ROADMAP.md` の「バグ修正」セクションに人間が直接書く前提
- **2.選択**: サイズ L は着手せず分割のみで1サイクル。**out of scope（undo / 双方向同期 /
  タブの切り離し）は追加提案もしない**ことを明記
- **3.実装**: Serena を使う。**`PROGRESS.md` / `DESIGN_IMPROVEMENT.md` / `REFACTOR_PLANS.md` を
  全文 Read するな、`ast-outline` を使え**というコンテキスト予算ルールを明記（D3）。Serena が
  落ちているときは 0. で判定済みの縮退モードに従う
- **4.テスト**: `bash scripts/loop_verify.sh`。2回連続同原因で失敗したら `git stash` →
  該当項目を `blocked` にして理由を書く → それだけを commit して終了
- **5.レビュー**: `qt-cpp-review` / `qt-qml-review` を変更内容に応じて起動。指摘は参照実装と同じ
  3分類（今サイクルで直す / ROADMAP へ回す / 誤検知）
- **6.ドキュメント**: 更新対象は **`roadmap-done.md` の1行のみ**を原則とし、ユーザー向け機能の
  増減があるときだけ `CLAUDE.md` の該当箇所を触る
- **7.commit & push**: 1サイクル=1コミット。**コミットメッセージは英語1行**（既存履歴に合わせる。
  ただし `(Phase 24b F7b)` のようなフェーズタグはループのサイクルには無いので付けない）。
  `git push -u origin HEAD`、master への push と `--force` は禁止
- **8.報告**: 参照実装の項目 + **アカウント確認の結果** + **`drive` をスキップしたか** +
  **実機確認が残っている観点**
- **禁止事項**: 参照実装のもの + out-of-scope 機能の実装 + 本番 MEGA アカウントでの操作 +
  `PROGRESS.md`/`DESIGN_IMPROVEMENT.md`/`investigations/` への書き込み

## L10. `.claude/settings.local.json` を拡充する

**これが欠けると無人運転が承認待ちで止まる。** 現状は `git add` と `git commit -m ' *` しか
無く、`CLAUDE.md` が義務づけている `git commit -F -` 形式すら通らない。

- `permissions.allow` に追加: `git commit -F -` / `git push` / `git switch` / `git fetch` /
  `git branch` / `git status` / `git log` / `git rev-list` / `git stash` / `git diff` /
  `bash scripts/git_unlock.sh` / `bash scripts/loop_verify.sh` / `bash scripts/ntfy-send.sh` /
  `C:/Qt/Tools/CMake_64/bin/cmake.exe *` / `ctest *` / `ast-outline *` / `gh issue *` /
  `megatool *` / `python .claude/skills/ui-style/scripts/ui_shot.py drive*`
- `env` に `MEGAEXPLORER_TEST_ACCOUNT`（H2）と `NTFY_TOPIC`（H4。どちらも設定済み）
- `hooks` に L8 の `PreToolUse`

作業には `update-config` スキルを使う。**追加後、実際に1サイクル手で回して承認プロンプトが
1回も出ないことを確認する**（L13）。設定変更が効かない場合はセッションを開き直す。

## L11. レビュー工程を1回手で実証する

`qt-cpp-review` / `qt-qml-review` を現状の差分に対して1回走らせ、**所要時間・出力量・
ワーキングツリー差分（未コミット）を対象にできるか**を確認する。参照実装の `/local-review` は
「未コミット差分を対象に、修正はせず指摘だけ返す」ものなので、そこがズレるなら SKILL 側で
補う（例: 先に commit してから差分レビューする順序に変える）。

## L12. `CLAUDE.md` にループの節を追加する

内容: ループの存在と起動方法（`/loop 2h /evolve`）、`ROADMAP.md` / `roadmap-done.md` の位置づけ、
**ループが書いてよい doc と書いてはいけない doc**、`evolve/NNN` の取り込み手順（人間側の
`git merge --ff-only`）、テストアカウントと `megatool` の存在。L3/L4 での参照書き換えもここで行う。

あわせて **`CLAUDE.md` の警告チェック節（現 212 行目付近）を直す**: いまは `qtcreator` MCP を
"Preferred"、CLI を "fallback" と書いているが、ループは MCP を使わないので `scripts/loop_verify.sh`
を第一の手段として書き、MCP は対話セッション用の任意手段に格下げする（方針11）。

---

# フェーズD — 試運転

## L13. 手でフルサイクルを1周する

`/evolve` を手動起動し、承認プロンプトが出ないか・`loop_verify.sh` の出力量が適切か・
1サイクルのトークン消費がどれくらいかを測る。ここで SKILL.md を直す。
**この1周は捨ててよい**（`evolve/001` ごと消す）前提で回す。

## L14. `/loop 2h /evolve` を開始し、運用メモを残す

人間側の手順（`git log --oneline master..evolve/NNN` で確認 → `git merge --ff-only` →
問題があれば `git revert` か ROADMAP への追記）を **L12 で追加した CLAUDE.md の節**に書く。
この作業用文書（`docs/LOOP_MIGRATION_TASKS.md`）はここで削除する。

---

# 残る未確定事項

**なし。** 方針1〜15ですべて確定した。着手はフェーズ0のチェックリストから。
