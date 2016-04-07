Twizzler (temporary name)
=========================

This is a kernel. It's a kernel that does some things. It's part of the NVRAM work.

Let me (Daniel Bittman) know if you have any questions about the kernel design and implementation, I'll be happy to talk about it.

Build Instructions
------------------

Good luck. You'll need a cross compiler.
There's a script in this repo called 'toolchain.sh' which will do it for you. I've tested it only
on the vegas nodes, so it'll probably work. Probably.

Once you have the cross compiler set up and installed, and in your PATH, you can edit config.cfg to set
options for the build.

To build, run 'make'. To test, run 'make test'. If you're using the x86 version, you may want to run 'QEMU_FLAGS="-enable-kvm" make test'
to make it run faster.