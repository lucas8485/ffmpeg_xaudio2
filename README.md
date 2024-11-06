

# ffmpeg_xaudio2

使用ffmpeg进行解码，并使用xaudio2输出解码后的pcm音频

use ffmpeg to decode audio file, and use xaudio2 to play decoded pcm, with a simple cli

###### 开发前的配置要求

1. ffmpeg latest build
2. directx sdk

### 文件目录说明

```
ffmpeg_xaudio2
├── audio_play_interface.hpp
├── ffmpeg_xaudio2_internal.hpp
├── audio_decode.cpp
├── ffmpeg_xaudio2.cpp
├── xaudio2_output_impl.cpp
├── ffmpeg_xaudio2.vcxproj
├── ffmpeg_xaudio2.vcxproj.filters
├── LICENSE
└── README.md

```

### 使用到的框架

- [XAudio2](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-introduction)
- [ffmpeg](https://ffmpeg.org/)


### 版权说明

This project is licensed under GNU GPLv2, see [LICENSE.txt](https://github.com/lucas8485/ffmpeg_xaudio2/blob/master/LICENSE.txt)

ffmpeg and XAudio2's licenses can be found in their website

ffmpeg: [ffmpeg_license](https://ffmpeg.org/legal.html)

XAudio2: [xaudio2_license](https://github.com/lucas8485/ffmpeg_xaudio2/blob/master/LICENSE.XAudio2.txt)




