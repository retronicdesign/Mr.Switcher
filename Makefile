# Name: Makefile
# Project: MrSwitcher
# Author Francis Gradel, B.Eng. (Retronic Design)
# Modified Date: 2021-01-04
# Tabsize: 4
# License: Proprietary, free under certain conditions. See Documentation.
USBFLAGS=   -ID:\libusb-win32-bin-1.2.7.1\include
USBLIBS=    -lhid -lsetupapi -LD:\libusb-win32-bin-1.2.7.1\lib D:\libusb-win32-bin-1.2.7.1\lib\gcc\libusb.a
EXE_SUFFIX= .exe

CC=				gcc
CXX=			g++
CFLAGS=			-O2 -Wall $(USBFLAGS) -mwindows -lcomdlg32
LIBS=			$(USBLIBS)
ARCH_COMPILE=	
ARCH_LINK=		

OBJ=		main.o usbcalls.o
RES=		MrSwitcher.rc
RESO=		$(RES).res.o
PROGRAM=	MrSwitcher$(EXE_SUFFIX)

all: $(PROGRAM)

$(PROGRAM): $(OBJ) $(RESO)
	$(CC) $(ARCH_LINK) $(CFLAGS) -o $(PROGRAM) $(OBJ) $(LIBS) $(RESO)


strip: $(PROGRAM)
	strip $(PROGRAM)

clean:
	rm -f $(OBJ) $(PROGRAM) $(RESO)

.c.o:
	$(CC) $(ARCH_COMPILE) $(CFLAGS) -c $*.c -o $*.o

$(RESO):
	windres -o $(RESO) -i $(RES)
