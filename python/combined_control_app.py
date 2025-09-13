from flask import Flask, render_template, jsonify, request, redirect
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import pyaudio
import time
import sys
import json
import os
import threading
import atexit
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

# 设备管理状态
devices = {
    'zhsf_1': {'name': '设备1', 'online': False, 'last_seen': None},
    'zhsf_2': {'name': '设备2', 'online': False, 'last_seen': None},
    'zhsf_3': {'name': '设备3', 'online': False, 'last_seen': None}
}
device_lock = Lock()

# 麦克风状态
mic_states = {}
mic_lock = Lock()

# 音量状态
volume_states = {}
volume_lock = Lock()

# MQTT响应监听
mqtt_responses = {}
mqtt_lock = Lock()

# 设备状态监控MQTT客户端
status_client = None
status_client_connected = False
status_lock = Lock()

# 设备发现线程状态
discovery_thread_started = False

# MQTT客户端初始化标志
mqtt_client_initialized = False

# ===== MQTT响应监听功能 =====
def mqtt_response_callback(client, userdata, msg):
    """处理MQTT响应消息"""
    try:
        payload = msg.payload.decode('utf-8')

        # 只处理真正的响应消息（以[re]开头）
        if payload.startswith('[re]'):
            print(f"📨 MQTT响应: {payload}")

            # 解析响应格式 [re]device_id:response_data
            parts = payload[4:].split(':', 1)
            if len(parts) == 2:
                device_id, response_data = parts

                with mqtt_lock:
                    mqtt_responses[device_id] = {
                        'data': response_data,
                        'timestamp': time.time()
                    }

                # 更新设备在线状态
                update_device_online_status(device_id)

                # 解析不同类型的响应
                if response_data.startswith('v'):  # 音量响应
                    try:
                        volume = int(response_data[1:])
                        with volume_lock:
                            volume_states[device_id] = volume
                        print(f"🔊 设备 {device_id} 音量: {volume}%")
                    except ValueError:
                        pass
                elif response_data in ['g', 'k']:  # 麦克风状态响应
                    with mic_lock:
                        mic_states[device_id] = response_data
                    status_text = "静音" if response_data == 'g' else "开麦"
                    print(f"🎤 设备 {device_id} 麦克风: {status_text}")
                elif response_data.isdigit():  # RGB模式响应
                    with rgb_lock:
                        current_states[device_id] = {
                            'mode': response_data,
                            'mode_name': RGB_MODES.get(response_data, {}).get('name', '未知'),
                            'last_update': time.strftime('%Y-%m-%d %H:%M:%S')
                        }
                    print(f"💡 设备 {device_id} RGB模式: {response_data} ({RGB_MODES.get(response_data, {}).get('name', '未知')})")
                else:
                    print(f"❓ 设备 {device_id} 未知响应: {response_data}")
            else:
                print(f"⚠️  无效响应格式: {payload}")
        else:
            # 检查是否是本机发出的命令消息
            if ':' in payload and not payload.startswith('[re]'):
                # 可能是本机发出的命令，如 "zhsf_1:n"
                parts = payload.split(':', 1)
                if len(parts) == 2:
                    device_id, command = parts
                    print(f"📤 MQTT命令: {device_id} -> {command}")
                else:
                    print(f"📤 MQTT消息: {payload}")
            else:
                print(f"📨 MQTT其他消息: {payload}")

    except Exception as e:
        print(f"❌ 处理MQTT消息错误: {e}")
        print(f"   原始消息: {msg.payload}")

def send_mqtt_command(device_id, command, timeout=5):
    """发送MQTT命令并等待响应"""
    message = f"{device_id}:{command}"

    # 清理旧的响应
    with mqtt_lock:
        if device_id in mqtt_responses:
            del mqtt_responses[device_id]

    try:
        auth = None
        if RGB_MQTT_USERNAME and RGB_MQTT_PASSWORD:
            auth = {'username': RGB_MQTT_USERNAME, 'password': RGB_MQTT_PASSWORD}

        publish.single(RGB_MQTT_TOPIC, message,
                      hostname=RGB_MQTT_BROKER,
                      port=RGB_MQTT_PORT,
                      auth=auth)

        # 记录发送历史
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        with rgb_lock:
            message_history.append({
                'timestamp': timestamp,
                'topic': RGB_MQTT_TOPIC,
                'message': message,
                'type': 'command'
            })

        # 等待响应
        start_time = time.time()
        while time.time() - start_time < timeout:
            with mqtt_lock:
                if device_id in mqtt_responses:
                    response = mqtt_responses[device_id]
                    del mqtt_responses[device_id]
                    print(f"✅ 设备 {device_id} 响应成功: {response['data']}")
                    return {'status': 'success', 'response': response['data']}

            time.sleep(0.1)

        print(f"⏰ 设备 {device_id} 响应超时 ({timeout}秒)")
        return {'status': 'timeout', 'message': '等待响应超时'}

    except Exception as e:
        return {'status': 'error', 'message': f'MQTT发送失败: {str(e)}'}

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

# ===== 麦克风控制功能 =====
def query_microphone_status(device_id):
    """查询麦克风状态"""
    return send_mqtt_command(device_id, 'm')

def control_microphone(device_id, action):
    """控制麦克风状态"""
    # action: 'g' for mute, 'k' for unmute
    return send_mqtt_command(device_id, action)

# ===== 音量控制功能 =====
def query_volume(device_id):
    """查询音量状态"""
    return send_mqtt_command(device_id, 'nv')

def set_volume(device_id, volume):
    """设置音量"""
    if not (0 <= volume <= 100):
        return {'status': 'error', 'message': '音量范围必须在0-100之间'}

    return send_mqtt_command(device_id, f'v{volume}')

# ===== 设备状态查询功能 =====
def query_device_status(device_id):
    """查询设备状态"""
    return send_mqtt_command(device_id, 'n')

def update_all_device_statuses():
    """更新所有设备状态"""
    results = {}
    for device_id in devices.keys():
        # 查询设备状态
        status_result = query_device_status(device_id)
        results[device_id] = {'status': status_result}

        # 查询麦克风状态
        mic_result = query_microphone_status(device_id)
        results[device_id]['microphone'] = mic_result

        # 查询音量状态
        volume_result = query_volume(device_id)
        results[device_id]['volume'] = volume_result

    return results

def discover_devices():
    """设备发现机制 - 主动查询所有设备状态"""
    print("🔍 开始设备发现...")

    for device_id in devices.keys():
        try:
            print(f"🔎 查询设备 {device_id} 状态...")

            # 发送设备状态查询
            result = query_device_status(device_id)

            # 检查是否收到真正的响应（不是本机发出的命令）
            if result.get('status') == 'success':
                # 只有收到真正的响应时，才更新设备为在线
                with device_lock:
                    devices[device_id]['online'] = True
                    devices[device_id]['last_seen'] = time.strftime('%Y-%m-%d %H:%M:%S')
                print(f"✅ 设备 {device_id} 在线 - 收到响应: {result.get('response')}")
            else:
                # 查询失败，检查是否需要标记为离线
                with device_lock:
                    if devices[device_id]['last_seen']:
                        try:
                            last_seen_time = time.strptime(devices[device_id]['last_seen'], '%Y-%m-%d %H:%M:%S')
                            last_seen_timestamp = time.mktime(last_seen_time)
                            current_time = time.time()

                            # 如果最后响应超过5分钟，标记为离线
                            if current_time - last_seen_timestamp > 300:  # 5分钟超时
                                devices[device_id]['online'] = False
                                print(f"❌ 设备 {device_id} 离线 - 最后响应超过5分钟")
                            else:
                                # 5分钟内有响应过，保持在线状态
                                print(f"⏳ 设备 {device_id} 可能在线 - 最近有响应，等待下次确认")
                        except ValueError:
                            # 时间解析失败，标记为离线
                            devices[device_id]['online'] = False
                            print(f"❌ 设备 {device_id} 离线 - 时间戳错误")
                    else:
                        # 从未响应过，标记为离线
                        devices[device_id]['online'] = False
                        print(f"❌ 设备 {device_id} 离线 - 从未收到响应")

        except Exception as e:
            print(f"设备发现错误 {device_id}: {e}")

def status_mqtt_on_disconnect(client, userdata, rc):
    """设备状态监控MQTT断开连接回调"""
    global status_client_connected
    with status_lock:
        was_connected = status_client_connected
        status_client_connected = False

    if rc == 0:
        if was_connected:  # 只在状态改变时记录
            print("📡 设备状态监控MQTT客户端正常断开")
    else:
        if was_connected:  # 只在状态改变时记录
            print(f"📡 设备状态监控MQTT客户端意外断开 (错误码: {rc})，将自动重连...")
        # 不需要手动重连，paho-mqtt会自动重连

def status_mqtt_on_connect(client, userdata, flags, rc):
    """设备状态监控MQTT连接回调"""
    global status_client_connected
    if rc == 0:
        with status_lock:
            was_connected = status_client_connected
            status_client_connected = True

        if not was_connected:  # 只在状态改变时记录
            print("📡 设备状态监控MQTT客户端已连接")
            client.subscribe(RGB_MQTT_TOPIC)
            print("📡 设备状态监控就绪!")
    else:
        print(f"📡 设备状态监控连接失败，错误码: {rc}")
        with status_lock:
            status_client_connected = False

def start_status_monitoring():
    """启动设备状态监控MQTT客户端"""
    global status_client, mqtt_client_initialized

    try:
        # 检查是否已经有监控在运行
        if status_client and status_client.is_connected():
            print("📡 设备状态监控已在运行，无需重复启动")
            return

        # 如果已经初始化过且客户端存在，直接重连
        if mqtt_client_initialized and status_client:
            print("📡 重新连接设备状态监控MQTT客户端...")
            status_client.reconnect()
            return

        client_id = f"status_monitor_{int(time.time())}"
        status_client = mqtt.Client(client_id, clean_session=False)

        # 设置回调函数
        status_client.on_connect = status_mqtt_on_connect
        status_client.on_disconnect = status_mqtt_on_disconnect
        status_client.on_message = mqtt_response_callback

        # 设置认证
        status_client.username_pw_set(RGB_MQTT_USERNAME, RGB_MQTT_PASSWORD)

        # 连接参数优化 - 更长的keepalive时间，避免频繁断开
        # 设置自动重连参数
        status_client.reconnect_delay_set(min_delay=1, max_delay=120)
        status_client.connect(RGB_MQTT_BROKER, RGB_MQTT_PORT, keepalive=600)

        # 启动循环
        status_client.loop_start()
        mqtt_client_initialized = True
        print("📡 设备状态监控MQTT客户端启动中...")
    except Exception as e:
        print(f"📡 启动设备状态监控失败: {e}")

def stop_status_monitoring():
    """停止设备状态监控"""
    global status_client, status_client_connected

    try:
        if status_client:
            status_client.loop_stop()
            status_client.disconnect()
            status_client = None

        with status_lock:
            status_client_connected = False

        print("📡 设备状态监控已停止")
    except Exception as e:
        print(f"📡 停止设备状态监控失败: {e}")

def start_device_discovery():
    """启动设备发现定时器"""
    global discovery_thread_started

    # 防止重复启动
    if discovery_thread_started:
        print("🔍 设备发现已在运行，无需重复启动")
        return

    # 首先启动设备状态监控MQTT客户端
    start_status_monitoring()

    def discovery_loop():
        while True:
            try:
                discover_devices()
                time.sleep(30)  # 每30秒发现一次设备
            except Exception as e:
                print(f"设备发现循环错误: {e}")
                time.sleep(30)

    # 启动设备发现线程
    discovery_thread = threading.Thread(target=discovery_loop, daemon=True)
    discovery_thread.start()
    discovery_thread_started = True
    print("🔍 开始设备发现...")
    print("🔄 设备发现线程已启动")

# 在设备状态响应时立即更新在线状态
def update_device_online_status(device_id):
    """更新设备在线状态"""
    with device_lock:
        if device_id in devices:
            devices[device_id]['online'] = True
            devices[device_id]['last_seen'] = time.strftime('%Y-%m-%d %H:%M:%S')
            print(f"📡 设备 {device_id} 确认在线")

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

    # 只处理音频数据，响应消息由独立的设备状态监控客户端处理
    if msg.topic == AUDIO_MQTT_TOPIC:
        with audio_lock:
            # 再次检查监听状态和音频流，避免在停止过程中处理新数据
            if audio_listening and audio_stream and audio_stream.is_active():
                audio_timeflag += 1
                sys.stdout.write("\033[F")
                print("\r" + str(audio_timeflag) + " " * 10)
                try:
                    # 解析带设备ID的音频数据 (格式: "设备ID:音频数据")
                    payload = msg.payload
                    if b':' in payload:
                        _, audio_data = payload.split(b':', 1)
                        # 确保音频流仍然有效
                        if audio_stream and audio_stream.is_active():
                            audio_stream.write(audio_data)
                    else:
                        # 确保音频流仍然有效
                        if audio_stream and audio_stream.is_active():
                            audio_stream.write(payload)
                except Exception as e:
                    print(f"处理音频数据错误: {e}")
            else:
                # 如果不在监听状态，忽略音频数据
                pass

def start_audio_listening():
    """开始音频监听"""
    global audio_client, audio_stream, p_audio, audio_listening

    with audio_lock:
        # 首先检查是否已经在监听中
        if audio_listening:
            return {'status': 'info', 'message': '已经在监听中'}

        # 确保所有资源都被清理
        cleanup_audio_resources()

        try:
            print("🎵 初始化PyAudio...")
            # 初始化音频 - PyAudio
            p_audio = pyaudio.PyAudio()
            audio_stream = p_audio.open(format=p_audio.get_format_from_width(1),
                                      channels=1, rate=16000, output=True)
            print("🎵 PyAudio初始化成功")

            print("📡 初始化MQTT客户端...")
            # 初始化MQTT客户端
            client_id = f"audio_listener_{int(time.time())}"
            audio_client = mqtt.Client(client_id)
            audio_client.on_connect = audio_on_connect
            audio_client.on_message = audio_on_message
            audio_client.username_pw_set(AUDIO_MQTT_USERNAME, AUDIO_MQTT_PASSWORD)

            audio_client.connect(AUDIO_MQTT_BROKER, AUDIO_MQTT_PORT, 15)
            audio_client.loop_start()
            print("📡 MQTT客户端初始化成功")

            audio_listening = True
            audio_timeflag = 0
            print("✅ 音频监听已启动")
            return {'status': 'success', 'message': '开始监听音频'}

        except Exception as e:
            print(f"❌ 启动音频监听失败: {e}")
            # 确保清理所有资源
            cleanup_audio_resources()
            return {'status': 'error', 'message': f'启动监听失败: {e}'}

def cleanup_audio_resources():
    """清理音频相关资源"""
    global audio_client, audio_stream, p_audio, audio_listening

    try:
        print("🧹 清理音频资源...")

        # 先停止MQTT客户端
        if audio_client:
            try:
                audio_client.loop_stop()
                audio_client.disconnect()
                print("📡 MQTT客户端已停止")
            except Exception as e:
                print(f"停止MQTT客户端失败: {e}")
            finally:
                audio_client = None

        # 再停止音频流
        if audio_stream:
            try:
                if audio_stream.is_active():
                    audio_stream.stop_stream()
                audio_stream.close()
                print("🎵 音频流已关闭")
            except Exception as e:
                print(f"关闭音频流失败: {e}")
            finally:
                audio_stream = None

        # 最后终止PyAudio
        if p_audio:
            try:
                p_audio.terminate()
                print("🎵 PyAudio已终止")
            except Exception as e:
                print(f"终止PyAudio失败: {e}")
            finally:
                p_audio = None

        print("✅ 音频资源清理完成")

    except Exception as e:
        print(f"清理音频资源时出错: {e}")

def stop_audio_listening():
    """停止音频监听"""
    global audio_listening

    with audio_lock:
        if not audio_listening:
            return {'status': 'info', 'message': '未在监听状态'}

        try:
            print("🛑 正在停止音频监听...")
            audio_listening = False  # 先设置标志，避免新的音频数据被处理

            # 清理所有资源
            cleanup_audio_resources()

            print("✅ 音频监听已停止")
            return {'status': 'success', 'message': '停止监听音频'}

        except Exception as e:
            print(f"❌ 停止音频监听失败: {e}")
            # 即使出错也要尝试清理资源
            cleanup_audio_resources()
            return {'status': 'error', 'message': f'停止监听失败: {e}'}

# ===== Flask路由 =====
@app.route('/')
def index():
    """根目录重定向到 /zhsf"""
    return redirect('/zhsf', code=301)

@app.route('/zhsf')
def main_control():
    """综合控制页面"""
    return render_template('dashboard.html')

@app.route('/zhsf/api/rgb/control', methods=['POST'])
def api_rgb_control():
    """RGB灯控制API"""
    data = request.get_json()
    device_id = data.get('device', '极速_1')
    mode = data.get('mode', '1')
    result = send_rgb_control(device_id, mode)
    return jsonify(result)

@app.route('/zhsf/api/rgb/modes')
def api_rgb_modes():
    """获取RGB模式列表"""
    return jsonify(RGB_MODES)

@app.route('/zhsf/api/rgb/history')
def api_rgb_history():
    """获取历史记录"""
    with rgb_lock:
        return jsonify(list(message_history))

@app.route('/zhsf/api/rgb/status')
def api_rgb_status():
    """获取当前状态"""
    with rgb_lock:
        return jsonify(current_states)

@app.route('/zhsf/api/audio/start', methods=['POST'])
def api_audio_start():
    """开始音频监听API"""
    result = start_audio_listening()
    return jsonify(result)

@app.route('/zhsf/api/audio/stop', methods=['POST'])
def api_audio_stop():
    """停止音频监听API"""
    result = stop_audio_listening()
    return jsonify(result)

@app.route('/zhsf/api/audio/status')
def api_audio_status():
    """获取音频状态API"""
    with audio_lock:
        return jsonify({
            'listening': audio_listening,
            'count': audio_timeflag
        })

# ===== 设备管理API =====
@app.route('/zhsf/api/devices')
def api_get_devices():
    """获取设备列表"""
    with device_lock:
        return jsonify(devices)

@app.route('/zhsf/api/devices/status')
def api_device_status():
    """获取所有设备状态"""
    results = update_all_device_statuses()
    return jsonify(results)

@app.route('/zhsf/api/devices/<device_id>/status')
def api_single_device_status(device_id):
    """获取单个设备状态"""
    status_result = query_device_status(device_id)
    mic_result = query_microphone_status(device_id)
    volume_result = query_volume(device_id)

    return jsonify({
        'device_id': device_id,
        'status': status_result,
        'microphone': mic_result,
        'volume': volume_result
    })

# ===== 麦克风控制API =====
@app.route('/zhsf/api/microphone/<device_id>/query', methods=['GET'])
def api_query_microphone(device_id):
    """查询麦克风状态"""
    result = query_microphone_status(device_id)
    return jsonify(result)

@app.route('/zhsf/api/microphone/<device_id>/control', methods=['POST'])
def api_control_microphone(device_id):
    """控制麦克风状态"""
    data = request.get_json()
    action = data.get('action')  # 'mute' or 'unmute'

    if action not in ['mute', 'unmute']:
        return jsonify({'status': 'error', 'message': '无效的操作'})

    command = 'g' if action == 'mute' else 'k'
    result = control_microphone(device_id, command)
    return jsonify(result)

# ===== 音量控制API =====
@app.route('/zhsf/api/volume/<device_id>/query', methods=['GET'])
def api_query_volume(device_id):
    """查询音量状态"""
    result = query_volume(device_id)
    return jsonify(result)

@app.route('/zhsf/api/volume/<device_id>/set', methods=['POST'])
def api_set_volume(device_id):
    """设置音量"""
    data = request.get_json()
    volume = data.get('volume', 0)

    try:
        volume = int(volume)
    except (ValueError, TypeError):
        return jsonify({'status': 'error', 'message': '无效的音量值'})

    result = set_volume(device_id, volume)
    return jsonify(result)

# ===== 综合控制API =====
@app.route('/zhsf/api/control/<device_id>', methods=['POST'])
def api_comprehensive_control(device_id):
    """综合控制接口"""
    data = request.get_json()
    actions = data.get('actions', {})

    results = {}

    # RGB灯控制
    if 'rgb_mode' in actions:
        rgb_result = send_rgb_control(device_id, actions['rgb_mode'])
        results['rgb'] = rgb_result

    # 麦克风控制
    if 'microphone' in actions:
        mic_action = 'g' if actions['microphone'] == 'mute' else 'k'
        mic_result = control_microphone(device_id, mic_action)
        results['microphone'] = mic_result

    # 音量控制
    if 'volume' in actions:
        vol_result = set_volume(device_id, int(actions['volume']))
        results['volume'] = vol_result

    return jsonify(results)

# ===== 批量控制API =====
@app.route('/zhsf/api/batch/control', methods=['POST'])
def api_batch_control():
    """批量控制多个设备"""
    data = request.get_json()
    device_ids = data.get('devices', [])
    actions = data.get('actions', {})

    if not device_ids:
        return jsonify({'status': 'error', 'message': '未选择设备'})

    results = {}
    for device_id in device_ids:
        device_results = {}

        # RGB灯控制
        if 'rgb_mode' in actions:
            rgb_result = send_rgb_control(device_id, actions['rgb_mode'])
            device_results['rgb'] = rgb_result

        # 麦克风控制
        if 'microphone' in actions:
            mic_action = 'g' if actions['microphone'] == 'mute' else 'k'
            mic_result = control_microphone(device_id, mic_action)
            device_results['microphone'] = mic_result

        # 音量控制
        if 'volume' in actions:
            vol_result = set_volume(device_id, int(actions['volume']))
            device_results['volume'] = vol_result

        results[device_id] = device_results

    return jsonify(results)

# ===== 系统状态API =====
@app.route('/zhsf/api/system/status')
def api_system_status():
    """获取系统整体状态"""
    with audio_lock:
        audio_status = {
            'listening': audio_listening,
            'packets_received': audio_timeflag
        }

    with device_lock:
        device_status = dict(devices)

        # 计算在线设备数量和状态
        online_count = sum(1 for device in device_status.values() if device.get('online', False))
        device_status['_summary'] = {
            'total': len(device_status),
            'online': online_count,
            'offline': len(device_status) - online_count
        }

    with rgb_lock:
        rgb_status = dict(current_states)

    with mic_lock:
        mic_status = dict(mic_states)

    with volume_lock:
        vol_status = dict(volume_states)

    return jsonify({
        'audio': audio_status,
        'devices': device_status,
        'rgb_states': rgb_status,
        'microphone_states': mic_status,
        'volume_states': vol_status,
        'timestamp': time.strftime('%Y-%m-%d %H:%M:%S')
    })

# ===== 设备发现API =====
@app.route('/zhsf/api/devices/discover')
def api_device_discovery():
    """手动触发设备发现"""
    try:
        discover_devices()
        return jsonify({'status': 'success', 'message': '设备发现已启动'})
    except Exception as e:
        return jsonify({'status': 'error', 'message': f'设备发现失败: {str(e)}'})

# ===== 测试API =====
@app.route('/zhsf/api/test/simulate_response/<device_id>')
def api_simulate_response(device_id):
    """模拟设备响应（用于测试）"""
    try:
        # 模拟一个设备响应
        simulated_response = f"[re]{device_id}:1"
        print(f"🎭 模拟设备响应: {simulated_response}")

        # 手动触发响应处理
        from unittest.mock import MagicMock
        msg = MagicMock()
        msg.payload = simulated_response.encode('utf-8')
        mqtt_response_callback(None, None, msg)

        return jsonify({
            'status': 'success',
            'message': f'已模拟设备 {device_id} 的响应',
            'simulated_response': simulated_response
        })
    except Exception as e:
        return jsonify({'status': 'error', 'message': f'模拟响应失败: {str(e)}'})

# ===== 模板创建和启动 =====
def create_templates():
    """创建HTML模板"""
    os.makedirs('templates', exist_ok=True)
    
    # 主控制页面模板 - 简化的基础版本
    with open('templates/dashboard.html', 'w') as f:
        f.write('''<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>张de三方 - 综合控制台</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .card { background: white; padding: 20px; margin: 10px 0; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
        .control-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        select, button, input { padding: 8px; border: 1px solid #ddd; border-radius: 4px; }
        button { background: #3498db; color: white; cursor: pointer; }
        button:hover { background: #2980b9; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
        .info { background: #d1ecf1; color: #0c5460; }
        .hidden { display: none; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🎬 张de三方 - 综合控制台</h1>
            <p>影视导播指挥系统</p>
        </div>

        <!-- 系统状态概览 -->
        <div class="card" style="grid-column: 1 / -1; margin-bottom: 20px;">
            <h3>📈 系统状态概览</h3>
            <div style="display: flex; justify-content: space-around; flex-wrap: wrap; gap: 20px;">
                <div style="text-align: center;">
                    <div style="font-size: 2em; color: #3498db;">🎵</div>
                    <div id="systemAudioStatus">音频: 未监听</div>
                </div>
                <div style="text-align: center;">
                    <div style="font-size: 2em; color: #e74c3c;">📦</div>
                    <div id="systemPackets">数据包: 0</div>
                </div>
                <div style="text-align: center;">
                    <div style="font-size: 2em; color: #27ae60;">📱</div>
                    <div id="systemDevices">设备: 3个</div>
                </div>
                <div style="text-align: center;">
                    <div style="font-size: 2em; color: #f39c12;">⏰</div>
                    <div id="systemTime">时间: --:--:--</div>
                </div>
            </div>
            <button onclick="refreshSystemStatus()" style="margin-top: 15px;">🔄 刷新状态</button>
        </div>

        <div class="grid">
            <!-- RGB灯控制 -->
            <div class="card">
                <h3>💡 RGB灯控制</h3>
                <div class="control-group">
                    <label>设备:</label>
                    <select id="rgbDevice">
                        <option value="zhsf_1">zhsf_1</option>
                        <option value="zhsf_2">zhsf_2</option>
                        <option value="zhsf_3">zhsf_3</option>
                    </select>
                </div>
                <div class="control-group">
                    <label>模式:</label>
                    <select id="rgbMode">
                        <option value="0">关闭</option>
                        <option value="1" selected>绿色常亮</option>
                        <option value="2">黄色闪烁</option>
                        <option value="3">黄色常亮</option>
                        <option value="4">红色闪烁</option>
                        <option value="5">红色常亮</option>
                        <option value="6">红蓝交替</option>
                    </select>
                </div>
                <button onclick="sendRGBControl()">发送控制</button>
                <div id="rgbStatus" class="status hidden"></div>
            </div>

            <!-- 麦克风控制 -->
            <div class="card">
                <h3>🎤 麦克风控制</h3>
            <div class="control-group">
                    <label>设备:</label>
                    <select id="micDevice">
                    <option value="zhsf_1">zhsf_1</option>
                    <option value="zhsf_2">zhsf_2</option>
                    <option value="zhsf_3">zhsf_3</option>
                </select>
                </div>
                <div style="display: flex; gap: 10px;">
                    <button onclick="controlMic('mute')">静音</button>
                    <button onclick="controlMic('unmute')">开麦</button>
                    <button onclick="queryMic()">查询状态</button>
                </div>
                <div id="micStatus" class="status hidden"></div>
            </div>

            <!-- 音量控制 -->
            <div class="card">
                <h3>🔊 音量控制</h3>
                <div class="control-group">
                    <label>设备:</label>
                    <select id="volDevice">
                        <option value="zhsf_1">zhsf_1</option>
                        <option value="zhsf_2">zhsf_2</option>
                        <option value="zhsf_3">zhsf_3</option>
                    </select>
                </div>
                <div class="control-group">
                    <label>音量 (0-100):</label>
                    <input type="number" id="volume" min="0" max="100" value="10">
                </div>
                <div style="display: flex; gap: 10px;">
                    <button onclick="setVolume()">设置音量</button>
                    <button onclick="queryVolume()">查询音量</button>
                </div>
                <div id="volStatus" class="status hidden"></div>
            </div>

            <!-- 音频监听 -->
            <div class="card">
                <h3>📻 音频监听</h3>
                <div style="text-align: center;">
                    <div id="audioStatus">状态: 未监听<br>包数: 0</div>
                    <br>
                    <button id="audioBtn" onclick="toggleAudio()">开始监听</button>
                </div>
                <div id="audioControlStatus" class="status hidden"></div>
            </div>

            <!-- 设备状态 -->
            <div class="card">
                <h3>📊 设备状态</h3>
            <div class="control-group">
                    <label>设备:</label>
                    <select id="statusDevice">
                        <option value="zhsf_1">zhsf_1</option>
                        <option value="zhsf_2">zhsf_2</option>
                        <option value="zhsf_3">zhsf_3</option>
                </select>
                </div>
                <button onclick="queryDeviceStatus()">查询设备状态</button>
                <button onclick="queryAllDevices()" style="margin-left: 10px;">查询所有设备</button>
                <div id="deviceStatus" class="status hidden"></div>
            </div>

            <!-- 批量控制 -->
            <div class="card">
                <h3>⚡ 批量控制</h3>
                <div class="control-group">
                    <label>选择设备:</label>
                    <div style="display: flex; flex-wrap: wrap; gap: 10px;">
                        <label><input type="checkbox" id="batch_zhsf_1" checked> zhsf_1</label>
                        <label><input type="checkbox" id="batch_zhsf_2" checked> zhsf_2</label>
                        <label><input type="checkbox" id="batch_zhsf_3" checked> zhsf_3</label>
                    </div>
                </div>

                <div class="control-group">
                    <label>操作类型:</label>
                    <select id="batchAction">
                        <option value="rgb">设置RGB灯模式</option>
                        <option value="microphone">控制麦克风</option>
                        <option value="volume">设置音量</option>
                    </select>
        </div>

                <div id="batchParams">
                    <!-- 参数将通过JavaScript动态生成 -->
    </div>

                <button onclick="executeBatchControl()" style="width: 100%; margin-top: 15px;">执行批量控制</button>
                <div id="batchStatus" class="status hidden"></div>
            </div>
        </div>
    </div>

    <script>
        // RGB控制
        async function sendRGBControl() {
            const device = document.getElementById('rgbDevice').value;
            const mode = document.getElementById('rgbMode').value;
            
            showStatus('rgbStatus', '发送中...', 'info');
            
            try {
                const response = await fetch('/zhsf/api/rgb/control', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ device, mode })
                });
                
                const result = await response.json();
                showStatus('rgbStatus', result.message, result.status === 'success' ? 'success' : 'error');
            } catch (error) {
                showStatus('rgbStatus', '网络错误: ' + error.message, 'error');
            }
        }

        // 麦克风控制
        async function controlMic(action) {
            const device = document.getElementById('micDevice').value;

            showStatus('micStatus', '操作中...', 'info');

            try {
                const response = await fetch(`/zhsf/api/microphone/${device}/control`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ action })
                });

                const result = await response.json();
                showStatus('micStatus', result.message, result.status === 'success' ? 'success' : 'error');
            } catch (error) {
                showStatus('micStatus', '网络错误: ' + error.message, 'error');
            }
        }

        async function queryMic() {
            const device = document.getElementById('micDevice').value;

            showStatus('micStatus', '查询中...', 'info');

            try {
                const response = await fetch(`/zhsf/api/microphone/${device}/query`);
                const result = await response.json();
                
                if (result.status === 'success') {
                    const status = result.response === 'g' ? '静音' : result.response === 'k' ? '开麦' : '未知';
                    showStatus('micStatus', `状态: ${status}`, 'success');
                } else {
                    showStatus('micStatus', result.message, 'error');
                }
            } catch (error) {
                showStatus('micStatus', '网络错误: ' + error.message, 'error');
            }
        }

        // 音量控制
        async function setVolume() {
            const device = document.getElementById('volDevice').value;
            const volume = document.getElementById('volume').value;

            showStatus('volStatus', '设置中...', 'info');

            try {
                const response = await fetch(`/zhsf/api/volume/${device}/set`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ volume: parseInt(volume) })
                });

                const result = await response.json();
                showStatus('volStatus', result.message, result.status === 'success' ? 'success' : 'error');
            } catch (error) {
                showStatus('volStatus', '网络错误: ' + error.message, 'error');
            }
        }

        async function queryVolume() {
            const device = document.getElementById('volDevice').value;

            showStatus('volStatus', '查询中...', 'info');

            try {
                const response = await fetch(`/zhsf/api/volume/${device}/query`);
                const result = await response.json();

                if (result.status === 'success') {
                    const vol = result.response.replace('v', '');
                    document.getElementById('volume').value = vol;
                    showStatus('volStatus', `音量: ${vol}%`, 'success');
                } else {
                    showStatus('volStatus', result.message, 'error');
                }
            } catch (error) {
                showStatus('volStatus', '网络错误: ' + error.message, 'error');
            }
        }

        // 音频监听
        let audioListening = false;
        let statusInterval = null;

        async function toggleAudio() {
            const btn = document.getElementById('audioBtn');
            const url = audioListening ? '/zhsf/api/audio/stop' : '/zhsf/api/audio/start';

            showStatus('audioControlStatus', '操作中...', 'info');

            try {
                const response = await fetch(url, { method: 'POST' });
                const data = await response.json();

                if (data.message.includes('开始') || data.message.includes('启动')) {
                    audioListening = true;
                    btn.textContent = '停止监听';
                    btn.style.background = '#e74c3c';
                    if (!statusInterval) {
                        statusInterval = setInterval(updateAudioStatus, 1000);
                    }
                } else {
                    audioListening = false;
                    btn.textContent = '开始监听';
                    btn.style.background = '#3498db';
                    if (statusInterval) {
                        clearInterval(statusInterval);
                        statusInterval = null;
                    }
                }

                showStatus('audioControlStatus', data.message, 'success');
            } catch (error) {
                showStatus('audioControlStatus', '操作失败: ' + error.message, 'error');
            }
        }

        async function updateAudioStatus() {
            try {
                const response = await fetch('/zhsf/api/audio/status');
                const data = await response.json();

                audioListening = data.listening;
                const statusDiv = document.getElementById('audioStatus');
                statusDiv.innerHTML = `状态: ${data.listening ? '监听中' : '未监听'}<br>包数: ${data.count}`;

                const btn = document.getElementById('audioBtn');
                if (data.listening) {
                    btn.textContent = '停止监听';
                    btn.style.background = '#e74c3c';
                } else {
                    btn.textContent = '开始监听';
                    btn.style.background = '#3498db';
                }
            } catch (error) {
                console.error('更新音频状态失败:', error);
            }
        }

        // 设备状态查询
        async function queryDeviceStatus() {
            const device = document.getElementById('statusDevice').value;

            showStatus('deviceStatus', '查询中...', 'info');

            try {
                const response = await fetch(`/zhsf/api/devices/${device}/status`);
                const data = await response.json();

                const rgbMode = data.status.response || '未知';
                const micStatus = data.microphone.response === 'g' ? '静音' : data.microphone.response === 'k' ? '开麦' : '未知';
                const volume = data.volume.response ? data.volume.response.replace('v', '') + '%' : '未知';

                showStatus('deviceStatus',
                    `RGB: ${rgbMode}<br>麦克风: ${micStatus}<br>音量: ${volume}`,
                    'success');
            } catch (error) {
                showStatus('deviceStatus', '查询失败: ' + error.message, 'error');
            }
        }

        // 查询所有设备状态
        async function queryAllDevices() {
            showStatus('deviceStatus', '查询所有设备中...', 'info');

            try {
                const response = await fetch('/zhsf/api/devices/status');
                const data = await response.json();

                let result = '';
                for (const [deviceId, deviceData] of Object.entries(data)) {
                    const rgbMode = deviceData.status?.response || '未知';
                    const micStatus = deviceData.microphone?.response === 'g' ? '静音' : deviceData.microphone?.response === 'k' ? '开麦' : '未知';
                    const volume = deviceData.volume?.response ? deviceData.volume.response.replace('v', '') + '%' : '未知';

                    result += `${deviceId}: RGB=${rgbMode}, 麦克风=${micStatus}, 音量=${volume}<br>`;
                }

                showStatus('deviceStatus', result, 'success');
            } catch (error) {
                showStatus('deviceStatus', '查询失败: ' + error.message, 'error');
            }
        }

        // 批量控制
        function updateBatchParams() {
            const action = document.getElementById('batchAction').value;
            const paramsDiv = document.getElementById('batchParams');

            if (action === 'rgb') {
                paramsDiv.innerHTML = `
                    <label>RGB模式:</label>
                    <select id="batchRgbMode">
                        <option value="0">关闭</option>
                        <option value="1" selected>绿色常亮</option>
                        <option value="2">黄色闪烁</option>
                        <option value="3">黄色常亮</option>
                        <option value="4">红色闪烁</option>
                        <option value="5">红色常亮</option>
                        <option value="6">红蓝交替</option>
                    </select>
                `;
            } else if (action === 'microphone') {
                paramsDiv.innerHTML = `
                    <label>麦克风操作:</label>
                    <select id="batchMicAction">
                        <option value="mute">静音</option>
                        <option value="unmute">开麦</option>
                    </select>
                `;
            } else if (action === 'volume') {
                paramsDiv.innerHTML = `
                    <label>音量设置:</label>
                    <input type="number" id="batchVolume" min="0" max="100" value="10">%
                `;
            }
        }

        async function executeBatchControl() {
            const selectedDevices = [];
            ['zhsf_1', 'zhsf_2', 'zhsf_3'].forEach(id => {
                if (document.getElementById(`batch_${id}`).checked) {
                    selectedDevices.push(id);
                }
            });

            if (selectedDevices.length === 0) {
                showStatus('batchStatus', '请选择至少一个设备', 'error');
                setTimeout(() => hideStatus('batchStatus'), 3000);
                return;
            }

            const action = document.getElementById('batchAction').value;
            let params = {};

            if (action === 'rgb') {
                params.rgb_mode = document.getElementById('batchRgbMode').value;
            } else if (action === 'microphone') {
                params.microphone = document.getElementById('batchMicAction').value;
            } else if (action === 'volume') {
                params.volume = document.getElementById('batchVolume').value;
            }

            showStatus('batchStatus', '批量执行中...', 'info');

            const results = [];
            for (const deviceId of selectedDevices) {
                try {
                    const response = await fetch(`/zhsf/api/control/${deviceId}`, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ actions: params })
                    });

                    const result = await response.json();
                    const actionName = action === 'rgb' ? 'RGB' : action === 'microphone' ? '麦克风' : '音量';
                    const status = result[action]?.status === 'success' ? '成功' : '失败';
                    results.push(`${deviceId}(${actionName}): ${status}`);
                } catch (error) {
                    results.push(`${deviceId}: 失败 - ${error.message}`);
                }
            }

            showStatus('batchStatus', '批量操作完成:\\n' + results.join('\\n'), 'success');
            setTimeout(() => hideStatus('batchStatus'), 5000);
        }

        // 工具函数
        function showStatus(elementId, message, type) {
            const element = document.getElementById(elementId);
            element.innerHTML = message;
            element.className = `status ${type}`;
            element.classList.remove('hidden');
            setTimeout(() => element.classList.add('hidden'), 3000);
        }

        // 系统状态更新
        let systemStatusInterval = null;

        async function refreshSystemStatus() {
            try {
                const response = await fetch('/zhsf/api/system/status');
                const data = await response.json();

                // 更新音频状态
                document.getElementById('systemAudioStatus').textContent =
                    `音频: ${data.audio.listening ? '监听中' : '未监听'}`;

                // 更新数据包数量
                document.getElementById('systemPackets').textContent =
                    `数据包: ${data.audio.packets_received}`;

                // 更新设备数量和在线状态
                const onlineDevices = Object.values(data.devices).filter(d => d.online).length;
                document.getElementById('systemDevices').textContent =
                    `设备: ${onlineDevices}/${Object.keys(data.devices).length} 在线`;

                // 更新时间戳
                document.getElementById('systemTime').textContent =
                    `更新: ${data.timestamp.split(' ')[1]}`;

                console.log('系统状态已更新:', data);
            } catch (error) {
                console.error('系统状态更新失败:', error);
            }
        }

        // 开始自动状态更新
        function startSystemStatusUpdates() {
            refreshSystemStatus(); // 立即更新一次
            if (!systemStatusInterval) {
                systemStatusInterval = setInterval(refreshSystemStatus, 5000); // 每5秒更新一次
            }
        }

        // 停止自动状态更新
        function stopSystemStatusUpdates() {
            if (systemStatusInterval) {
                clearInterval(systemStatusInterval);
                systemStatusInterval = null;
            }
        }

        // 初始化
        document.addEventListener('DOMContentLoaded', function() {
            updateAudioStatus();
            updateBatchParams();
            startSystemStatusUpdates();

            // 批量控制操作类型变更事件
            document.getElementById('batchAction').addEventListener('change', updateBatchParams);
        });

        // 页面卸载时清理定时器
        window.addEventListener('beforeunload', function() {
            stopSystemStatusUpdates();
            if (statusInterval) {
                clearInterval(statusInterval);
            }
        });
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
            <a href="/zhsf/tally">RGB灯控制</a>
            <a href="/zhsf/listen">音频监听</a>
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
                const response = await fetch('/zhsf/api/audio/status');
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
                const url = isListening ? '/zhsf/api/audio/stop' : '/zhsf/api/audio/start';
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

def cleanup():
    """程序退出时的清理函数"""
    print("\n🧹 正在清理资源...")
    stop_status_monitoring()
    print("✅ 清理完成")

if __name__ == '__main__':
    # 注册退出时的清理函数
    atexit.register(cleanup)

    create_templates()

    # 启动设备发现线程
    start_device_discovery()

    print("🚀 启动张de三方 - 综合控制服务")
    print("=" * 50)
    print("🎬 主控制台: http://localhost:5120/")
    print("⚡ 统一控制台: http://localhost:5120/zhsf")
    print("=" * 50)
    print("📋 支持功能:")
    print("  • RGB灯控制 (7种模式)")
    print("  • 麦克风远程控制 (开麦/静音)")
    print("  • 音量控制 (0-100%)")
    print("  • 设备状态查询")
    print("  • 批量控制")
    print("  • 实时状态监控")
    print("  • 自动设备发现")
    print("  • 独立设备状态监控")
    print("=" * 50)
    print("🔍 设备发现: 每30秒自动检测设备在线状态")
    print("📡 设备监控: 持续监听设备响应消息")
    print("=" * 50)
    try:
        app.run(host='0.0.0.0', port=5120, debug=True)
    except KeyboardInterrupt:
        print("\n👋 收到中断信号，正在退出...")
    finally:
        cleanup()
