

# MU5250 tweaking  

Some modification records for the ZTE U60Pro (  

Disclaimer: This document is for learning and reference purposes only. All trademarks and software mentioned belong to their respective manufacturers and individuals. My reverse engineering activities are strictly limited to personal learning, security research, or interoperability purposes, and all code is open-sourced under the GPLv3 license. It must not be used for any illegal purposes, including but not limited to bypassing software licensing mechanisms, creating pirated copies, stealing trade secrets, or harming the legitimate rights of original software developers. This project is provided "as is", without warranty of any kind, express or implied. The author is not liable for any direct or indirect losses (including legal disputes, data corruption, business interruption, etc.) arising from the use of this project.  

## Enable Debug Mode

You can enable ADB using the [script](https://github.com/MlgmXyysd/openadb_MU5250) by MlgmXyysd.  

If you don't want to set up a local PHP environment, you can ask an AI to convert the script into BAT, Python, Bash, or other languages. (I converted it to Python and ran it, since my computer already has a Python environment, and my basic Python knowledge makes debugging easier.)

## Enable SSH

The first thing to do after enabling ADB is, of course, to enable SSH! (I assume no one will object to this, right? haha)  

Upon entering the system, we immediately notice that many partitions are set to read-only. So if you try to install packages now, it will prompt that `/usr` is read-only...  

Since `/overlay` is the partition where OpenWrt mainline stores non-volatile data, observation reveals that we can mount read-only partitions like `/usr` into `/data` using the overlay mechanism. As for why mount them to `/data`, it's simply because `/data` corresponds to the userdata partition in the partition table, which has the largest capacity. (If you've played with Android, you'll likely notice the partition table has a very obvious Android structure, after all, this device uses the Qualcomm X75 chipset.)  

The mounting script is as follows. You can paste it directly into the terminal or create a script to run it.
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

Once mounted, we can start installing various software! (Yay yay yay)

First up is, of course, our SSH:

`opkg update`  
`opkg install openssh-server openssh-sftp-server vsftpd`

Then use the OpenWrt service command (similar to systemd) to start the SSH server:

`service sshd start`

Before using SSH, since we don't know the default root password for the ZTE account, we need to change the password and allow root login. Specifically, change `PermitRootLogin` to `yes` in `/etc/ssh/sshd_config`.


## Backup Partitions

Since we are going to perform various ~~tweaks~~ modifications on the device, it's a good idea to back up the flash partitions and keep an image first. (Just kidding, even after backing up, you can't restore it if you brick the device, because there's no corresponding Firehose tool. Fortunately, the manufacturer set all partitions to read-only at the factory, so it's generally hard to brick.)

We can back up partitions via SSH:

`ssh -C root@192.168.0.1 "dd if=/dev/mmcblk0 bs=4M" > remote_disk.img`

Then, verify the file's integrity using 7-Zip on Windows or the `file` command on Linux:

Windows: Right-click -> 7-Zip -> Open Archive. If it opens successfully, the disk image was created successfully.  
Linux: `file remote_disk.img`. If the output says something like `DOS/MBR boot sector`, it's fine.

## Persistent SSH & Boot ADB

Q: Enabling ADB is great, but it's really tedious to do it every time. Is there a way to auto-start it on boot?  
A: Yes, there is, but the implementation method is a bit ugly o.o  

Specifically, copy the `init.sh` and `enable_debugging` scripts to the `/data` directory, and add an execution command in `/etc/rc.local`. I tried the service method, but for some reason, it couldn't start after enabling...  

Add the following line just before `exit 0` in `/etc/rc.local`:  

`(sleep 10; /bin/sh /data/init.sh > /tmp/mount.log 2>&1) &`

Also, remember to change the `pasword` in `enable_debugging.sh` to your own password. (I was too lazy to find the exact command, so I directly used the web interface method to enable ADB. However, this might pose a security risk, since anyone with ADB shell access could potentially retrieve the password. But probably no one would bother doing that anyway.)  

## Some Notes  

The whole point of tinkering is to have fun. Since the underlying system is OpenWrt, I probably don't need to explain this part, right? (Actually, I do need to explain it.)  

The `distfeed.conf` file comes with software packages from Qualcomm and ZTE's private categories in the SDK. So it's normal for the first execution to fail. You need to remove `openwrt_qti*` and `openwrt_zte_apps` from `distfeed.conf`. Also, since it's an X75 chipset, official OpenWrt doesn't provide target support for X75, so you also need to delete `core`.

When installing packages, you might find that some packages depending on kmod cannot be installed:  

```
root@OpenWrt:~# opkg install zerotier
Unknown package 'zerotier'.
Collected errors:
 * pkg_hash_check_unresolved: cannot find dependency kmod-tun for zerotier
 * pkg_hash_fetch_best_installation_candidate: Packages for zerotier found, but incompatible with the architectures configured
 * opkg_install_cmd: Cannot install package zerotier.
```  

This is because there's no `core` repository source, and adding it doesn't help (since it's not an official kernel). Therefore, we need to manually install certain packages. Just extract the opkg `.ipk` file, then extract `data.tar.gz` to the root directory.  

The commands are `tar -zxvf xxx.ipk` and `tar -zxvf data.tar.gz -C /` 

## Install Software

  - [sing-box](docs/sing-box.md)
  - [zerotier](docs/zerotier.md)
  - [Modify Boot Animation](docs/bootanim.md)

## Conclusion

Overall, although the U60Pro runs a customized version of OpenWrt, since it is OpenWrt, the playability is actually quite high, and you can tinker with it freely.  

If I have time later, I'll investigate how the screen is controlled. (Currently, it seems the `zte_topsw_devui` program writes directly to the framebuffer, but I haven't analyzed the specifics yet.)
