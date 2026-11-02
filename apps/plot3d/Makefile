apps += Plot3D::App
app_headers += apps/plot3d/app.h

apps_src += $(addprefix apps/plot3d/,\
  app.cpp \
  render/fixed_math.cpp \
  render/rasterizer.cpp \
  plot/plot_view.cpp \
  plot/plot_controller.cpp \
  list/list_controller.cpp \
  list/type_menu_controller.cpp \
)

i18n_files += $(call i18n_for_locale,plot3d/base,universal)

$(eval $(call depends_on_image,apps/plot3d/app.cpp,apps/plot3d/plot3d_icon.png))
