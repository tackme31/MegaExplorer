---
name: release
description: >-
  Cut a MegaExplorer release from develop: bump the version, write the release
  notes from master..develop, merge into master, tag it, build and package the
  Release zip, unpack that zip and check the app actually launches from it, then
  push and create the GitHub release with gh. Works with no argument -- it picks
  the next version itself from what shipped since master; `/release 0.2.0`
  overrides that. Stops for approval exactly twice -- on the release notes,
  and on the launch check. Use only when cutting a release.
---

# /release — リリースを 1 本切る

**引数は無くてよい**——次の版は 1. で自分で決める。渡されたときはそれが優先（`X.Y.Z`、`v`
付きなら剥がす）。**develop 上で人間が対話的に走らせるコマンド**で、サブエージェントには
出さない——止まる箇所が 2 つあり、`AskUserQuestion` が要る。

止まるのはこの 2 箇所だけ。それ以外は失敗しない限り進む。

1. **リリース文を見せて承認をもらう** → その後に master マージとタグ
2. **zip の展開と起動確認の結果を見せて承認をもらう** → その後に push と GitHub 公開

`master` を触るのはこのコマンドだけ（`CLAUDE.md`「ブランチ」節）。`/evolve` は `master` に
一切触らないので、両者が同じものを書き換えることはない。

## 0. 前提確認（1 つでも欠けたら、何もせずに理由を言って終わる）

```
git rev-parse --abbrev-ref HEAD        # develop であること
git status --porcelain                 # 空であること
git fetch origin --tags
git log --oneline origin/develop..develop develop..origin/develop   # 乖離していないこと
git rev-list --count master..develop   # 1 以上であること
git branch --no-merged develop | grep evolve/   # 未マージの evolve/NNN が無いこと
```

（タグと GitHub release の重複確認は、版が決まったあと 1. で行う。）

- `origin/develop` が先行していたら `git pull --ff-only` で追いつく。乖離していたら中断。
- 未マージの `evolve/NNN` があるのは、サイクルがマージに失敗して止まった跡（`CLAUDE.md`
  「ループエンジニアリング」）。**先にそれを片付けるのが筋なので中断する。**
- **`/evolve` のループが回っているなら止めてもらう。** cron はそれを登録したセッションに
  紐づくので、このセッションから `CronList` で見えるとは限らない。ループが動いていないか
  一言確認し、動いているなら向こうのセッションで `/evolve-loop off` を先に打ってもらう。
  リリース中にサイクルが develop へマージしてくると、master に入る中身が動いた版とずれる。
- 最後に `bash scripts/loop_verify.sh` を通す。**警告 1 本でも落ちるのでそこで中断**——
  リリース版が壊れているかどうかを、後の Release ビルドの前にここで確定させる。

## 1. 次のバージョンを決めて上げる

引数で渡されていればそれを使う。無ければ**この 3 手で決める。人間に聞かない。**

**手順 1 — 今回出荷されたものを数える。** 出荷の証拠は `docs/roadmap-done.md` に足された行で、
`docs/ROADMAP.md` から消えた行**ではない**——ループは「やらないと決めた項目」も削除するので、
消えただけの行を数えると出荷していない機能追加を数えてしまう（実例: `v0.1.0` 以降で消えた
唯一の `機能追加` 行「目録が巨大な zip」は、実装ではなく取り下げだった）。

```
git diff master..develop -- docs/roadmap-done.md | grep '^+|'          # 出荷された項目
git log -p master..develop -- docs/ROADMAP.md | grep '^-|'             # 種類の在処
```

**手順 2 — 出荷された項目の 種類 を突き合わせる。** 上の 2 つを項目名で対応付け、各項目が
`機能追加` か `不具合` かを拾う。roadmap-done の行は 1 行が数千トークンあるので、
`cut -c1-200` などで頭だけ見る——全文は読まない。

**手順 3 — 数え方。**

| 出荷された 種類 | 次の版 |
| --- | --- |
| `機能追加` が 1 つ以上 | MINOR を上げる（`0.1.3` → `0.2.0`） |
| 全部 `不具合`（＋リファクタ・ドキュメント・スキル） | PATCH を上げる（`0.1.0` → `0.1.1`） |

- **基準は ROADMAP の 種類 であって、画面に何が増えたかではない。** 不具合として起票された
  ものは、直し方が新しい表示の追加であっても PATCH のまま（`evolve/090` `/091` の
  「読み込み中」表示がこれ。無反応に見える不具合の是正であって、機能ではない）。
- `.claude/` のスキルや `scripts/` の変更は配布物に入らないので、版の判断に数えない。
- MAJOR は**人間が明示的に指示したときだけ**。1.0.0 を切るかどうかは採番の問題ではない。

決めたら、版と理由（出荷 N 件、うち機能追加 0 件、など）を 1〜2 行でターミナルに出す。
**ここでは止まらない**——版は 2. の承認①でリリース文と一緒に目に入るので、違うと言われたら
まだ push していない版上げコミットを `git commit --amend` で作り直せばよい。

版が決まったので、ここで重複を確認する。どちらかが埋まっていたら中断:

```
git tag -l vX.Y.Z                      # 空であること
gh release view vX.Y.Z                 # 「release not found」であること
```

`CMakeLists.txt` 3 行目の `project(MegaExplorer VERSION ...)` が唯一の版の在処で、
`MEGAEXPLORER_VERSION` も CPack の zip 名もそこから派生する。書き換えたら、旧版の文字列が
他に残っていないか `grep -rn "<旧版>" --include='*.txt' --include='*.md' --include='*.json' .`
で見る（`docs/` の履歴や `roadmap-done.md` の過去行は**直さない**——過去の記述だから）。

develop に 1 コミット積む。本文は 1〜2 行で足りる。

```
git add CMakeLists.txt
git commit    # Subject: Bump the version to X.Y.Z
```

コミット末尾の trailer はこのリポジトリの他のコミットに合わせる。

## 2. リリース文を書く（承認①）

材料は **`git log master..develop`**。subject と本文の 1 段落目までで足りる。

```
git log master..develop --format='%h %s%n%b%n---'
```

`docs/roadmap-done.md` は分類の裏取りにだけ使ってよいが、**行を全文読まない**——1 行が数千
トークンある。必要なら `ast-outline grep '<項目名>' docs/roadmap-done.md` で当たりを取る。

書き方:

- **日本語**、ユーザー視点。内部の設計判断・リファクタ・テスト追加・ドキュメント更新は
  リリース文に出さない（コミットログに残っている）。
- 見出しは中身のあるものだけ置く: `### 新機能` / `### 改善` / `### 不具合修正`。
- **ユーザーが気づかないような細かい修正は 1 行にまとめる**——「その他いくつかの不具合を修正」。
  一つ一つ並べない。
- 末尾に環境の 1 行を置く。数字は覚えているものではなく、その場で確かめたものを書く:
  Qt は `CMakePresets.json` の `CMAKE_PREFIX_PATH`、SDK は
  `git -C third_party/sdk describe --tags`。
- 冒頭 1 行は「何のリリースか」。初回リリースからの差分が大きいときだけ 2〜3 行の要約を足す。

`build/release-notes-X.Y.Z.md` に書く（`/build*/` は gitignore 済み＝コミットされない）。
そのままターミナルにも出し、**`AskUserQuestion` で「この版・この文面で master へ進む /
直す / 中止」**を聞く。直すと言われたら直してもう一度聞く。**この承認は版も兼ねる**——版を
変えると言われたら 1. の版上げコミットを `git commit --amend` で作り直し、zip 名もタグも
そこから派生するので他に直す場所は無い。

## 3. master へマージしてタグを打つ

承認が出てから、ここで初めて `master` を触る。**まだ何も push しない。**

```
git checkout master
git merge --ff-only develop        # 落ちたら理由を見る。develop は master の子孫のはず
git tag -a vX.Y.Z                  # 注釈付き。v0.1.0 と同じ形
```

`--ff-only` が落ちるのは master に直接何かが入ったときで、それは想定外の状態。**勝手に
`--no-ff` へ倒さず**、何が入っているかを見せて指示を仰ぐ。

タグの注釈は「`MegaExplorer X.Y.Z`」＋空行＋リリース文の要約 3〜4 行（zip 名まで含める）。
v0.1.0 の注釈が手本。

## 4. Release ビルドとパッケージ

```
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

Release ビルド → CPack の ZIP → 中身の検証（Qt の DLL、platform プラグイン、QML モジュール、
FFmpeg、MSVC ランタイム、LICENSE、THIRD-PARTY-NOTICES）まで、このスクリプトが持っている。
**自分で cmake を叩き直さない。** 出来上がりは
`build/msvc-debug/package/MegaExplorer-X.Y.Z-win64.zip`（Release でも `msvc-debug` の下に
出るのは configure preset 名だから）。**zip 名の版が引数と一致しているかを見る**——ここが
バージョン反映の実質的な確認になる。

## 5. zip を展開して起動確認（承認②）

**Qt を `PATH` に足さない。** 足したら「Qt が無いマシンで動くか」というこの確認の意味が消える。
Qt の `bin` を `PATH` に入れたシェルからは実行しないこと。

```powershell
$ver = 'X.Y.Z'
$zip  = "build/msvc-debug/package/MegaExplorer-$ver-win64.zip"
$dest = Join-Path $env:TEMP "MegaExplorer-release-check-$ver"
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $zip -DestinationPath $dest
$exe = Get-ChildItem -Recurse -Filter MegaExplorer.exe $dest | Select-Object -First 1
$p = Start-Process -FilePath $exe.FullName -WorkingDirectory $exe.DirectoryName -PassThru
Start-Sleep -Seconds 15
$p.Refresh()
if ($p.HasExited) { throw "exited with $($p.ExitCode)" }
if (-not $p.MainWindowTitle) { throw "no window after 15s" }
"OK: $($p.MainWindowTitle) (pid $($p.Id))"
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(10000) | Out-Null
```

`-WorkingDirectory` は SDK の `megaclient_statecache*.db` を一時ディレクトリ側へ落とすため
（リポジトリに書かせない）。アプリは保存済みセッションでテストアカウントに自動ログインする
ので、ウィンドウが出ること＝DLL が揃っていることの確認になる。**見た目の確認はしない**——
それは人間の仕事。

結果（zip のパスとサイズ、ウィンドウタイトル、終了できたこと）を見せ、**`AskUserQuestion` で
「公開して良い / 中止」**を聞く。ここまではローカルにしか何も無いので、中止なら 8. の戻し方へ。

## 6. 公開

承認が出てから、この順で。

```
git push origin develop            # 1. の版上げコミット
git push origin master
git push origin vX.Y.Z
gh release create vX.Y.Z --verify-tag --title "vX.Y.Z" \
    --notes-file build/release-notes-X.Y.Z.md \
    build/msvc-debug/package/MegaExplorer-X.Y.Z-win64.zip
```

`--verify-tag` は、タグの push が落ちていたときに GitHub 側でタグを作らせないため。
`--draft` / `--prerelease` は**指示されたときだけ**付ける。

## 7. 後片付けと報告

```
git checkout develop
```

一時展開ディレクトリ (`$env:TEMP\MegaExplorer-release-check-X.Y.Z`) を消す。報告は数行:
リリース URL、zip 名とサイズ、含まれるコミット数、`docs/roadmap-done.md` に「実機確認が
残っている」と書かれたまま出た項目があればその件数。**`/evolve-loop` の再開は人間の判断**——
勝手に登録し直さない。

## 8. 途中で落ちたとき / 中止のとき

push より前は全部ローカルなので戻せる。**戻す操作は破壊的なので、実行前に何をするか見せて
確認を取る。**

```
git checkout develop
git tag -d vX.Y.Z                     # 3. まで進んでいたら
git branch -f master origin/master    # master のマージを巻き戻す（未 push のときだけ）
git reset --hard HEAD~1               # 1. の版上げコミットを捨てるとき
```

push した後に問題が見つかったら、タグと release を消すのではなく**次の patch を切る**のが
既定。どうしても消すなら `gh release delete` と `git push origin :refs/tags/vX.Y.Z` を
人間の明示的な指示のもとで。

## 禁止事項

- **承認①の前に `master` を触らない**、**承認②の前に何も push しない**。
- `--no-verify` でコミットしない。`git push --force` を `master` に使わない。
- `scripts/package.ps1` を迂回して手で zip を作らない（検証が抜ける）。
- `-Config Debug` の zip を配らない（デバッグ CRT は再配布できない）。
- `docs/ROADMAP.md` / `docs/roadmap-done.md` を書き換えない（ループの持ち物）。
- 起動確認を「ビルド木の `Release/MegaExplorer.exe`」で代用しない。確認対象は展開した zip。
