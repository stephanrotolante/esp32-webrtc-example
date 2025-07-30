cmake_minimum_required(VERSION 3.18)


add_compile_definitions(WIFI_SSID="")
add_compile_definitions(WIFI_PASSWORD="")
add_compile_definitions(WHIP_URL="http://192.168.1.201:8080/whip")

set(EXTRA_COMPONENT_DIRS 
    "/app/deps/srtp" 
    "/app/components/peer"
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
