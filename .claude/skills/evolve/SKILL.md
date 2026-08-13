---
name: evolve
description: >-
  Run one autonomous development cycle on MegaExplorer: pick the top item off
  docs/ROADMAP.md, implement it, verify it with scripts/loop_verify.sh, review
  it, land it on an evolve/NNN branch, and merge that into master. Invoked on a timer as
  `/loop 2h /evolve`, or by hand for a single cycle. Use only when asked to run
  a cycle -- ordinary feature work does not go through this skill.
---

# /evolve — ディスパッチャ

**このファイルは手順を持たない。** 1 サイクルの中身は `.claude/skills/evolve/cycle.md` にあり、
それを読んで実行するのは**サブエージェント**であって、このセッション（親）ではない。

理由は測ってある。`/loop 2h /evolve` はサイクルを同じセッションに積み続けるので、**1 周あたり
実測 45k トークン**（重い周で 100k）が親のコンテキストに恒久的に残り、15〜20 周＝30〜40 時間で
窓が埋まる。サブエージェント内の消費は親に一切乗らない——同じ実測で、37k 使ったエージェントが
親に残したのは 0.9k だった。だから本文をまるごと外へ出す。親の負担は spawn プロンプトと報告だけ
（~2k/周）になる。

## やること（これだけ）

1. `Agent` ツールで `general-purpose` を **1 体だけ**、**フォアグラウンドで**
   （`run_in_background: false`）、`model: "opus"` を明示して spawn し、次を渡す:

   > MegaExplorer で `/evolve` の 1 サイクルを回してください。手順は
   > `.claude/skills/evolve/cycle.md` に全部書いてあります。**まずそれを `Read` で全文読み**、
   > 書かれているとおりに 0. から 7. まで実行してください。`CLAUDE.md` の規約もそのまま適用され
   > ます。あなたの最終出力は cycle.md の「7. 報告」の書式そのものにしてください——それが人間の
   > 読む唯一の出力です。中止した周も、どのチェックで落ちたか・復旧手順を同じ書式で返してくだ
   > さい。

2. 返ってきた報告を**そのまま**ユーザーに中継する。エージェントの最終出力はユーザーに表示され
   ないので、中継しないと消える。**要約・省略はしない**——人間が読む唯一の出力なので、短くする
   判断を親がしてはいけない。

3. それ以外は何もしない。

## 禁止事項

- **`cycle.md` を親で `Read` しない。** 23KB あり、毎周これを読むと分割した意味が消える。中身を
  知る必要があるのはサブエージェントだけで、親が手順を把握する必要はない。
- **親で ROADMAP / REQUESTS / コードを読まない。git を叩かない。** 読んだ時点で親のコンテキスト
  に乗る。サイクルの中で起きることは、すべてサブエージェントの中で完結させる。
- **`run_in_background: true` で投げない。** ループの 1 ティック = 1 サイクルで、次のティックが
  来る前に終わっている必要がある。バックグラウンドにすると報告の中継がティックをまたぐ。
- **1 ティックで 2 体以上 spawn しない。** サイクルが 2 本並走すると、同じ ROADMAP 項目を 2 回
  取り、`evolve/NNN` の採番も衝突する。
- **報告が返る前にユーザーへ結果を書かない。** 予測して書かない。
- **サブエージェントが中止を報告してきたとき、親が代わりにやり直さない。** 中止は失敗ではない
  （cycle.md の 0. 参照）。そのまま中継して終わる。
