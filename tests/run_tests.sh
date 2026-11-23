#!/bin/sh
# =========================================================================
# Tous les tests, depuis la racine du dépôt :  tests/run_tests.sh
#
# 1. test_visibility : la mosaïque calculée par visibility_build contre un
#    lancer de rayons (zone de la démo et 300 zones au hasard).
# 2. test_render : le dessin des lignes (décor, caisse, recouvrements)
#    contre un rendu de référence naïf, sur 5 000 écrans environ.
# 3. Le simulateur complet, sans fenêtre (deux CPU, VBlank, programme
#    68000 de la démo) : la manette suit un scénario, l'image capturée
#    doit être identique, octet pour octet, à l'image de référence
#    calculée par test_render pour la position attendue de la caméra et
#    de la caisse. Le simulateur tourne en temps réel : une vingtaine de
#    secondes en tout. Chaque routine d'interruption est attendue jusqu'à
#    son retour (pixel/platform_headless.c) : le résultat ne dépend pas de
#    la charge de la machine.
# =========================================================================
set -e

BUILD=${BUILD:-build}
OUT=$BUILD/tests
mkdir -p "$OUT"

cmake -S . -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target test_visibility test_render dds32_headless \
  >/dev/null

# Tests compilés avec AddressSanitizer et UBSan (voir CMakeLists.txt) ;
# les outils de conversion ne libèrent pas leur mémoire en sortant : pas
# de recherche de fuites.
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}

echo "== test_visibility"
"$BUILD/test_visibility"
echo "== test_render"
"$BUILD/test_render"

# scenario NOM IMAGES MANETTE CAPTURE CAMX CAMY U V W [OPTIONS]
#   La manette (DEBUT-FIN:BOUTONS, bornes comprises) déplace la caisse,
#   depuis (48, 96, 32) : une unité par image, quatre avec C. La capture
#   est prise une fois la caisse arrêtée en (U, V, W), la caméra en
#   (CAMX, CAMY). OPTIONS : passées telles quelles au simulateur.
scenario() {
  name=$1 frames=$2 pad=$3 shot=$4 cx=$5 cy=$6 u=$7 v=$8 w=$9
  shift 9
  options=${1:-}
  padopt=""
  for step in $pad; do padopt="$padopt --pad $step"; done
  # shellcheck disable=SC2086
  "$BUILD/dds32_headless" --frames "$frames" $options $padopt \
    --shot "$shot:$OUT/$name.ppm" 2>"$OUT/$name.log"
  "$BUILD/test_render" --ppm "$cx" "$cy" "$u" "$v" "$w" "$OUT/$name.ref.ppm"
  if cmp -s "$OUT/$name.ppm" "$OUT/$name.ref.ppm"; then
    echo "OK    $name (camera $cx, $cy ; caisse $u, $v, $w)"
  else
    echo "ECHEC $name : $OUT/$name.ppm differe de $OUT/$name.ref.ppm"
    exit 1
  fi
}

echo "== simulateur complet"
scenario depart 60 "" 50 128 56 48 96 32
scenario droite 100 "10-49:R" 90 168 76 88 96 32
# Rapide (4 unités par image), caméra à une abscisse impaire (SFT)
scenario rapide 120 "10-40:RC 50-80:DC" 110 173 157 172 175 32
# Sur le pont, au-dessus de la rivière (une marche plus haut)
scenario pont 200 "10-41:R 50-153:D" 190 56 108 80 200 48
# Sous l'arche : le linteau passe devant la caisse
scenario arche 200 "10-153:R" 190 272 128 192 96 32
# La marche, puis le plateau (deux niveaux plus haut)
scenario plateau 420 "10-73:D 80-319:R 330-360:U" 410 335 160 288 129 64
# Hôte très chargé : chaque interruption prise avec 100 µs de retard
scenario lent 100 "10-49:R" 90 168 76 88 96 32 "--latency 100"

# Mesures : l'écran entier est redessiné à chaque image ; celle du
# scénario « pont » (caisse visible, caméra au milieu de la zone)
echo "== mesures (scenario pont, image 190)"
grep -E "^\[image 190\] (acces bus|cycles CPU)" "$OUT/pont.log" |
  sed 's/^/   /'

echo "Tous les tests sont passes."
