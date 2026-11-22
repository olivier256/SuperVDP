# SuperVDP : des tests qui ne dépendent plus de la charge de la machine

Nov 22, 2026 · @Olivier BROSSERON

Les scénarios du simulateur complet échouaient parfois sans raison apparente : ce n'était pas le moteur, c'était la plate-forme de test. Ce patch rend les captures exactes quelle que soit la charge de la machine, et ajoute un test qui le prouve.

## Le symptôme

De temps en temps, `make test` affichait par exemple `ECHEC milieu : build/tests/milieu.ppm differe de build/tests/milieu.ref.ppm`. On relançait, et le même test passait.

- Le rendu du moteur n'était pas en cause : `test_compose` vérifie plus d'un million de lignes sans jamais échouer.
- L'échec touchait seulement les scénarios du simulateur complet, ceux qui tournent avec deux threads CPU, le VBlank et les interruptions.
- Le journal du scénario qui échouait montrait toujours des H\_INT « en retard », par exemple `master traitees 39853 (en retard 690)`.

## La cause

Le simulateur tourne en temps réel, et la synchro vidéo n'attendait la routine H\_INT du Master que 50 µs. Sur une machine chargée, c'était parfois trop court.

Voici ce qui se passe à chaque ligne affichée :

1. La synchro vidéo affiche la ligne L, avec le décalage d'écran (SFT) en vigueur.
2. Elle lève la H\_INT et envoie un signal au thread du Master.
3. La routine du Master acquitte la H\_INT, puis écrit le SFT de la ligne L + 1.
4. Une ligne plus tard (63,6 µs), la synchro vidéo affiche la ligne L + 1 avec le SFT qu'elle trouve.

Sur la console, l'étape 3 prend quelques cycles. Dans le simulateur, le thread du Master doit d'abord obtenir un cœur de la machine hôte, ce qui peut prendre plus d'une ligne. La ligne L + 1 s'affiche alors avec l'ancien décalage.

Trois défauts se cumulaient :

- **Une attente trop courte** : 50 µs au plus, puis la synchro vidéo continuait.
- **Une attente sur le mauvais signal** : elle s'arrêtait à l'acquittement. Or la routine acquitte au début de l'étape 3, avant d'écrire SFT.
- **La V\_INT pas attendue du tout** : c'est sa routine qui écrit le SFT de la ligne 0.

Pourquoi l'échec était-il rare ? Une routine en retard ne fausse l'image que si le décalage change justement entre ces deux lignes, sur l'image capturée. Dans le scénario « impair » (xA et xB impairs), toutes les lignes ont le même décalage : un retard n'y change rien. Dans « milieu » (xA pair, xB impair), le décalage varie d'une ligne à l'autre : c'est lui qui échouait.

## La correction

La plate-forme des tests attend maintenant que chaque routine d'interruption soit **terminée** avant de continuer le balayage. La fenêtre, elle, garde le temps réel.

| Fichier | Changement |
| --- | --- |
| `pixel/interface.h`, `interface.c` | Nouveau compteur `interface_routines_done()` : les routines V\_INT et H\_INT terminées, comptées au retour de chaque routine. `interface_hblank()` et `interface_raise_vint()` renvoient le nombre de CPU à qui l'interruption a été transmise. |
| `pixel/platform_headless.c` (tests) | Après chaque H\_INT et chaque V\_INT, la synchro vidéo attend la fin des routines lancées. Garde-fou : 1 s, compté comme « abandonnée ». Nouvelle option `--latency US`. |
| `pixel/platform_minifb.c` (fenêtre) | Toujours 50 µs au plus, mais l'attente porte sur la fin de la routine et non plus sur son acquittement. |
| `tests/run_tests.sh` | Nouveau scénario « lent », et le compteur des routines attendues recopié sous chaque scénario. |

L'attente suit ce schéma :

```c
const unsigned long before = interface_routines_done();
waitRoutinesDone(before, interface_hblank(0)); /* ou interface_raise_vint() */
```

Trois choix méritent une explication :

- **Pourquoi un compteur de routines terminées ?** C'est le seul signal qui garantit que SFT est écrit. L'acquittement arrive trop tôt, au début de la routine.
- **Pourquoi une barrière mémoire ?** Le compteur est incrémenté en « release » et lu en « acquire ». Sur un Mac à processeur ARM, sans elle, la synchro vidéo pourrait voir le compteur avancer avant de voir la nouvelle valeur de SFT.
- **Pourquoi pas de limite de temps ?** L'image ne dépend que de l'ordre des événements, pas de l'heure. Quand le balayage attend, il prend du retard sur l'horloge, puis le rattrape : les lignes suivantes partent aussitôt. Les captures, la manette du scénario et les défilements comptent en images, pas en secondes.

La fenêtre garde volontairement le temps réel : c'est là qu'on regarde la démo tourner à 60 images/s. Sur une machine très chargée, une ligne peut encore y être mal décalée, et c'est compté dans « en retard ».

## La preuve

Un échec rare ne se démontre pas en relançant les tests : avec les deux cœurs saturés, l'ancienne version a passé 8 essais sur 8, malgré 1 216 H\_INT en retard. L'option `--latency US` rend le problème reproductible : chaque interruption est prise avec ce retard, comme sur un hôte très chargé.

Scénario « milieu » (xA = 100, xB = 79), image 180 comparée à la référence :

| Latence imposée | Avant le patch | Après le patch |
| --- | --- | --- |
| aucune | juste (échec rare, au hasard) | juste, 3 s |
| 30 µs | fausse : 20 860 octets différents | juste, 6 s |
| 100 µs | fausse : 40 937 octets différents | juste, 9 s |

Pour la colonne « avant », l'option a été ajoutée temporairement à l'ancienne version, et seulement elle.

Ce cas devient un test permanent : le scénario « lent » de `run_tests.sh` reprend « milieu » avec `--latency 100`. Il passe avec 27 220 routines servies en plus d'une ligne. Si une future modification cassait l'attente des routines, il échouerait à tous les coups, et non plus une fois sur dix.

Le reste ne change pas : mêmes statistiques d'écritures et de cycles, plus aucune H\_INT « en retard » dans les scénarios. La suite complète passe de 23 à 26 s environ, à cause du nouveau scénario.

## Appliquer les patches

Il y a deux patches pour la même correction : un pour SuperVDP (`main`), un pour la branche `hypo-bus32`. Ils contiennent le même code ; seuls `run_tests.sh` et `VERSION.md` diffèrent, parce que les patches bus32 avaient déjà modifié ces deux fichiers.

| Patch | Dépôt local | Branche | Prérequis |
| --- | --- | --- | --- |
| `0001-captures-independantes-de-la-charge.patch` | `~/Documents/sega/SuperVDP` | `main` | aucun |
| `0003-bus32-captures-independantes-de-la-charge.patch` | `~/Documents/sega/SuperVDP-bus32` | `hypo-bus32` | patches bus32 1 et 2 déjà appliqués |

Pour SuperVDP :

```sh
cd ~/Documents/sega/SuperVDP
git switch main
git am ~/Downloads/0001-captures-independantes-de-la-charge.patch
make test
git push
```

Pour la branche bus32 :

```sh
cd ~/Documents/sega/SuperVDP-bus32
git switch hypo-bus32
git am ~/Downloads/0003-bus32-captures-independantes-de-la-charge.patch
make test
make HYPO=bus32 test
git push
```

Dans les deux cas, la liste des scénarios se termine maintenant par `OK    lent`. En bus32, ce scénario utilise xB = 81 au lieu de 79, puisque l'écran fait 256 pixels de large.

Si vous rebasez un jour `hypo-bus32` sur `main`, git trouvera la correction des deux côtés. En cas de conflit dans `run_tests.sh`, gardez la version de la branche bus32 : elle contient les deux listes de scénarios.

## Lire les nouveaux compteurs

Sous chaque scénario, `run_tests.sh` affiche une ligne de plus, par exemple :

```
[image 180] routines attendues plus d'une ligne 184, abandonnees 0
```

- **Routines attendues plus d'une ligne** : le nombre de fois où la routine a mis plus de 63,6 µs à se terminer. Chacune est un cas où l'ancienne attente aurait pu fausser l'image. C'est une mesure de la charge de votre machine, pas une erreur : de 90 à 400 par scénario sur 2 cœurs, sans doute moins sur votre Mac.
- **Abandonnées** : des attentes arrêtées au bout d'une seconde. Doit rester à 0. Une valeur non nulle signale une vraie faute du jeu : une routine qui ne revient pas, ou des interruptions restées masquées.
- **En retard** (ligne des H\_INT, dans les journaux `build/tests/*.log`) : doit maintenant rester à 0 dans les tests.

Deux limites restent connues :

- **Machine très surchargée** : avec beaucoup plus de processus actifs que de cœurs, chaque attente cède le processeur pour plusieurs millisecondes, et les tests deviennent très lents. Ils restent justes.
- **La fenêtre** (`make run`, `make stats`) garde le temps réel : une ligne mal décalée reste possible si la machine est très chargée, et elle est comptée dans « en retard ».
