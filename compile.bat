@echo off

if not exist .\build mkdir .\build

set TARGET=server.exe
set CFLAGS=-std=c89 -Wall -Werror -pedantic -g
set LIBS=-lws2_32
set SOURCES= src/core.c src/net.c src/proto.c src/server.c
set OUT_DIR=build/

clang %CFLAGS% %SOURCES% -o %OUT_DIR%%TARGET% %LIBS% -Wno-long-long

set TARGET=peer.exe
set CFLAGS=-std=c89 -Wall -Werror -pedantic -g
set LIBS=-lws2_32
set SOURCES= src/core.c src/net.c src/proto.c src/peer.c
set OUT_DIR=build/

clang %CFLAGS% %SOURCES% -o %OUT_DIR%%TARGET% %LIBS% -Wno-long-long

