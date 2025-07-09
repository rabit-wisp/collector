#!/bin/bash
# THIS SCRIPT IS A QUICK AND DIRTY PLACEHOLDER BEFORE PROPERY OPENWRT INTEGRATION
echo "Building IPK packages for luci interface"

name=iw-telemetry
version="$(git describe --tags --abbrev=0)"

temp_dir=$(mktemp -d)
source_dir=$(realpath ..)

mkdir -p ../ipk-output

mkdir -p $temp_dir/data/
mkdir -p $temp_dir/control
touch $temp_dir/control/{control,postinst,preinst,prerm}
chmod 644 $temp_dir/control/control
chmod 755 $temp_dir/control/{postinst,prerm,preinst}

cp -R ../package/luci-$name/files/* $temp_dir/data

cat <<EOF > $temp_dir/control/control
Package: luci-$name
Version: $version
Architecture: noarch
Maintainer: memetb@gmail.com
Description: Wireless Telemetry user interface
Priority: optional
Section: utils
Depends: $name luci
EOF

cat $temp_dir/control/control

cat <<EOF $temp_dir/control/preinst
#!/bin/sh
EOF

cat <<EOF > $temp_dir/control/postinst
#!/bin/sh
rm /tmp/luci-indexcache.*.json
/etc/init.d/rpcd restart
/etc/init.d/uhttpd restart
EOF

cat <<EOF > $temp_dir/control/prerm
#!/bin/sh
EOF

echo "2.0" > $temp_dir/debian-binary

pushd $temp_dir
tree

cd control
tar --numeric-owner --group=0 --owner=0 -zcvf ../control.tar.gz ./*
cd ..

cd data
tar --numeric-owner --group=0 --owner=0 -zcvf ../data.tar.gz ./*
cd ..

rm -f $source_dir/ipk-output/luci-${name}_${version}-noarch.ipk
tar --numeric-owner --group=0 --owner=0 -zcf \
    $source_dir/ipk-output/luci-${name}_${version}-noarch.ipk \
    ./debian-binary ./data.tar.gz ./control.tar.gz
