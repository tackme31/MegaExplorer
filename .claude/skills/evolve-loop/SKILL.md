---
name: evolve-loop
description: >-
  Schedule /evolve to run on a timer without running a cycle right now. Use this
  instead of `/loop 2h /evolve`, whose final step fires one cycle immediately, at a
  time unrelated to the schedule it just registered. `/evolve-loop 2h` registers the
  cron only; `/evolve-loop off` cancels it. A single cycle by hand is still `/evolve`.
---

# /evolve-loop — タイマー登録だけをする

**このスキルはサイクルを回さない。** `CronCreate` を 1 回呼んで終わる。1 周走らせたいなら
`/evolve` を直接呼ぶ——そちらは即開始のままで、このスキルは何も変えない。

`/loop 2h /evolve` を置き換えるために在る。`/loop` は登録直後に 1 周を即実行する仕様で、その
開始時刻は cron の発火時刻と何の関係もない。実測では手動開始 01:39・初回発火 02:13 となり、
32 分かかったサイクルが終わった 1 分後に次が始まった。**即実行さえ無ければ間隔は常に cron の
周期そのものになる**ので、位相合わせのような小細工は要らない——登録だけして黙る、が全部。

## やること

1. 引数から間隔を読む。`2h` `3h` `90m` のような形。**引数が無ければ `2h`。**
   `off` / `stop` / `止めて` なら 4. へ。

2. cron 式を組む。

   - **時間単位 `Nh`**: `N` は 24 の約数（2, 3, 4, 6, 8, 12）に限る。そうでなければ最も近い
     約数へ丸め、丸めたことを報告に書く。式は `MM */N * * *`。`N` が 24 を割るので日付を
     またいでも間隔は崩れない。
   - **分単位 `Nm`**: `*/N * * * *`。

   `MM` は `:00` と `:30` を避けた任意の分。世界中のジョブがその 2 つに集中するため。

   **1 周目の開始が早いか遅いかは気にしない。** 位相をずらして「今から N 時間後」に寄せたく
   なるが、逆効果でしかない——2 周目以降の間隔は cron の周期そのもので位相に依らず、位相が
   決めるのは 1 周目までの待ち時間だけなので、寄せると待ちが必ず最大の N 時間になる。

3. `CronCreate` を `prompt: "/evolve"`、`recurring: true` で呼ぶ。**呼ぶのはこれ 1 回だけ。**
   終わったら、cron 式・次の発火時刻・ジョブ ID・「7 日で自動失効する」「セッションを閉じると
   消える」を数行で報告して終了する。

4. 停止のとき: `CronList` で `/evolve` のジョブを探し、`CronDelete` で消す。消えたことを
   `CronList` で確かめ、1 行報告して終了する。

## 禁止事項

- **サイクルを走らせない。** `Agent` を spawn しない、`cycle.md` を読まない、ROADMAP・
  REQUESTS・ソースを読まない、git を叩かない。登録が済んだらその場で終わる。
- **既存ジョブの上に重ねない。** 登録前に `CronList` を見て、`/evolve` のジョブが既に在れば
  先に `CronDelete` する。2 本あるとサイクルが並走し、同じ ROADMAP 項目を二重に取る。
- **`prompt` に `/evolve force` を登録しない。** 踏み越えは人間がその場で打つ 1 周限りの判断
  で、cron に焼くと以後の無人周が全部ゲート無しになる。登録するのは常に素の `/evolve`。
- **`/loop` へ委譲しない。** `/loop 2h /evolve` を代わりに呼ぶのは、このスキルが避けている
  即実行をそのまま呼び戻すことになる。
