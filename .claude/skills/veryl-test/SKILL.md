---
name: veryl-test
description: Verylプロジェクトのテスト・ビルド実行手順と既知の注意点。veryl test / veryl build / veryl check を実行するタスクで必ず参照する。
---

# 規約

- **コード変更後・コミット前に `.\scripts\veryl.ps1 fmt` を必ず実行する**．CIは `veryl fmt --check` で整形崩れを検出して失敗する
- **RTL変更時は `.\scripts\verif.ps1`（合成後検査，verif/ 一括実行）もリグレッションとして実行する**．`$sv::` プリミティブの接続ミスはネイティブテストでは検出できず，CIのverifジョブでも実行される

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

コンテナが使えない環境（Linux のリモート実行環境等，SACの制約がなくpodmanもない場合）では，`container/Containerfile` と**同一の取得元・SHA-256** で veryl バイナリを直接取得して使える。CI と同じ検証を行うため pin を勝手に変えないこと:

```bash
curl -fL --proto '=https' --tlsv1.2 -o veryl.zip \
  https://github.com/veryl-lang/veryl/releases/download/v0.20.2/veryl-x86_64-linux.zip
echo "217c94e9dccb8dbaec0a0e01ebad15a4d5554428364bfeb48858bbb84071b4c2  veryl.zip" | sha256sum -c -
unzip -o veryl.zip -d /usr/local/bin && chmod +x /usr/local/bin/veryl*
```

この方法では合成（yosys/nextpnr）とformal（sby）は使えない（OSS CAD Suite が入らない）。`veryl fmt/check/build/test` = L0/L1 までは完全に実行でき，`bash scripts/ci.sh` で CI と同一シーケンスを回せる。

# ネイティブテストの書き方

- **`Top` はネイティブテストできない**: rPLL / IOBUF 等の Gowin プリミティブ（実体は nextpnr が解決するブラックボックス）を含むためエラボレートできない。Top の結線は L0（`veryl check` / `build`）+ L4（実機）で担保し，サブシステムのテストは**Top と同じ結線をテストベンチ側で再現**して書く（実例: `src/tfcard/test_text_demo.veryl`）
- `#[test(名前)]` を付けた通常のVerylモジュールがネイティブテストになる（`embed sv` 不要）
- クロック/リセットは `inst clk: $tb::clock_gen;` / `inst rst: $tb::reset_gen (clk);`
- `initial` ブロック内で使用可能: `rst.assert()`，`clk.next(N)`，`$assert(条件)`，`$assert_continue(条件)`，`$display(fmt, ...)`，`$finish()`
- 位相: `rst.assert()` 復帰直後を t=0 として，`clk.next(N)` 後の観測値は tサイクル目のFF更新後の値

# 既知の落とし穴（2026-07時点，veryl組込みシミュレータ）

- **`clk.next(式)` は進行しない**: `clk.next(InvCount - 1)` のような算術式を引数に渡すと0サイクル進行になる（interpret/cranelift両バックエンドで再現）。必ず `const InvCountM1: u32 = InvCount - 1;` のようにconstへ束縛してから渡すこと
- **`$display` / `$assert` のフォーマット引数が展開されない**: `$display("got %02x", v)` も `$display("got {:02x}", v)` も書式文字列がそのまま出力され，値は落ちる（cc/interpret両バックエンドで再現）。**値を見ながらのデバッグができない**ため，期待値と実測値の突き合わせは次のいずれかで行う:
  - 受信列を配列（`var got: logic<8> [N];`）に貯め，フィールド範囲ごとに `$assert_continue` を分けてメッセージ文字列で切り分ける（どの範囲が壊れたかはメッセージで分かる）
  - `--wave` で VCD を出して信号を直接見る（ただし長時間テストではVCDが数百MBに達するので注意．`*.vcd` は .gitignore 済み）
- **配列の動的インデックス参照はシミュレータが未対応**: `arr[idx]`（`idx` が変数）は `veryl check` / `veryl build` は通るが，`veryl test` のエラボレートで `unsupported description: this description is not supported by the simulator` になる。定数インデックスは可。回避策は「参照したいバイトを常に先頭へ持ってくるシフトレジスタ」にすること（実例: `src/tfcard/fat32_reader.veryl` の `name_sh` — 8.3名照合で1バイトずつローテートしている）
- **巨大な case 式でコード生成がスタックオーバーフロー**: 数千分岐の case 式（生成テーブル等）は `veryl test` が `fatal runtime error: stack overflow` で abort する。`RUST_MIN_STACK=134217728 veryl test ...` で通ればこれが原因と確定できるが，恒久対策は**関数を分割する**こと（実例: `scripts/gen_tf_test_image.py` はディスクイメージを1セクタ=最大512分岐の関数に分け，LBAでディスパッチする形に生成している）
- **予約語が識別子を弾く**: `step` / `repeat` / `final` / `inside` / `same` / `converse` / `type` / `bit` は変数名に使えない（RTLでありがちな名前が含まれる点に注意）。エラーは `'step' is a reserved keyword and cannot be used as an identifier` と明示されるので，読めば分かる
- 比較演算子はSVと異なり小なり `<:`，大なり `>:`（`<` `>` はビット幅指定に予約）
- 三項演算子は `cond ? a : b` ではなく **`if cond ? a : b`**（if式）。case式 `case x { 0: a, default: b, }` / switch式も使える
- `for` のループ変数に型指定は書けない（`for i in 0..10 {`）
- package 内の `function` に `pub` は付けられない（package 自体の `pub` で公開される）。付けると `Unexpected token: 'pub'`
- ループ変数を本文で使わない場合は `_` 始まりにする（`for _n in 0..512`）。`veryl check` が `unused_variable` **Warning** を出し，CIはWarningでも失敗する
- `inst` のポート接続は名前渡し。`leds[0]` のような式を渡す場合はポート名の明示が必要（`o_led: leds[0]`）
- VSCodeのVeryl言語サーバはファイル分割・リネーム後にシンボルを二重登録し，大量の「"XXX" is duplicated」を出すことがある。`veryl check` が通るのにIDEだけエラーの場合はウィンドウリロードで解消する
