#!/bin/bash


function git_reversion()
{
    git_version=`git rev-parse --verify --short HEAD`
    if [ -z "$(git status --untracked-files=no --porcelain)" ]; then
        git_version+=""
    else
        git_version+="_M"
    fi
    echo -n "${git_version}"
}

dms_target="DsmProt"
adas_target="AdasProt"
output_folder="output/"

device=$1
target=wsi
buildpath="arm/"
buildtarget=$buildpath"bin/"$target

mkdir -p $buildpath
cd $buildpath
if [ "$device" == "a" -o "$device" == "adas" ]
then
    echo "./building for adas"
    target_dist=$adas_target
    ../adas_cmake.sh
else
    echo "./building for dms"
    target_dist=$dms_target
    ../dms_cmake.sh
fi
make
cd ..

outputpath=$output_folder`git_reversion`"/"
mkdir -p $outputpath
echo "generate target $outputpath"

echo "cp $buildtarget $outputpath$target_dist"
cp $buildtarget $outputpath$target_dist

strip=/home/xiao/bin/ndk/android-ndk-r11c/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-strip
stripdir="strip_target/"
echo "mkdir $outputpath$stripdir"
mkdir -p $outputpath$stripdir
cp $buildtarget $outputpath$stripdir$target_dist
echo "strip..."
$strip $outputpath$stripdir$target_dist

