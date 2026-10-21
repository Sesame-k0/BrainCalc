# SHARP Brain (Windows CE 6.0 / ARM926EJ-S) Simulator/Port Target
# By default, use CeGCC (arm-mingw32ce-g++) if available, or allow overriding TOOLCHAIN=windows for host testing.
CEGCC_CHECK := $(shell command -v arm-mingw32ce-g++ 2> /dev/null)
ifdef CEGCC_CHECK
TOOLCHAIN ?= cegcc
else
TOOLCHAIN ?= cegcc
endif

EXE = exe
EPSILON_GETOPT = 0
EPSILON_TELEMETRY ?= 0
HOME_DISPLAY_EXTERNALS = 0

SFLAGS := $(filter-out -fPIE,$(SFLAGS))
SFLAGS += -DNOMINMAX -DUNICODE -D_UNICODE -DTARGET_BRAIN=1
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=gnu++17

