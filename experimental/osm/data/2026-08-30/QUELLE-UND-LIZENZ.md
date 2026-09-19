# Quelle und Lizenz

## Eingabedaten

Die Datei `germany.osm.pbf` ist ein Deutschland-Abzug von
[BBBike](https://download.bbbike.org/osm/bbbike/Germany/), abgerufen am
30. August 2026. Der Inhalt stammt aus OpenStreetMap:

> © OpenStreetMap-Mitwirkende

OpenStreetMap-Daten stehen unter der
[Open Database License 1.0 (ODbL)](https://www.openstreetmap.org/copyright).
BBBike ist ausschließlich der Anbieter des regionalen PBF-Abzugs.

## Ableitung

`postleitzahlen.geojson` wurde ausschließlich aus `germany.osm.pbf` erzeugt.
Es wurden keine Daten der ZSHH, der Deutschen Post oder des Projekts
`yetzt/postleitzahlen` verwendet.

Der Exporter liegt unter `../../pbf-parser/`. Er ist freestanding C ohne
Standard-C-Bibliothek und ohne externe Laufzeitabhängigkeiten. Der Parser wurde
aus dem CC0-Projekt
[MathiasSchindler/pbf-parser](https://github.com/MathiasSchindler/pbf-parser)
vom Commit `4b07ae927922cc436cd4f36a731c086e779fdb26` übernommen. Details zu
Upstream und lokalen Änderungen stehen in `../../pbf-parser/UPSTREAM.md`.

Reproduktionsbefehl auf macOS ARM64, ausgeführt im Verzeichnis
`experimental/osm/pbf-parser`:

```sh
make macos-postal-tools
build/freestanding-macos-arm64/osm-postal \
  ../data/2026-08-30/germany.osm.pbf \
  ../data/2026-08-30/postleitzahlen.geojson \
  --report ../data/2026-08-30/postleitzahlen-report.tsv \
  --strict \
  > ../data/2026-08-30/postleitzahlen-statistik.txt
```

Die Ausgabe enthält ausschließlich gültige fünfstellige PLZ aus
`boundary=postal_code`-Relationen. Der genaue Deutschland-Filter, die
Multipolygon-Rekonstruktion und der Umgang mit unvollständigen Relationen sind
in `../../pbf-parser/README.md` dokumentiert.

## Ergebnis und Prüfstatus

- 8.175 GeoJSON-Features
- 8.712 äußere und 144 innere Ringe
- 65.699 aufgelöste Ways und 3.550.926 aufgelöste Nodes
- keine Relation mit fehlerhaft rekonstruierter Geometrie
- 76 ausgeschlossene Relationen mit ungültiger, nicht fünfstelliger PLZ
- 44 unvollständige ausländische Grenzrelationen mit fehlenden Way-Mitgliedern
- erfolgreicher Lauf mit `--strict`
- JSON-Syntax, Featureanzahl, PLZ-Format und Geometrietypen mit `jq` geprüft

Die Einzelentscheidungen stehen in `postleitzahlen-report.tsv`, die Summen in
`postleitzahlen-statistik.txt`.

## Lizenz der Ausgabe

Die abgeleitete Polygonsammlung wird unter den Bedingungen der ODbL 1.0
bereitgestellt. Bei Nutzung und Weitergabe sind insbesondere die
OpenStreetMap-Namensnennung und die einschlägigen ODbL-Pflichten einzuhalten.
Die CC0-Freigabe des Werkzeugs erstreckt sich nicht auf die verarbeiteten
OpenStreetMap-Daten oder die daraus erzeugte Polygonsammlung.
