CXX ?= c++
CFLAGS += --std=c++23 -Wall -Wextra -pedantic
ifeq ($(DEBUG),true)
	CFLAGS += -g
endif

.DEFAULT_GOAL := all

.PHONY: all
all: server client
	
.PHONY: server
server: target
	@cd ./$@/ && \
	CXX=$(CXX) CFLAGS='$(CFLAGS)' $(MAKE) --no-print-directory
	@mv $@/$@ target/

.PHONY: client
client: target
	@cd ./$@/ && \
	CXX=$(CXX) CFLAGS='$(CFLAGS)' $(MAKE) --no-print-directory
	@mv $@/$@ target/

target:
	@mkdir -p target

.PHONY: clean
clean:
	@rm -rf target
