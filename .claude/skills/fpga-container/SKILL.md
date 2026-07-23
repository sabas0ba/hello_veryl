---
name: fpga-container
description: FPGAツールチェーンコンテナでのコマンド実行規約。yosys/nextpnr/コンテナ内python/pdftotext/デバッグprobe/一時テストを実行するタスクで必ず参照する。
---

# コンテナ実行の規約

すべてのツール実行は pin 済みコンテナ（container/Containerfile）経由で行う。
**生の `podman run ...` を組み立てず，以下のラッパを使う**（allowlist 済みで承認不要）:

- `.\scripts\veryl.ps1 <veryl args>` : veryl サブコマンド（fmt/check/build/test/doc）
- `.\scripts\verif.ps1` : 合成後検査（verif/）一括実行
- `.\scripts\fpga-run.ps1 <cmd> [args...]` : 任意コマンド。cwd は /work（= repo ルート）
  - 例: `.\scripts\fpga-run.ps1 yosys -s scripts/synth.ys`
  - 例: `.\scripts\fpga-run.ps1 bash verif/run.sh`
  - コンテナ内 python は `/opt/oss-cad-suite/py3bin/python3`（システム python3 は無い）
  - pdftotext あり（docs/datasheets/ の読解: `pdftotext -layout <pdf> <txt>`）
- `.\scripts\build-container.ps1` : フルビルド（veryl build→合成→PnR→bitstream）

# デバッグの規約

- 探索・検査スクリプト（python/sh）は使い捨てにせず，まず build/psram_probe/ 等の
  gitignore 領域へファイルとして置き，`fpga-run.ps1` で実行する。
  恒久化する価値が出たら verif/ または scripts/ へ昇格する
- **ソースの機械編集は Edit/Write ツールで行う**。python ヒアドキュメント等の
  ワンライナーテキスト処理は使わない（CLAUDE.md の書き捨てスクリプト禁止に準拠）
- 一時テスト（切り分け用 #[test]）は src/ 配下に `test_*_tmp.veryl` として置き，
  切り分け完了後に必ず削除する

# 環境の前提

- コンテナは毎回 podman build されるがキャッシュで即時。--network none
- veryl の std キャッシュはコンテナ実行毎に veryl が展開する。std ソースを
  読むときは同一コンテナ内で先に veryl check を走らせる:
  `.\scripts\fpga-run.ps1 bash -c "veryl check >/dev/null 2>&1; cat /root/.cache/veryl/std/*/<path>"`
