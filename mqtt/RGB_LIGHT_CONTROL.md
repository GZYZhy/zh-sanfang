# RGB灯MQTT控制说明

## 设备ID
当前设备ID: `zhsf_1`

## MQTT控制主题
- 主题: `zhsf/tally`
- 格式: `设备ID:模式编号`

## 控制指令格式
向主题 `zhsf/tally` 发布消息，格式为：
```
设备ID:模式编号
```

例如：
- `zhsf_1:0` - 关闭灯
- `zhsf_1:1` - 绿色（开机默认）
- `zhsf_1:2` - 黄色闪烁
- `zhsf_1:3` - 黄色常亮
- `zhsf_1:4` - 红色闪烁
- `zhsf_1:5` - 红色常亮
- `zhsf_1:6` - 红蓝交替闪烁

## 模式编号对应表
| 模式编号 | 模式描述 | 效果 |
|---------|---------|------|
| 0 | MODE_OFF | 关闭所有灯 |
| 1 | MODE_GREEN | 绿色常亮（开机默认） |
| 2 | MODE_YELLOW_FLASH | 黄色闪烁（500ms间隔） |
| 3 | MODE_YELLOW | 黄色常亮 |
| 4 | MODE_RED_FLASH | 红色闪烁（500ms间隔） |
| 5 | MODE_RED | 红色常亮 |
| 6 | MODE_RED_BLUE_ALTERNATE | 红蓝交替闪烁（500ms间隔） |

## 硬件连接
RGB灯引脚定义（可根据实际硬件修改）：
- 红色引脚: GPIO 12
- 绿色引脚: GPIO 14
- 蓝色引脚: GPIO 27

注意：灯是高电平点亮，低电平熄灭。

## 使用示例
使用MQTT客户端发送控制指令：
```bash
mosquitto_pub -h 24.233.0.55 -t "zhsf/tally" -m "zhsf_1:2"
```

## 单点控制机制
系统会检查消息中的设备ID，只有匹配当前设备ID的指令才会被执行，确保单点控制安全。