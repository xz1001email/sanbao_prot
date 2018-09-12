#!/system/bin/sh


cd /mnt/obb
busybox mv package.bin package.tar.gz
busybox tar xzf package.tar.gz

mount -o remount,rw /system

upgrade_bin="AdasProt DsmProt adasProt.sh dsmProt.sh"
bin_dir="/system/bin/"

for bin in $upgrade_bin
do
    echo "list $bin"

    if [ -f $bin ]; then
        echo "update $bin..."
        busybox chmod 0755 $bin
        busybox cp $bin $bin_dir$bin".new"
        busybox mv $bin_dir$bin".new" $bin_dir$bin
        sync
    else
        echo "$bin not exsist"
    fi
done

upgrade_conf="prot.json prot_upgrade.sh"
conf_dir="/data/"

for conf in $upgrade_conf
do
    echo "list $conf"

    if [ -f $conf ]; then
        echo "update $conf..."
        busybox chmod 0755 $conf
        busybox cp $conf $conf_dir$conf".new"
        busybox mv $conf_dir$conf".new" $conf_dir$conf
        sync
    else
        echo "$conf not exsist"
    fi
done

sync

