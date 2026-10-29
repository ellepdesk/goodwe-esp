# Builds the host CLI against lib/aa55. Needs hidapi:
#   macOS:  brew install hidapi
#   Debian: apt install libhidapi-dev pkg-config
LIB := ../../lib/aa55/src
ifeq ($(shell uname),Darwin)
HIDAPI ?= hidapi
else
HIDAPI ?= hidapi-hidraw
endif

CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -I$(LIB) -DAA55_BAUD_DEFAULT=9600 $(shell pkg-config --cflags $(HIDAPI))
LDLIBS += $(shell pkg-config --libs $(HIDAPI))

goodwe: goodwe.cpp $(LIB)/aa55.cpp $(LIB)/master.cpp $(LIB)/aa55.h $(LIB)/master.h
	$(CXX) $(CXXFLAGS) -o $@ goodwe.cpp $(LIB)/aa55.cpp $(LIB)/master.cpp $(LDLIBS)

clean:
	rm -f goodwe

.PHONY: clean
