# ffmpeg_multi-streams-player
基于 ffmpeg/libav 开发的多媒体播放器（插件）。支持多路视频同时播放，支持rtsp/rtmp/hls等常见协议。
# Demo
src目录下MediaPlayer.java类以jni方式调用libmediaplayer.so中的native方法。同时播放4路rtmp流，以四分屏显示。可以是不同的rtmp流，rtsp/hls流也可以播放。
# 效率优化
native发生pixel format转换，可以去掉这一步。native和java之间发生数据拷贝，可以去掉这一步。
# ffmpeg/libav
ffmpeg源码从 http://ffmpeg.org/ 下载，实际上ffmpeg源码包里有一个很经典的播放器ffplay。
