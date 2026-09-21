# AGENTS.md

## プロジェクト概要

[emukidid/cleanrip](https://github.com/emukidid/cleanrip) のフォーク。
GameCube (DOL-101) から**外付け USB 光学ドライブ**でディスクを吸い出すための
機能を追加している。

背景: 既製の USB Dolphin (makeo) は GC 側で **SD 互換デバイス**として振る舞い、
**512B ブロック固定**・**生 SCSI CDB を送れない**ため光学ドライブには使えない。
そこで GC 側の **ディスクソース抽象化** と、**自作 USB-EXI アダプタ (RP2040)** を実装する。

## リポジトリ構成

- `source/` `include/` … 上流 CleanRip（C / libogc2）
  - `disc_source.{c,h}` … ディスク読み出し元の抽象化（内蔵 DI / 外付け / IDE-EXI 枠）
  - `disc_source_usb_scsi.c` … 自作アダプタ経由の外付けドライブ読み出し（GC 専用）
  - `usb_exi.h` … GC ⇔ アダプタのプロトコル定義とクライアント API
  - `main.c` … USB Dolphin 検出・表示、ディスクソース選択 UI、読み出し経路の差し替え
  - `gc_dvd.c` … 内蔵 DI（上流のまま）
- `hardware/usb-exi-adapter/` … RP2040 ファームウェア（pico-sdk / TinyUSB / CMake）
- `docs/`
  - `USB-EXI-ADAPTER.md` … プロトコル・結線・検証手順
  - `MAKEO-FEATURE-REQUEST.md` … 既製 USB Dolphin への要望文面

## 接続構成（DOL-101）

| 位置 | EXI | 役割 |
|---|---|---|
| Slot A | ch0 dev0 | 自作アダプタ（外付けドライブ入力） |
| Slot B | ch1 dev0 | USB Dolphin Slot A/B 版（Swiss 起動元 + 出力） |
| SP1 | ch0 dev2 | 予備（未使用） |

## ビルド

### GC / Wii（devkitPro + libogc2）

Windows では **devkitPro 版 MSYS2 をユーザー領域に隔離**して使う（管理者不要・既存 MSYS2 非破壊）。

- 導入先: `C:\Users\Umaaa\devkitPro\msys2`
- 導入方法: 公式 updater が取得する `https://pkg.devkitpro.org/msys-2.10.0.7z` を展開し、
  `etc/pacman.conf` に `[libogc2-devkitpro]`（`packages.libogc2.org`）を `[dkp-libs]` より前に追加、
  `SigLevel = Never` にしてから `pacman -Syu` → `pacman -S devkitPPC gamecube-tools-git libogc2-git libogc2-libdvm-git libogc2-libntfs-git ppc-mxml`
- 注意: `cmake` は `gcc`（ホスト）に依存するため `pacman -S gcc cmake python make git` も必要

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
make -f Makefile.gc     # → cleanrip-gc.dol
make                    # → CleanRip.dol (Wii)
```

### Pico ファーム

**MSYS 版 cmake は POSIX パスを生成ヘッダに埋めるため不可**。ネイティブ Windows ツールで
ビルドする（`PICO_SDK_PATH` は Windows 形式 `C:/...` で渡す）。

```powershell
$env:PICO_SDK_PATH='C:/path/to/pico-sdk'
$env:PICO_TOOLCHAIN_PATH='C:/Users/Umaaa/devkitPro/msys2/opt/devkitpro/devkitARM'
$env:PATH='C:\Users\Umaaa\devkitPro\msys2\opt\devkitpro\devkitARM\bin;C:\msys64\ucrt64\bin;' + $env:PATH
cmake -S hardware/usb-exi-adapter -B build-fw -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build-fw        # → usb_exi_adapter.uf2
```

### ビルド検証（2026-09-21 時点）

- `cleanrip-gc.dol` 2,085,024 B … **-Wall 警告ゼロ**
- `CleanRip.dol`（Wii）2,234,880 B … **-Wall 警告ゼロ**
- `usb_exi_adapter.uf2` 49,152 B … **警告ゼロ**
- `disc_scramble.c` はホスト上で機能テスト済み（16 セクタ解除＋EDC 検証＋シード探索）

## 検証ステージ（外付けドライブ）

`EXI ID (PING)` → `USB INQUIRY` → `READ CAPACITY` → `通常DVD READ(10)` →
`0xE7（GCディスク）` → 全dump の MD5 を PC `friidump` と一致

参照: `C:\Users\Umaaa\workspace\friidump`（`0xE7` / method12 の知見）

## 関連プロジェクト（外付けドライブでの再生）

- `groyr/gc-live-disc-server` … OmniDrive ドライブの正規 GC ディスクを
  FUSE で「ライブ ISO」として公開し、GC(Swiss) が WizNet(W5500) Ethernet 経由の
  SMB/FSP で読んで再生する（B案の先行実装）
- `groyr/swiss-gc`（**A案の実装先候補だが、現在は着手不可**）… A案の ODE 型デバイスを追加する構想
  - **注意**: 上流 Swiss の `AGENTS.md` は「**LLM 貢献を許可しない**」と明記し、
    コード生成・修正・デバッグ・テスト作成・ドキュメント生成を含む LLM 利用を明確に拒否している。
    そのため本プロジェクトでは **Swiss のコードに LLM で変更を加えない**。
    A案を進める場合は、ユーザーが手作業で実装するか、方針を見直す必要がある
- 方針: 本フォーク（吸い出し）は低コスト維持し、**Issue #1（USB Dolphin の
  raw CDB パススルー）が通れば外付けドライブの GC 直結吸い出し**を追実装する

## コーディング規約

- コメント・ログ・エラーは日本語 / 変数・関数名は英語
- 既存コードのスタイル（タブインデント）に合わせる
- 上流の挙動を壊さない（既定は内蔵 DI のまま）
- コメントなしの新規ドキュメントは日本語で作成

## バージョン管理

- **公開フォーク: https://github.com/groyr/cleanrip（`master` を運用）**
  - 上流 `origin = emukidid/cleanrip` はそのまま（`jj git fetch` で追従）
  - push: `jj bookmark set master -r @` → `jj git push --remote groyr --bookmark master`
  - fork の Actions で Wii/GC ビルドの CI が動く（`TARGET`=ディレクトリ名なので fork 名 `cleanrip` が必須）
- **jj (Jujutsu)** で運用（`jj git init --colocate` 済み）
- `core.autocrlf` は無効化し、作業ツリーは LF
- 作業前に `jj new`、完了時に `jj describe`（日本語）
- 上流へ PR は作成しない

## 実装状況

- GC 側: ディスクソース抽象化・USB Dolphin 検出・ディスクソース選択 UI
- GC 側 raw 読み出し: `0xE7` + `READ(12)` streaming + method12 相当
  （`source/disc_source_usb_scsi.c` / `source/disc_scramble.c`）
- Pico ファーム: TinyUSB host + 任意 CDB + EXI スレーブ（`hardware/usb-exi-adapter/`）
- **ビルド確認済み（2026-09-21）**。**実機ブリングアップ未実施**

## 既知の制約

- 外付けドライブ経路は **RP2040 = USB フルスピード**のみ。FS 非対応ブリッジは
  HS 対応 MCU（STM32F723 等）へ切り替える。EXI プロトコルは MCU 非依存
- `exi_slave.c` のエッジ位相は実機ロジアナで確定が必要
- EXI は現状 CPU ビットバン + 4MHz。速度は **PIO 化で 16MHz 以上**に上げる必要あり
  （詳細は `docs/USB-EXI-ADAPTER.md`）
- `0xE7` のベースアドレスはドライブ依存。既定は MN103S 系の `0xA13000`。
  他機種（GDR-8082N 等）は `drive_profile` 対応が今後必要
- `0xE7` は転送律速（~0.3MB/s）。全ディスクで 60〜90 分
- Wii では外付けドライブ経路は未対応（スタブ）
- BCA は内蔵 DI 専用（外付け経路では未取得）
