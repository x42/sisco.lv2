Simple Scope
============

A simple audio oscilloscope with variable time scale, triggering, cursors
and numeric readout in LV2 plugin format.

The minimum grid resolution is 50 micro-seconds - or a 32 times oversampled
signal. The maximum buffer-time is 15 seconds.

Currently variants up to four channels are available.

For documentation please see http://x42.github.io/sisco.lv2/

Background Information
----------------------

This project was created to test and exemplify LV2 Atom-Vector communication
and demonstrate the short-comings of LV2 thread synchronization and LV2Atoms
for visualization UIs:

LV2Atom messages are written into a ringbuffer in the LVhost in the DSP-thread.
This ringbuffer is sent to the UI in another thread (jalv and ardour use a
`g_timeout()` usually at 40ms ~ 25fps), and finally things are painted in the
X11/gtk-main thread. Accurate (low-latency, high-speed) visualization is a
valid use-case for LV2 instance access in particular if visual sync to v-blank
is or importance.

These shortcomings are less pronounced and were mitigated by introducing
triggering and hold-off times to decouple screen synchronization.

The basic structure of this is now available as eg05-scope example plugin
from the official lv2plug.in repository.

Compared to the example, this plugin goes to some length to add features in
order to make it use-able beyond simple visualization and make it useful
for scientific measurements. It is however still rather simple compared to
a fully fledged oscilloscope. See the TODO file included with the source.

Install
-------

Compiling this plugin requires the LV2 SDK, gnu-make, a c-compiler,
gtk+2.0, libpango, libcairo and openGL (sometimes called: glu, glx, mesa).

```bash
  git clone git://github.com/x42/sisco.lv2.git
  cd sisco.lv2
  make submodules
  make
  sudo make install PREFIX=/usr
  
  # debug run (note the RDF communication)
  jalv.gtk -d 'http://gareus.org/oss/lv2/sisco#Mono_gtk'
  
  # test run
  jalv.gtk 'http://gareus.org/oss/lv2/sisco#Stereo_gtk'
  
  sudo make uninstall PREFIX=/usr
```

Note to packagers: The Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CFLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CFLAGS`).


Screenshots
-----------

![screenshot](https://raw.github.com/x42/sisco.lv2/master/img/sisco1.png "Single Channel Slow")
![screenshot](https://raw.github.com/x42/sisco.lv2/master/img/sisco4.png "Four Channel Variant")

Oscilloscope vs Waveform Display
--------------------------------

![screenshot](https://raw.github.com/x42/sisco.lv2/master/img/scopeVSwave.png "oscilloscope vs waveform")
