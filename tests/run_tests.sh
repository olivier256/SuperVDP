#!/bin/sh
# =========================================================================
# Tous les tests, depuis la racine du dépôt :  tests/run_tests.sh
#
# 1. test_compose : compose_line contre un rendu de référence naïf, sur
#    plus d'un million de lignes (voir tests/test_compose.c).
# 2. Le simulateur complet, sans fenêtre (deux CPU, VBlank, programme
#    68000 de la démo) : la manette suit un scénario, l'image capturée
#    doit être identique, octet pour octet, à l'image de référence
#    calculée par test_compose pour les défilements attendus.
#    Le simulateur tourne en temps réel : environ 20 secondes en tout.
#    Chaque routine d'interruption est attendue jusqu'à son retour
#    (pixel/platform_headless.c) : le résultat ne dépend pas de la charge
#    de la machine.
# =========================================================================
set -e

# Hypothèse matérielle (voir pixel/vdp.h) : vide (vraie 32X) ou bus32
HYPO=${HYPO:-}
BUILD=${BUILD:-build${HYPO:+-$HYPO}}
OUT=$BUILD/tests
mkdir -p "$OUT"

cmake -S . -B "$BUILD" -DSUPERVDP_HYPO="$HYPO" >/dev/null
cmake --build "$BUILD" --target test_compose supervdp_headless >/dev/null

echo "== compose_line"
"$BUILD/test_compose"

# scenario NOM IMAGES MANETTE CAPTURE XA XB [OPTIONS]
#   La manette (DEBUT-FIN:BOUTONS) fait avancer la vue ; la capture est
#   prise une fois la vue arrêtée. xB = (xA x 352 + 224) / 448
#   (bus32, écran de 256 : xB = (xA x 416 + 256) / 512).
#   OPTIONS : passées telles quelles au simulateur (--latency US...).
scenario() {
  name=$1 frames=$2 pad=$3 shot=$4 xa=$5 xb=$6 options=${7:-}
  padopt=""
  for step in $pad; do padopt="$padopt --pad $step"; done
  # shellcheck disable=SC2086
  "$BUILD/supervdp_headless" --frames "$frames" $options $padopt \
    --shot "$shot:$OUT/$name.ppm" 2>"$OUT/$name.log"
  "$BUILD/test_compose" --ppm "$xa" "$xb" 0 0 "$OUT/$name.ref.ppm"
  if cmp -s "$OUT/$name.ppm" "$OUT/$name.ref.ppm"; then
    echo "OK    $name (xA=$xa, xB=$xb)"
  else
    echo "ECHEC $name : $OUT/$name.ppm differe de $OUT/$name.ref.ppm"
    exit 1
  fi
  grep -E "acces bus|cycles|\] derniere image :|\] routines attendues" \
    "$OUT/$name.log" | head -5 | sed 's/^/      /'
}

echo "== simulateur complet${HYPO:+ (hypothese $HYPO)}"
# Le scénario « lent » : hôte très chargé, chaque interruption prise avec
# 100 µs de retard (plus d'une ligne). Le décalage d'écran varie d'une
# ligne à l'autre ici (xA pair, xB impair) : sans attente des routines,
# l'image serait fausse.
if [ "$HYPO" = bus32 ]; then
  scenario depart 120 "" 100 0 0
  scenario impair 140 "10-46:R" 100 37 30
  scenario milieu 200 "10-109:R" 180 100 81
  scenario bout 300 "10-209:RC" 280 512 416
  scenario retour 300 "10-209:RC 220-229:L" 280 502 408
  scenario lent 130 "10-109:R" 120 100 81 "--latency 100"
else
  scenario depart 120 "" 100 0 0
  scenario impair 140 "10-46:R" 100 37 29
  scenario milieu 200 "10-109:R" 180 100 79
  scenario bout 300 "10-209:RC" 280 448 352
  scenario retour 300 "10-209:RC 220-229:L" 280 438 344
  scenario lent 130 "10-109:R" 120 100 79 "--latency 100"
fi

echo "Tous les tests sont passes."
