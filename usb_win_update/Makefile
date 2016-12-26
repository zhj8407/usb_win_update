CC := gcc
CPP := g++

CFLAGS := -W -Wall -g -std=c++11
LDFLAGS := -g -lssl -lcrypto

all: dfu_tool

%.o: %.cpp
	$(CPP) -c $(CFLAGS) $< -o $@

dfu_tool: main.o usb_linux.o md5_utils.o plcm_dfu.o
	$(CPP) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o
	rm -f dfu_tool
