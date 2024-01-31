CXX ?= c++
CFLAGS += --std=c++23 -Wall -Wextra -pedantic
TARGET_DIR ?= target
ifeq ($(DEBUG),true)
	CFLAGS += -gdwarf
	REAL_TARGET_DIR := $(TARGET_DIR)/debug
else
	CFLAGS += -O3
	REAL_TARGET_DIR := $(TARGET_DIR)/release
endif
SRC_DIR := src
OBJ_DIR := $(REAL_TARGET_DIR)/obj

.DEFAULT_GOAL := all

.PHONY: all
all: server client
	
.PHONY: server
server: target $(OBJ_DIR)/server.o $(OBJ_DIR)/lib.o
	$(CXX) $(filter %.o,$^) -o $(REAL_TARGET_DIR)/$@ $(LDFLAGS)

.PHONY: client
client: target $(OBJ_DIR)/client.o $(OBJ_DIR)/lib.o
	$(CXX) $(filter %.o,$^) -o $(REAL_TARGET_DIR)/$@ $(LDFLAGS)

.PHONY: target
target:
	@mkdir -p $(OBJ_DIR)

.PHONY: clean
clean:
	@rm -rf $(TARGET_DIR)

$(OBJ_DIR)%.o: $(SRC_DIR)%.cpp
	$(CXX) -c $< -o $@ $(CFLAGS)
