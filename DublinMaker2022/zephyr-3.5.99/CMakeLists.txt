# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.cpp src/display.cpp src/controller.cpp src/console.cpp src/sound.cpp src/invaders.cpp src/sprite.cpp src/brick.cpp src/brici.cpp src/mesh.c src/battleship.cpp src/messaging.cpp src/frame_buffer.cpp src/menu.cpp src/osk.cpp src/realm.cpp  )
