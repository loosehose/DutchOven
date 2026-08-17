CC ?= cc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc
BOF_CC ?= $(WINDOWS_CC)
BOF_OBJDUMP ?= x86_64-w64-mingw32-objdump

# Native build: parser tests and dry-run behavior on the development host.
CPPFLAGS := -Iinclude
CFLAGS := -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
SOURCES := src/gate.c src/main.c
TEST_SOURCES := tests/test_gate.c src/gate.c

BUILD_DIR := build
TARGET := $(BUILD_DIR)/dutchoven
TEST_TARGET := $(BUILD_DIR)/dutchoven_tests
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.c=$(BUILD_DIR)/%.test.o)

WINDOWS_BUILD_DIR := build-windows
WINDOWS_TARGET := $(WINDOWS_BUILD_DIR)/dutchoven.exe
WINDOWS_TEST_TARGET := $(WINDOWS_BUILD_DIR)/dutchoven_tests.exe
WINDOWS_OBJECTS := $(SOURCES:%.c=$(WINDOWS_BUILD_DIR)/%.o)
WINDOWS_TEST_OBJECTS := $(TEST_SOURCES:%.c=$(WINDOWS_BUILD_DIR)/%.test.o)
# Windows build: the same core compiled with the live WFP implementation enabled.
WINDOWS_CPPFLAGS := -Iinclude
WINDOWS_CFLAGS := $(CFLAGS) -D_WIN32_WINNT=0x0601 -D__USE_MINGW_ANSI_STDIO=1
WINDOWS_LDLIBS := -lfwpuclnt -lrpcrt4 -ladvapi32

BOF_BUILD_DIR := bof/build
BOF_TARGET := $(BOF_BUILD_DIR)/dutchoven.x64.o
BOF_DIST := bof/dutchoven.x64.o
BOF_SOURCE := bof/dutchoven_bof.c
BOF_HEADERS := bof/include/beacon.h bof/include/bofdefs.h
# BOF build: no CRT, unwind metadata, stack protector, or undeclared imports.
BOF_CPPFLAGS := -Ibof/include -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE
BOF_CFLAGS := -Os -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
	-fno-asynchronous-unwind-tables -fno-builtin -fno-ident -fno-stack-protector

.PHONY: all test check windows bof inspect-bof clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_TARGET): $(TEST_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.test.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: $(TARGET) $(TEST_TARGET)
	$(TEST_TARGET)
	$(TARGET) --app 'C:\Program Files\Contoso\TelemetryAgent.exe' --dry-run

$(WINDOWS_TARGET): $(WINDOWS_OBJECTS)
	@mkdir -p $(@D)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) $^ $(WINDOWS_LDLIBS) -o $@

$(WINDOWS_TEST_TARGET): $(WINDOWS_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) $^ $(WINDOWS_LDLIBS) -o $@

$(WINDOWS_BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(WINDOWS_CC) $(WINDOWS_CPPFLAGS) $(WINDOWS_CFLAGS) -MMD -MP -c $< -o $@

$(WINDOWS_BUILD_DIR)/%.test.o: %.c
	@mkdir -p $(@D)
	$(WINDOWS_CC) $(WINDOWS_CPPFLAGS) $(WINDOWS_CFLAGS) -MMD -MP -c $< -o $@

windows: $(WINDOWS_TARGET) $(WINDOWS_TEST_TARGET)

$(BOF_TARGET): $(BOF_SOURCE) $(BOF_HEADERS)
	@mkdir -p $(@D)
	$(BOF_CC) $(BOF_CPPFLAGS) $(BOF_CFLAGS) -c $(BOF_SOURCE) -o $@

$(BOF_DIST): $(BOF_TARGET)
	cp $(BOF_TARGET) $(BOF_DIST)

bof: $(BOF_DIST)
	bash bof/check_symbols.sh $(BOF_OBJDUMP) $(BOF_DIST)

inspect-bof: $(BOF_DIST)
	$(BOF_OBJDUMP) -t $(BOF_DIST)

check: test windows bof

clean:
	$(RM) -r -- $(BUILD_DIR) $(WINDOWS_BUILD_DIR) $(BOF_BUILD_DIR)

-include $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)
-include $(WINDOWS_OBJECTS:.o=.d) $(WINDOWS_TEST_OBJECTS:.o=.d)
