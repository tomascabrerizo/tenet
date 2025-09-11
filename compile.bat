@echo off

if not exist .\build mkdir .\build

set TARGET=server.exe
set CFLAGS=-std=c89 -Wall -Werror -pedantic -g
set LIBS=-lws2_32
set SOURCES= src/core.c src/net.c src/proto.c src/server.c
set OUT_DIR=build/

clang %CFLAGS% %SOURCES% -o %OUT_DIR%%TARGET% %LIBS% -Wno-long-long

rem set TARGET=peer.exe
rem set CFLAGS=-std=c89 -Wall -Werror -pedantic -g
rem set LIBS=-lws2_32
rem set SOURCES= src/core.c src/message.c src/net.c src/peer.c
rem set OUT_DIR=build/
rem 
rem clang %CFLAGS% %SOURCES% -o %OUT_DIR%%TARGET% %LIBS% -Wno-long-long

