# hello_veryl

[![CI](https://github.com/sabas0ba/hello_veryl/actions/workflows/ci.yml/badge.svg)](https://github.com/sabas0ba/hello_veryl/actions/workflows/ci.yml)

[Veryl](https://veryl-lang.org/) で記述した Lチカ・UART エコー・LCD テキストコンソール・
内蔵 PSRAM コントローラ（起動時 memtest）を OSS ツールチェーンのみで
Tang Nano 9K (GOWIN GW1NR-LV9QN88PC6/I5) 上で動作させるプロジェクト．

## 必要環境

| 要件 | 動作確認 | 用途 |
| --- | --- | --- |
| Windows 11 + PowerShell 5.1 以降 | — | スクリプト実行 |
| [Podman](https://podman.io/) | 5.8.3 + WSL2 マシン | ビルドコンテナの実行（veryl 含む，ホストへの veryl 導入は不要） |
| Windows 版 OSS CAD Suite | 2026-07-20 | 書き込み（下記手順 1 で導入） |
| [Zadig](https://zadig.akeo.ie/) | — | JTAG ドライバの WinUSB 化（初回のみ） |

## 初回セットアップ

### 1. 書き込みツール準備

```powershell
.\scripts\setup-toolchain.ps1
```

Windows 版 OSS CAD Suite をダウンロード・SHA-256 検証し，`tools/`（git 管理外）へ展開する．
システム PATH・レジストリは変更しない（撤去は `tools/` の削除のみ）．

### 2. USB ドライバ設定（手動）

[Zadig](https://zadig.akeo.ie/) で以下を実施する（本フローで唯一のシステム変更）:

- **JTAG Debugger (Interface 0)**（VID 0403 / PID 6010）のドライバを **WinUSB** に置き換える
- Interface 1 は UART（COM ポート）のため置き換えない
- 復帰: デバイスマネージャでドライバ削除 → 再接続

## ビルド・書き込み・動作確認

```powershell
.\scripts\build-container.ps1          # ビルド（コンテナ内で合成～bitstream 生成）
.\scripts\flash.ps1                    # SRAM へロード（電源断で消える）
.\scripts\flash.ps1 -Flash             # 内蔵フラッシュへ書き込み（永続）
.\scripts\uart-send.ps1 -Data "hello"  # 動作確認: 1回送信して応答表示 (-Hex でバイト列表示)
.\scripts\uart-term.ps1                # 動作確認: 対話ターミナル (Esc で終了)
```

送信した文字がエコーされ，同じ内容が LCD へ表示される（ローカルエコーは無効にする）．
S1 ボタンで画面全消去．各操作は VSCode の tasks.json からも実行できる．

## ドキュメント

| 文書 | 内容 |
| --- | --- |
| [docs/](docs/README.md) | 設計（RTL・PSRAM サブシステム・検証方針・開発環境） |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 貢献（運用ルール・注意事項） |
