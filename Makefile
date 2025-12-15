BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python3
VENV_DIR ?= .venv
REQ_FILE ?= requirements.txt
CLANG_FORMAT ?= clang-format-20
FORMAT_DIRS ?= src tests

ifeq ($(OS),Windows_NT)
  VENV_PYTHON := $(VENV_DIR)/Scripts/python.exe
else
  VENV_PYTHON := $(VENV_DIR)/bin/python3
endif

VENV_STAMP := $(VENV_DIR)/.stamp

.PHONY: all configure compiler clean test venv format

all: compiler

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

compiler: configure
	$(CMAKE) --build $(BUILD_DIR) --target dhad

test: configure
	$(CMAKE) --build $(BUILD_DIR) --target tokenize_ex1 parse_ex1 codegen_ex1
	cd $(BUILD_DIR) && ctest --output-on-failure

venv: $(VENV_STAMP)

$(VENV_STAMP): $(REQ_FILE)
	$(PYTHON) -m venv $(VENV_DIR)
	$(VENV_PYTHON) -m pip install --upgrade pip
	$(VENV_PYTHON) -m pip install -r $(REQ_FILE)
	touch $(VENV_STAMP)

format:
	@command -v $(CLANG_FORMAT) >/dev/null || \
	  (echo "clang-format not found: $(CLANG_FORMAT)"; exit 1)
	find $(FORMAT_DIRS) -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | \
	  xargs -0 $(CLANG_FORMAT) -i

clean:
	rm -rf $(BUILD_DIR) $(VENV_DIR)
