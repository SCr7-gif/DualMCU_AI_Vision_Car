#include "console_control.h"

#include "audio_codec.h"
#include "behavior_manager.h"
#include "board_diagnostics.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "qwen_realtime_client.h"
#include "robot_tools.h"
#include "sensor_cache.h"
#include "stm32_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "console";
static TaskHandle_t s_console_task;

static void trim_line(char *line)
{
    size_t len = strlen(line);
    while ((len > 0) && ((line[len - 1] == '\r') || (line[len - 1] == '\n') || (line[len - 1] == ' '))) {
        line[len - 1] = '\0';
        len--;
    }
}

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  help\n");
    printf("  status\n");
    printf("  link\n");
    printf("  diag\n");
    printf("  net\n");
    printf("  stop\n");
    printf("  move <forward|backward|left|right|rotate_left|rotate_right> <speed 0-60> <ms>\n");
    printf("  gimbal <pan 0-180> <tilt 30-150> [ms]\n");
    printf("  gimbal_action <center|look_left_soft|look_right_soft|nod_yes|shake_no|scan_slow|curious_tilt|look_up_soft|look_down_soft|scan_fast|sleepy|wake_up|shy_look_down|happy_wiggle|thinking_motion>\n");
    printf("  led <idle|listening|speaking|thinking|error|cute|patrol|solid>\n");
    printf("  behavior <dance|patrol|stop_patrol>\n");
    printf("  audio <tone|tone_long [ms]|mic|status>\n");
    printf("  capture\n");
    printf("  qwenctl <status|start|voice_start|voice_stop|audio_test|stop|restart>\n");
    printf("  qwen <text>\n\n");
}

static void print_net_status(void)
{
    printf("network backend=%s started=%d connected=%d ip=%s native_wifi=%d qwen_state=%u/%s qwen_ready=%d\n",
           network_manager_backend_name(),
           network_manager_is_started(),
           network_manager_is_connected(),
           network_manager_ip_string(),
           network_manager_has_native_wifi(),
           (unsigned)qwen_realtime_client_get_state(),
           qwen_realtime_client_state_name(),
           qwen_realtime_client_is_connected());
}

static void print_qwen_status(void)
{
    printf("qwen state=%u/%s ready=%d voice=%d network_connected=%d\n",
           (unsigned)qwen_realtime_client_get_state(),
           qwen_realtime_client_state_name(),
           qwen_realtime_client_is_connected(),
           qwen_realtime_client_is_voice_enabled(),
           network_manager_is_connected());
}

static void print_status(void)
{
    sensor_cache_snapshot_t st = robot_tools_get_status();
    behavior_manager_status_t bs;
    behavior_manager_get_status(&bs);
    printf("stm32=%d flags=%u dist_x10=%u temp=%u hum=%u dht=%d motor=%u/%u/%u gimbal=%u/%u behavior=%u/%d/%u high=%s/%u loops=%lu decision=%s err=%s gps=%s\n",
           st.basic.stm32_online,
           st.basic.flags,
           st.sensor.distance_cm_x10,
           st.sensor.temperature,
           st.sensor.humidity,
           st.sensor.dht_ok,
           st.motor.state,
           st.motor.target_speed,
           st.motor.current_speed,
           st.gimbal.pan,
           st.gimbal.tilt,
           st.behavior.behavior_id,
           st.behavior.running,
           st.behavior.step,
           bs.mode_name,
           bs.step,
           (unsigned long)bs.loops,
           bs.last_decision,
           bs.last_error,
           st.gps_nmea);
}

static void print_link_status(void)
{
    stm32_link_diag_t d;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    stm32_link_get_diagnostics(&d);
    printf("link online=%d tx=%lu tx_fail=%lu rx_bytes=%lu rx_frames=%lu checksum_err=%lu bad_len=%lu unknown=%lu ack=%lu last_tx=0x%02X last_rx=0x%02X rx_age_ms=%lu\n",
           d.online,
           (unsigned long)d.tx_frames,
           (unsigned long)d.tx_failures,
           (unsigned long)d.rx_bytes,
           (unsigned long)d.rx_frames,
           (unsigned long)d.checksum_errors,
           (unsigned long)d.bad_length_errors,
           (unsigned long)d.unknown_frames,
           (unsigned long)d.heartbeat_acks,
           d.last_tx_cmd,
           d.last_rx_cmd,
           (unsigned long)((d.last_valid_frame_ms == 0u) ? 0u : (now - d.last_valid_frame_ms)));
    printf("frames basic=%lu sensor=%lu gps=%lu gimbal=%lu motor=%lu behavior=%lu\n",
           (unsigned long)d.status_basic_frames,
           (unsigned long)d.status_sensor_frames,
           (unsigned long)d.status_gps_frames,
           (unsigned long)d.status_gimbal_frames,
           (unsigned long)d.status_motor_frames,
           (unsigned long)d.status_behavior_frames);
}

static void print_result(const char *cmd, esp_err_t ret)
{
    printf("%s -> %s (%d)\n", cmd, esp_err_to_name(ret), ret);
}

static void print_audio_status(void)
{
    int16_t peak = 0;
    size_t samples_read = 0;
    esp_err_t ret = audio_codec_measure_mic_peak(&peak, &samples_read, 300);
    printf("audio mic_peak=%d samples=%u ret=%s (%d)\n",
           peak,
           (unsigned)samples_read,
           esp_err_to_name(ret),
           ret);
}

static void handle_command(char *line)
{
    char cmd[24] = {0};
    char arg1[64] = {0};
    int a = 0;
    int b = 0;
    int c = 0;

    trim_line(line);
    if (line[0] == '\0') {
        return;
    }

    if (sscanf(line, "%23s", cmd) != 1) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_help();
    } else if (strcmp(cmd, "status") == 0) {
        print_status();
    } else if (strcmp(cmd, "link") == 0) {
        print_link_status();
    } else if (strcmp(cmd, "diag") == 0) {
        board_diagnostics_print_console();
    } else if (strcmp(cmd, "net") == 0) {
        print_net_status();
    } else if (strcmp(cmd, "stop") == 0) {
        print_result(cmd, robot_tools_stop());
    } else if (strcmp(cmd, "move") == 0) {
        if (sscanf(line, "%23s %63s %d %d", cmd, arg1, &a, &b) == 4) {
            print_result(cmd, robot_tools_move(arg1, (uint8_t)a, (uint16_t)b));
        } else {
            printf("usage: move forward 25 600\n");
        }
    } else if (strcmp(cmd, "gimbal") == 0) {
        int matched = sscanf(line, "%23s %d %d %d", cmd, &a, &b, &c);
        if (matched >= 3) {
            print_result(cmd, robot_tools_gimbal_set((uint8_t)a, (uint8_t)b, (uint16_t)((matched == 4) ? c : 700)));
        } else {
            printf("usage: gimbal 90 90 700\n");
        }
    } else if (strcmp(cmd, "gimbal_action") == 0) {
        if (sscanf(line, "%23s %63s", cmd, arg1) == 2) {
            print_result(cmd, robot_tools_gimbal_action(arg1));
        } else {
            printf("usage: gimbal_action scan_slow\n");
        }
    } else if (strcmp(cmd, "led") == 0) {
        if (sscanf(line, "%23s %63s", cmd, arg1) == 2) {
            print_result(cmd, robot_tools_led_effect(arg1, 0, 0, 0, 0));
        } else {
            printf("usage: led cute\n");
        }
    } else if (strcmp(cmd, "behavior") == 0) {
        if (sscanf(line, "%23s %63s", cmd, arg1) == 2) {
            print_result(cmd, robot_tools_behavior(arg1));
        } else {
            printf("usage: behavior patrol\n");
        }
    } else if (strcmp(cmd, "audio") == 0) {
        if (sscanf(line, "%23s %63s", cmd, arg1) != 2) {
            print_audio_status();
        } else if (strcmp(arg1, "tone") == 0) {
            audio_codec_play_test_tone();
            printf("audio tone -> done\n");
        } else if (strcmp(arg1, "tone_long") == 0) {
            int matched = sscanf(line, "%23s %63s %d", cmd, arg1, &a);
            uint32_t duration_ms = (matched == 3 && a > 0) ? (uint32_t)a : 30000u;
            audio_codec_play_1khz_test_tone(duration_ms);
            printf("audio tone_long %u ms -> done\n", (unsigned)duration_ms);
        } else if ((strcmp(arg1, "mic") == 0) || (strcmp(arg1, "status") == 0)) {
            print_audio_status();
        } else {
            printf("usage: audio tone|tone_long [ms]|mic|status\n");
        }
    } else if (strcmp(cmd, "capture") == 0) {
        print_result(cmd, robot_tools_capture_image());
    } else if (strcmp(cmd, "qwenctl") == 0) {
        if (sscanf(line, "%23s %63s", cmd, arg1) != 2) {
            print_qwen_status();
        } else if (strcmp(arg1, "status") == 0) {
            print_qwen_status();
        } else if (strcmp(arg1, "start") == 0) {
            print_result("qwen_start", qwen_realtime_client_start());
        } else if (strcmp(arg1, "voice_start") == 0) {
            print_result("qwen_voice_start", qwen_realtime_client_set_voice_enabled(true));
        } else if (strcmp(arg1, "voice_stop") == 0) {
            print_result("qwen_voice_stop", qwen_realtime_client_set_voice_enabled(false));
        } else if (strcmp(arg1, "audio_test") == 0) {
            print_result("qwen_audio_test", qwen_realtime_client_play_local_audio_test(3000));
        } else if (strcmp(arg1, "stop") == 0) {
            print_result("qwen_stop", qwen_realtime_client_stop());
        } else if (strcmp(arg1, "restart") == 0) {
            esp_err_t ret = qwen_realtime_client_stop();
            if (ret == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(300));
                ret = qwen_realtime_client_start();
            }
            print_result("qwen_restart", ret);
        } else {
            printf("usage: qwenctl status|start|voice_start|voice_stop|audio_test|stop|restart\n");
        }
    } else if (strcmp(cmd, "qwen") == 0) {
        char *text = line + strlen(cmd);
        while (*text == ' ') {
            text++;
        }
        print_result(cmd, qwen_realtime_client_send_text(text));
    } else {
        printf("unknown command: %s\n", cmd);
        print_help();
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[192];

    print_help();
    while (1) {
        printf("car> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        handle_command(line);
    }
}

esp_err_t console_control_init(void)
{
    if (s_console_task != NULL) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(console_task, "console_ctrl", 4096, NULL, 4, &s_console_task);
    if (ok == pdPASS) {
        ESP_LOGI(TAG, "serial monitor console ready");
    }
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
