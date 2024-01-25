CXX ?= c++
CFLAGS += -Wall -Wextra -pedantic

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
