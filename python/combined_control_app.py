from flask import Flask, render_template, jsonify, request
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import pyaudio
import time
import sys
import json
import os
from collections import deque
from threading import Lock

app = Flask(__name__)

# ===== 配置部分 =====
# RGB灯控制配置
RGB_MQTT_BROKER = "24.233.0.55"
RGB_MQTT_PORT = 1883
RGB_MQTT_TOPIC = "zhsf/tally"
RGB_MQTT_USERNAME = "esptalk"
RGB_MQTT_PASSWORD = "zhsanfang"

# 音频监听配置
AUDIO_MQTT_BROKER = "24.233.0.55"
AUDIO_MQTT_PORT = 1883
AUDIO_MQTT_TOPIC = "zhsf/audio"
AUDIO_MQTT_USERNAME = "esptalk"
AUDIO_MQTT_PASSWORD = "zhsanfang"

# RGB灯模式定义
RGB_MODES = {
    "0": {"name": "关闭", "description": "关闭所有灯"},
    "1": {"name": "绿色常亮", "description": "绿色常亮（开机默认）"},
    "2": {"name": "黄色闪烁", "description": "黄色闪烁（500ms间隔）"},
    "3": {"name": "黄色常亮", "description": "黄色常亮"},
    "4": {"name": "红色闪烁", "description": "红色闪烁（500ms间隔）"},
    "5": {"name": "红色常亮", "description": "红色常亮"},
    "6": {"name": "红蓝交替", "description": "红蓝交替闪烁（500ms间隔）"}
}

# ===== 全局变量和锁 =====
# RGB灯状态
message_history = deque(maxlen=100)
current_states = {}
rgb_lock = Lock()

# 音频监听状态
audio_listening = False
audio_client = None
audio_stream = None
p_audio = None
audio_timeflag = 0
audio_lock = Lock()

# ===== RGB灯控制功能 =====
def send_rgb_control(device_id, mode):
    """发送RGB灯控制指令"""
    message = f"{device_id}:{mode}"
    try:
        auth = None
        if RGB_MQTT_USERNAME and RGB_MQTT_PASSWORD:
            auth = {'username': RGB_MQTT_USERNAME, 'password': RGB_MQTT_PASSWORD}
        
        publish.single(RGB_MQTT_TOPIC, message, 
                      hostname=RGB_MQTT_BROKER, 
                      port=RGB_MQTT_PORT,
                      auth=auth)
        
        # 更新状态和历史记录
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        with rgb_lock:
            message_history.append({
                'timestamp': timestamp,
                'topic': RGB_MQTT_TOPIC,
                'message': message
            })
            
            current_states[device_id] = {
                'mode': mode,
                'mode_name': RGB_MODES.get(mode, {}).get('name', '未知'),
                'last_update': timestamp
            }
        
        return {'status': 'success', 'message': f'已发送控制指令: {message}'}
    except Exception as e:
        return {'status': 'error', 'message': f'MQTT发送失败: {str(e)}'}

# ===== 音频监听功能 =====
def audio_on_connect(client, userdata, flags, rc):
    """音频MQTT连接回调"""
    if rc == 0:
        print("音频监听MQTT客户端已连接")
        client.subscribe(AUDIO_MQTT_TOPIC)
        print("音频监听就绪!")
    else:
        print(f"音频监听连接失败，错误码: {rc}")

def audio_on_message(client, userdata, msg):
    """音频MQTT消息回调"""
    global audio_timeflag
    with audio_lock:
        if audio_listening and audio_stream and msg.topic == AUDIO_MQTT_TOPIC:
            audio_timeflag += 1
            sys.stdout.write("\033[F")
            print("\r" + str(audio_timeflag) + " " * 10)
            try:
                # 解析带设备ID的音频数据 (格式: "设备ID:音频数据")
                payload = msg.payload
                if b':' in payload:
                    _, audio_data = payload.split(b':', 1)
                    audio_stream.write(audio_data)
                else:
                    audio_stream.write(payload)
            except Exception as e:
                print(f"处理音频数据错误: {e}")

def start_audio_listening():
    """开始音频监听"""
    global audio_client, audio_stream, p_audio, audio_listening
    
    with audio_lock:
        if audio_listening:
            return {'status': 'info', 'message': '已经在监听中'}
        
        try:
            # 初始化音频
            p_audio = pyaudio.PyAudio()
            audio_stream = p_audio.open(format=p_audio.get_format_from_width(1), 
                                      channels=1, rate=16000, output=True)
            
            # 初始化MQTT客户端
            client_id = time.strftime('%Y%m%d%H%M%S', time.localtime(time.time()))
            audio_client = mqtt.Client(client_id)
            audio_client.on_connect = audio_on_connect
            audio_client.on_message = audio_on_message
            audio_client.username_pw_set(AUDIO_MQTT_USERNAME, AUDIO_MQTT_PASSWORD)
            
            audio_client.connect(AUDIO_MQTT_BROKER, AUDIO_MQTT_PORT, 15)
            audio_client.loop_start()
            
            audio_listening = True
            audio_timeflag = 0
            return {'status': 'success', 'message': '开始监听音频'}
            
        except Exception as e:
            print(f"启动音频监听失败: {e}")
            if audio_stream:
                audio_stream.close()
            if p_audio:
                p_audio.terminate()
            return {'status': 'error', 'message': f'启动监听失败: {e}'}

def stop_audio_listening():
    """停止音频监听"""
    global audio_client, audio_stream, p_audio, audio_listening
    
    with audio_lock:
        if not audio_listening:
            return {'status': 'info', 'message': '未在监听状态'}
        
        try:
            if audio_client:
                audio_client.loop_stop()
                audio_client.disconnect()
                audio_client = None
            
            if audio_stream:
                audio_stream.stop_stream()
                audio_stream.close()
                audio_stream = None
            
            if p_audio:
                p_audio.terminate()
                p_audio = None
            
            audio_listening = False
            return {'status': 'success', 'message': '停止监听音频'}
            
        except Exception as e:
            print(f"停止音频监听失败: {e}")
            return {'status': 'error', 'message': f'停止监听失败: {e}'}

# ===== Flask路由 =====
@app.route('/ctrl/tally')
def tally_control():
    """RGB灯控制页面"""
    return render_template('control.html')

@app.route('/ctrl/listen')
def audio_control():
    """音频控制页面"""
    return render_template('audio_control.html')

@app.route('/ctrl/api/rgb/control', methods=['POST'])
def api_rgb_control():
    """RGB灯控制API"""
    data = request.get_json()
    device_id = data.get('device', '极速_1')
    mode = data.get('mode', '1')
    result = send_rgb_control(device_id, mode)
    return jsonify(result)

@app.route('/ctrl/api/rgb/modes')
def api_rgb_modes():
    """获取RGB模式列表"""
    return jsonify(RGB_MODES)

@app.route('/ctrl/api/rgb/history')
def api_rgb_history():
    """获取历史记录"""
    with rgb_lock:
        return jsonify(list(message_history))

@app.route('/ctrl/api/rgb/status')
def api_rgb_status():
    """获取当前状态"""
    with rgb_lock:
        return jsonify(current_states)

@app.route('/ctrl/api/audio/start', methods=['POST'])
def api_audio_start():
    """开始音频监听API"""
    result = start_audio_listening()
    return jsonify(result)

@app.route('/ctrl/api/audio/stop', methods=['POST'])
def api_audio_stop():
    """停止音频监听API"""
    result = stop_audio_listening()
    return jsonify(result)

@app.route('/ctrl/api/audio/status')
def api_audio_status():
    """获取音频状态API"""
    with audio_lock:
        return jsonify({
            'listening': audio_listening,
            'count': audio_timeflag
        })

# ===== 模板创建和启动 =====
def create_templates():
    """创建HTML模板"""
    os.makedirs('templates', exist_ok=True)
    
    # 主控制页面模板
    with open('templates/control.html', 'w') as f:
        f.write('''<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>综合控制台</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 1000px; margin: 0 auto; }
        .section { background: white; padding: 20px; margin: 20px 0; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        h1, h2 { color: #333; }
        .control-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        select, button { padding: 10px; margin: 5px; border-radius: 5px; border: 1px solid #ddd; }
        button { background: #007bff; color: white; border: none; cursor: pointer; }
        button:hover { background: #0056b3; }
        .quick-controls { display: flex; gap: 10px; margin: 10px 0; }
        .quick-btn { padding: 8px 15px; }
        .btn-off { background: #6c757d; }
        .btn-green { background: #28a745; }
        .btn-yellow { background: #ffc107; color: black; }
        .btn-red { background: #dc3545; }
        .status { padding: 10px; margin: 10px 0; border-radius: 5px; display: none; }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .nav { display: flex; gap: 20px; margin-bottom: 20px; }
        .nav a { padding: 10px 20px; background: #007bff; color: white; text-decoration: none; border-radius: 5px; }
        .nav a:hover { background: #0056b3; }
    </style>
</head>
<body>
    <div class="container">
        <h1>综合控制台</h1>
        
        <div class="nav">
            <a href="/ctrl/tally">RGB灯控制</a>
            <a href="/ctrl/listen">音频监听</a>
        </div>

        <div class="section">
            <h2>RGB灯控制</h2>
            
            <div class="control-group">
                <label for="deviceSelect">选择设备:</label>
                <select id="deviceSelect">
                    <option value="zhsf_1">zhsf_1</option>
                    <option value="zhsf_2">zhsf_2</option>
                    <option value="zhsf_3">zhsf_3</option>
                </select>
            </div>

            <div class="quick-controls">
                <button class="quick-btn btn-off" onclick="controlRGB('0')">关闭</button>
                <button class="quick-btn btn-green" onclick="controlRGB('1')">绿色</button>
                <button class="quick-btn btn-yellow" onclick="controlRGB('3')">黄色</button>
                <button class="quick-btn btn-red" onclick="controlRGB('5')">红色</button>
            </div>

            <div class="control-group">
                <label for="modeSelect">选择模式:</label>
                <select id="modeSelect">
                    <option value="0">0 - 关闭所有灯</option>
                    <option value="1" selected>1 - 绿色常亮（默认）</option>
                    <option value="2">2 - 黄色闪烁</option>
                    <option value="3">3 - 黄色常亮</option>
                    <option value="4">4 - 红色闪烁</option>
                    <option value="5">5 - 红色常亮</option>
                    <option value="6">6 - 红蓝交替闪烁</option>
                </select>
            </div>

            <button onclick="sendRGBControl()">发送控制指令</button>

            <div id="rgbStatus" class="status"></div>
        </div>
    </div>

    <script>
        // RGB控制功能
        async function controlRGB(mode) {
            document.getElementById('modeSelect').value = mode;
            await sendRGBControl();
        }

        async function sendRGBControl() {
            const device = document.getElementById('deviceSelect').value;
            const mode = document.getElementById('modeSelect').value;
            
            const statusDiv = document.getElementById('rgbStatus');
            statusDiv.style.display = 'block';
            statusDiv.className = 'status';
            statusDiv.textContent = '发送中...';
            
            try {
                const response = await fetch('/ctrl/api/rgb/control', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({ device, mode })
                });
                
                const result = await response.json();
                
                if (result.status === 'success') {
                    statusDiv.className = 'status success';
                } else {
                    statusDiv.className = 'status error';
                }
                statusDiv.textContent = result.message;
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '网络错误: ' + error.message;
            }
            
            setTimeout(() => {
                statusDiv.style.display = 'none';
            }, 3000);
        }
    </script>
</body>
</html>''')

    # 音频控制页面模板
    with open('templates/audio_control.html', 'w') as f:
        f.write('''<!DOCTYPE html>
<html>
<head>
    <title>音频监听控制</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .container { text-align: center; max-width: 600px; margin: 0 auto; }
        button { padding: 15px 30px; font-size: 16px; margin: 10px; border-radius: 5px; border: none; cursor: pointer; }
        .listening { background-color: #4CAF50; color: white; }
        .stopped { background-color: #f44336; color: white; }
        .status { margin: 20px; font-size: 18px; }
        .nav { display: flex; gap: 20px; margin-bottom: 20px; justify-content: center; }
        .nav a { padding: 10px 20px; background: #007bff; color: white; text-decoration: none; border-radius: 5px; }
        .nav a:hover { background: #0056b3; }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav">
            <a href="/ctrl/tally">RGB灯控制</a>
            <a href="/ctrl/listen">音频监听</a>
        </div>
        
        <h1>音频监听控制</h1>
        <button id="toggleBtn" class="stopped" onclick="toggleListening()">开始监听</button>
        <div class="status">
            <p>状态: <span id="statusText">未监听</span></p>
            <p>接收包数: <span id="packetCount">0</span></p>
        </div>
    </div>

    <script>
        let isListening = false;
        let packetCount = 0;
        let statusInterval = null;
        
        async function updateStatus() {
            try {
                const response = await fetch('/ctrl/api/audio/status');
                const data = await response.json();
                
                isListening = data.listening;
                packetCount = data.count;
                
                document.getElementById('statusText').textContent = 
                    isListening ? '监听中' : '未监听';
                document.getElementById('packetCount').textContent = packetCount;
                document.getElementById('toggleBtn').textContent = 
                    isListening ? '停止监听' : '开始监听';
                document.getElementById('toggleBtn').className = 
                    isListening ? 'listening' : 'stopped';
            } catch (error) {
                console.error('状态更新失败:', error);
            }
        }
        
        async function toggleListening() {
            try {
                const url = isListening ? '/ctrl/api/audio/stop' : '/ctrl/api/audio/start';
                const response = await fetch(url, { method: 'POST' });
                const data = await response.json();
                
                alert(data.message);
                
                // 更新状态显示
                await updateStatus();
                
                // 开始或停止状态轮询
                if (data.message.includes('开始') || data.message.includes('启动')) {
                    if (!statusInterval) {
                        statusInterval = setInterval(updateStatus, 1000);
                    }
                } else if (statusInterval) {
                    clearInterval(statusInterval);
                    statusInterval = null;
                }
            } catch (error) {
                console.error('切换监听状态失败:', error);
                alert('操作失败: ' + error.message);
            }
        }
        
        // 初始状态更新
        updateStatus();
    </script>
</body>
</html>''')

if __name__ == '__main__':
    create_templates()
    print("启动综合控制服务")
    print("RGB灯控制: http://localhost:5000/ctrl/tally")
    print("音频监听控制: http://localhost:5000/ctrl/listen")
    app.run(host='0.0.0.0', port=5000, debug=True)
