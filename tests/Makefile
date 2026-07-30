# Host unit tests for the pure Tuya BLE protocol layer.
# No ESP / no external crypto needed — MD5+AES are vendored in host_crypto.cpp.
#   make test

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter

SRC = test_protocol.cpp ../components/tuya_lock/tuya_protocol.cpp host_crypto.cpp

test: run
	./run

run: $(SRC) ../components/tuya_lock/tuya_protocol.h
	$(CXX) $(CXXFLAGS) -I. $(SRC) -o run

clean:
	rm -f run

.PHONY: test clean
