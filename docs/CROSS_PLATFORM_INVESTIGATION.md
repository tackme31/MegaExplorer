# 調査メモ: クロスプラットフォーム対応ビルドへ移行できるか（2026-07-30）

現状のビルド構成はMSVC + vcpkg + Visual Studioジェネレータ前提で、Windows専用になっている。将来的
にLinux/macOSへ展開する場合にこの構成が障壁になるかの調査記録。**コード変更は未実施**（調査のみ）。

## 結論

**移行は可能で、ビルド構成上の障壁は小さい。** 現状「MSVC必須」なのは*Windowsでビルドする場合に限
った制約*であって、プロジェクト全体を縛ってはいない。CMake側の変更は`if(WIN32)`ガード数箇所とプリ
セット追加の**追加作業**であり、書き換えではない。

実作業の本体はビルド構成ではなく、**`WindowsSessionStore`（DPAPI）の非Windows実装**の1点に集約さ
れる。

## 根拠1: SDK側はもともとマルチプラットフォーム前提

`docs/BUILD.md`が記録する「Ninjaが使えない」制約は、SDK側でWindows限定にガードされている:

```cmake
# third_party/sdk/cmake/modules/sdklib_variables.cmake:6-12
if (NOT WIN32)
    set(USE_PTHREAD 1)
    set(USE_CPPTHREAD 0)
else()
    set(USE_CPPTHREAD 1)
    set(CMAKE_GENERATOR_TOOLSET "v142")   # ← WIN32のときだけ
endif()
```

問題の`CMAKE_GENERATOR_TOOLSET "v142"`は`else()`（= WIN32）側にしかないため、**Linux/macOSでは
NinjaもUnix Makefilesも使える**。

他にもSDK側にマルチプラットフォーム前提の作りが揃っている:

- `third_party/sdk/cmake/vcpkg_overlay_triplets/` に `x64-linux-mega` / `arm64-linux-mega` /
  `x64-osx-mega` / `arm64-osx-mega` などが用意されている。
- `third_party/sdk/CMakePresets.json` に **`megasync-unix` / `megasync-windows`** プリセットがある
  — 公式クライアント[MEGAsync](https://github.com/meganz/MEGAsync/)は、まさにこのSDKを同じCMake構
  成でUnix向けにビルドしている。
- `vcpkg_management.cmake:27-74` がOS/アーキテクチャ別にtripletを自動選択する。

ただし本プロジェクトは`add_subdirectory`方式で組み込んでいるため、この自動選択マクロは**走らない**
（`third_party/sdk/CMakeLists.txt:56` の `if(NOT PROJECT_NAME)` ガード）。triplet等は手動指定が必
要だが、これは現状すでにやっていることをOS別に増やすだけ。

## 根拠2: 自前コードのWindows依存は1ファイルだけ

`src/`全体を走査した結果、Windows APIを叩いているのは
**`src/platform/WindowsSessionStore.cpp:12` の `<windows.h>`/`<wincrypt.h>`（DPAPI）だけ**だった。

ポータブルで問題なしと確認できたもの:

- QMLは全11ファイルともポータブル。`Qt.platform.os`分岐もフォント直指定もゼロ（"Windows Explorer"
  はコメント内の表現のみ）。
- パス処理は`QStandardPaths` / `QDir::toNativeSeparators` / `QUrl::fromLocalFile`に統一済み。
- ファイルを開く処理（`src/qml/DownloadController.cpp:107`）は`QDesktopServices::openUrl`。
- `src/core` / `src/mega` / `src/qml` にOS固有API呼び出しは皆無。

なお、ウィザードが生成した初期の`CMakeLists.txt`（コミット`f665a34`）にはMSVC固有の記述が一行もな
く、むしろ`MACOSX_BUNDLE`系のボイラープレートが最初から入っていた（現在の`CMakeLists.txt:105-119`
はその名残）。**MSVC依存はキット／プリセット層の選択にすぎず、プロジェクトに焼き付いてはいない。**

## 対応が必要な箇所

### ビルド設定（軽い・機械的）

| 箇所 | 内容 |
|---|---|
| `CMakeLists.txt:134` | `/W4`はMSVC専用 → GCC/Clangは`-Wall -Wextra`系に分岐（下記「由来」参照） |
| `CMakeLists.txt:140` | `crypt32`リンクを`if(WIN32)`で囲む |
| `CMakeLists.txt:68-69` | `WindowsSessionStore.{h,cpp}`をOS別ソース選択に |
| `tests/CMakeLists.txt:17,22,46` | 同上（テスト側も同じ分岐が必要） |
| `CMakePresets.json` | `linux-debug` / `macos-debug`を追加。generatorはNinja、tripletは`x64-linux-mega` / `arm64-osx-mega` |
| manifest features | OS別の差分あり（次項） |

`VCPKG_MANIFEST_FEATURES`のOS別差分:

- **Linux**: `use-readline`の追加が必要。`sdklib_options.cmake:26`で非Windowsは`USE_READLINE`が既
  定ONになり、`sdklib_libraries.cmake:163`が`pkg_check_modules(readline REQUIRED)`を呼ぶため。
  あるいは`-DUSE_READLINE=OFF`を明示する。
- **macOS**: `use-openssl`を外す。`sdklib_options.cmake:30-34`でAPPLEは既定OFF（SecureTransportを
  使う）。

### コード（ここが本体）

`WindowsSessionStore`の代替が唯一の実作業。ただし`ISessionStore`というポートが既にあるため、**設
計上は完全に想定内の差し替え**で、アーキテクチャの変更は伴わない。選択肢は3つ:

1. **QtKeychainを導入** — 1実装でWindows/macOS/Linuxすべてを賄える。`WindowsSessionStore`ごと置き
   換えられる代わりに外部依存が1つ増える。
2. **OS別に3実装** — macOSはSecurity.framework、Linuxはlibsecret。既存のDPAPI実装をそのまま残せる
   が保守面が3倍になる。
3. **暫定フォールバック** — 非Windowsは平文ファイル + `0600`。セキュリティは落ちるが、検証環境が
   整うまでのつなぎとしては成立する。

`main.cpp:47-48`の`WindowsSessionStore`直接生成を`createSessionStore()`ファクトリに寄せれば、分岐
はコンポジションルート1箇所に閉じる。

## `/W4` と `crypt32` の由来（調査で判明した詳細）

自前コードに入り込んだMSVC主義は3サイト（`CMakeLists.txt:134`の`/W4`、同`:140`の`crypt32`、
`tests/CMakeLists.txt:46`の`crypt32`）。それぞれ経緯を確認した。

### `/W4` — SDKが「原因」ではないが、SDKが「形」を決めている

追加コミットは`9e2df0d`で、記録された理由はSDKと無関係（「自前コードがMSVC既定の`/W1`で、警告チェ
ックの習慣に引っかかるものが無かった」）。ただし**SDK側も自分自身に同じことをしている**:

```cmake
# third_party/sdk/cmake/modules/sdklib_target.cmake:444-448
target_platform_compile_options(
    TARGET SDKlib
    WINDOWS /W4
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)
```

`/W4`という値の選択はSDKの流儀の踏襲であり、`add_compile_options`ではなく
`target_compile_options(... PRIVATE)`にスコープを絞った理由（「SDKlib/third_partyが騒がしくならな
いように」）も、SDKサブツリーの存在が直接の動機。

**移植時の含意**: 非Windowsの警告オプションは、上記UNIX側の指定（`-Wall -Wextra -Wconversion`）を
そのまま真似れば整合が取れる。

### `crypt32` — 自前都合。ただしアプリ側は現状冗長

追加コミットは`7ef1aff`（DPAPI版`WindowsSessionStore`と同時）でSDKとは無関係。ただし調べると
**SDKlibはすでに`crypt32.lib`をリンクしている**:

```cmake
# third_party/sdk/cmake/modules/sdklib_target.cmake:426-428
if(WIN32)
    target_link_libraries(SDKlib PRIVATE
        ws2_32 winhttp Shlwapi Secur32.lib crypt32.lib Wldap32.lib
```

`add_library(SDKlib)`（`sdklib_target.cmake:2`）は型指定なしで`BUILD_SHARED_LIBS`もどこにも設定さ
れていないため、**SDKlibはSTATIC**。静的ライブラリの場合CMakeはPRIVATE依存を`$<LINK_ONLY:...>`と
して利用側に伝播させるので、`CMakeLists.txt:140`の`crypt32`は**現状は冗長**（書かなくてもリンクは
通る）。とはいえDPAPIを直接呼んでいる以上、サードパーティ静的ライブラリのprivate依存が漏れてくる
のに頼るのは脆いため、明示は正しい記述。

一方 **`tests/CMakeLists.txt:46`の`crypt32`は必須**。`MegaExplorerTests`は`MegaExplorerCore` / Qt /
GTestしかリンクしておらず**SDKlibをリンクしていない**ため、伝播元が存在しない。

## 実機がないと分からないリスク

**「設計上の障壁はゼロだが、実測値もゼロ」**という状態である点に注意。困難があるとすればコンフィグ
ではなく初回セットアップの側にある。

- vcpkgでffmpeg / pdfium / freeimageをLinux・macOS向けにビルドする工程は重く、nasm・autoconf・
  libtool・pkg-config・X11/GL開発ヘッダ等が揃っていないと通らない。
- CIがなく、検証できるmacOS/Linux実機も現状ない。
- `docs/BUILD.md`のswscaleワークアラウンド（`CMakeLists.txt:25-28`）が他OSでも必要かは未検証。
  `if(TARGET ...)`ガード済みなので無害ではある。
- `gfxworker`のリンク失敗（現状Windowsで発生、`docs/BUILD.md`参照）が他OSでどうなるかも未検証。
  ただし`appMegaExplorer`だけをビルドする運用なら無関係。
- `CMakeLists.txt:109`の`MACOSX_BUNDLE_GUI_IDENTIFIER`がコメントアウトのまま — macOS配布時に必要。

## 推奨する進め方

全部を今やる必要はないが、**次の2点は実機がなくても着手でき、放置するほどWindows前提が増えていく**
ため、先に済ませておく価値がある:

1. `ISessionStore`の生成をファクトリ経由にし、Windows依存を`main.cpp`から追い出す。
2. `/W4`と`crypt32`に`if(WIN32)`／ジェネレータ式のガードを付ける。

実際のLinux/macOSビルド検証は、環境が用意できてからで十分。

なお、目指す形は「MinGWで統一」ではない。SDKのWindowsビルドはMSVC + vcpkgしかサポートされていない
ため、**Windows = MSVC / Linux = GCC / macOS = Clang をプリセットで並べる**構成になる。

## 既存ドキュメントとの関係

`docs/TITLEBAR_TABS_INVESTIGATION.md:66` に「現状クロスプラットフォーム前提のコードではない（MSVC
専用ビルド、Windows専用のセッションストア）ため、Win32ネイティブ実装を選んでもアーキテクチャ上の矛
盾は少ない」という記述があるが、本調査の結果、**「MSVC専用ビルド」はキット/プリセット層の選択にす
ぎず、コードに焼き付いた制約ではない**ことが分かった。当該doc執筆時点の前提として残すが、判断材料
としては本docを優先すること。

同docが挙げるタイトルバー統合タブ（QWindowKit等）を導入するとWindows専用度が一段上がるため、**その
意思決定より前に本docの方針を決めておく**のが順序として望ましい。

## ステータス

未着手。「推奨する進め方」の1・2に着手するかどうかも未決定。ロードマップ（`docs/PROGRESS.md`）には
まだ載せていない — 着手を決めたらそちらにフェーズとして追加する。
