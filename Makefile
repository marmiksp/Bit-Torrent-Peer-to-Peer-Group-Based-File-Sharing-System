# Makefile for P2P File Sharing System

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lssl -lcrypto -pthread

# Targets
all: tracker client

tracker: Tracker_Master.cpp common.h
	$(CXX) $(CXXFLAGS) Tracker_Master.cpp -o tracker $(LDFLAGS)

client: Client_Master.cpp common.h
	$(CXX) $(CXXFLAGS) Client_Master.cpp -o client $(LDFLAGS)

# Debug build with extra logging
debug: CXXFLAGS += -g -DDEBUG
debug: all

clean:
	rm -f tracker client

.PHONY: all clean debug
