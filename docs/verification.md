# 検証方針

リポジトリ全体で踏襲する検証レイヤの定義と割当原則．
サブシステム固有の検証項目は各設計文書（例: [psram.md](psram.md)）に記述し，
本文書のレイヤ定義を参照する．

## 原則

各検証項目を，それを**反証できる最も下位（高速・高観測性）のレイヤ**に割り当てる．
実機（L4）に持ち込むのは，下位レイヤでは構造的に反証不能なものに限定する．

## 検証レイヤ

| レイヤ | 手法 | 反証できるもの | 構造的に反証できないもの | 特性 |
| --- | --- | --- | --- | --- |
| L0 静的 | veryl fmt / check，クロックドメイン注釈，yosys-slang の elaboration | 構文・型・ビット幅・未接続・未使用．**クロックドメイン交差**（Veryl は交差をコンパイルエラーとし，`unsafe (cdc)` ブロックによる明示を強制する） | 機能全般 | 秒オーダー，CI 済み |
| L1 RTL シム | Veryl ネイティブテスト（`#[test]` + 埋め込み SV，Verilator バックエンド）+ 自作ビヘイビアモデル | FSM 遷移，プロトコル手順（自らのデータシート解釈との整合），データ経路の値，CDC の論理機能 | ①データシート解釈そのものの誤り（DUT とモデルが同一解釈を共有する共通モード誤り），②プリミティブ実挙動，③タイミング，④メタステーブル | 分オーダー，波形（`--wave`）で全信号観測可．反復が最速．主戦場 |
| L1F formal | SymbiYosys (sby) + SMT ソルバ（OSS CAD Suite 同梱）: BMC / k-induction / cover | 記述したプロパティに対する**全入力系列**での違反（L1 の「試した系列のみ」に対し網羅的） | L1 と同じく解釈の共通モード誤り（プロパティが同じ解釈を符号化するため）．大規模設計では計算量爆発 | 小さな FSM + カウンタ構成なら秒〜分．反例は最短波形で得られデバッグ効率が高い |
| L2 合成後シム | yosys 出力 + cells_sim でのシミュレーション | 合成での意味変化，プリミティブが分解されず保存されたか | 配置配線タイミング，実機ビット割当 | プリミティブ境界の確認に限定利用 |
| L3 STA | nextpnr タイミングレポート | ファブリック内部のタイミング充足 | ① IO から先の経路（外部タイミング制約対象外），②位相設定の妥当性，③タイミングモデル自体の精度（Gowin 実機との相関は未較正） | ビルド毎に無料で得られる |
| L4 実機 | デモ・テストデザイン（UART / LCD 報告） | 下位で反証不能な残余すべて | — | 観測性が最低（UART/LCD 経由の間接観測のみ），反復が最遅 |

## L4（実機）の運用規則

1. **1 実験につき未知変数は 1 つ**．複数の未検証要素を同時に実機へ持ち込まない
2. L4 投入前に，同一シナリオが L1 で通っていることを必須とする
   （実機は仮説確認であって探索ではない）
3. L4 で得た事実（ID 値・動作周波数・タイミング設定等）は設計文書へ追記し，
   L1 モデルへ反映して共通モード誤りを一点ずつ潰す

## formal（L1F）の適用境界

- formal は**プロトコル不変条件・安全性プロパティ**を担う．
  データ経路の値の正しさ・エンドツーエンド動作は L1 が担う
- 非同期 CDC の形式検証はクロック比のモデル化に設計判断が入るためスコープ外とし，
  CDC は L0（静的検出）+ L1（機能）+ 検証済み stdlib 部品（`$std::async_handshake`）で扱う
- Veryl は RTL 本体へのアサーション構文を持たないため，プロパティは
  トランスパイル出力（`target/*.sv`）の DUT を包む独立の SV ハーネス
  （`formal/` 配下）+ sby 設定として記述する．生成物には手を入れない
- sby・SMT ソルバは pin 済み OSS CAD Suite に同梱される（追加依存なし）．
  同梱確認済み（2026-07-22，OSS CAD Suite 2026-07-20 コンテナ内）:
  sby / yosys-smtbmc / boolector / bitwuzla / yices / z3

## 採用しない手法

- cocotb（Veryl 統合はあるが Python 依存が増えるため）．
  ネイティブテスト + 埋め込み SV で足りる範囲を維持する
- UVM 等の大規模検証フレームワーク（本設計規模に対して過剰）

## 現行テスト一覧（L1）

Veryl ネイティブテスト 51 件: Blink 3 / UART TX・RX・loopback・overrun 7 /
VideoTiming 1 / TextConsole 2 / PsramSpike 1 / ActivityLed 1 /
PsramCtrl 3（init・R/W+WSTRB・straddle）/ CdcHandshake 1 / PsramAxi 1 /
PsramMemtest 1 / PsramPhySdr 1 / PsramAxiArb2 1 / ImageScanout 2 /
TF カード 17（TfCrc 既知ベクタ 1・SpiMaster 2・TfSpikeInit 1・TfCtrl 6・
Fat32Reader 3・TfTextDemo 2・TfImageDemo 2，[tfcard.md](tfcard.md)）/
RISC-V 9（RvDecoder 2・RvAlu 1・RvRegFile 1・RvCore 2・RvTrap 1・RvSoc 1 と
L2 の riscv-tests ランナー 1（rv32ui/um/mi 60 件を内包），[riscv.md](riscv.md)）．実行方法は
[environment.md](environment.md)，CI 規約は
[CONTRIBUTING.md](../CONTRIBUTING.md) を参照．
