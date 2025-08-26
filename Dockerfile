FROM docker.1ms.run/python:3.9-slim

# 安装系统依赖（pyaudio 需要）
RUN apt-get update && apt-get install -y \
    portaudio19-dev \
    gcc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 复制应用文件
COPY combined_control_app.py .

# 安装 Python 依赖
RUN pip install --no-cache-dir flask paho-mqtt pyaudio

# 暴露端口
EXPOSE 5000

# 启动应用
CMD ["python", "combined_control_app.py"]
