#!/system/bin/sh


cd /mnt/obb
busybox mv package.bin package.tar.gz
busybox tar xzf package.tar.gz

if [ -f AdasProt ]; then
    echo "update AdasProt..."
    busybox chmod 0755 AdasProt
    busybox cp AdasProt /data/AdasProt_new
    busybox mv /data/AdasProt_new /data/AdasProt
else
    echo "AdasProt not exsist"
fi

if [ -f DsmProt ]; then
    echo "update DsmProt..."
    busybox chmod 0755 DsmProt
    busybox cp DsmProt /data/DsmProt_new
    busybox mv /data/DsmProt_new /data/DsmProt
else
    echo "DsmProt not exsist"
fi
