idf_component_register(
   SRCS
      "TPS65185.c" "STC3115.c" "main.c" "tca6408.c" "kxtj3_1057.c"
      "rv3028.c" "esp_hosted_wifi.c" "sd_card_functions.c" "web_server.c" "epd_functions.c"
   EMBED_TXTFILES
      "index.html" "matrix_editor.html"
   INCLUDE_DIRS "."
)