#!/bin/bash
# THIS SCRIPT IS A QUICK AND DIRTY PLACEHOLDER BEFORE PROPERY OPENWRT INTEGRATION
echo "Building IPK packages for executable"
#set -x
name=iw-telemetry
version="$(git describe --tags --abbrev=0)"

temp_dir=$(mktemp -d)
source_dir=$(realpath ..)

mkdir -p ../ipk-output
mkdir -p $temp_dir/data/{usr/bin,etc}
mkdir -p $temp_dir/control
touch $temp_dir/control/{control,postinst,preinst,prerm}
chmod 644 $temp_dir/control/control
chmod 755 $temp_dir/control/{postinst,prerm,preinst}

cp -R ../package/$name/files/* $temp_dir/data

cat <<EOF $temp_dir/control/preinst
#!/bin/sh
exit 0
EOF

cat <<EOF > $temp_dir/control/postinst
#!/bin/sh
/etc/init.d/$name enable
/etc/init.d/$name start
exit 0
EOF

cat <<EOF > $temp_dir/control/prerm
#!/bin/sh
/etc/init.d/$name disable
/etc/init.d/$name stop
exit 0
EOF

echo "2.0" > $temp_dir/debian-binary

shopt -s nullglob
pushd $temp_dir
echo for t in $source_dir
for i in "$source_dir"/cmake/toolchains/*.cmake
do
    target=$(basename $i | cut -d'-' -f1)
    arch=$(grep 'set(CMAKE_SYSTEM_PROCESSOR' $i  | cut -d" " -f2 | cut -d")" -f1)

    cat <<EOF > $temp_dir/control/control
Package: $name
Version: $version
Architecture: $arch
Maintainer: memetb@gmail.com
Description: Wireless Telemetry Collector
Priority: optional
Section: utils
Depends: libc
EOF


    rm -f *.tar.gz # do this to clear previous loop's artifacts
    echo packaging $target with arch $arch
    rm -f data/usr/bin/*
    ls "$source_dir/build/$target/binaries/"
    cp "$source_dir/build/$target/binaries/"* data/usr/bin/

    cd control
    tar --numeric-owner --group=0 --owner=0 -zcvf ../control.tar.gz ./*
    cd ..

    cd data
    tar --numeric-owner --group=0 --owner=0 -zcvf ../data.tar.gz ./*
    cd ..

    rm -f $source_dir/ipk-output/${name}_$version-$target.ipk
    tar --numeric-owner --group=0 --owner=0 -zcf \
        $source_dir/ipk-output/${name}_$version-$target.ipk \
        ./debian-binary ./data.tar.gz ./control.tar.gz
done
popd

shopt -u nullglob
