# CeGCC Toolchain for SHARP Brain (Windows Embedded CE 6.0 / ARM926EJ-S Armv5TEJ)
CEGCC_TOOLCHAIN_PREFIX ?= arm-mingw32ce-

CC = $(CEGCC_TOOLCHAIN_PREFIX)gcc
CXX = $(CEGCC_TOOLCHAIN_PREFIX)g++
LD = $(CEGCC_TOOLCHAIN_PREFIX)g++
WINDRES = $(CEGCC_TOOLCHAIN_PREFIX)windres

SFLAGS := $(filter-out -fPIE -Os,$(SFLAGS))
SFLAGS += -O3 -D_USE_MATH_DEFINES -D_GLIBCXX_USE_C99_MATH=1 -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -DNOMINMAX -DUNICODE -D_UNICODE
SFLAGS += -march=armv5tej -mcpu=arm926ej-s -mno-unaligned-access
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=gnu++17

LDFLAGS += -march=armv5tej -mcpu=arm926ej-s -Wl,--stack,0x400000 -static -s -lcommctrl -lcommdlg -lm

initializer_list = $(shell printf '%s' "$(1)" | od -An -tx1 | sed 's/ *\([0-9a-f][0-9a-f]\)/0x\1,/g' | tr -d '\n ')0
