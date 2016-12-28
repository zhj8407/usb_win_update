CC := gcc
CPP := g++

CFLAGS := -W -Wall -g -std=c++11
LDFLAGS := -g -lssl -lcrypto

SOURCE_SRC := ../usb_win_update

all: dfu_tool

%.o: $(SOURCE_SRC)/%.cpp
	$(CPP) -c $(CFLAGS) $(SOURCE_SRC)/$< -o $@

dfu_tool: main.o usb_linux.o md5_utils.o plcm_dfu.o
	$(CPP) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o
	rm -f dfu_tool
