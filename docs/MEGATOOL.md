# `megatool` — MEGA を CLI から触る

`MegaExplorerCore` の `IMegaClient` をそのまま叩くコンソールツール。**GUI を操作せずに MEGA 側の
状態（フォルダ・ファイル・お気に入り・ゴミ箱の中身）を用意する**のが目的で、`/evolve` ループの
ために作ったが手作業でも使える。

バイナリは `build/msvc-debug/Debug/megatool.exe`。`scripts/loop_verify.sh` が他の 3 ターゲットと
一緒にビルドする（`CMakePresets.json` の `buildPresets.targets`）。

## 認証

| 変数 | 置き場 |
|---|---|
| `MEGAEXPLORER_TEST_ACCOUNT` | `.claude/settings.local.json` の `env` ブロック（gitignore 済み） |
| `MEGAEXPLORER_TEST_PASSWORD` | Windows のユーザー環境変数。`setx` で設定する |

`setx` で入れた値は**既に起動しているプロセスには届かない**ので、設定後は Claude Code のセッションを
開き直すこと。リポジトリ内のファイルには絶対に書かない。

`whoami` **以外**のすべてのコマンドはこの 2 つでログインする。**アプリの保存セッションは見ない**ので、
アプリが本番アカウントを開いていても `megatool` の操作は必ずテストアカウントに向く。

SDK のステートキャッシュ DB はアプリと別ディレクトリ（`<AppLocalData>/MegaExplorer/megatool`）に
置く。同じ SQLite ファイルを 2 プロセスで開かせないため。代償として毎回 `fetchNodes` が走るが、
テストアカウントの規模なら数秒。

## コマンド

```
megatool whoami                  アプリの保存セッションがどのアカウントのものかを表示し、
                                 MEGAEXPLORER_TEST_ACCOUNT と比較する
megatool ls <path>               フォルダの中身（ルートは '.'）
megatool mkdir <path>            フォルダ作成。途中の階層も作る（mkdir -p 相当）
megatool put <local> <path>      ローカルのファイル 1 個をフォルダへアップロード
megatool rm <path>               ノードをゴミ箱へ移動
megatool fixture reset           /MegaExplorerFixture を既知の状態へ作り直す
```

### `whoami` — ループの安全装置

これだけは**アプリの `session.dat` を読んで** `fastLogin` し、そのセッションのメールアドレスを
答える。`ui_shot.py launch` はアプリを保存セッションで自動ログインさせるので、そこが本番アカウント
のままだと破壊的な検証を本番で行いうる。**一致したときだけ exit 0** を返すので、ループはこれ 1 本を
`0.` の事前チェックに置ける。

```
$ megatool whoami
env    : tocnisika@gmail.com
session: tocnisika@gmail.com
match  : yes
```

`logout()` は呼ばない——アプリが握っているセッションを無効にしてしまうため。

一致しなかったときの復旧はアプリ側の操作: ログアウトしてテストアカウントでログインし直す。

**これが見ているのは `session.dat` の中身であって、起動中のアプリのメモリ上のセッションではない。**
両者がずれうるのは「別アカウントでログイン中だがまだ保存していないアプリが動いている」場合だけで、
`loop_verify.sh` はどのみち先頭でアプリを終了させるため、**次に起動したときに使われるのは
`session.dat` のほう**。つまりこのゲートは、ループが実際に開くことになるアカウントを見ている。

### `fixture reset` が作る木

毎回ゴミ箱へ流してから作り直す（＝ゴミ箱にも中身ができる）。

```
MegaExplorerFixture/
├── docs/
│   ├── readme.txt   ← お気に入りフラグ付き
│   └── notes.txt
├── images/          （空）
├── empty/           （空）
└── nested/a/b/
    └── deep.txt
```

一覧・空状態・深い階層・お気に入り・ゴミ箱をひととおり踏むための最小構成。増やすときは
`tools/megatool.cpp` の `cmdFixtureReset` を直接編集する。

**ルートは `/` ではなく `.`。** Git Bash は引数の裸の `/` をプロセスに届く前に自分のインストール
ディレクトリ（`C:/Program Files/Git/`）へ書き換えてしまうため、`/` はシェル経由では使えない。

## 実装上の注意

- **`rm` はゴミ箱への移動で、完全削除ではない。** `IMegaClient` は `MegaApi::remove` を意図的に
  公開していない（`moveToRubbish` のコメント）。このツールはそれを追加する理由にはならない。
- **`installLogging()` を呼ばない。** あれはログファイルを `WriteOnly` で開くので、呼ぶとアプリの
  `MegaExplorer.log` を切り詰めてしまう。ログは Qt の既定ハンドラで stderr へ流し、`*.debug` と
  `*.info` は落としてある。
- **パスワードをログに出さない。** `MegaSdkLogger` は渡されたものをそのままログファイルへ送る。
- **`await()` は 10 分で打ち切る。** 無制限に待つとハングになり、無人のループでは誰かが
  プロセスを殺すまで止まったままになる（クラッシュより悪い）。上限は 640k ノードの
  `fetchNodes` 実測 385 秒を包める値。
- **MEGA は同名の兄弟ノードを許す。** `resolve` / `makeDirs` は一覧の**最初の一致**を取るので、
  同名フォルダが 2 つあると `rm` や `fixture reset` がどちらを掴むかは一覧順しだい。
  `fixture reset` は毎回作り直すので実際には起きにくいが、手で同名を作ったときは注意。
- `IMegaClient` のコールバックは SDK 自身のスレッドで届くので、`tools/megatool.cpp` の `await()` が
  1 本ずつブロックして待つ。**アプリ側はこれと逆に GUI スレッドへ marshal して待たない**設計なので、
  この `await()` を `src/` へ持ち込まないこと。
