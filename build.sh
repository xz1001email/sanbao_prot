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

cp $buildtarget $outputpath$target_dist

