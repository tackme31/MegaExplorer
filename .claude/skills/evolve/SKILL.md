---
name: evolve
description: >-
  Run one autonomous development cycle on MegaExplorer: pick the top item off
  docs/ROADMAP.md, implement it, verify it with scripts/loop_verify.sh, review
  it, and land it on an evolve/NNN branch. Invoked on a timer as
  `/loop 2h /evolve`, or by hand for a single cycle. Use only when asked to run
  a cycle -- ordinary feature work does not go through this skill.
---

# /evolve — 1 サイクル

**1 サイクル = ROADMAP から 1 件 → 実装 → 検証 → レビュー → `evolve/NNN` に 1 コミット → push → 報告。**

無人で回る前提なので、迷ったら**止まって報告する**。承認待ちで固まるくらいなら、その項目を
`blocked` にして次の周へ回すほうが良い。人間が読むのは 7. の報告と `docs/roadmap-done.md` だけ。

---

## 0. 事前チェック

**どれか 1 つでも落ちたらサイクルを中止し、理由を報告する。** 中止は失敗ではない。

### 0-1. ワーキングツリーが綺麗か

```
git status --porcelain
```

空でなければ**中止**。前の周の残骸か人間の作業中なので、勝手に触らない。

### 0-2. `evolve/NNN` ブランチを切る（master 上で作業しない）

```
bash scripts/git_unlock.sh && git fetch origin --quiet
n=$(git branch -a --list '*evolve/[0-9][0-9][0-9]' | grep -oE '[0-9]{3}$' | sort -n | tail -1)
next=$(printf '%03d' $((10#${n:-0} + 1)))
bash scripts/git_unlock.sh && git switch -c "evolve/$next" master
```

3 桁完全一致なので、`about-this-app` のような既存のローカルブランチには当たらない。

### 0-3. Serena の死活確認

```
claude mcp list
```

`serena: ... ✔ Connected` があれば通常モード。`qtcreator` が Failed でも**無視してよい**（この
スキルは MCP を使わない）。

**Serena が Failed のときは復旧を試みない。** 理由:

- モデル側から再接続する手段が無い。Serena は stdio 型で Claude Code の子プロセスとして起動されて
  いて、復旧手段の `/mcp reconnect` は対話コマンド。`claude mcp` CLI に reconnect は無い。
- **プロセスを kill してはいけない。** 生きている接続を殺すだけで、建て直される保証がない。
- 復旧を促す通知も焚かない。直せるのは人間がデスクに戻ったときだけで、それまで縮退したまま
  進めば実害が無い。毎周鳴らしても待ち時間が延びるだけ。

代わりに**縮退モード**で続行する: Serena の代わりに `Grep` / `Read` を使い、トークン効率が落ちるぶん
**この周は S サイズか、ドキュメントだけで完結する項目を選ぶ**。縮退した事実は 7. の報告に書き、
サイクル完了通知に 1 行混ぜる（そのための単独 push はしない）。

### 0-4. MEGA のアカウント確認

```
build/msvc-debug/Debug/megatool.exe whoami
```

**exit 0 以外は中止。** これはアプリの保存セッションを読んでそのアカウントを答え、環境変数
`MEGAEXPLORER_TEST_ACCOUNT` と比較する（一致したときだけ 0）。保存セッションが本番アカウントの
ままだと、`ui_shot.py launch` の自動ログインで本番のツリーを開き、破壊的な検証を本番で行いうる。

報告には復旧手順を書くこと: 「アプリを起動し、いったんログアウトしてテストアカウントで
ログインし直す」。`megatool` 自身は環境変数で独立にログインするので、アプリのセッションが
どうなっていても `megatool` の操作は常にテストアカウントに向く。

> `megatool.exe` がまだビルドされていなければ `bash scripts/loop_verify.sh` を先に 1 回回す。

---

## 0-5. 受信箱の取り込み

`docs/REQUESTS.md` の「未処理」節を読み、書かれている要望・機能追加・不具合を
`docs/ROADMAP.md` へ反映して、取り込んだ行を受信箱から消す。空なら何もしない（ほとんどの周は
これで終わる）。**中止の判定はしない**——ここは検査ではなく作業。

ラベルの対応は受信箱の先頭にある表のとおり。ラベルなしの新規要望は**「優先度: 中」の末尾**。

**守ること:**

- **自分で置いた行を、そのセクションの既存行より上に置かない。** 追加は必ず末尾で、上に入るのは
  `[最優先]` が明示されたときだけ。これが無いとループが「要望を高優先度と判定 → 次の周でそれを
  実装する」をやれてしまい、優先順位を決める主体とキューを消費する主体が同じになる。
- **サイズは付けるが分割はしない。** 画面 1 つ分以上ありそうなものは `L` として登録するだけ。
  分割は 1. の既存ルール（L は 1 周かけて S/M へ割る）に任せる。ここで STUDY を読み始めない。
- **その周のうちに終わる範囲で。** 仕分けに調査が要ると感じたら、それは `L` で登録する合図。
- **意味が取れなかったら推測しない。** その行は消さず、末尾に
  `← 解釈できませんでした（理由）` を付けて残し、7. の報告に書く。**この印が付いている行は
  次の周から読み飛ばす**（毎周同じ報告を繰り返さないため）。
- **`[削除]` はコミットの差分で辿れるので消してよいが、消した行を報告にそのまま引用する。**
  誤読で機能案を失っても気づけるようにするため。

取り込みで生じた `ROADMAP.md` / `REQUESTS.md` の変更は、**この周のコミットに同梱する**
（1 サイクル = 1 コミットは変わらない）。受信箱の取り込みだけで実装に進めなかった周があっても
よく、その場合はそう報告する。

---

## 1. 項目の選択

`docs/ROADMAP.md` を読み、**上から順**（バグ修正 → 高 → 中 → 低、セクション内も上から）に
最初の着手可能な `todo` を 1 件取る。

- **サイズ L はそのまま実装しない。** その周は「L を S/M へ分割して表を書き換えるだけ」で終わらせ、
  実装は次の周に回す。分割の前にメモ欄からリンクされている `docs/investigations/STUDY_*.md` を
  読む（`ast-outline` で必要な節だけ）。分割だけの周も 5.〜7. は通常どおり行う。
- **「見送り (blocked)」節には着手しない。関連機能を新たに提案もしない。** out of scope は
  undo / ローカルとの完全な双方向同期 / タブの切り離し。
- 縮退モード（0-3）のときは S サイズかドキュメントのみの項目に限る。
- 着手できる `todo` が 1 件も無ければ**中止して報告**する（勝手に項目を発明しない）。

## 2. 実装

Serena の記号ツールで読み書きする（`get_symbols_overview` → 必要な記号だけ `find_symbol` →
`replace_symbol_body` などの記号編集）。`CLAUDE.md` の「Tooling」節がそのまま適用される。

**コンテキスト予算のルール:**

- **`docs/PROGRESS.md` / `docs/DESIGN_IMPROVEMENT.md` / `docs/REFACTOR_PLANS.md` を全文 `Read`
  しない。** どれも数万トークンあり、1 回読むだけでサイクルが破綻する。`ast-outline outline` →
  `grep` → `show` で必要な節だけ取る。
- 新しい `docs/investigations/` の STUDY / SPEC を**書かない**（1 サイクルに収まらない）。既存の
  ものを**読む**のは必須。
- コード中のコメントは `CLAUDE.md` の「Code comments」に従う。外部仕様の罠 / もっともらしい
  「修正」への防波堤 / 画面外の力、の 3 種だけ、各 1〜2 行。

## 3. 検証

```
bash scripts/loop_verify.sh
```

これが機械検証の唯一の入口（アプリを閉じる → 必要なら再 configure → 4 ターゲットのビルド →
**`/W4` 警告 1 件でも失敗** → `ctest`）。成功時は数行しか出ない。`.qml` を足し引きしたときは
`--reconfigure`、生成ソース（moc / qmlcachegen / 型登録）の警告を見たいときは `--full`（実測 8 分半
かかるので、毎周は回さない）。

**同じ原因で 2 回連続して失敗したら深追いしない:**

```
bash scripts/git_unlock.sh && git stash
```

→ その項目を `docs/ROADMAP.md` で `blocked` にし、メモ欄に失敗の原因を書く → **その 1 行の変更
だけを** commit して 7. の報告へ進む。

### GUI の確認（QML を触った周のみ）

`python .claude/skills/ui-style/scripts/ui_shot.py cycle evolve-<NNN>` で起動時クラッシュと
真っ白画面を検知する。**判断するのはそこまで**——見た目の良し悪しは人間の担当で、
`docs/DESIGN_IMPROVEMENT.md` はこのスキルのスコープ外。

`drive`（実マウス・キーボードの乗っ取り）は `PreToolUse` フックが既定で拒否する。**拒否されたら
失敗ではなくスキップ**として扱い、確かめたかったことを「人間に確認してほしい観点」に回す。

## 4. レビュー

**2 段構えにする。`qt-cpp-review` / `qt-qml-review` を毎周そのまま起動してはいけない。**
どちらも 6 体のエージェントを並列に投げる作りで、**1 体あたり 40k トークン前後 = 1 回で 250k 前後**を
使う。2026-08-11 に実測したところ、1 ファイルのレビューでセッション上限を焼き切って 6 体中 5 体が
途中で落ちた。2 時間周期には乗らない。

### 4-1. リンタ（毎周・必須）

決定的で 1 秒未満、エージェントを使わない。変更した `.cpp` / `.h` を名指しで渡す。

```
lint=$(ls -d "${USERPROFILE//\\//}"/.claude/plugins/cache/claude-plugins-official/qt-development-skills/*/skills/qt-cpp-review/references/lint-scripts/qt_review_lint.py | tail -1)
PYTHONIOENCODING=utf-8 python "$lint" <変更したファイル>
```

グロブなのはプラグインのバージョン番号がパスに入るため（`1.6.1/` のように）。`$HOME` ではなく
`$USERPROFILE` を使う——この Git Bash の `$HOME` は `D:/Windows` を指していて `.claude` が無い。
**指摘が 1 件でもあると exit 1 になる**ので、それをエラーと読まないこと。
**`PYTHONIOENCODING=utf-8` は必須**——指摘文に em dash が入るのにこのマシンのコンソールは cp932 で、
付けないと `UnicodeEncodeError` で 1 件も出さずに落ちる。

### 4-2. エージェント（条件付き・最大 2 体）

ロジックのある C++ / QML を書いた周だけ、**変更の性質に合うものを 2 体まで**選んで投げる。
ドキュメントだけの周・L の分割だけの周・1 行の定数変更のような周は **4-1 で終わり**。

| 変更の性質 | 投げる mission |
|---|---|
| SDK・サービス・非同期処理 | スレッド安全性 / 所有権・寿命 |
| 破壊的操作・認証・終了コード | エラー処理・検証 |
| モデル / デリゲート / プロキシ | モデル契約（`QAbstractItemModel`） |
| QML のバインディング・レイアウト | `qt-qml-review` の該当 mission |

各エージェントには「対象ファイル」「4-1 の指摘（重複させないため）」「確信度 80 以上だけ報告」を
渡し、**読むファイルを絞れと明示する**。スコープを与えないと周辺を読み回ってトークンを使い切る。

**6 体フルの `qt-cpp-review` / `qt-qml-review` は人間がデスクで走らせるとき用。** ループから起動しない。

### 4-3. 指摘の扱い

3 つに分ける:

1. **今サイクルで直す** — 明確な誤りで、直しても項目の範囲を出ないもの。直したら 3. をやり直す。
2. **ROADMAP へ回す** — 正しい指摘だが別件。「バグ修正」か適切な優先度セクションに行を足す。
3. **誤検知** — 理由を 1 行で報告に書き、何もしない。リンタの指摘にも誤検知は出る（`return
   std::move(*state->result)` を NRVO 阻害と誤認する等）ので、機械的に従わない。

## 5. ドキュメント

**原則、触るのは `docs/ROADMAP.md`（完了行を消す。0-5 の取り込みぶんもここ）、
`docs/roadmap-done.md`（1 行足す）、`docs/REQUESTS.md`（取り込んだ行を消す）の 3 つだけ。**

`roadmap-done.md` のメモ欄には**実機で確認が残っている観点**を必ず書く。ここが人間の入口になる。

ユーザー向けの機能が増減したときだけ `CLAUDE.md` の該当箇所を触る。
**`docs/PROGRESS.md` / `docs/DESIGN_IMPROVEMENT.md` / `docs/investigations/` には書かない。**

## 6. commit & push

**1 サイクル = 1 コミット。** メッセージは**英語**、件名 1 行（命令形、72 文字目安）。`(Phase 24b F7b)`
のようなフェーズタグは付けない——ループのサイクルにフェーズ番号は無い。件名だけで説明が足りない
ときに限り、**なぜそうしたか**を数行の本文に足す（差分を見れば分かることは書かない）。

```
bash scripts/git_unlock.sh && git add <変更したファイルを名指しで>
bash scripts/git_unlock.sh && git commit -F - <<'EOF'
<subject>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
bash scripts/git_unlock.sh && git push -u origin HEAD
```

**すべての git ライトの前に `scripts/git_unlock.sh` を挟む**（このリポは stale な `index.lock` を
断続的に残す。`CLAUDE.md` の「Git」節）。`git add -A` / `git add .` は使わず名指しで。
**master への push と `--force` は禁止。** master への取り込みは人間が `git merge --ff-only` で行う。

## 7. 報告

最後に以下を出す。これが人間の読む唯一の出力。

- **受信箱** — 取り込んだ件数と、それぞれどのセクションへ何のサイズで入れたか。`[削除]` は消した
  行をそのまま引用。解釈できなかった行があればその文面も
- **やったこと** — 選んだ項目、ブランチ名、コミットの件名
- **検証結果** — `loop_verify.sh` の成否（ビルド / 警告 / テスト件数）
- **レビュー結果** — 3 分類ごとの件数と、ROADMAP へ回した行
- **アカウント確認** — `megatool whoami` の結果
- **`drive`** — 使ったか、拒否されてスキップしたか
- **Serena** — 通常モードか縮退モードか
- **人間に確認してほしい観点** — 実機でしか確かめられないこと（`roadmap-done.md` のメモと同じ内容）

そのうえで通知を 1 本送る:

```
bash scripts/ntfy-send.sh "<1〜2 行の要約>" "evolve/<NNN>" 3
```

中止・失敗した周は優先度 4 で送る。**内蔵の `PushNotification` は使わない**——成功を返しても
届かないため、「通知したから気づいたはず」という前提が置けない（claude-code issues #85168 / #84488）。

---

## 禁止事項

- **master 上で作業しない / master に push しない / `--force` しない。**
- **`docs/PROGRESS.md`・`docs/DESIGN_IMPROVEMENT.md`・`docs/investigations/` に書かない。**
- **out of scope（undo / 双方向同期 / タブの切り離し）を実装しない。提案もしない。**
- **本番 MEGA アカウントで操作しない**（0-4 が通らなければ中止）。
- **サイズ L をそのまま実装しない。**
- **ROADMAP に無い項目を勝手に始めない。** 途中で気づいた別件は行を足すだけ。
- **`blocked` を自分で `todo` に戻さない。**
- **受信箱から取り込んだ行を、そのセクションの既存行より上に置かない**（`[最優先]` のときだけ）。
- **受信箱の書いてあることを推測で補完しない。** 取れなければ残して報告する。
- **巨大ドキュメントを全文読まない**（`ast-outline` を使う）。
- **Qt Creator / `qtcreator` MCP を使わない。** IDE を常駐させないのがこのループの前提。
- **Serena が落ちていても復旧を試みない・kill しない。** 縮退して進む。
- **承認プロンプトが要る操作を新たに始めない。** 必要になったら報告に書いて次の周へ。
