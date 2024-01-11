INCS := $(shell find ./ -name '*.h') $(shell find ../utilities/ -name '*.h')
SRCS := $(shell find ./ -name '*.cpp') $(shell find ../utilities/ -name '*.cpp')
OBJS := $(subst .cpp,.o,$(SRCS))

CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -I../utilities/

remoteClient: $(OBJS) $(INCS)
	@$(CXX) $(CXXFLAGS) $(OBJS) -o remoteClient

.SILENT: $(OBJS)

%.o: %.cc
	@$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean

clean:
	@echo "Cleaning up (client)..."
	@rm -f $(OBJS) ./remoteClient
