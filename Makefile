# DataGuard 构建脚本（w64devkit + MinGW Make）
# 目标：all / gtest / test / test-py / lint / clean

CXX       := E:/w64devkit/bin/g++.exe
AR        := E:/w64devkit/bin/ar.exe
PYTHON    := E:/anaconda3/python.exe
GTEST_SRC := E:/googletest-main/googletest-main/googletest
GTEST_INC := $(GTEST_SRC)/include

BUILD   := build
OBJ     := $(BUILD)/obj
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude -DUNICODE -D_UNICODE
LDFLAGS  := -static -ladvapi32 -luser32

CORE_SRCS := \
    src/core/path_utils.cpp \
    src/core/filter.cpp \
    src/core/scanner.cpp \
    src/archive/crc32.cpp \
    src/win/win_metadata.cpp

CORE_OBJS := $(patsubst src/%.cpp,$(OBJ)/%.o,$(CORE_SRCS))

TEST_SRCS := \
    tests/test_crc32.cpp \
    tests/test_filter.cpp \
    tests/test_scanner.cpp

TEST_BINS := $(patsubst tests/%.cpp,$(BUILD)/tests/%.exe,$(TEST_SRCS))

.PHONY: all gtest test test-py lint clean

all: $(BUILD)/dataguard-cli.exe

# ---------- 核心库 ----------
$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/dataguard-cli.exe: $(CORE_OBJS) $(OBJ)/cli/main.o
	@mkdir -p $(dir $@)
	$(CXX) $^ -o $@ $(LDFLAGS)
	@echo "[OK] $@"

# ---------- gtest（源码直编） ----------
gtest: $(BUILD)/libgtest.a

$(BUILD)/libgtest.a: $(GTEST_SRC)/src/gtest-all.cc $(GTEST_SRC)/src/gtest_main.cc
	@mkdir -p $(BUILD)/gtest
	$(CXX) $(CXXFLAGS) -I$(GTEST_INC) -I$(GTEST_SRC) -c $(GTEST_SRC)/src/gtest-all.cc -o $(BUILD)/gtest/gtest-all.o
	$(CXX) $(CXXFLAGS) -I$(GTEST_INC) -I$(GTEST_SRC) -c $(GTEST_SRC)/src/gtest_main.cc -o $(BUILD)/gtest/gtest_main.o
	$(AR) rcs $@ $(BUILD)/gtest/gtest-all.o $(BUILD)/gtest/gtest_main.o
	@echo "[OK] $@"

# ---------- 单元测试 ----------
$(BUILD)/tests/%.exe: tests/%.cpp $(CORE_OBJS) $(BUILD)/libgtest.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(GTEST_INC) $< $(CORE_OBJS) $(BUILD)/libgtest.a -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "===== run $$t ====="; \
	    ./$$t || exit 1; \
	done
	@echo "[ALL TESTS PASSED]"

test-py:
	$(PYTHON) -m unittest discover -s tests/py -v

lint:
	$(PYTHON) -m cpplint --linelength=120 --recursive include src tests 2>&1 || true

clean:
	rm -rf $(BUILD)
