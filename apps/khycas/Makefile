apps += Khycas::App
app_headers += apps/khycas/app.h

khycas_app_src := $(addprefix apps/khycas/,\
  app.cpp \
  khycas_controller.cpp \
  khycas_view.cpp \
  khycas_port.cpp \
)
apps_src += $(khycas_app_src)

libtommath_src = $(wildcard apps/khycas/libtommath/*.c)
apps_src += $(libtommath_src)

# Full Giac core sources
giac_src = $(addprefix apps/khycas/giac/,\
  first.cc \
  tinymt32.cc \
  identificateur.cc \
  gen.cc \
  unary.cc \
  usual.cc \
  sym2poly.cc \
  gausspol.cc \
  threaded.cc \
  maple.cc \
  ti89.cc \
  mathml.cc \
  moyal.cc \
  misc.cc \
  permu.cc \
  quater.cc \
  desolve.cc \
  input_parser.cc \
  symbolic.cc \
  index.cc \
  modpoly.cc \
  modfactor.cc \
  ezgcd.cc \
  derive.cc \
  solve.cc \
  intg.cc \
  intgab.cc \
  risch.cc \
  lin.cc \
  series.cc \
  subst.cc \
  vecteur.cc \
  sparse.cc \
  csturm.cc \
  tex.cc \
  global.cc \
  ifactor.cc \
  alg_ext.cc \
  gauss.cc \
  isom.cc \
  help.cc \
  plot.cc \
  plot3d.cc \
  rpn.cc \
  prog.cc \
  pari.cc \
  cocoa.cc \
  input_lexer.cc \
  kdisplay.cc \
  kadd.cc \
)
apps_src += $(giac_src)

$(call object_for,$(khycas_app_src)): SFLAGS += -Iapps/khycas -DNUMWORKS -DKHICAS
$(call object_for,$(giac_src) $(libtommath_src)): SFLAGS += -Iapps/khycas -Iapps/khycas/giac -Iapps/khycas/libtommath -DNUMWORKS -DKHICAS -DHAVE_CONFIG_H -DIN_GIAC -DTIMEOUT -DNO_STDEXCEPT -DGIAC_BINARY_ARCHIVE -DNO_UNARY_FUNCTION_COMPOSE -DNO_TEMPLATE_MULTGCD -DGIAC_NO_OPTIMIZATIONS -DSTATIC_BUILTIN_LEXER_FUNCTIONS -DUSE_GMP_REPLACEMENTS -DNO_PHYSICAL_CONSTANTS -w -Os
ifeq ($(TOOLCHAIN),cegcc)
$(call object_for,$(giac_src) $(libtommath_src)): SFLAGS += -Iapps/khycas/wince_compat -DGNUWINCE -D_WIN32_WCE
endif
# Optimize Giac and LibTomMath for size (-Os) to reduce binary footprint on WinCE
# CeGCC GCC 9.3.0 emits non-COMDAT C4/D4 ctor/dtor symbols under -Os -fdeclone-ctor-dtor
$(call object_for,$(giac_src)): CXXFLAGS += -frtti -fpermissive
ifeq ($(TOOLCHAIN),cegcc)
$(call object_for,$(giac_src)): CXXFLAGS += -fno-declone-ctor-dtor
endif

i18n_files += $(call i18n_for_locale,khycas/base,universal)

$(eval $(call depends_on_image,apps/khycas/app.cpp,apps/khycas/khycas_icon.png))
