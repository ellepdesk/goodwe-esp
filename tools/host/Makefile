# Builds the host tools against lib/aa55 (and lib/goodwe_ha for goodwe-mqtt).
#   macOS:  brew install hidapi mosquitto
#   Debian: apt install libhidapi-dev libmosquitto-dev pkg-config
LIB := ../../lib/aa55/src
HA := ../../lib/goodwe_ha/src
ifeq ($(shell uname),Darwin)
HIDAPI ?= hidapi
else
HIDAPI ?= hidapi-hidraw
endif

CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -I$(LIB) -I$(HA) -DAA55_BAUD_DEFAULT=9600 $(shell pkg-config --cflags $(HIDAPI))
HID_LIBS := $(shell pkg-config --libs $(HIDAPI))

AA55 := $(LIB)/aa55.cpp $(LIB)/master.cpp
DEPS := $(AA55) $(LIB)/aa55.h $(LIB)/master.h transport.cpp transport.h

all: goodwe goodwe-mqtt

goodwe: goodwe.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ goodwe.cpp transport.cpp $(AA55) $(HID_LIBS)

goodwe-mqtt: goodwe-mqtt.cpp $(DEPS) $(HA)/ha.cpp $(HA)/ha.h
	$(CXX) $(CXXFLAGS) -o $@ goodwe-mqtt.cpp transport.cpp $(AA55) $(HA)/ha.cpp $(HID_LIBS) -lmosquitto -lpthread

clean:
	rm -f goodwe goodwe-mqtt

.PHONY: all clean
