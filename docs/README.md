# 設計文書

設計・検証・開発環境に関する文書を集めたディレクトリ．

```
docs/
├── rtl.md            RTL 設計（全体構成・クロック方針・ピン割当）
│   ├── psram.md        PSRAM サブシステム
│   └── tfcard.md       TF カードコントローラサブシステム
├── verification.md   検証方針（検証レイヤの定義と割当原則，formal）
├── environment.md    開発環境（ビルドフロー，コンテナ，外部依存の pin）
└── datasheets/       データシート置き場（git 管理外）
```

設計の全体像は [rtl.md](rtl.md) と関連する各文書を参照．
[psram.md](psram.md)・[tfcard.md](tfcard.md) などのサブシステム文書は rtl.md の詳細にあたり，
共通の方針は rtl.md・verification.md 側に置いている．

ビルドや書き込みの手順はルートの [README](../README.md)，
運用ルールは [CONTRIBUTING](../CONTRIBUTING.md) にある．
