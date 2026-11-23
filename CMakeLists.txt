cmake_minimum_required(VERSION 3.16)

project(DDS32 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- Simulateur 32X « pixel » ----------------------------------------------
# VDP, interface (COMM, V_INT) et plates-formes, repris de SuperVDP.

set(PIXEL_SOURCES pixel/vdp.c pixel/interface.c)

# Fenêtre : MiniFB (sous-module git). Sans lui, seules les cibles sans
# fenêtre sont construites (tests, captures).
set(HAVE_MINIFB OFF)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/minifb/CMakeLists.txt)
	enable_language(CXX)
	if(APPLE)
		enable_language(OBJC)
	endif()
	set(MINIFB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(MINIFB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory(minifb)
	set(HAVE_MINIFB ON)
else()
	message(STATUS "MiniFB absent (git submodule update --init) : pas de fenetre")
endif()

# ---- Outils ---------------------------------------------------------------
# bmp2c : sprites BMP 8 bits -> C. dds32conv : zone MagicaVoxel -> C.

add_executable(bmp2c tools/bmp2c.c tools/bmpread.c)
target_include_directories(bmp2c PRIVATE dds32 tools)
target_compile_options(bmp2c PRIVATE -Wall -Wextra)

add_executable(dds32conv tools/dds32conv.c tools/bmpread.c dds32/visibility.c)
target_include_directories(dds32conv PRIVATE dds32 tools)
target_compile_options(dds32conv PRIVATE -Wall -Wextra)

# ---- Données de la démo ----------------------------------------------------
# Converties à chaque modification d'une image, de la zone ou des cubes.

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(DEMO_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/assets.txt)
file(GLOB DEMO_BMP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/*.bmp)
set(DEMO_VOX ${CMAKE_CURRENT_SOURCE_DIR}/demo/zone/demo.vox)
set(DEMO_CUBES ${CMAKE_CURRENT_SOURCE_DIR}/demo/zone/cubes.txt)

add_custom_command(
	OUTPUT ${GENERATED}/demo_assets.c ${GENERATED}/demo_assets.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND bmp2c ${DEMO_MANIFEST} ${GENERATED}/demo_assets
	DEPENDS bmp2c ${DEMO_MANIFEST} ${DEMO_BMP}
	COMMENT "Conversion des sprites de la demo"
)
add_custom_command(
	OUTPUT ${GENERATED}/zone_demo.c ${GENERATED}/zone_demo.h
	       ${CMAKE_CURRENT_BINARY_DIR}/apercu_demo.bmp
	COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED}
	COMMAND dds32conv ${DEMO_VOX} ${DEMO_CUBES}
	        ${CMAKE_CURRENT_SOURCE_DIR}/demo/assets demo ${GENERATED}/zone_demo
	        ${CMAKE_CURRENT_BINARY_DIR}/apercu_demo.bmp
	DEPENDS dds32conv ${DEMO_VOX} ${DEMO_CUBES} ${DEMO_BMP}
	COMMENT "Conversion de la zone de la demo (apercu : build/apercu_demo.bmp)"
)

# Contenu de la démo (sprites, zone, jeu) et dessin, partagés par les
# exécutables et les tests
set(DDS32_INCLUDES pixel dds32 demo ${GENERATED})
add_library(demo_content STATIC ${GENERATED}/demo_assets.c
	${GENERATED}/zone_demo.c demo/demo.c dds32/render.c dds32/sprite.c)
target_include_directories(demo_content PUBLIC ${DDS32_INCLUDES})
target_compile_options(demo_content PRIVATE -Wall -Wextra)

# ---- DDS 32 : moteur + démo ------------------------------------------------

set(DDS32_SOURCES
	dds32/engine.c
	dds32/framebuffer.c
	demo/md68k.c
	${PIXEL_SOURCES}
)

# Avec fenêtre
if(HAVE_MINIFB)
	add_executable(dds32 ${DDS32_SOURCES} pixel/platform_minifb.c)
	target_include_directories(dds32 PRIVATE ${DDS32_INCLUDES})
	target_compile_options(dds32 PRIVATE -Wall -Wextra)
	target_link_libraries(dds32 PRIVATE demo_content minifb Threads::Threads)
	set_target_properties(dds32 PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Sans fenêtre : tests et captures (voir pixel/platform_headless.c)
add_executable(dds32_headless ${DDS32_SOURCES} pixel/platform_headless.c)
target_include_directories(dds32_headless PRIVATE ${DDS32_INCLUDES})
target_compile_options(dds32_headless PRIVATE -Wall -Wextra)
target_link_libraries(dds32_headless PRIVATE demo_content Threads::Threads)

# ---- Tests -----------------------------------------------------------------

add_executable(test_visibility tests/test_visibility.c dds32/visibility.c)
target_include_directories(test_visibility PRIVATE ${DDS32_INCLUDES})
target_compile_options(test_visibility PRIVATE -Wall -Wextra)
target_link_libraries(test_visibility PRIVATE demo_content)

add_executable(test_render tests/test_render.c)
target_include_directories(test_render PRIVATE ${DDS32_INCLUDES})
target_compile_options(test_render PRIVATE -Wall -Wextra)
target_link_libraries(test_render PRIVATE demo_content)
