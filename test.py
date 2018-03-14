#!/usr/bin/env python3

import socket

sk = socket.socket()
sk.connect(("localhost", 4500))
sk.send(bytes("1 2 3\r\n", encoding="ascii"))
sk.send(bytes("Host: localhost:8080\r\n", encoding="ascii"))
sk.send(bytes("\r\n", encoding="ascii"))
sk.close()

print("Hello ")
