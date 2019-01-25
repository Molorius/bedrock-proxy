By Blake Felt - blake.w.felt@gmail.com

Bedrock Proxy
=============

A program that allows Xbox One users of Minecraft to connect to any server they
want, similar to how Android, iOS, and Windows 10 users can. Note that this
requires a computer to be running this program any time you want to play.

Tested on Linux, should work out of the box on macOS.

Compiling
=========

Requires gcc and make. Edit the SERVER_PORT and SERVER_ADDR to the desired values
in main.c. Then compile with `make` in the terminal.

Running
=======

Run by typing `./bedrock-proxy` into the terminal on your main device. Both the
Xbox and the device running this need to be on the same network. You should find
the server you want in the Minecraft Friends section under LAN Games.

I have included an example systemd service file to start this automatically at boot
for any Linux users of systemd.

How It Works
============

This works by sending all udp packets from the CLIENT_PORT to the server address
and vice-versa. This will appear to the Xbox as if the server is playing on your LAN.
