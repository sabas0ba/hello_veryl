# Contributing

本リポジトリの運用ルールと注意事項．
開発環境の構成・使い方は [docs/environment.md](docs/environment.md) を参照．

## branch / commit

- 機能追加・修正は branch（または worktree）で行い，main へは PR で統合する
- コミットメッセージは [Conventional Commits](https://www.conventionalcommits.org/) に従う
- 生成物（`src/video/font_rom.veryl` 等）は生成スクリプトと同一コミットで更新する
- 一時ファイル・実験は git 管理外ディレクトリ（`build/`，`tools/` 等）内で行う

## CI・コード品質

- コミット前に `.\scripts\veryl.ps1 fmt` を適用すること
- CI（`fmt --check` → `check` → `build` → `test`）は Warning でも失敗する
  （`veryl check` は Warning のみでも exit 1）

## 外部依存の追加・更新

- 依存の追加は抑制する．追加する場合はバージョンを SHA-256 / digest で
  一意に固定し，取得時検証をスクリプトに組み込む
  （現状の pin は [docs/environment.md](docs/environment.md) を参照）
- ツールチェーン更新は単独パッチとし，`chore:` で Containerfile /
  setup-toolchain.ps1 のハッシュを同時更新する
- Veryl 標準ライブラリはコンパイラ同梱であり，公開 API は Veryl 1.0 まで不安定．
  Veryl 更新時は stdlib API への追随（IF 定義の変更対応）を同一パッチで行う

## 文書

- 設計判断・実機で得た事実は該当する設計文書（`docs/`）へ追記する
  （検証で得た事実の扱いは [docs/verification.md](docs/verification.md) の運用規則）
- データシート等の再配布制限がある資料は `docs/datasheets/`（git 管理外）に置き，
  取得元・版数・SHA-256 を設計文書の一次資料表に記録する

## ライセンス・権利

- 外部 RTL・コードの引用や流用を行う場合はライセンスを確認し，
  表示義務（LICENSE / NOTICE）を満たすこと．出所不明のコードは持ち込まない
- 商標・特許等で問題になりうる方式・実装の混入を避ける
