# Alias so that `make PLATFORM=brain` works identically to `make PLATFORM=simulator TARGET=brain`
PLATFORM = simulator
TARGET = brain
include build/platform.simulator.mak

