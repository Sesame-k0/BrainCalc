apps += CashCalc::App
app_headers += apps/cashcalc/app.h

apps_src += $(addprefix apps/cashcalc/,\
  app.cpp \
  cashcalc_engine.cpp \
  cashcalc_view.cpp \
  cashcalc_controller.cpp \
)

i18n_files += $(call i18n_for_locale,cashcalc/base,universal)

$(eval $(call depends_on_image,apps/cashcalc/app.cpp,apps/cashcalc/cashcalc_icon.png))
