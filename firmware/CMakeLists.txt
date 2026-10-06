cmake_minimum_required(VERSION 3.16)
 
# NODE_ID / TOTAL_NODES TIDAK di-set lewat -D command line lagi (cara itu
# terbukti tidak reliable dengan cache CMake ESP-IDF). Sekarang diatur
# lewat Kconfig - lihat main/Kconfig.projbuild dan sdkconfig.node0 /
# sdkconfig.node1 di folder ini, dipakai dengan flag -D SDKCONFIG_DEFAULTS
# saat build (lihat README.md).
 
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aether_node)
 