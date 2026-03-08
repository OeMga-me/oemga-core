cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oemga_portal2)
target_sources(app PRIVATE src/main.c src/i2c_driver.c src/lsm6dsox_reg.c)
