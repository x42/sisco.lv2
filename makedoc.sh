#!/bin/sh

export OUTDIR=/tmp/siscodoc/
export KEEPOUTDIR=1

lv2ls \
	| grep gareus \
	| grep sisco \
	| ~/data/coding/lv2toweb/tools/create_doc.sh

CUTLN=$(cat -n $OUTDIR/index.html | grep '<!-- start of page !-->' | cut -f 1 | tr -d ' ')
# TODO test if $CUTLN is an integer

head -n $CUTLN $OUTDIR/index.html > $OUTDIR/index.html_1

cat >> $OUTDIR/index.html_1 << EOF
<h1>SiSco.lv2</h1>
<div style="margin:1em auto; width:66em;">
<img src="sisco.png" alt="" style="float:right;"/>
<p>
<a href="https://github.com/x42/sisco.lv2">sisco.lv2</a> - a simple audio oscilloscope in LV2 plugin format.
</p>
<p>Operation Modes</p>
<ul>
<li>...</li>
<li>...</li>
<li>...</li>
</ul>
</div>
<div style="clear:both;"></div>
EOF

tail -n +$CUTLN $OUTDIR/index.html >> $OUTDIR/index.html_1
mv $OUTDIR/index.html_1 $OUTDIR/index.html

rm doc/http__*
cp -a $OUTDIR/*.html doc/
cp -a $OUTDIR/*.png doc/

echo -n "git add+push doc? [y/N] "
read -n1 a
echo
if test "$a" != "y" -a "$a" != "Y"; then
	exit
fi

cd doc/ && git add *.html *.png && git commit "update documentation" && git push
