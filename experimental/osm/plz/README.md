# Experiment: PLZ-Zuordnung aus OpenStreetMap-Polygonen

> **Historischer Vergleichsstand:** Dieses Experiment verwendet den
> `yetzt/postleitzahlen`-Release 2026.02. Eine aktuelle, unmittelbar aus dem
> lokalen OSM-PBF-Abzug erzeugte und von diesem Drittprojekt unabhängige
> Polygonsammlung liegt unter
> [`../data/2026-08-30/`](../data/2026-08-30/QUELLE-UND-LIZENZ.md). Der
> zugehörige freestanding C-Exporter ist unter
> [`../pbf-parser/`](../pbf-parser/README.md#postal-code-polygons)
> dokumentiert.

## Aktueller unabhängiger Stand 2026-08-30

Das Zuordnungswerkzeug kann inzwischen auch die unmittelbar mit `osm-postal`
erzeugte GeoJSON-Sammlung lesen. Der vollständige Lauf gegen
`../data/2026-08-30/postleitzahlen.geojson` liegt unter `2026-08-30/`:

| Kennzahl | Datensätze |
|---|---:|
| insgesamt | 23.252.486 |
| genau eine aktuelle OSM-PLZ | 23.252.435 |
| kein Polygon | 43 |
| mehrere PLZ | 8 |
| amtliche PLZ unverändert erhalten | 8.528.354 |
| OSM-PLZ bei zuvor leerem Feld ergänzt | 14.724.111 |
| weiterhin ohne PLZ | **21** |

Dieser Stand ist die PLZ-Eingabe für `libreHKDE`. Die historische
`yetzt`-Auswertung weiter unten bleibt als Vergleich erhalten.

Dieses Experiment ordnet den Punkten des konsolidierten Bundesadressbestands
Postleitzahlen aus OSM-Polygonen zu. Es verändert den amtlichen Bestand unter
`adressen/bund/konsolidiert/` nicht.

## Angereicherte Adressdatei

Die direkt nutzbare
[`adressen.csv.gz`](adressen.csv.gz) im Verzeichnis `experimental/osm/plz/`
besitzt dasselbe 14-spaltige Schema wie der konsolidierte Bundesbestand.

Für ihre PLZ gilt strikt:

1. Eine vorhandene amtliche PLZ bleibt unverändert.
2. Nur bei leerer amtlicher PLZ und genau einer OSM-Polygon-PLZ wird diese
   OSM-PLZ ergänzt.
3. Bei keinem Polygon oder mehreren unterschiedlichen PLZ bleibt das Feld
   leer.
4. Bei einer OSM-Ergänzung wird auch `vollstaendige_adresse` mit der ergänzten
   PLZ neu aufgebaut.
5. `datensatznummer`, Koordinaten und alle übrigen fachlichen Felder bleiben
   erhalten.

Ergebnis:

| Kennzahl | Datensätze |
|---|---:|
| insgesamt | 23.252.486 |
| amtliche PLZ unverändert erhalten | 8.528.354 |
| eindeutige OSM-PLZ ergänzt | 14.723.718 |
| insgesamt mit PLZ | **23.252.072** |
| weiterhin ohne PLZ | **414** |
| PLZ-Abdeckung | **99,99822 %** |

Die Herkunft jedes Werts ist über `bund_id` und
[`2026-02/plz-zuordnung.csv.gz`](2026-02/plz-zuordnung.csv.gz)
nachvollziehbar. In jener Datei stehen `amtliche_plz`, `osm_plz`,
OSM-Relationen, Polygonanzahl und Lageklasse nebeneinander.

Prüfsummen und Statistiken:

- [`adressen-export-statistik.txt`](adressen-export-statistik.txt)
- [`adressen-statistik.json`](adressen-statistik.json)
- [`adressen-validierung.json`](adressen-validierung.json)

Reproduktion aus dem konsolidierten Bundesbestand und der bereits erzeugten
OSM-Zuordnung:

```sh
bash experimental/osm/plz/tools/build-enriched.sh
```

Der Build prüft die zeilengleiche `bund_id`-Zuordnung, erhält amtliche PLZ,
sortiert die Ausgabe mit `LC_ALL=C`, komprimiert mit `gzip -n -9` und
validiert anschließend Schema, Datensatzzahlen, PLZ, Anzeigeadressen,
Koordinaten und Gzip-Reproduzierbarkeit.

## Ergebnis

Verwendet wurde der Release **2026.02** des Projekts
[`yetzt/postleitzahlen`](https://github.com/yetzt/postleitzahlen/releases/tag/2026.02).
Er enthält 8.176 aus OpenStreetMap extrahierte
`boundary=postal_code`-Geometrien.

| Kennzahl | Ergebnis |
|---|---:|
| untersuchte Adressobjekte | 23.252.486 |
| genau eine OSM-PLZ | 23.252.042 |
| kein PLZ-Polygon | 437 |
| mehrere unterschiedliche PLZ | 7 |
| amtliche PLZ als Kontrollmenge | 8.528.354 |
| übereinstimmend | 8.509.485 |
| widersprüchlich | 18.839 |
| kein Polygon in der Kontrollmenge | 29 |
| mehrere PLZ in der Kontrollmenge | 1 |
| Abdeckung der Kontrollmenge mit genau einer OSM-PLZ | 99,99965 % |
| Übereinstimmung bei eindeutiger OSM-Zuordnung | **99,77910 %** |

Von **14.724.132** Adressen ohne amtliche PLZ erhalten **14.723.718**
einen eindeutigen OSM-Kandidaten. **414** bleiben ohne eindeutigen Kandidaten.
Das entspricht einer Kandidatenabdeckung von **99,99719 %**.

Die Ergebnisse sind sehr gut, bleiben aber experimentell: Ein OSM-Polygon ist
keine amtliche postalische Bestätigung, und PLZ für Postfächer,
Großempfänger oder andere nicht flächenhafte Sonderfälle werden durch
Gebietspolygone nicht vollständig modelliert.

## Dateien

| Datei | Inhalt |
|---|---|
| `2026-02/plz-zuordnung.csv.gz` | Zuordnung für alle 23.252.486 Adressen |
| `2026-02/auswertung.json` | Gesamt- und Länderstatistik |
| `2026-02/validierung.json` | technische Vollprüfung |
| `2026-02/abweichungen.csv.gz` | 19.283 Widersprüche, Lücken und Mehrdeutigkeiten |
| `2026-02/abweichungsanalyse.json` | Abweichungen nach Land und PLZ-Paar |
| `2026-02/export-statistik.txt` | Zeilenzahlen und SHA-256-Prüfsummen |
| `2026-02/source/` | unveränderter OSM-Polygonbestand, Release-Metadaten und Lizenz |
| `tools/` | reproduzierbare Zuordnungs- und Prüfpipeline |
| `adressen.csv.gz` | Bundesadressbestand mit amtlichen und ergänzten OSM-PLZ |

`plz-zuordnung.csv.gz` enthält:

1. `bund_id`
2. `bundesland`
3. `amtliche_plz`
4. `osm_plz`
5. `osm_relationen`
6. `polygon_treffer`
7. `unterschiedliche_plz`
8. `lageklasse`
9. `validierung`

`osm_plz` bleibt leer, wenn kein Polygon oder mehrere unterschiedliche
PLZ-Polygone gefunden wurden. Die OSM-Relationen bleiben für Audits erhalten.

## Methode

1. Das Brotli-komprimierte TopoJSON wird ohne verlustbehaftete
   Geometrieänderung dekodiert.
2. Polygon- und Multipolygon-Geometrien einschließlich innerer Ringe werden
   rekonstruiert.
3. Ein grobes 0,1-Grad-Raster indexiert die Polygon-Bounding-Boxes.
4. Ein feines 0,01-Grad-Raster markiert konservativ jede Zelle, deren
   Bounding-Box von einem Polygonsegment geschnitten werden kann.
5. Punkte in Grenzzellen werden individuell per Point-in-Polygon geprüft.
6. Nur Zellen ohne mögliche Grenzlinie verwenden ein gecachtes Ergebnis.
7. Mehrere Relationen mit derselben PLZ wären zulässig; mehrere
   unterschiedliche PLZ werden als mehrdeutig behandelt.
8. Die Zuordnung wird zeilengleich über `bund_id` gegen den Bundesbestand
   validiert.

Das feine Raster ist keine geometrische Näherung für die PLZ-Zuordnung. Es
entscheidet nur, ob das exakte Point-in-Polygon-Ergebnis einer sicheren
Innenzelle wiederverwendet werden darf.

### Lageklassen

- `innenzelle`: In der 0,01-Grad-Zelle liegt keine mögliche Polygongrenze.
- `grenzzelle`: Die Zelle kann eine Polygongrenze enthalten; der konkrete
  Punkt wurde deshalb individuell geprüft.

In der amtlichen Kontrollmenge beträgt die Übereinstimmung bei eindeutiger
Zuordnung:

- Innenzellen: **99,91945 %**
- Grenzzellen: **99,56828 %**

Die Lageklasse ist bewusst grob und kein Abstand zur tatsächlichen Grenze.

## Ergebnisse nach Land

`amtlich`, `stimmt` und `Widerspruch` sind nur für Länder mit PLZ in der
verwendeten amtlichen Quelle befüllt.

| Land | Adressen | eindeutige OSM-PLZ | kein Polygon | mehrere PLZ | amtlich | stimmt | Widerspruch | Genauigkeit eindeutig |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| BB | 868.435 | 868.435 | 0 | 0 | 867.290 | 867.054 | 236 | 99,97279 % |
| BE | 394.456 | 394.455 | 0 | 1 | 394.303 | 393.668 | 634 | 99,83921 % |
| BW | 3.387.256 | 3.387.249 | 6 | 1 | 0 | - | - | - |
| BY | 3.761.180 | 3.760.785 | 394 | 1 | 0 | - | - | - |
| HB | 176.527 | 176.527 | 0 | 0 | 176.374 | 174.815 | 1.559 | 99,11608 % |
| HE | 1.632.549 | 1.632.545 | 0 | 4 | 0 | - | - | - |
| HH | 303.414 | 303.414 | 0 | 0 | 302.408 | 300.971 | 1.437 | 99,52481 % |
| MV | 517.422 | 517.418 | 4 | 0 | 0 | - | - | - |
| NI | 2.709.334 | 2.709.331 | 3 | 0 | 0 | - | - | - |
| NW | 4.516.958 | 4.516.954 | 4 | 0 | 4.509.538 | 4.498.960 | 10.575 | 99,76550 % |
| RP | 1.407.373 | 1.407.373 | 0 | 0 | 0 | - | - | - |
| SH | 955.211 | 955.186 | 25 | 0 | 953.360 | 950.962 | 2.373 | 99,75108 % |
| SL | 335.181 | 335.181 | 0 | 0 | 334.991 | 334.711 | 280 | 99,91642 % |
| SN | 993.939 | 993.938 | 1 | 0 | 990.090 | 988.344 | 1.745 | 99,82375 % |
| ST | 667.116 | 667.116 | 0 | 0 | 0 | - | - | - |
| TH | 626.135 | 626.135 | 0 | 0 | 0 | - | - | - |

Die Differenz zwischen `amtlich` und der Summe aus `stimmt` und
`Widerspruch` sind amtliche Kontrollpunkte ohne genau eine OSM-PLZ.

## Abweichungen

Die 18.839 widersprüchlichen Zuordnungen sind stark konzentriert. Die größten
PLZ-Paare sind:

| Land | amtliche PLZ | OSM-PLZ | Datensätze |
|---|---:|---:|---:|
| NW | 42489 | 42549 | 5.066 |
| SN | 02692 | 02633 | 735 |
| SH | 25868 | 25879 | 339 |
| HH | 22119 | 22117 | 309 |
| NW | 41540 | 50769 | 252 |
| HB | 28201 | 28279 | 216 |
| HB | 28197 | 28259 | 207 |
| SN | 02692 | 01877 | 171 |
| NW | 50389 | 50997 | 160 |
| NW | 47167 | 47169 | 158 |

Alle 18.839 PLZ-Widersprüche sowie sämtliche 437 Fälle ohne Polygon und sieben
Fälle mit mehreren PLZ stehen mit Adresse, Koordinate und OSM-Relation in
`abweichungen.csv.gz`. Ein Widerspruch beweist für sich allein keinen
OSM-Fehler: Ursachen können veraltete Polygone, Quellfehler,
unterschiedliche Stände, Sonderzustellungen oder fachlich verschiedene
PLZ-Begriffe sein. Beispielsweise bildet das größte Paar einen räumlich
zusammenhängenden Block und sollte als eigener Qualitätsfall untersucht
werden, statt 5.066 unabhängige Zufallsfehler anzunehmen.

## Reproduktion

Voraussetzungen:

- Node.js mit Brotli-Unterstützung;
- npm;
- Bash, `curl`, `gzip`, `jq`, `shasum`;
- der konsolidierte Bundesbestand vom 28.08.2026.

Vom Projekt-Hauptverzeichnis:

```sh
bash experimental/osm/plz/tools/run.sh
```

Der Lauf installiert die mit `package-lock.json` festgeschriebene
`csv-parse`-Version, lädt den 13,9-MB-Release nur bei Bedarf, prüft seine
SHA-256-Prüfsumme, erstellt die Zuordnung und führt die Vollprüfung sowie die
Abweichungsanalyse aus.

Alternative Ziel- und Eingabepfade:

```sh
bash experimental/osm/plz/tools/run.sh \
  /pfad/zum/experiment-snapshot \
  /pfad/zu/adressen.csv.gz
```

Die Zuordnung ist bei identischen Eingaben und derselben Node-/zlib-Version
bytegenau reproduzierbar. Ein zweiter vollständiger Lauf in derselben Umgebung
wurde mit `cmp` gegen CSV-Gzip und JSON-Auswertung geprüft. Zwischen
verschiedenen Betriebssystemen oder zlib-Versionen kann ein semantisch
identischer Gzip-Inhalt andere Bytes besitzen; dort sind dekomprimierter
Inhalt, Statistiken und Zeilenzahlen die maßgeblichen Vergleiche.

## Quelle und Lizenz

- Quelle: © OpenStreetMap-Mitwirkende
- Lizenz: Open Data Commons Open Database License 1.0 (ODbL)
- Polygonexport: `yetzt/postleitzahlen`, Release 2026.02
- Release:
  <https://github.com/yetzt/postleitzahlen/releases/tag/2026.02>
- OSM Copyright:
  <https://www.openstreetmap.org/copyright>
- ODbL:
  <https://opendatacommons.org/licenses/odbl/1-0/>

Der unveränderte Quelldownload, Release-Metadaten, die Projekt-README und der
vollständige Lizenztext liegen unter `2026-02/source/`.

Die Zuordnungstabelle enthält neben OSM-Ableitungen auch `bund_id` und die
amtliche Kontroll-PLZ. Sie und die angereicherte `adressen.csv.gz` sind damit
kombinierte experimentelle Datenbankprodukte, für die bei einer
Veröffentlichung die ODbL-Share-Alike-Pflichten zu beachten sind. Die Ablage
bleibt deshalb strikt vom amtlichen konsolidierten Bestand getrennt. Die
amtliche Bundesdatei wird durch dieses Experiment weder überschrieben noch neu
lizenziert.

## Grenzen und nächste Schritte

- Der OSM-Stand 2026.02 ist älter als die Länder-/Bundesmomentaufnahme vom
  August 2026.
- Polygone modellieren keine nicht flächenhaften Postfach- oder
  Großempfänger-PLZ.
- Eine hohe Übereinstimmungsquote ersetzt keine amtliche PLZ-Bestätigung.
- Die 19.283 Ausnahmefälle sollten nach räumlichen Clustern und
  Aktualitätsstand untersucht werden.
- Direkte OSM-Tags `addr:postcode` können später speziell für Ausnahme- und
  Grenzfälle ausgewertet werden; dafür ist kein sofortiger großer
  Deutschland-Dump erforderlich.
