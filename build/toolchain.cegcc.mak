# CeGCC Toolchain for SHARP Brain (Windows Embedded CE 6.0 / ARM926EJ-S Armv5TEJ)
CEGCC_TOOLCHAIN_PREFIX ?= arm-mingw32ce-

CC = $(CEGCC_TOOLCHAIN_PREFIX)gcc
CXX = $(CEGCC_TOOLCHAIN_PREFIX)g++
LD = $(CEGCC_TOOLCHAIN_PREFIX)g++
WINDRES = $(CEGCC_TOOLCHAIN_PREFIX)windres

SFLAGS := $(filter-out -fPIE -Os,$(SFLAGS))
SFLAGS += -O3 -D_USE_MATH_DEFINES -DNOMINMAX -DUNICODE -D_UNICODE
SFLAGS += -march=armv5tej -mcpu=arm926ej-s
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=gnu++17

LDFLAGS += -march=armv5tej -mcpu=arm926ej-s -static -s -lcommctrl -lcommdlg

