# aviutl2-open-last-project

AviUtl ExEdit2（AviUtl2）を起動するときに、**前回開いていたプロジェクト（.aup2）を自動で開く**ランチャです。

AviUtl2 本体には「起動時に前回のプロジェクトを開く」設定がないので、外側から補います。
AviUtl2 本体や、AviUtl2 の設定ファイルには一切書き込みません。

## 使い方

### AviUtl2 カタログから入れる（おすすめ）

1. [AviUtl2 カタログ](https://github.com/Neosku/aviutl2-catalog) で「前回のプロジェクトを開く」をインストールします。
2. スタートメニューに **「AviUtl2（前回のプロジェクトを開く）」** ができるので、そこから起動します。

タスクバーにピン留めしておくと、普段の起動をこれに置き換えられます。
アンインストールすると、スタートメニューのショートカットも消えます。

### 手動で入れる

1. [Releases](../../releases) から zip をダウンロードして展開します。
2. `aviutl2-open-last.exe` を `aviutl2.exe` と同じフォルダに置きます。
3. `aviutl2-open-last.exe` を起動します。

`aviutl2.exe` と同じフォルダに置けない場合は、下の `--set-app` で AviUtl2 の場所を教えてください。

何も開かずに起動したいときは、今まで通り `aviutl2.exe` を直接起動すれば大丈夫です。

## 仕組み

AviUtl2 は最近開いたプロジェクトを `history.ini` の `[project]` に新しい順で記録しています。

```ini
[project]
1=C:\...\いちばん最近のプロジェクト.aup2
2=C:\...\その前のプロジェクト.aup2
```

このランチャは 1 番目から順に見て、**実際に存在する最初のファイル**を
`aviutl2.exe "<パス>"` の形で渡して起動します。

- 1 番目のファイルが消えたり移動していたりしたら、2 番目、3 番目…と繰り下がります。
- 履歴が空、または全部存在しないときは、引数なしで普通に起動します。
- `history.ini` は読むだけで、変更しません。

### `history.ini` の場所

1. `aviutl2.exe` と同じフォルダに `Data` フォルダがあれば `Data\history.ini`（ポータブル構成）
2. なければ `%ProgramData%\aviutl2\history.ini`（通常の構成）

### `aviutl2.exe` の探し方

次の順に探します。

1. `--set-app` で記録した場所（ランチャと同じフォルダの `aviutl2-open-last.ini`）
2. ランチャと同じフォルダ
3. `..\AviUtl2\aviutl2.exe`
4. `..\aviutl2.exe`
5. `..\..\AviUtl2\aviutl2.exe`

見つからないときはエラーを表示して終了します。

## コマンドライン

| 引数 | 動作 |
|---|---|
| （なし） | AviUtl2 を起動して、前回のプロジェクトを開く |
| `--set-app "<フォルダ>"` | `aviutl2.exe` のあるフォルダを記録し、スタートメニューにショートカットを作る |
| `--uninstall` | 記録とショートカットを消す |

`--set-app` と `--uninstall` は画面を出さずに終わります。終了コードは次のとおりです。
0 = 成功 / 1 = 引数が不正 / 2 = 指定したフォルダに `aviutl2.exe` が無い / 3 = 記録に失敗 / 4 = ショートカットの作成に失敗。

AviUtl2 カタログは、インストール時に `--set-app` を、アンインストール時に `--uninstall` を呼んでいます。

## 動作環境

- Windows 10 / 11（64bit）
- AviUtl ExEdit2 2.1.8 で動作確認

## ビルド

MinGW-w64 の gcc で `build.bat` を実行するか、次のコマンドでビルドできます。

```
gcc -O2 -s -municode -mwindows -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE -o aviutl2-open-last.exe aviutl2-open-last.c -lshlwapi -lole32 -luuid
```

`-mwindows` を外して `-DTESTMAIN` を付けると、履歴の読み取り結果だけを表示するコンソール版になります
（AviUtl2 は起動しません）。

```
test.exe "<aviutl2.exe のあるフォルダ>"
```

## 注意

- 非公式のツールです。AviUtl2 の作者様とは関係ありません。
- `history.ini` の形式は AviUtl2 の内部仕様なので、今後のバージョンで変わる可能性があります。

## 更新履歴

- **v1.1.0** — AviUtl2 カタログに対応。`--set-app` / `--uninstall` を追加し、スタートメニューにショートカットを作るようにした。
- **v1.0.0** — 最初の公開版。

## ライセンス

[MIT](LICENSE)
