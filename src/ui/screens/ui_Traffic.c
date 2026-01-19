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
static lv_obj_t * ui_TrafficTitle;
static lv_obj_t * ui_TrafficTodayRxLabel;
static lv_obj_t * ui_TrafficTodayRxValue;
static lv_obj_t * ui_TrafficTodayTxLabel;
static lv_obj_t * ui_TrafficTodayTxValue;
static lv_obj_t * ui_TrafficTotalRxLabel;
static lv_obj_t * ui_TrafficTotalRxValue;
static lv_obj_t * ui_TrafficTotalTxLabel;
static lv_obj_t * ui_TrafficTotalTxValue;
lv_obj_t * ui_txtTraffic1;

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
    lv_label_set_text(ui_TrafficTodayRxValue, buffer);
    
    format_traffic(stats.today_tx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_TrafficTodayTxValue, buffer);
    
    format_traffic(stats.rx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_TrafficTotalRxValue, buffer);
    
    format_traffic(stats.tx_bytes, buffer, sizeof(buffer));
    lv_label_set_text(ui_TrafficTotalTxValue, buffer);
}

// 定时器回调函数
static void traffic_timer_cb(lv_timer_t *timer)
{
    update_traffic_display();
}

void ui_Traffic_screen_init(void)
{
    ui_Traffic = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Traffic, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    ui_TrafficTitle = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTitle, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTitle, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_TrafficTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_TrafficTitle, 10);
    lv_label_set_text(ui_TrafficTitle, "流量统计");
    lv_obj_set_style_text_font(ui_TrafficTitle, &ui_font_MiSans24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 当日下载
    ui_TrafficTodayRxLabel = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTodayRxLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTodayRxLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTodayRxLabel, -60);
    lv_obj_set_y(ui_TrafficTodayRxLabel, 60);
    lv_obj_set_align(ui_TrafficTodayRxLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_TrafficTodayRxLabel, "今日下载:");
    lv_obj_set_style_text_font(ui_TrafficTodayRxLabel, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TrafficTodayRxValue = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTodayRxValue, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTodayRxValue, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTodayRxValue, 60);
    lv_obj_set_y(ui_TrafficTodayRxValue, 60);
    lv_obj_set_align(ui_TrafficTodayRxValue, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_TrafficTodayRxValue, "0 B");
    lv_obj_set_style_text_font(ui_TrafficTodayRxValue, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 当日上传
    ui_TrafficTodayTxLabel = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTodayTxLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTodayTxLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTodayTxLabel, -60);
    lv_obj_set_y(ui_TrafficTodayTxLabel, 100);
    lv_obj_set_align(ui_TrafficTodayTxLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_TrafficTodayTxLabel, "今日上传:");
    lv_obj_set_style_text_font(ui_TrafficTodayTxLabel, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TrafficTodayTxValue = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTodayTxValue, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTodayTxValue, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTodayTxValue, 60);
    lv_obj_set_y(ui_TrafficTodayTxValue, 100);
    lv_obj_set_align(ui_TrafficTodayTxValue, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_TrafficTodayTxValue, "0 B");
    lv_obj_set_style_text_font(ui_TrafficTodayTxValue, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总下载
    ui_TrafficTotalRxLabel = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTotalRxLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTotalRxLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTotalRxLabel, -60);
    lv_obj_set_y(ui_TrafficTotalRxLabel, 160);
    lv_obj_set_align(ui_TrafficTotalRxLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_TrafficTotalRxLabel, "总下载:");
    lv_obj_set_style_text_font(ui_TrafficTotalRxLabel, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TrafficTotalRxValue = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTotalRxValue, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTotalRxValue, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTotalRxValue, 60);
    lv_obj_set_y(ui_TrafficTotalRxValue, 160);
    lv_obj_set_align(ui_TrafficTotalRxValue, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_TrafficTotalRxValue, "0 B");
    lv_obj_set_style_text_font(ui_TrafficTotalRxValue, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 总上传
    ui_TrafficTotalTxLabel = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTotalTxLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTotalTxLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTotalTxLabel, -60);
    lv_obj_set_y(ui_TrafficTotalTxLabel, 200);
    lv_obj_set_align(ui_TrafficTotalTxLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_TrafficTotalTxLabel, "总上传:");
    lv_obj_set_style_text_font(ui_TrafficTotalTxLabel, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TrafficTotalTxValue = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_TrafficTotalTxValue, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_TrafficTotalTxValue, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_TrafficTotalTxValue, 60);
    lv_obj_set_y(ui_TrafficTotalTxValue, 200);
    lv_obj_set_align(ui_TrafficTotalTxValue, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_TrafficTotalTxValue, "0 B");
    lv_obj_set_style_text_font(ui_TrafficTotalTxValue, &ui_font_MiSans20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 页码
    ui_txtTraffic1 = lv_label_create(ui_Traffic);
    lv_obj_set_width(ui_txtTraffic1, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_txtTraffic1, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_txtTraffic1, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_txtTraffic1, -5);
    lv_label_set_text(ui_txtTraffic1, "");
    lv_obj_set_style_text_font(ui_txtTraffic1, &ui_font_MiSans16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 初始更新
    update_traffic_display();
    
    // 设置定时器，每2秒更新一次
    lv_timer_create(traffic_timer_cb, 2000, NULL);
}
