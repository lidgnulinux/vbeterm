vbeterm
=======

Custom terminal based on VTE. There are many terminals available. Many
of them are based on VTE, a library implementing a terminal emulator
widget for GTK+. Some are highly configurable, some are small but none
of them match exactly my expectations.

![Vbeterm](https://github.com/lidgnulinux/vbeterm/blob/master/vbeterm.png "Vbeterm")

Features
--------

 - No tab support
 - Use of VTE 2.90 (GTK3)
 - Single instance managing several terminals.
 - dabbrev-expand (mapped on `Alt-/`)
 - Copy & paste to clipboard.
 - Copy & paste to primary.

Installation
------------

Execute the following commands:

    $ ./configure
    $ make
    $ sudo make install

You need VTE 0.40.x which is not yet widely available. You can look at commit
[d98dad](https://github.com/vincentbernat/vbeterm/tree/d98dad045089929917c7e400808d410628019ef0)
for a version working with a more ancient version. On Debian, the
appropriate package is `libvte-dev`.
