#!/bin/bash


output="install_setup"
pack_name="subiao_netprot"
pack_tgz=$pack_name".tgz"
dst_dir=$output"/"$pack_name


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

git_folder=`git_reversion`
echo "git folder: $git_floder"

last_pack="output/"`git_reversion`"/strip_target/"
mkdir -p $output

if [ $# -ne 0 ]
then
    src_dir="$1"
else
    src_dir=$last_pack
fi

##clear dst_dir
if [ -d $dst_dir ]
then
    echo "#clean old floder!"
    rm -r $dst_dir
    ls $output
fi

cp -r $src_dir $dst_dir
if [ -f $pack_tgz ]
then
    echo -e "#clear old package!\n"
    rm $pack_tgz
fi

echo -e "pack in $output :\n"
#files=`ls`
file="AdasProt  adasProt.sh  DsmProt  dsmProt.sh"
file2="prot.json  prot_upgrade.sh"

cd $output
tar czf $pack_tgz $pack_name

cd $pack_name
if [ $? == 0 ]
then
    echo "file list:"; ls; echo "do mdsum:"
    md5sum * >../md5.txt
    cat ../md5.txt
fi

cd ..
cp md5.txt output.txt
for s in $file
do
    echo $s
    sed -i 's/'$s'/\/system\/bin\/'$s'/' output.txt
done

for s in $file2
do
    sed -i 's/'$s'/\/data\/'$s'/' output.txt
done


echo "done!"

