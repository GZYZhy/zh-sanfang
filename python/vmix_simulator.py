#!/usr/bin/env python3
"""
vMix TALLY 模拟器
用于测试开发板的vMix连接功能
"""

import socket
import threading
import time

class VmixSimulator:
    def __init__(self, host='0.0.0.0', port=8099):
        self.host = host
        self.port = port
        self.server_socket = None
        self.client_socket = None
        self.running = False
        
    def start_server(self):
        """启动TCP服务器"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(1)
            
            print(f"vMix模拟器启动在 {self.host}:{self.port}")
            print("等待设备连接...")
            
            self.running = True
            while self.running:
                self.client_socket, client_address = self.server_socket.accept()
                print(f"设备已连接: {client_address}")
                
                # 处理客户端连接
                self.handle_client()
                
        except Exception as e:
            print(f"服务器错误: {e}")
        finally:
            self.stop_server()
    
    def handle_client(self):
        """处理客户端连接"""
        try:
            # 发送欢迎消息
            self.client_socket.sendall(b"vMix TALLY Simulator Ready\n")
            
            while self.running:
                # 显示菜单
                print("\n=== vMix TALLY 控制 ===")
                print("1. 发送 TALLY OK 0000 (所有输入未激活)")
                print("2. 发送 TALLY OK 1000 (输入1为Program)")
                print("3. 发送 TALLY OK 2000 (输入2为Preview)")
                print("4. 发送 TALLY OK 1200 (输入1 Program, 输入2 Preview)")
                print("5. 自定义TALLY状态")
                print("6. 断开连接")
                print("0. 退出")
                
                choice = input("请选择操作: ").strip()
                
                if choice == '1':
                    self.send_tally("0000")
                elif choice == '2':
                    self.send_tally("1000")
                elif choice == '3':
                    self.send_tally("2000")
                elif choice == '4':
                    self.send_tally("1200")
                elif choice == '5':
                    tally_state = input("请输入TALLY状态(4位数字, 0=未激活, 1=Program, 2=Preview): ").strip()
                    if len(tally_state) == 4 and all(c in '012' for c in tally_state):
                        self.send_tally(tally_state)
                    else:
                        print("无效的TALLY状态格式")
                elif choice == '6':
                    print("断开连接...")
                    break
                elif choice == '0':
                    self.running = False
                    break
                else:
                    print("无效选择")
                    
        except Exception as e:
            print(f"客户端处理错误: {e}")
        finally:
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None
    
    def send_tally(self, tally_state):
        """发送TALLY指令"""
        try:
            message = f"TALLY OK {tally_state}\n"
            self.client_socket.sendall(message.encode('utf-8'))
            print(f"已发送: {message.strip()}")
        except Exception as e:
            print(f"发送错误: {e}")
    
    def stop_server(self):
        """停止服务器"""
        self.running = False
        if self.client_socket:
            self.client_socket.close()
        if self.server_socket:
            self.server_socket.close()
        print("服务器已停止")

def main():
    # 获取本机IP地址
    host = socket.gethostbyname(socket.gethostname())
    print(f"本机IP地址: {host}")
    print("请在设备上设置vMix IP为上述地址")
    
    simulator = VmixSimulator(host=host)
    
    try:
        # 启动服务器线程
        server_thread = threading.Thread(target=simulator.start_server)
        server_thread.daemon = True
        server_thread.start()
        
        # 主线程等待
        while simulator.running:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n收到中断信号，正在停止...")
    finally:
        simulator.stop_server()

if __name__ == "__main__":
    main()