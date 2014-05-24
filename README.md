WiPacket
=========

Work in progress! At this point, don't expect this to work at all, or not blow up your computer!

This program provides a very simple interface for basic packet communication over standard WiFi hardware, without the use of any access point or "infrastructure mode". The program can provide a userspace socket for reading and writing raw data to a 802.11 interface. This makes it easy to develop radio applications where you do not want or need a full TCP/IP stack, but just the raw data packets.
