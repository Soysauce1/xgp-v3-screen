// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 zzzz0317

#include "ui_Traffic.h"
#include "../ui_helpers.h"
#include "../../screen_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 流量统计状态文件（持久化存储）
#define TRAFFIC_STATE_FILE "/etc/xgp_screen_traffic.dat"

lv_obj_t * ui_Traffic;
lv_obj_t * ui_txtTraffic1;
static lv_obj_t * ui_headerTraffic;
static lv_obj_t * ui_txtTraffic;
static lv_obj_t * ui_txtTodayRx;
static lv_obj_t * ui_valTodayRx;
static lv_obj_t * ui_txtTodayTx;
static lv_obj_t * ui_valTodayTx;
static lv_obj_t * ui_txtTotalRx;
static lv_obj_t * ui_valTotalRx;
static lv_obj_t * ui_txtTotalTx;
static lv_obj_t * ui_valTotalTx;

// 流量统计结构
typedef struct {
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
    unsigned long long today_rx_bytes;
    unsigned long long today_tx_bytes;
    char date[16];  // 存储日期，用于判断是否跨天
} traffic_stats_t;

// 格式化流量显示
static void format_traffic(unsigned long long bytes, char *buffer, size_t size)
{
    if (bytes < 1024) {
        snprintf(buffer, size, "%llu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, size, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

// 获取当前日期字符串
static void get_current_date(char *date_str, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(date_str, size, "%Y-%m-%d", tm_info);
}

// 读取WAN口流量
static void read_wan_traffic(unsigned long long *rx_bytes, unsigned long long *tx_bytes)
{
    FILE *fp;
    char line[256];
    char iface[32];
    unsigned long long rx, tx;
    
    *rx_bytes = 0;
    *tx_bytes = 0;
    
    // 读取 /proc/net/dev
    fp = fopen("/proc/net/dev", "r");
    if (!fp) return;
    
    // 跳过前两行标题
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    // 查找wan口或eth1
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%31[^:]:%llu %*u %*u %*u %*u %*u %*u %*u %llu",
                   iface, &rx, &tx) == 3) {
            // 去除接口名前的空格
            char *iface_name = iface;
            while (*iface_name == ' ') iface_name++;
            
            // 跳过loopback和lan接口
            if (strncmp(iface_name, "lo", 2) == 0 ||
                strncmp(iface_name, "br-lan", 6) == 0 ||
                strncmp(iface_name, "eth0", 4) == 0 ||
                strncmp(iface_name, "phy", 3) == 0) {
                continue;
            }
            
            // 检查是否在配置的接口列表中
            for (int i = 0; i < g_screen_config.traffic.interface_count; i++) {
                const char *configured_iface = g_screen_config.traffic.interfaces[i];
                size_t len = strlen(configured_iface);
                // 支持前缀匹配，例如配置"wwan"可以匹配"wwan0"、"wwan0_1"
                if (strncmp(iface_name, configured_iface, len) == 0) {
                    *rx_bytes += rx;
                    *tx_bytes += tx;
                    break;
                }
            }
        }
    }
    
    fclose(fp);
}

// 读取流量统计状态
static void load_traffic_stats(traffic_stats_t *stats)
{
    // 检查是否需要清零
    if (g_screen_config.traffic.reset_flag) {
        printf("Traffic: Reset flag detected, clearing all statistics\n");
        // 删除状态文件
        unlink(TRAFFIC_STATE_FILE);
        // 清除UCI中的清零标志
        system("uci set xgp_screen.settings.reset='0' && uci commit xgp_screen");
        // 重新加载配置使标志位生效
        g_screen_config.traffic.reset_flag = false;
        
        // 初始化为0
        stats->rx_bytes = 0;
        stats->tx_bytes = 0;
        stats->today_rx_bytes = 0;
        stats->today_tx_bytes = 0;
        get_current_date(stats->date, sizeof(stats->date));
        return;
    }
    
    FILE *fp = fopen(TRAFFIC_STATE_FILE, "r");
    if (fp) {
        fscanf(fp, "%llu %llu %llu %llu %15s",
               &stats->rx_bytes, &stats->tx_bytes,
               &stats->today_rx_bytes, &stats->today_tx_bytes,
               stats->date);
        fclose(fp);
    } else {
        // 首次运行，初始化
        stats->rx_bytes = 0;
        stats->tx_bytes = 0;
        stats->today_rx_bytes = 0;
        stats->today_tx_bytes = 0;
        get_current_date(stats->date, sizeof(stats->date));
    }
}

// 保存流量统计状态
static void save_traffic_stats(const traffic_stats_t *stats)
{
    FILE *fp = fopen(TRAFFIC_STATE_FILE, "w");
    if (fp) {
        fprintf(fp, "%llu %llu %llu %llu %s\n",
                stats->rx_bytes, stats->tx_bytes,
                stats->today_rx_bytes, stats->today_tx_bytes,
                stats->date);
        fclose(fp);
    }
}

// 更新流量显示
static void update_traffic_display(void)
{
    static traffic_stats_t last_stats = {0};
    static unsigned long long last_wan_rx = 0;
    static unsigned long long last_wan_tx = 0;
    static int first_run = 1;
    
    traffic_stats_t stats;
    unsigned long long current_rx, current_tx;
    char buffer[64];
    char current_date[16];
    
    // 读取当前WAN流量
    read_wan_traffic(&current_rx, &current_tx);
    
    // 加载历史统计
    load_traffic_stats(&stats);
    
    // 获取当前日期
    get_current_date(current_date, sizeof(current_date));
    
    // 检查是否跨天
    if (strcmp(stats.date, current_date) != 0) {
        // 新的一天，重置当日流量
        stats.today_rx_bytes = 0;
        stats.today_tx_bytes = 0;
        strncpy(stats.date, current_date, sizeof(stats.date));
        first_run = 1;  // 重置标志
    }
    
    if (first_run) {
        // 首次运行，记录初始值
        last_wan_rx = current_rx;
        last_wan_tx = current_tx;
        first_run = 0;
    } else {
        // 计算增量（处理计数器重置的情况）
        unsigned long long rx_delta = 0;
        unsigned long long tx_delta = 0;
        
        if (current_rx >= last_wan_rx) {
            rx_delta = current_rx - last_wan_rx;
        }
        if (current_tx >= last_wan_tx) {
            tx_delta = current_tx - last_wan_tx;
        }
        
        // 更新统计
        stats.rx_bytes += rx_delta;
        stats.tx_bytes += tx_delta;
        stats.today_rx_bytes += rx_delta;
        stats.today_tx_bytes += tx_delta;
        
        last_wan_rx = current_rx;
        last_wan_tx = current_tx;
    }
    
    // 保存状态
    save_traffic_stats(&stats);
    
    // 更新显示
    format_traffic(stats.today_rx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_valTodayRx, buffer);
    
    format_traffic(stats.today_tx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_valTodayTx, buffer);
    
    format_traffic(stats.rx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_valTotalRx, buffer);
    
    format_traffic(stats.tx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_valTotalTx, buffer);
}

// 定时器回调函数
static void traffic_timer_cb(lv_timer_t *timer)
{
    update_traffic_display();
}

void ui_Traffic_screen_init(void)
{
    ui_Traffic = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_Traffic, LV_OBJ_FLAG_SCROLLABLE);

    // 头部容器
    ui_headerTraffic = lv_obj_create(ui_Traffic);
    lv_obj_set_width(ui_headerTraffic, 320);
    lv_obj_set_height(ui_headerTraffic, 30);
    lv_obj_set_x(ui_headerTraffic, 0);
    lv_obj_set_y(ui_headerTraffic, -70);
    lv_obj_set_align(ui_headerTraffic, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_headerTraffic, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    ui_txtTraffic = lv_label_create(ui_headerTraffic);
    lv_obj_set_width(ui_txtTraffic, lv_pct(100));
    lv_obj_set_height(ui_txtTraffic, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_txtTraffic, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTraffic, "流量统计");
    ui_object_set_themeable_style_property(ui_txtTraffic, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_default);
    ui_object_set_themeable_style_property(ui_txtTraffic, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_default);
    lv_obj_set_style_text_align(ui_txtTraffic, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTraffic, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 页码
    ui_txtTraffic1 = lv_label_create(ui_headerTraffic);
    lv_obj_set_width(ui_txtTraffic1, lv_pct(100));
    lv_obj_set_height(ui_txtTraffic1, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_txtTraffic1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTraffic1, "(1/6)");
    ui_object_set_themeable_style_property(ui_txtTraffic1, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,
                                           _ui_theme_color_default);
    ui_object_set_themeable_style_property(ui_txtTraffic1, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA,
                                           _ui_theme_alpha_default);
    lv_obj_set_style_text_align(ui_txtTraffic1, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTraffic1, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 今日下载标签
    ui_txtTodayRx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_txtTodayRx, 100);
    lv_obj_set_height(ui_txtTodayRx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_txtTodayRx, -100);
    lv_obj_set_y(ui_txtTodayRx, -30);
    lv_obj_set_align(ui_txtTodayRx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTodayRx, "今日下载: ");
    lv_obj_set_style_text_align(ui_txtTodayRx, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTodayRx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 今日下载值
    ui_valTodayRx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_valTodayRx, 200);
    lv_obj_set_height(ui_valTodayRx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_valTodayRx, 50);
    lv_obj_set_y(ui_valTodayRx, -30);
    lv_obj_set_align(ui_valTodayRx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_valTodayRx, "0 B");
    lv_obj_set_style_text_font(ui_valTodayRx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 今日上传标签
    ui_txtTodayTx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_txtTodayTx, 100);
    lv_obj_set_height(ui_txtTodayTx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_txtTodayTx, -100);
    lv_obj_set_y(ui_txtTodayTx, 0);
    lv_obj_set_align(ui_txtTodayTx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTodayTx, "今日上传: ");
    lv_obj_set_style_text_align(ui_txtTodayTx, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTodayTx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 今日上传值
    ui_valTodayTx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_valTodayTx, 200);
    lv_obj_set_height(ui_valTodayTx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_valTodayTx, 50);
    lv_obj_set_y(ui_valTodayTx, 0);
    lv_obj_set_align(ui_valTodayTx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_valTodayTx, "0 B");
    lv_obj_set_style_text_font(ui_valTodayTx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总下载标签
    ui_txtTotalRx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_txtTotalRx, 100);
    lv_obj_set_height(ui_txtTotalRx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_txtTotalRx, -100);
    lv_obj_set_y(ui_txtTotalRx, 30);
    lv_obj_set_align(ui_txtTotalRx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTotalRx, "总下载: ");
    lv_obj_set_style_text_align(ui_txtTotalRx, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTotalRx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总下载值
    ui_valTotalRx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_valTotalRx, 200);
    lv_obj_set_height(ui_valTotalRx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_valTotalRx, 50);
    lv_obj_set_y(ui_valTotalRx, 30);
    lv_obj_set_align(ui_valTotalRx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_valTotalRx, "0 B");
    lv_obj_set_style_text_font(ui_valTotalRx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总上传标签
    ui_txtTotalTx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_txtTotalTx, 100);
    lv_obj_set_height(ui_txtTotalTx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_txtTotalTx, -100);
    lv_obj_set_y(ui_txtTotalTx, 60);
    lv_obj_set_align(ui_txtTotalTx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_txtTotalTx, "总上传: ");
    lv_obj_set_style_text_align(ui_txtTotalTx, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_txtTotalTx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总上传值
    ui_valTotalTx = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_valTotalTx, 200);
    lv_obj_set_height(ui_valTotalTx, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_valTotalTx, 50);
    lv_obj_set_y(ui_valTotalTx, 60);
    lv_obj_set_align(ui_valTotalTx, LV_ALIGN_CENTER);
    lv_label_set_text(ui_valTotalTx, "0 B");
    lv_obj_set_style_text_font(ui_valTotalTx, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 初始更新
    update_traffic_display();
    
    // 设置定时器，每2秒更新一次
    lv_timer_create(traffic_timer_cb, 2000, NULL);
}
