# secret_manager 设计

## 1. 目标

`secret_manager` 负责读取 Wi-Fi、Qwen API Key、模型名和少量运行参数。真实 API Key 和 Wi-Fi 密码不能写死进源码，不能提交 Git。

## 2. 配置来源优先级

第一版使用清晰、易调试的顺序：

1. NVS 中的配置。
2. 本地 `config.local.ini`。
3. menuconfig 配置项。
4. `config.example.ini` 仅作为示例，不作为真实运行配置。

如果没有配置 API Key：

- Qwen 客户端不启动。
- Web 调试页面显示 `Qwen: not configured`。
- 日志只打印“未配置 API Key”，不打印任何密钥内容。

## 3. 文件规则

允许提交：

- `esp32p4_car_controller/config/config.example.ini`
- `esp32p4_car_controller/config/nvs_secrets.example.csv`
- `components/secret_manager/` 源码
- menuconfig 选项定义

禁止提交：

- `config.local.ini`
- `nvs_secrets.local.csv`
- `nvs_secrets.local.bin`
- `nvs_secrets.csv`
- 任何包含真实 `api_key` 的文件
- 任何包含真实 Wi-Fi 密码的文件

`.gitignore` 已加入：

```text
esp32p4_car_controller/config/config.local.ini
esp32p4_car_controller/config/*.secret.ini
esp32p4_car_controller/config/nvs_secrets.csv
esp32p4_car_controller/config/nvs_secrets.local.csv
esp32p4_car_controller/config/nvs_secrets.local.bin
```

## 4. 第一闭环接口

```c
typedef struct {
    char wifi_ssid[64];
    char wifi_password[64];
    char qwen_api_key[160];
    char qwen_model[64];
    char qwen_endpoint[160];
    uint8_t has_wifi;
    uint8_t has_qwen_key;
} secret_config_t;

esp_err_t SecretManager_Init(void);
esp_err_t SecretManager_Load(secret_config_t *out);
```

## 5. 安全输出规则

日志允许：

```text
Qwen API Key: configured
Qwen API Key: missing
Wi-Fi SSID: configured
```

日志禁止：

```text
sk-...
完整 Wi-Fi 密码
Authorization: Bearer <完整密钥>
```

## 6. 测试方法

无配置：

- 编译烧录 ESP32。
- 观察日志：Qwen 未配置。
- Web 页面显示 Qwen not configured。

本地配置：

- 推荐运行 `tools/provision_secrets.ps1` 写入 NVS。
- 或复制 `nvs_secrets.example.csv` 为 `nvs_secrets.local.csv`。
- 填入真实值。
- 确认 `git status` 不显示本地密钥文件。
- 烧录后确认 secret_manager 能读取配置，但日志不打印完整密钥。
