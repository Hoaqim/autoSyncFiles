INCS := $(shell find ./ -name '*.h') $(shell find ../utilities/ -name '*.h')
SRCS := $(shell find ./ -name '*.cpp') $(shell find ../utilities/ -name '*.cpp')
OBJS := $(subst .cpp,.o,$(SRCS))

CXX = g++
CXXFLAGS = -Wall -O2 -std=c++11 -I../utilities/ -pthread

dataServer: $(OBJS) $(INCS)
	@$(CXX) $(CXXFLAGS) $(OBJS) -o dataServer

.SILENT: $(OBJS)

%.o: %.cc
	@$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean

clean:
	@echo "Cleaning up (server)..."
	@rm -f $(OBJS) ./dataServer
