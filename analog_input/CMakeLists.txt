# Top-level project CMakeLists.txt reference:
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/build-system.html#project-cmakelists-txt
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(potentiometer-to-pwm)
