# MU5250 tweaking  

一些对ZTE U60Pro的魔改记录（  

## 开启调试模式

使用 MlgmXyysd 大佬的[脚本](https://github.com/MlgmXyysd/openadb_MU5250)开启adb即可。  

如果本地不想下载php环境，可以丢给ai把脚本转成bat python bash各种编程语言（ 我是把脚本转为python以后运行的，因为电脑上有现成的python运行环境，而且我稍微懂一点点python所以比较容易debug）。

## 开启ssh

开启完adb以后要做的第一件事当然就是开启ssh了！（应该没人反驳我吧ww 

我们进到系统后，第一发现的是好多分区都已经设置为只读，所以如果现在安装软件包的话，会提示`/usr`是read only...  

既然`/overlay`是openwrt mainline存放非易失数据的分区，经观察易知我们可以把`/usr`等只读分区通过overlay机制挂到`/data`中。至于为什么要挂到`/data`，纯粹是因为`/data`是对应分区表里userdata的分区，容量最大（如果玩过android的小伙伴应该能看出分区表是很明显的android结构，毕竟此机用的是膏通x75）。  

挂载的脚本如下，直接粘贴到终端里，或者创建一个脚本运行就可以。
```sh
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
```

当挂载完以后，我们就可以开始安装各种软件了（耶耶耶

首先当然是我们的ssh：

`opkg update`
`opkg install openssh-server openssh-sftp-server vsftpd`

然后使用openwrt service（类似systemd）来启动ssh服务器：

`service sshd start`

使用ssh之前，由于我们并不知道中兴root账户的密码，所以在登录之前，还需要修改一下密码，并且允许root登录，即需要把`/etc/ssh/sshd_config`中的`PermitRootLogin`改为yes。


## 备份分区

由于我们要对我们的设备进行各种各样的~~调教~~魔改，所以先提前备份一下闪存分区，留一个镜像比较好（骗你的备份完成就算搞炸了也没办法还原，因为没有对应的firehose，还好出厂的时候把各种分区都做了只读所以一般不会搞炸）

我们使用ssh备份分区：

`ssh -C root@192.168.0.1 "dd if=/dev/mmcblk0 bs=4M" > remote_disk.img`

然后我们win下用7-zip、linux下用file确认一下文件的有效性：

win：右键 7-zip-打开压缩包，如果能成功打开，就证明成功镜像下来力  
linux: `file remote_disk.img`，如果输出是 DOS/MBR boot sector 类似的字样就可以了

## ssh持久化及开机启动adb

Q: 开启adb固然是好事，但是每次操作真的好麻烦啊，有没有什么能开机自启动呢？  
A: 你好有的，只不过实现方式比较ugly o.o  

具体就是把init.sh和enable_debugging脚本复制到`/data`目录里，然后在`/etc/rc.local`处添加一个执行命令。service的方法我试过了，但是不知道为什么enable以后无法启动...  

`/etc/rc.local`中在`exit 0`前一行添加：  

`(sleep 10; /bin/sh /data/init.sh > /tmp/mount.log 2>&1) &`

然后记得把`enable_debugging里`的`pasword`改成自己的密码（懒得去找具体执行的命令了所以直接用了网页开启adb的方法，不过这样好像会有安全隐患，毕竟拿到设备adb shell就可以把密码拿出来，不过一般人也不会想着干这种事吧大概）  

## 一些注意事项  

折腾不就是为了玩嘛毕竟底层系统是openwrt所以这部分我应该不用说了吧（其实还是要说的（  

`distfeed.conf`里自带了sdk里膏通和zte私有分类的软件包，所以第一次执行的时候失败是正常的，需要去`distfeed.conf`里删掉`openwrt_qti*`和`openwrt_zte_apps`的。而且因为它是x75，官方openwrt没有提供x75的target支持，所以也要把`core`删除。

修改完以后你会发现好像有些依赖kmod的软件包也装不了：  

```
root@OpenWrt:~# opkg install zerotier
Unknown package 'zerotier'.
Collected errors:
 * pkg_hash_check_unresolved: cannot find dependency kmod-tun for zerotier
 * pkg_hash_fetch_best_installation_candidate: Packages for zerotier found, but incompatible with the architectures configured
 * opkg_install_cmd: Cannot install package zerotier.

```  

因为没有core的相关源，但是添加了也没用（因为不是官方的内核），所以需要我们手动安装某些软件包。把opkg的ipk解压，再把data.tar.gz解压到根目录里就可以了。

## sing-box

当当当！我折腾它的目的当然是为了它了，怎么能不提它呢ww

起因是我看到了kernel config里有TUN相关的支持（可能是因为有网易UU加速器），所以就想能不能搞一下这个。  

最初是想装passwall的，但是由于zte的web界面被严重魔改了，就连uhttpd也动过，后来想还是用命令行控制吧。  


需要修改一下sing-box的启动服务文件，因为默认dnsmasq在监听53端口，如果想要做到sing-box代理dns的话，需要在inbound里添加一下53端口，这样会冲突，所以需要在init.d服务脚本启动和停止的时候修改一下dnsmasq的端口。直接把`files/sing-box`替换`/etc/init.d`里面的就可以了。  

sing-box用的是tun模式，所以还需要修改一下防火墙配置，让局域网的流量能够转发到tun里。

1. /etc/config/network里添加singbox接口
```
config interface 'singbox'
        option device 'singbox'
        option proto 'static'
        option ipv6 '1'
        option ipaddr '172.18.0.1'
        option netmask '255.255.255.252'
```

2. /etc/config/firewall lan zone里添加singbox
```
config zone
        option name 'lan'
        list network 'lan'
        list network 'singbox'
        list network 'wan'
        list network 'wan_v6'
        option input 'ACCEPT'
        option output 'ACCEPT'
        option forward 'ACCEPT'
        option masq '0'
```

写好配置以后，sing-box启动！

`service sing-box start`

## zerotier

