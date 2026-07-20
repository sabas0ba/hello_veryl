# hello_veryl

[![CI](https://github.com/sabas0ba/hello_veryl/actions/workflows/ci.yml/badge.svg)](https://github.com/sabas0ba/hello_veryl/actions/workflows/ci.yml)

[Veryl](https://veryl-lang.org/) で記述したLチカを OSS ツールチェーンのみで
Tang Nano 9K (GOWIN GW1NR-LV9QN88PC6/I5) 上で動作させるプロジェクト．

## フロー構成

```
veryl build            : Veryl -> SystemVerilog (target/)      [Windows ホスト]
yosys (+ yosys-slang)  : 論理合成 (synth_gowin)            -> build/top_synth.json  [コンテナ]
nextpnr-himbaechel     : 配置配線 (Project Apicula チップDB) -> build/top_pnr.json  [コンテナ]
gowin_pack             : bitstream 生成                     -> build/top.fs         [コンテナ]
openFPGALoader         : ボードへの書き込み                                         [Windows ホスト]
```

合成～bitstream 生成は podman コンテナ（`container/Containerfile`）内で行う．
Windows ネイティブ実行は Smart App Control が未署名バイナリ
（nextpnr-himbaechel.exe 等）をブロックし安定しないため採用しない
（`scripts/build.ps1` にネイティブ版フローを残置．SAC 無効環境では動作する）．

### pin 済み外部物

| 項目 | 値 |
|---|---|
| base image | `debian:13-slim@sha256:020c0d20b9880058cbe785a9db107156c3c75c2ac944a6aa7ab59f2add76a7bd` |
| OSS CAD Suite (コンテナ内) | 2026-07-20 / `oss-cad-suite-linux-x64-20260720.tgz` / SHA-256 `ba680b02915bc59da92c64fbbbacd86fd2650f74480503f9d3b6eb47c2ea2a53` |
| OSS CAD Suite (Windows, 書き込み用) | 2026-07-20 / `oss-cad-suite-windows-x64-20260720.exe` / SHA-256 `03ab812dcd2e094148bc2009ca7ba7358c805a0d848fe46df14d9cb4bfed5893` |

SHA-256 は GitHub Releases API のアセット digest と照合済み．各スクリプトが
取得時に再検証し，不一致なら中断する．
なお配布物と digest は同一オリジン（GitHub リリース）であり上流の署名も存在しないため，
リリース自体の改竄は検出できない．この pin が保証するのは転送路改竄と pin 後の変更の検出である．
バージョン更新時は各スクリプト冒頭の値と本表を更新する．

## 必要環境

- Windows 11 / PowerShell 5.1 以降
- [Veryl](https://veryl-lang.org/)（動作確認: veryl 0.20.2）
- [Podman](https://podman.io/)（動作確認: podman 5.8.3 + podman-machine-default (WSL2)．
  Podman Desktop のオンボーディングで導入可能）
- 書き込み時のみ: Windows 版 OSS CAD Suite（`scripts/setup-toolchain.ps1` で導入）と
  Zadig によるドライバ設定（後述）

## 手順

### 1. 書き込みツール準備（初回のみ）

```powershell
.\scripts\setup-toolchain.ps1
```

Windows 版 OSS CAD Suite を pin 済み URL からダウンロードし，SHA-256 検証のうえ
`tools/oss-cad-suite/`（git 管理外）へ展開する．システム PATH・レジストリは変更しない
（撤去は `tools/` の削除のみ）．書き込み（openFPGALoader）と SAC 無効環境での
ネイティブビルドに使用する．

### 2. USB ドライバ設定（書き込み前に初回のみ・手動）

Tang Nano 9K 内蔵 USB-JTAG (BL702 ベース，FT2232H 互換 VID 0403 / PID 6010) を
openFPGALoader から使うため，[Zadig](https://zadig.akeo.ie/) で
**JTAG Debugger (Interface 0)** のドライバを WinUSB に置き換える．
Interface 1 は UART (COM ポート) のため置き換えない．
本フローで唯一のシステム変更であり，対象インターフェース限定．
復帰はデバイスマネージャからドライバ削除→再接続．

### 3. ビルド（合成～bitstream 生成）

```powershell
.\scripts\build-container.ps1
```

ホストで `veryl build` を実行後，コンテナイメージを構築（初回のみダウンロード発生）し，
コンテナ内で yosys → nextpnr → gowin_pack を実行して `build/top.fs` を生成する．

### 4. 書き込み

```powershell
.\scripts\flash.ps1          # SRAM へロード（電源断で消える）
.\scripts\flash.ps1 -Flash   # 内蔵フラッシュへ書き込み（永続）
```

### テスト（シミュレーション）

```powershell
veryl test
```

## ピン割当

`constraints/tangnano9k.cst` に記載．ピン番号の出典は Sipeed 公式サンプル
（記述自体は本リポジトリで独自作成）．

| 信号 | ピン | 備考 |
|---|---|---|
| i_clk | 52 | 27 MHz オシレータ |
| i_rst | 4 | S1 ボタン，active-low（Veryl 既定リセット async_low と整合） |
| leds[5:0] | 16,15,14,13,11,10 | active-low |

## ディレクトリ構成

```
src/           Veryl ソース (top.veryl, blink.veryl)
constraints/   物理制約 (tangnano9k.cst)
container/     Containerfile（合成用イメージ定義，pin 済み）
scripts/       build-container.ps1 / synth_pnr.sh / synth.ys / flash.ps1
               setup-toolchain.ps1 / build.ps1 (ネイティブ版，SAC 無効環境用)
target/        Veryl が生成する SystemVerilog（git 管理外）
build/         合成・配置配線・bitstream 出力（git 管理外）
tools/         Windows 版 OSS CAD Suite 展開先（git 管理外）
```

## References

- Veryl: https://veryl-lang.org/ — HDL 本体・ドキュメント
- OSS CAD Suite: https://github.com/YosysHQ/oss-cad-suite-build — ツールチェーンバンドル（release 2026-07-20 を使用）
- Podman: https://podman.io/ — コンテナランタイム（Podman Desktop: https://podman-desktop.io/）
- Debian container image: https://hub.docker.com/_/debian — ベースイメージ（digest 固定）
- Yosys: https://github.com/YosysHQ/yosys — 論理合成
- yosys-slang: https://github.com/povik/yosys-slang — SystemVerilog フロントエンド（OSS CAD Suite 同梱）
- nextpnr: https://github.com/YosysHQ/nextpnr — 配置配線 (himbaechel/gowin)
- Project Apicula: https://github.com/YosysHQ/apicula — GOWIN bitstream ドキュメント・gowin_pack
- openFPGALoader: https://trabucayre.github.io/openFPGALoader/ — 書き込みツール（board 定義 `tangnano9k`）
- Tang Nano 9K 公式 wiki: https://wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/Nano-9K.html — ボード仕様
- Tang Nano 9K 回路図: https://dl.sipeed.com/shareURL/TANG/Nano%209K/2_Schematic
- Sipeed 公式サンプル (ピン割当の出典): https://github.com/sipeed/TangNano-9K-example/blob/main/led/src/9K_LED_project.cst
- Zadig: https://zadig.akeo.ie/ — WinUSB ドライバ割当
