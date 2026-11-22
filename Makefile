# Raccourcis ; la compilation elle-même est décrite dans CMakeLists.txt.
.PHONY: all clean rebuild run stats headless test

# Hypothèse matérielle (voir pixel/vdp.h) : vide pour la vraie 32X, ou
# HYPO=bus32. Chaque hypothèse a son propre répertoire de construction.
HYPO ?=
BUILD := build$(if $(HYPO),-$(HYPO))

all:
	cmake -S . -B $(BUILD) -DSUPERVDP_HYPO=$(HYPO)
	cmake --build $(BUILD)

clean:
	cmake -E rm -rf $(BUILD)

rebuild: clean all

# La démo, dans une fenêtre
run: all
	./$(BUILD)/supervdp

# La même, avec les compteurs (toutes les 5 secondes, sur le terminal)
stats: all
	VDP_STATS=1 ./$(BUILD)/supervdp

# Sans fenêtre : 5 secondes, capture de la dernière image
headless: all
	./$(BUILD)/supervdp_headless --frames 300 --pad 10-200:R --shot 299:$(BUILD)/capture.ppm

# Tous les tests (environ 25 secondes)
test:
	HYPO=$(HYPO) BUILD=$(BUILD) tests/run_tests.sh
