#!/bin/sh

#service uhttpd stop
/etc/init.d/uhttpd stop

for dir in root lib www sbin usr; do
    if mount | grep -q "overlay on /$dir type overlay"; then
        echo "/$dir already mounted"
    else
        lower="/$dir"
        mkdir -p /data/overlay/$dir-upper_a
        mkdir -p /data/overlay/.$dir-work_a
        mount -t overlay overlay \
            -o lowerdir=$lower,upperdir=/data/overlay/$dir-upper_a,workdir=/data/overlay/.$dir-work_a \
            /$dir
    fi
done

mount -t ext4 -o ro /dev/block/bootdevice/by-name/ztedata /usr/zte_web

for service in sshd uhttpd vsftpd zerotier; do
    /etc/init.d/$service start
done

# --- 禁用 calling home（向中兴云/远程服务器上报） ---
# 云端 MQTT 上报(ufi.seecom.com.cn)、FOTA 结果上报、星控/精卫查询、智能管理(ztehome.com.cn)
for service in zte_mqtt_sdk_st zte_topsw_fota_result zte_topsw_jwxk_query zte_smart_manage; do
    /etc/init.d/$service stop 2>/dev/null
done
# procd 服务带 respawn，stop 后可能被重拉，用 killall 兜底强杀残余进程
killall -q zte_mqtt_sdk_st zte_topsw_fota_result zte_topsw_jwxk_query zte_smart_manage 2>/dev/null
# 从源头关闭云上报开关（/etc/config/zwrt_mqtt、zwrt_smart_mng）
uci set zwrt_mqtt.config.mqttOnreportEnable=0 2>/dev/null
uci commit zwrt_mqtt 2>/dev/null
uci set zwrt_smart_mng.smart_mng.acc_effect_enable=0 2>/dev/null
uci commit zwrt_smart_mng 2>/dev/null

# --- 禁用 TR-069 远程管理（ACS 云端管理 CWMP/周期上报） ---
# 停掉 procd 托管的 TR-069 子进程
for service in zte_topsw_tr069_sub zte_topsw_tr098db; do
    /etc/init.d/$service stop 2>/dev/null
done
# killall 兜底强杀 TR-069 主进程与残余子进程
killall -q zte_topsw_tr069 zte_topsw_tr069_sub zte_topsw_tr098db 2>/dev/null
# 从源头关闭 CWMP 远程管理、周期上报与监控开关（/etc/config/zwrt_tr069）
uci set zwrt_tr069.ManagementServer.EnableCWMP=0 2>/dev/null
uci set zwrt_tr069.ManagementServer.PeriodicInformEnable=0 2>/dev/null
uci set zwrt_tr069.Switch.monitor_enable=0 2>/dev/null
uci commit zwrt_tr069 2>/dev/null

# enable debugging after boot
# waiting web interface up
sleep 2
ubus call zwrt_bsp.usb set '{"mode":"debug"}'
