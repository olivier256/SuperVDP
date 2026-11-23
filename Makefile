# Raccourcis ; la compilation elle-même est décrite dans CMakeLists.txt.
.PHONY: all clean rebuild run stats headless test

all:
	cmake -S . -B build
	cmake --build build

clean:
	cmake -E rm -rf build

rebuild: clean all

# La démo, dans une fenêtre (il faut MiniFB : voir README.md)
run: all
	@test -x build/dds32 || { echo "Pas de fenetre : MiniFB absent. Une fois : git submodule update --init"; exit 1; }
	./build/dds32

# La même, avec les compteurs (toutes les 5 secondes, sur le terminal)
stats: all
	@test -x build/dds32 || { echo "Pas de fenetre : MiniFB absent. Une fois : git submodule update --init"; exit 1; }
	VDP_STATS=1 ./build/dds32

# Sans fenêtre : 5 secondes, capture de la dernière image
headless: all
	./build/dds32_headless --frames 300 --pad 10-110:R --pad 120-200:D --shot 299:build/capture.ppm

# Tous les tests (environ 25 secondes)
test:
	tests/run_tests.sh
