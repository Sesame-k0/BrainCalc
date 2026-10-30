apps += TimerApp::App
app_headers += apps/timer/app.h

apps_src += $(addprefix apps/timer/,\
  app.cpp \
  timer_controller.cpp \
  timer_view.cpp \
)

i18n_files += $(call i18n_for_locale,timer/base,universal)

$(eval $(call depends_on_image,apps/timer/app.cpp,apps/timer/timer_icon.png))
