Simple Scope
============

Test and example plugin to demonstrate the short-comings of LV2 thread
synchronization and LV2Atoms for visualization UIs.

LV2Atom messages are written into a ringbuffer in the LVhost in the DSP-thread.
This ringbuffer is sent to the UI in another thread (jalv and ardour use a
g_timeout() usually at 40ms ~ 25fps), and finally things are painted in the
X11/gtk-main thread.

NB. /high-speed/ visualization is a valid use-case for LV2 instance access.


Apart from that this plugin implements a simple audio oscilloscope
Mono and Stereo variants are available.


Usage
-----

Compiling these plugin requires the LV2 SDK, gnu-make, a c-compiler,
gtk+2.0 and libcairo.

```bash
  git clone git://github.com/x42/sisco.lv2.git
  cd sisco.lv2
  make
  sudo make install PREFIX=/usr
  
  # debug run (note the RDF communication)
  jalv.gtk -d 'http://gareus.org/oss/lv2/sisco#Mono'
  
  # test run
  jalv.gtk 'http://gareus.org/oss/lv2/sisco#Stereo'
  
  sudo make uninstall PREFIX=/usr
```

Note to packagers: The Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CFLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CFLAGS`).


Screenshots
-----------

![screenshot](https://raw.github.com/x42/sisco.lv2/master/sisco1.png "Screenshot Slow")
![screenshot](https://raw.github.com/x42/sisco.lv2/master/sisco2.png "Screenshot Fast")
