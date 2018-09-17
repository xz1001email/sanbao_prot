#!/bin/bash


home=`pwd`
upgrade_tgz="subiao_upgrade.tar.gz"
upgrade_bin="subiao_upgrade.bin"


if [ $# -ne 0 ]
then
    dst_dir="$1"
else
    dst_dir="release/upgrade/"
fi


if [ ! -d $dst_dir ]
then
    echo "dir not exsist!"
    exit 1
fi


cd $dst_dir
rm $upgrade_bin
echo -e "enter $dst_dir, start pack:\n"
echo "`ls`"
echo -e "\n"

tar czf $upgrade_tgz *
mv $upgrade_tgz $upgrade_bin

echo -e "$upgrade_bin packed done!"

