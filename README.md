# vsr4aviutl

RTX Video Super Resolution を使用したデノイズ、アップスケーリングを行うプラグイン

## 前提

RTX VSRが動作するPC

## 使い方

- [Releases](https://github.com/Pachira762/vsr4aviutl/releases/latest)から最新バージョンをダウンロードします。  
- ダウンロードしたzipファイルの中の `vsr4aviutl.auf2` と `nvngx_vsr.dll` を AviUtl2 のプラグインフォルダ (デフォルトでは `C:\ProgramData\aviutl2\Plugin`) にコピーします  
- `フィルタ効果を追加` > `加工` > `RTX VSR` から効果を追加します

## ビルド

CUDA Toolkit 12 以上をインストールして環境変数`CUDA_PATH`を通してください。  
[RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk) をダウンロードして`NV_RTX_VIDEO_SDK`を設定してください。
