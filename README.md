# VRCEasyTrans
一个基于 C++ Qt 框架的软件，轻量化实现实时语音识别+翻译，专为 VRchat 虚拟现实社交平台设计。


### 🚀 项目特色

- ⚡ 超低延迟 - C++ 原生实现，响应速度比 Python 版本快 5-10 倍
- 💾 轻量高效 - 内存占用仅 20-40MB，启动时间 < 0.3秒
- 🎯 精准识别 - 集成优化的语音识别引擎
- 🌍 多语言翻译 - 支持 DeepSeek API 实时翻译


### 📊 软件性能(对比 python 版本)

- 对比 PyQt6(Python)，C++ Qt 提升：
- 启动时间 1.2-2.0 秒 0.1-0.3 秒 6倍
- 内存占用 80-150 MB 20-40 MB 4倍
- 按钮响应 15-40 ms 1-5 ms 8倍
- 语音识别 200-500 ms 50-150 ms 3倍
- 包大小 ~200 MB ~15 MB 13倍


### 🎙️使用方法


1. 进入 release 分支
2. 下载打包好的压缩包并解压，进入软件根目录
3. 找到VRChatEasyTrans.exe，双击运行
4. 填写你的配置，以及讯飞星火和 Deepseek 的 API 认证信息（下面会讲怎么获取）
6. 点击启动，这时配置会保存，下次启动无需再次配置

- 讯飞星火配置
1. 注册讯飞开放平台：https://www.xfyun.cn
2. 登录后点击右上角的“控制台”
3. 创建一个应用，进入应用
4. 左边栏点击“语音识别”模块下的“语音听写”
5. 右边 Websocket服务接口认证信息，可以看到你创建的应用的APPID，APISecret 和 APIKey，复制到 VRChatEasyTrans 中对应的讯飞星火部分。
6. 每天有500次的免费额度，也就是500句话，应该够用了吧。。不够用的自己买

- Deepseek 配置
1. 注册 Deepseek开放平台：https://platform.deepseek.com
2. 登陆后点击左边栏的 API kyes
3. 点击 创建API key。注意，创建后API key只会立刻显示一次，请立即复制保存！将 API key 复制到 VRChatEasyTrans 中对应的Deepseek部分。
4. 点击左边栏的 用量信息 ，查看账户余额
5. 没有余额的话点击左边栏 充值
6. 先充值 10 块钱就行，能用好久


### 🛠 技术架构

流式结构

```
|
├── 语音捕获、分割
├── 音频数据编码、提交语音识别模型API
├── 得到的中文文本，提交翻译模型API
├── 得到的外文文本，打包成OSC发送到VRChat
├── 文本翻译显示
V
```

技术栈

- 前端界面: Qt6 Widgets + QML
- 语音识别: 讯飞星火
- 文本翻译: Deepseek
- 音频采集: Qt Multimedia
- 网络通信: Qt Network (HTTP/OSC)
- 配置管理: 静态管理类 + 配置文件
- 构建系统: CMake
- 编译器: mingw_32


### 📦 构建与使用

系统要求

- 操作系统: Windows 10/11, macOS 10.14+, Ubuntu 18.04+
- 编译器: C++17 兼容编译器
- 依赖: Qt6.0+, CMake 3.16+

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

VRchat 设置

1. 在 VRchat 中启用 OSC：
   · 设置 → 搜索 OSC → 启用 OSC
   · 默认端口：9000
2. 在 VRCEasyTrans 中的配置：
   · OSC 主机：127.0.0.1
   · OSC 端口：9000



### 🔧 开发者指南

项目结构

```
src/
├── main.cpp              # 程序入口
├── MainWindow/           # 主窗口类
├── AudioCapture/         # 音频采集
├── SpeechRecogniser/     # 语音识别
├── Translator/           # 翻译服务
└── ConfigManager/        # 配置管理
```

计划实现的功能

1. 可配置的接入其他语音识别API的方案
2. 可配置的接入其他AI翻译API的方案
3. 接入可识别中文方言的API
4. 为系统资源冗余的用户提供本地语音识别模型



### 🤝 参与贡献

本项目欢迎各种形式的贡献！
本人是学生平时比较忙，如果开启 pull request 很久没有回复可以发送邮件提醒，请谅解👉🏻👈🏻


### 📞 支持与反馈

如果您遇到问题或有建议：

1. 查看或提交新的 Issues
2. 发送邮件至: 1375803462@qq.com
---
