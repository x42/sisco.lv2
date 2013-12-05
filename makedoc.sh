#!/bin/sh

export OUTDIR=/tmp/siscodoc/
export KEEPOUTDIR=1
export KEEPIMAGES=1

lv2ls \
	| grep gareus \
	| grep sisco \
	| ~/data/coding/lv2toweb/tools/create_doc.sh

CUTLN=$(cat -n $OUTDIR/index.html | grep '<!-- start of page !-->' | cut -f 1 | tr -d ' ')
# TODO test if $CUTLN is an integer

head -n $CUTLN $OUTDIR/index.html > $OUTDIR/index.html_1

cat >> $OUTDIR/index.html_1 << EOF
<div style="margin:1em auto; width:66em; background-color:#fff; padding:1em;">
<h1>SiSco.lv2</h1>
<img src="sisco.png" alt="" style="float:right;"/>
<p>A simple audio oscilloscope with variable time scale in LV2 plugin format.</p>
<ul>
<li>Download from <a href="https://github.com/x42/sisco.lv2">Source Code Repository</a></li>
<li>Download <a href="https://github.com/x42/sisco.lv2/releases">Tagged release</a></li>
<li>SiSco.lv2 is [in progress to become] part of the <a href="http://packages.debian.org/search?keywords=x42-plugins">x42-plugin bundle collection</a>, part of the debian (and derived) GNU/Linux Distributions</li>
</ul>
<p>
SiSco implements a classic oscilloscope display with time on the X-axis and signal level on the Y-axis (for an XY mode display please
see Stereo-Phase-scope part of the <a href="https://github.com/x42/meters.lv2">meters.lv2</a> plugin bundle).
</p><p>
The minimum time-scale is 50 micro-seconds per grid, yielding a maximum resolution of 960kHz at 48SPS. The maximum buffer-time is 15 seconds.
</p><p>The time-scale setting [ (1) in the image below ] is the only parameter that affects data acquisition. All other settings act on the display of the data only.
</p><p>
The vertical axis displays floating-point audio-sample values with the unit [-1..+1].
The amplitude can be scaled by a factor of [-6..+6], negative values allow to invert polarity of the signal, the numeric readout is not affected by amplitude scaling.
</p><p>
Channels can be offset horizontally and vertically. The offset applies to the display only and does not span multiple buffers (the data does not extend beyond the original display). This allows to adjust the display in 'paused' mode after sampling a signal.
</p><p>
The LV2 plugin comes in 1-4 channel variants. The oscilloscope allows to visually hide channels as well as to freeze the current display buffer of each channel. Regardless of display, data-acquisition for every channel continues and the channel can be used for triggering.
</p>
<p><strong>Operation Modes</strong></p>
<dl>
<dt>No Triggering</dt>
<dd>The Scope runs free, with the display update-frequency depending on audio-buffer-size and selected time-scale. For update-frequencies less than 10Hz a vertical bar of the current acquisition position is displayed. This bar separates recent data (left of it) and previously acquired data (right of it).</dd>
<dt>Single Sweep</dt>
<dd>Manually trigger acquisition using the push-button, honoring trigger settings. Acquires exactly one complete display buffer.</dd>
<dt>Continuous Triggering</dt>
<dd>Continuously triggered data acquisition with a fixed hold time between runs.</dd>
</dl>

<p><strong>Usage</strong></p>

<p>All control elements are operated in using the mouse:</p>
<dl>
<dt>Click+Drag</dt><dd>left/down: decrease, right/up: increase value. Hold the Ctrl key to increase sensitivity.</dd>
<dt>Shift+Click</dt><dd>reset to default value</dd>
<dt>Scroll wheel</dt><dd>up/down by 1 step (smallest possible adjustment for given setting). Rapid continuous scrolling increases the step-size.</dd>
</dl>
<p>In paused mode, the cursors can be positioned with mouse click on the canvas. Button 1 (left) sets the first cursor's position, button 3 (right) the second cursor.</p>

<p><strong>User Interface Elements</strong></p>

<div style="margin:.5em auto; background-color:#fff; padding:1em;">
<img src="sisco_doc.png" alt="" style="margin:.5em auto; max-width:100%;"/>
</div>

<dl>
<dt>(1) Time-Scale Configuration</dt><dd>Allows to set horizontal grid-spacing in a range from 50 &#181;sec to 1 second. The maximum oversampling-factor is 32. If the plugin runs at sampling-rates below than 32KSPS, the actual grid may differ from the selected setting. Check numeric the display (2) for the actual scale. If this setting is modified while paused (3), it will only become effective with the next acquisition run. Changing the time-scale also clears the display-buffer memory.</dd>
<dt>(2) Time-Scale Display</dt><dd>Actual time-scale display. The unit per horizontal grid and the total range of data-acquisition is displayed in both seconds and Hz. Usually the value is identical to the setting of (1), but may differ for low or odd sample-rates. This readout is based on actual samples-per-pixel mapping and always valid for the displayed data.</dd>
<dt>(3) Pause Toggle Button</dt><dd>Allows to freeze the display in order to inspect acquired data. Pause is only available in free-run (no trigger) and in continuous-trigger after a complete acquisition cycle has been completed. Freezing the display also enables cursors (10, 11) to analyze the data.</dd>
<dt>(4) Channel Settings</dt><dd>Allows to alter the waveform display. Vertical Zoom and polarity can be set with the "Amp" dial in a range from -6 to +6 (linear scale, corresponding to 30dB). The 'gang' button enables identical amplitude scaling for all channels. X and Y offset are in percent of the screen width. These settings apply to the display-buffer only and can also be modified even when acquisition is paused. A Channel can be hidden (display, green button), the display can be frozen (memory, red button) and the on-screen-display numeric annotations can be toggled (yellow button).</dd>
<dt>(5) Amplitude Indicator</dt><dd>Color-coded indication of channel-amplitude range (green: left/chn 1, red: right/chn 2, blue: chn 3, magenta: chn 4).</dd>
<dt>(6) Trigger Mode and Type</dt><dd>Allows to select trigger mode [Off, Manual, Cont'd] and Type [rising edge, falling edge and channel].</dd>
<dt>(7) Manual Trigger Button</dt><dd>Only available in <em>Manual Trigger</em> mode, initiate a single sweep honoring trigger-settings (8).</dd>
<dt>(8) Trigger Settings</dt><dd>When triggering is enabled, the trigger-level can be set in the range corresponding to raw audio-data [-1..+1]. The X-position allows to position the trigger point in percent of screen-width. Note that the wave-form will only be displayed once the trigger position has been reached (setting this value to 100% will avoid any redraw flickering). The hold-time is only available in continuous trigger mode. It specifies the minimal delay between two acquisition cycles.</dd>
<dt>(9) Trigger Indicator</dt><dd>A vertical dashed line indicating trigger position and channel and a cross at the given trigger level.</dd>
<dt>(10) Info Area</dt><dd>When pause this area displays the time delta between the two cursor points in both seconds and Hertz. When running, this area is used to display trigger-status (if any).</dd>
<dt>(11) Cursor Settings</dt><dd>Allows to set horizontal position of cursors for numeric readout when the display is paused. Regardless of channels, two cursors are available. The x-position is given in pixels from the left screen edge. The channel number for each cursor defines the numeric readout: The sample-value of the specified channel at given x-position is printed on the display.</dd>
<dt>(12) Cursor and Numerics</dt><dd>Visual Markers indicating signal level at given cursor position for the chosen channel. At low-zoom levels - ie when one pixel corresponds to more than one raw audio sample - the minimum and maximum signal level at the marker-position is displayed. Additionally detailed per channel information can be displayed for the time range between the two marker positions. The latter includes RMS, peak-to-peak and absolute values. This on-screen-display is only available in paused mode (1) and can be toggled using the channel-display settings (4). If the selected area spans two acquisition cycles, a separation marker is added to the display and the OSD is postfixed with an asterisk.</dd>
</dl>
<hr/>
EOF

tail -n +$CUTLN $OUTDIR/index.html \
	| sed 's/<!-- end of page !-->/<\/div>/' \
	>> $OUTDIR/index.html_1
mv $OUTDIR/index.html_1 $OUTDIR/index.html

rm doc/http__*.html
cp -a $OUTDIR/*.html doc/

if test -n "$KEEPIMAGES"; then
	cp -an $OUTDIR/*.png doc/
else
	cp -a $OUTDIR/*.png doc/
fi

echo -n "git add+push doc? [y/N] "
read -n1 a
echo
if test "$a" != "y" -a "$a" != "Y"; then
	exit
fi

cd doc/ && git add *.html *.png && git commit -m "update documentation" && git push
