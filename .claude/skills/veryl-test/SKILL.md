---
name: veryl-test
description: Verylプロジェクトのテスト・ビルド実行手順と既知の注意点。veryl test / veryl build / veryl check を実行するタスクで必ず参照する。
---

# コマンド

- `veryl test` : 全テスト実行。exit code 0/1で成否判定
- `veryl test -t <substr>` : テスト名の部分一致フィルタ
- `veryl test <files...>` : 解析対象ファイルを限定（編集途中で構文エラーのあるファイルを除外して実行できる。依存ファイルは列挙する）
- `veryl test --wave` : 波形ダンプ
- `veryl test --backend <interpret|cranelift|cc>` : 組込みシミュレータのバックエンド選択。挙動不審時は interpret と比較する
- `veryl build` : トランスパイル（SystemVerilog生成）
- `veryl check` : 構文・意味チェックのみ

出力の扱い: INFO/ERROR等のログはstderr，`$display` の出力はstdoutに出る。stderrを捨てるとテスト成否のログが見えなくなるため，フィルタする場合は注意する。

環境note: ホストの veryl は Smart App Control でブロックされるため，全コマンドは `.\scripts\veryl.ps1 <args>` で podman コンテナ内実行する（例: `.\scripts\veryl.ps1 test -t uart`）。コンテナには gcc があり，デフォルトの `--backend cc` がそのまま動作する。

# ネイティブテストの書き方

- `#[test(名前)]` を付けた通常のVerylモジュールがネイティブテストになる（`embed sv` 不要）
- クロック/リセットは `inst clk: $tb::clock_gen;` / `inst rst: $tb::reset_gen (clk);`
- `initial` ブロック内で使用可能: `rst.assert()`，`clk.next(N)`，`$assert(条件)`，`$assert_continue(条件)`，`$display(fmt, ...)`，`$finish()`
- 位相: `rst.assert()` 復帰直後を t=0 として，`clk.next(N)` 後の観測値は tサイクル目のFF更新後の値

# 既知の落とし穴（2026-07時点，veryl組込みシミュレータ）

- **`clk.next(式)` は進行しない**: `clk.next(InvCount - 1)` のような算術式を引数に渡すと0サイクル進行になる（interpret/cranelift両バックエンドで再現）。必ず `const InvCountM1: u32 = InvCount - 1;` のようにconstへ束縛してから渡すこと
- 比較演算子はSVと異なり小なり `<:`，大なり `>:`（`<` `>` はビット幅指定に予約）
- 三項演算子は `cond ? a : b` ではなく **`if cond ? a : b`**（if式）。case式 `case x { 0: a, default: b, }` / switch式も使える
- `for` のループ変数に型指定は書けない（`for i in 0..10 {`）
- `inst` のポート接続は名前渡し。`leds[0]` のような式を渡す場合はポート名の明示が必要（`o_led: leds[0]`）
- VSCodeのVeryl言語サーバはファイル分割・リネーム後にシンボルを二重登録し，大量の「"XXX" is duplicated」を出すことがある。`veryl check` が通るのにIDEだけエラーの場合はウィンドウリロードで解消する
