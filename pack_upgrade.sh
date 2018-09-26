#!/bin/bash


#平台要求命名规则：

#  终端通过JT/T 808中的终端控制指令对终端进行升级，升级文件命名规则如下：

#  <设备类型>_<厂家编号>_<设备型号>_<依赖软件版本号>_<软件版本号>.<后缀名>。

#  字段定义如下：

#  设备类型：01——终端；02——保留；03——ADAS；04——DSM；05——BSD；06——TPMS。

#  厂家ç号：设备厂家名称编号，由数字和字母组成。

#  设备型号：由设备厂家定义的设备型号，由数字和字母组成。

#  依赖软件版本号：软件升级需要依赖的软件版本，由数字和字母组成。

#  软件版本号：本次升级的软件版本，由数字和字母组成。

#  后缀名：设备厂家自定义升级文件后缀名，由数字和字母组成





# 03_MINIEYE_M4_1.1.1_1.1.5.bin
# 04_MINIEYE_M4_1.1.1_1.1.5.bin


home=`pwd`
upgrade_tgz="subiao_upgrade.tar.gz"
#upgrade_bin="subiao_upgrade.bin"
upgrade_bin="03_MINIEYE_M4_1.1.1_1.1.5.bin"

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

