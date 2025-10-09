# Changelog / 変更履歴

All notable changes to this project will be documented in this file.
プロジェクトの主な変更点はこのファイルに記録します。

This project adheres to [Semantic Versioning](https://semver.org/).
本プロジェクトは[セマンティック バージョニング](https://semver.org/lang/ja/)に従います。

## [Unreleased]

### Added / 追加
- Initial repository scaffolding with bilingual README and automation guide.
  README（英語/日本語）および自動化ガイドを整備。
- Arduino CLI configuration (`arduino-cli.yaml`), example sketch, and build instructions.
  Arduino CLI設定（`arduino-cli.yaml`）、サンプルスケッチ、ビルド手順を追加。
- MIT License and Arduino Library Manager metadata (`library.properties`, `keywords.txt`).
  MITライセンスおよびArduinoライブラリマネージャ向けメタデータ（`library.properties`, `keywords.txt`）。
- Build artifacts ignored via `.gitignore`.
  `.gitignore` でビルド生成物を除外。
- Core OBS WebSocket client skeleton with status/error callbacks and reconnect stubs.
  ステータス・エラーコールバックと再接続スタブを備えたOBS WebSocketクライアント骨組みを追加。
