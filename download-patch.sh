#!/bin/bash

if test x$1 = x; then
	echo "Usage: $0 <version>"
	exit 1
fi

v=$1
let v0=$v-1

echo "$v0 -> $v"
echo 'Downloading patch info...'
names="`wget -O- http://mabi-nexon16.ktics.co.kr/patch/$v/${v0}_to_${v}.txt|tail -n+2|sed 's/,.*//g'`"
if [ "$names" = "" ]; then
	echo Patch not found
	exit 1
fi

echo $names
for name in $names; do
	echo Downloading $name...
	wget http://mabi-nexon16.ktics.co.kr/patch/$v/$name
done

echo 'Concatenating patch files...'
cat $names > ${v0}_to_${v}.zip
rm $names

