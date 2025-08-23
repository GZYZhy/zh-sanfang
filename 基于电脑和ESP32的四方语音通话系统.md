# 基于电脑和ESP32的四方语音通话系统

## Core Features

- 电脑WiFi热点管理

- 网页可视化控制界面

- 四方实时语音通话

- 闭麦功能控制

- 状态指示灯控制

- Tally灯状态显示

- 通话录音功能

- 设备状态实时监控

## Tech Stack

{
  "Backend": {
    "language": "Python",
    "framework": "FastAPI",
    "websocket": "WebSockets",
    "audio": "PyAudio",
    "database": "SQLite"
  },
  "Frontend": {
    "arch": "react",
    "language": "TypeScript",
    "component": "tdesign",
    "build": "Vite"
  },
  "ESP32": {
    "language": "C++",
    "framework": "Arduino",
    "bluetooth": "A2DP",
    "communication": "WebSocket"
  }
}

## Design

Glassmorphism Tech Blue UI风格，深蓝色科技主题，半透明玻璃质感面板，圆角设计，现代科技感界面布局

## Plan

Note: 

- [ ] is holding
- [/] is doing
- [X] is done

---

[ ] 初始化Python后端项目结构，创建main.py和requirements.txt

[ ] 实现FastAPI基础框架和WebSocket通信模块

[ ] 开发WiFi热点管理功能，支持热点创建和设备连接监控

[ ] 实现音频采集和播放模块，使用PyAudio处理音频流

[ ] 创建WebSocket音频数据传输协议，实现实时语音通信

[ ] 开发设备状态管理模块，存储和查询设备连接信息

[ ] 实现录音功能模块，支持音频录制和文件存储

[ ] 初始化React前端项目，配置Vite和TypeScript

[ ] 集成TDesign UI组件库，搭建基础布局框架

[ ] 开发WebSocket客户端连接模块，实现实时数据通信

[ ] 创建设备状态卡片组件，显示连接状态和设备信息

[ ] 实现通话控制面板，包含闭麦按钮和音量控制

[ ] 开发录音控制界面，支持开始/停止录音和文件管理

[ ] 编写ESP32 Arduino固件框架，包含WiFi连接模块

[ ] 实现ESP32 WebSocket客户端，与电脑服务端通信

[ ] 开发蓝牙音频模块，支持A2DP协议连接蓝牙耳机

[ ] 实现GPIO控制模块，驱动状态LED和Tally灯

[ ] 集成I2S音频编解码，处理音频数据输入输出

[ ] 测试四方语音通话功能，优化音频延迟和质量

[ ] 进行系统集成测试，验证所有功能模块协同工作
