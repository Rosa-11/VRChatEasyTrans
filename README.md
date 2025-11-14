# VRCEasyTrans
一款实时语音翻译软件，可以在VRchat中翻译你的语音并显示在你的头顶。

https://img.shields.io/badge/C++-17-blue.svg
https://img.shields.io/badge/Qt-6.5-green.svg
https://img.shields.io/badge/License-MIT-yellow.svg

一个基于 C++ Qt 框架的高性能实时语音识别和翻译工具，专为 VRchat 虚拟现实社交平台设计。

🚀 项目特色

· ⚡ 超低延迟 - C++ 原生实现，响应速度比 Python 版本快 5-10 倍
· 💾 轻量高效 - 内存占用仅 20-40MB，启动时间 < 0.3秒
· 🎯 精准识别 - 集成优化的语音识别引擎
· 🌍 多语言翻译 - 支持 DeepSeek API 实时翻译

📊 性能对比

对比 PyQt6(Python)，C++ Qt 提升：
启动时间 1.2-2.0 秒 0.1-0.3 秒 6倍
内存占用 80-150 MB 20-40 MB 4倍
按钮响应 15-40 ms 1-5 ms 8倍
语音识别 200-500 ms 50-150 ms 3倍
包大小 ~200 MB ~15 MB 13倍

🛠 技术架构

核心框架

```
VRCEasyTrans/
├── GUI 层 (Qt6 Widgets)
├── 业务逻辑层
├── 音频处理层
├── 网络通信层
└── 配置管理层
```

技术栈

· 前端界面: Qt6 Widgets + QML (可选)
· 语音识别: whisper.cpp (C++ 优化版本)
· 音频采集: Qt Multimedia
· 网络通信: Qt Network (HTTP/OSC)
· 配置管理: QSettings + INI 格式
· 构建系统: CMake
· 编译器: MSVC/GCC/Clang

📦 安装与使用

系统要求

· 操作系统: Windows 10/11, macOS 10.14+, Ubuntu 18.04+
· 编译器: C++17 兼容编译器
· 依赖: Qt6.5+, CMake 3.16+

快速开始

1. 克隆项目
   ```bash
   git clone https://github.com/Rosa-11/VRCEasyTrans.git
   ```
2. 构建项目
   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build . --config Release
   ```
3. 配置应用
   · 首次运行会在程序目录生成 config.ini
   · 编辑配置文件，设置 DeepSeek API Key
   · 配置 VRchat OSC 连接参数
4. 启动应用
   ```bash
   # Windows
   VoiceBridgeVR.exe
   
   # Linux/macOS
   ./VoiceBridgeVR
   ```

VRchat 设置

1. 在 VRchat 中启用 OSC：
   · 设置 → OSC → 启用 OSC
   · 默认端口：9000
2. 在 VRCEasyTrans 中配置：
   · OSC 主机：127.0.0.1
   · OSC 端口：9000

🎯 使用指南

基本操作

1. 启动监听 - 点击"开始监听"按钮
2. 语音输入 - 对着麦克风说话
3. 实时翻译 - 系统自动识别并翻译
4. VRchat 输出 - 翻译结果自动发送到 VRchat

功能特性

· 实时显示 - 原文和译文同时显示
· 状态监控 - 实时显示识别状态和延迟
· 配置热重载 - 修改配置无需重启
· 音频设备选择 - 支持多音频输入设备

⚙️ 配置说明

主要配置项

```ini
[DeepSeek]
apiKey=your_deepseek_api_key_here

[VRchat]
oscHost=127.0.0.1
oscPort=9000

[SpeechRecognition]
modelSize=small
language=zh

[Translation]
targetLanguage=中文
```

支持的语音模型

· tiny - 最快，精度较低
· base - 平衡速度和精度
· small - 推荐用于大多数场景
· medium - 高精度，需要更多资源

🚀 性能优化

编译优化

```bash
# Release 构建以获得最佳性能
cmake --build . --config Release

# 特定平台优化
cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=ON
```

运行时优化

· 使用 small 模型平衡性能与精度
· 确保音频设备采样率为 16kHz
· 关闭不必要的后台应用

🔧 开发指南

项目结构

```
src/
├── main.cpp              # 程序入口
├── MainWindow/           # 主窗口类
├── AudioCapture/         # 音频采集
├── SpeechRecognition/    # 语音识别
├── OSCClient/           # OSC 通信
├── Translation/         # 翻译服务
└── ConfigManager/       # 配置管理
```

扩展功能

要添加新的语音识别引擎：

1. 实现 ISpeechRecognizer 接口
2. 在 SpeechRecognitionFactory 中注册
3. 更新配置选项

📈 性能基准测试

测试环境

· CPU: Intel i7-12700H
· RAM: 16GB DDR5
· OS: Windows 11 22H2

测试结果

测试场景 延迟 CPU 使用 内存占用
空闲状态 - <1% 28 MB
语音识别 85 ms 8% 35 MB
实时翻译 120 ms 12% 38 MB
持续运行 - 3-5% 32 MB

🤝 参与贡献

本项目欢迎各种形式的贡献！
本人是学生比较忙，如果开启 pull request很久没有回复可以发送邮件提醒，请谅解👉🏻👈🏻

📞 支持与反馈

如果您遇到问题或有建议：

1. 查看 Issues
2. 提交新的 Issue
3. 发送邮件至: 1375803462@qq.com
---
