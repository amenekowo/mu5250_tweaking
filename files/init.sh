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

# enable debugging after boot
# waiting web interface up

sleep 10
/bin/sh /data/enable_debugging.sh
