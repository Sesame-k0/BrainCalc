# SHARP Brain (Windows Embedded CE 6.0) Ion Platform Makefile

ion_src += $(addprefix ion/src/simulator/brain/, \
  main.cpp \
  display.cpp \
  framebuffer.cpp \
  keyboard.cpp \
  events.cpp \
  timing.cpp \
  rtc.cpp \
  random.cpp \
  clipboard.cpp \
  console.cpp \
)

ion_src += ion/src/shared/collect_registers.cpp

# Remove SDL-dependent simulator files added by ion/src/simulator/Makefile
sdl_simu_needs_to_be_removed += $(addprefix ion/src/simulator/shared/, \
  main.cpp \
  clipboard.cpp \
  console.cpp \
  rtc.cpp \
  display.cpp \
  events.cpp \
  events_platform.cpp \
  framebuffer.cpp \
  keyboard.cpp \
  layout.cpp \
  random.cpp \
  timing.cpp \
  window.cpp \
  actions.cpp \
  state_file.cpp \
)

ion_src := $(filter-out $(sdl_simu_needs_to_be_removed),$(ion_src))

SFLAGS := $(filter-out -Iion/src/simulator/external/sdl/include,$(SFLAGS))
SFLAGS += -D NOMINMAX -D UNICODE -D _UNICODE

ifeq ($(TOOLCHAIN),windows)
LDFLAGS += -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -s
endif

# Convenience target to generate Brain app bundle (AppMain.exe, AppMain.cfg, index.din)
.PHONY: AppMain.exe brain_bundle
AppMain.exe: $(BUILD_DIR)/AppMain.exe
brain_bundle: $(BUILD_DIR)/AppMain.exe $(BUILD_DIR)/AppMain.cfg $(BUILD_DIR)/index.din

$(BUILD_DIR)/AppMain.exe: $(BUILD_DIR)/epsilon.$(EXE)
	@echo "COPY    $@"
	$(Q) cp $< $@

$(BUILD_DIR)/AppMain.cfg: | $(BUILD_DIR)/.
	@echo "TOUCH   $@"
	$(Q) touch $@

$(BUILD_DIR)/index.din: | $(BUILD_DIR)/.
	@echo "TOUCH   $@"
	$(Q) touch $@

