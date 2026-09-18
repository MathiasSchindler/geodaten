# Offene amtliche Adressdaten Deutschland

Dieses Projekt sammelt amtliche Adressdaten der deutschen Länder, bringt sie
in ein gemeinsames CSV-Schema und verbindet sie mit den deutschlandweiten
Adresspunkten aus **basemap.de Web Vektor**. Das Hauptergebnis ist ein
konsolidierter Bundesbestand mit nachvollziehbarer Herkunft je Datensatz.

## Schnellzugriff

- [Aufbau des Adressarchivs](adressen/README.md)
- [Konsolidierter Bundesbestand](adressen/bund/konsolidiert/2026-08-28/)
- [Quellen-, Lizenz- und Methodendokumentation](adressen/bund/konsolidiert/2026-08-28/QUELLEN-UND-LIZENZEN.md)
- [Deutschlandweiter basemap.de-Abzug](adressen/bund/basemap/2026-08-28/)
- [Technische Werkzeuge](adressen/bund/basemap/tools/)
- [Experimentelle PLZ-Zuordnung aus OSM-Polygonen](experimental/osm/plz/README.md)
- [Eigener freestanding OSM-PBF-zu-PLZ-Polygon-Exporter](experimental/osm/pbf-parser/README.md#postal-code-polygons)
- [Aktuelle, selbst erzeugte OSM-PLZ-Polygonsammlung](experimental/osm/data/2026-08-30/QUELLE-UND-LIZENZ.md)
- [Machbarkeit eines offenen HK-DE-5.2-kompatiblen Exports](experimental/ZSHH/MACHBARKEIT-LIBREHKDE.md)
- [Erzeugter libreHKDE-Bundesbestand](experimental/ZSHH/libreHKDE/2026-08-31/QUELLE-UND-LIZENZ.md)

Der konsolidierte Stand vom 28.08.2026 enthält:

- **23.252.486** Adressobjekte;
- **8.528.354** Adressobjekte mit einer amtlich übernommenen PLZ;
- **23.252.486** Provenienzzeilen, also genau eine je Adresse;
- **212** bewusst nicht automatisch aufgelöste Mehrdeutigkeiten;
- Daten aus allen 16 Ländern.

## Was reproduzierbar ist

Es gibt drei unterschiedliche Reproduzierbarkeitsebenen:

1. **Vorhandene Veröffentlichung prüfen:** Die gespeicherten Gzip-Dateien
   lassen sich unmittelbar gegen die dokumentierten SHA-256-Prüfsummen prüfen.
2. **Bundesbestand erneut bauen:** Aus den gespeicherten Länderexporten und den
   1.360 prüfsummierten basemap-Rohdatenblöcken lassen sich Basemap-Abzug,
   Ländervergleich und Konsolidierung erneut erzeugen. Die resultierenden
   Gzip-Archive sind bei identischen Eingaben bytegleich.
3. **Alle Quellen frisch abrufen:** Der basemap.de-Abruf ist vollständig
   skriptgesteuert. Für die historischen Länderabrufe sind Quelldateien,
   Metadaten und die jeweilige Verarbeitung detailliert dokumentiert, die
   damaligen länderspezifischen Einmal-Konverter aber nicht durchgängig als
   eigenständige Programme erhalten. Ein komplett neuer Länderabruf ist daher
   anhand der Dokumentation nachvollziehbar, aber noch kein einheitlicher
   Ein-Befehl-Build. Außerdem verändern aktualisierte amtliche Quellen
   erwartungsgemäß Datensatzzahlen und Prüfsummen.

Diese Unterscheidung ist wichtig: Die Veröffentlichung ist vollständig
prüfbar, und die bundesweite Verarbeitung ist reproduzierbar. Für einen
zukünftigen Datenstand sollten die Länderimporte zusätzlich als dauerhaft
versionierte ETL-Programme umgesetzt werden.

## Verzeichnisstruktur

```text
.
├── README.md
└── adressen/
    ├── README.md
    ├── <Ländercode>/<Abrufdatum>/
    │   ├── adressen.csv.gz
    │   ├── QUELLE-UND-LIZENZ.md
    │   ├── export-statistik.txt
    │   └── Quelldateien und Metadatennachweise
    └── bund/
        ├── basemap/
        │   ├── tools/
        │   └── 2026-08-28/
        │       ├── chunks/
        │       ├── adressen.csv.gz
        │       └── vergleich-laender/
        └── konsolidiert/2026-08-28/
            ├── adressen.csv.gz
            ├── provenienz.csv.gz
            ├── konflikte.csv.gz
            ├── quellen.csv
            └── QUELLEN-UND-LIZENZEN.md
```

Ein Datumsordner ist eine unveränderliche Momentaufnahme. Neue Abrufe sollten
in einem neuen Datumsordner landen, nicht vorhandene Stände überschreiben.

## Gemeinsames Adressschema

Alle `adressen.csv.gz`-Dateien sind UTF-8-CSV mit 14 Spalten:

1. `vollstaendige_adresse`
2. `bundesland`
3. `landesschluessel`
4. `postleitzahl`
5. `ortsname_post`
6. `gemeindename`
7. `ortsteilname`
8. `strassenname`
9. `hausnummer`
10. `hausnummernzusatz`
11. `longitude_wgs84`
12. `latitude_wgs84`
13. `datensatznummer`
14. `hausschluessel`

Leere PLZ-Felder sind beabsichtigt, wenn eine amtliche Quelle keine
postalischen Angaben enthält. Die Konsolidierung rät keine PLZ anhand von
Nachbarn oder PLZ-Polygonen.

## Datenquellen

Die folgende Tabelle beschreibt die in der Konsolidierung verwendeten
Komponenten. Produkt-URLs, gespeicherte Metadaten, Feldzuordnungen,
Verarbeitungsschritte, Qualitätsprüfungen und vollständige Quellenvermerke
stehen in der jeweils verlinkten Detaildokumentation.

| Code | Gebiet | Produkt / Quelle | Bereitsteller | Lizenz | PLZ im verwendeten Bestand | Dokumentation |
|---|---|---|---|---|---|---|
| BASEMAP | Deutschland | basemap.de Web Vektor, Layer `Adresse` | GeoBasis-DE / BKG | CC BY 4.0 | nein | [Details](adressen/bund/basemap/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-SH | Schleswig-Holstein | Hauskoordinaten aus ALKIS | GeoBasis-DE / LVermGeo SH | CC BY 4.0 | ja | [Details](adressen/SH/2026-08-27/QUELLE-UND-LIZENZ.md) |
| LAND-HH | Hamburg | Zentraler AdressService, Hauskoordinaten | LGV Hamburg | DL-DE-BY-2.0 | ja | [Details](adressen/HH/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-NI | Niedersachsen | ALKIS Open Data und ALKIS-Straßennamen | LGLN | CC BY 4.0 | nein | [Details](adressen/NI/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-HB | Bremen | ALKIS-Gebäudeadressen | GeoInformation Bremen | CC BY 4.0 | ja | [Details](adressen/HB/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-NW | Nordrhein-Westfalen | Gebäudereferenzen und INSPIRE-Adressen ALKIS | Geobasis NRW | DL-DE-ZERO-2.0 | ja, bis auf 5 Objekte | [Details](adressen/NW/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-HE | Hessen | Hauskoordinaten ohne postalische Angaben | HLBG | DL-DE-ZERO-2.0 | nein | [Details](adressen/HE/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-RP | Rheinland-Pfalz | Hauskoordinaten ohne postalische Anreicherung | GeoBasis-DE / LVermGeoRP | DL-DE-BY-2.0 | nein | [Details](adressen/RP/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-BW | Baden-Württemberg | Hauskoordinaten ohne postalische Angaben | LGL Baden-Württemberg | DL-DE-BY-2.0 | nein | [Details](adressen/BW/2026-08-28/QUELLE-UND-LIZENZ.md) |
| BASEMAP | Bayern | basemap.de Web Vektor | GeoBasis-DE / BKG | CC BY 4.0 | nein | [Details](adressen/BY/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-SL | Saarland | Hauskoordinaten | GeoBasis-DE / LVGL-SL | DL-DE-BY-2.0 | ja, bis auf 31 Objekte | [Details](adressen/SL/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-BE | Berlin | BB-BE Gazetteer / amtliche Hauskoordinaten | GeoBasis-DE / LGB und Geoportal Berlin | DL-DE-BY-2.0 | ja | [Details](adressen/BE/2026-08-27/QUELLE-UND-LIZENZ.md) |
| LAND-BB | Brandenburg | BB-BE Gazetteer / Hauskoordinaten | GeoBasis-DE / LGB | DL-DE-BY-2.0 | ja | [Details](adressen/BB/2026-08-27/QUELLE-UND-LIZENZ.md) |
| BASEMAP | Mecklenburg-Vorpommern | basemap.de Web Vektor | GeoBasis-DE / BKG | CC BY 4.0 | nein | [nicht verwendeter MV-WFS und Begründung](adressen/MV/2026-08-27/QUELLE-UND-LIZENZ.md) |
| LAND-SN | Sachsen | Hauskoordinaten / georeferenzierte Gebäudeadressen | GeoSN | DL-DE-BY-2.0 | ja | [Details](adressen/SN/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-ST | Sachsen-Anhalt | Gebäudereferenzen | GeoBasis-DE / LVermGeo ST | DL-DE-BY-2.0 | nein | [Details](adressen/ST/2026-08-28/QUELLE-UND-LIZENZ.md) |
| LAND-TH | Thüringen | Hauskoordinaten | GDI-Th | DL-DE-BY-2.0 | nein | [Details](adressen/TH/2026-08-28/QUELLE-UND-LIZENZ.md) |

Bayern wird mangels eines gleichwertigen offenen landeseigenen
Hauskoordinaten-Downloads aus basemap.de abgedeckt. Für
Mecklenburg-Vorpommern wurde der untersuchte INSPIRE-WFS wegen seiner nicht
eindeutig offenen externen Weitergabebedingungen aus dem konsolidierten
Bestand ausgeschlossen; auch dort wird nur basemap.de verwendet.

Die maschinenlesbare Zuordnung von Quellcode, Produkt, Bereitsteller,
Datenstand, Lizenz und vorgeschriebenem Quellenvermerk steht in
[`quellen.csv`](adressen/bund/konsolidiert/2026-08-28/quellen.csv).

## Voraussetzungen

Die bundesweite Pipeline wurde auf macOS ausgeführt. Benötigt werden:

- Node.js mit eingebautem `node:sqlite`; verwendet wurde **Node.js 26.7.0**;
- npm;
- eine POSIX-Shell; einzelne Skripte verlangen ausdrücklich Bash;
- `curl`, `gzip`, `sort`, `comm`, `cmp`, `find`, `awk`, `jq` und `shasum`;
- eine stabile Internetverbindung für den frischen basemap.de-Abruf;
- ausreichend lokale SSD-Kapazität.

Empfehlung: mindestens **80 GB freier Speicher** für einen kompletten Neuaufbau.
Der damalige Kachelabzug übertrug rund 5,45 GB. Die unkomprimierten
Sortier- und Konsolidierungsdateien belegten zeitweise mehr als 30 GB.

Unter Linux kann `sha256sum` statt `shasum -a 256` verwendet werden. Die
Skripte selbst rufen derzeit `shasum` auf. Für bytegleiche Sortierung muss
`LC_ALL=C` gesetzt bleiben.

Abhängigkeiten installieren:

```sh
cd adressen/bund/basemap/tools
npm ci
cd ../../../..
```

Die Abhängigkeiten sind in `package-lock.json` festgeschrieben:
`@mapbox/tile-cover`, `csv-parse` und `vtt`.

## Vorhandene Veröffentlichung schnell prüfen

```sh
snapshot="adressen/bund/konsolidiert/2026-08-28"

gzip -t \
  "$snapshot/adressen.csv.gz" \
  "$snapshot/provenienz.csv.gz" \
  "$snapshot/konflikte.csv.gz"

shasum -a 256 \
  "$snapshot/adressen.csv.gz" \
  "$snapshot/provenienz.csv.gz" \
  "$snapshot/konflikte.csv.gz" \
  "$snapshot/quellen.csv" \
  "$snapshot/konsolidierungs-statistik.json" \
  "$snapshot/validierungs-statistik.json"
```

Die Sollwerte stehen in
[`export-statistik.txt`](adressen/bund/konsolidiert/2026-08-28/export-statistik.txt).
Die drei großen Archive haben folgende SHA-256-Werte:

```text
4d3761bccc2db2e3f08a0d86845a90629437400f1d85b641096f1d7b4be84dd4  adressen.csv.gz
1be96b3d9fab7d0035e16e8b910c2bc698699f9261c713aab2f2c9b4dba695a5  provenienz.csv.gz
ca64109816647ea8479cb233a2c17fb5747946edbe3f6d8c43e8c9f2253ad823  konflikte.csv.gz
```

## Basemap-Abzug reproduzieren

### 1. Kachelliste erstellen

Der amtliche Layer `Adresse` liegt in Zoomstufe 15. Die gespeicherte
Deutschlandmaske bestimmt, welche Kacheln abgerufen werden:

```sh
tools="adressen/bund/basemap/tools"
basemap="adressen/bund/basemap/2026-08-28"

node "$tools/plan-tiles.mjs" \
  "$basemap/deutschland-mask.geojson" \
  "$basemap/tiles-z15.txt" \
  > "$basemap/tile-plan-statistics.json"
```

Der dokumentierte Stand umfasst **679.682** Kachelpositionen.

### 2. Kacheln abrufen

Der Extraktor ist wiederaufnehmbar. Bereits vollständig geschriebene und
prüfsummierte Chunks werden erkannt. Ein einzelner Prozess:

```sh
CONCURRENCY=12 CHUNK_SIZE=500 \
  node "$tools/extract-addresses.mjs" \
  "$basemap/tiles-z15.txt" \
  "$basemap"
```

Optional können disjunkte Shards parallel laufen:

```sh
pids=""
for shard in 0 1 2 3; do
  SHARD_COUNT=4 SHARD_INDEX="$shard" CONCURRENCY=8 CHUNK_SIZE=500 \
    node "$tools/extract-addresses.mjs" \
    "$basemap/tiles-z15.txt" \
    "$basemap" \
    > "$basemap/extraction-worker-$shard.log" 2>&1 &
  pids="$pids $!"
done

for pid in $pids; do
  wait "$pid"
done
```

Der Endpunkt ist im Extraktor fest eingetragen:

```text
https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/{z}/{x}/{y}.pbf
```

HTTP 404 wird als nicht vorhandene Kachel behandelt. Andere Fehler werden bis
zu siebenmal mit Verzögerung erneut versucht. Jeder Chunk erhält eine
Statistikdatei mit SHA-256-Prüfsumme.

### 3. Extraktion und Chunks prüfen

```sh
node "$tools/verify-extraction.mjs" \
  "$basemap/tiles-z15.txt" \
  "$basemap" \
  > "$basemap/extraction-verification.json"
```

Für den vorhandenen Stand müssen alle 679.682 Positionen, 1.360 Chunks und
deren Prüfsummen bestätigt werden.

### 4. Bundes-CSV erzeugen

```sh
bash "$tools/assemble.sh" "$basemap"
bash "$tools/verify-final.sh" "$basemap" 22750793
```

`assemble.sh` führt die Chunkdaten zusammen, entfernt nur exakt identische
Rohzeilen, teilt die Rohdaten nach Ländern, erzeugt das 14-spaltige Schema,
sortiert byteweise und komprimiert deterministisch mit `gzip -n -9`.

Wird nur aus den bereits gespeicherten Chunks neu gebaut, findet kein
Netzabruf statt.

## Länderexporte

Jeder Länderordner enthält:

- die verwendete Originaldatei oder Dienstmetadaten, soweit speicherbar;
- eine ausführliche `QUELLE-UND-LIZENZ.md`;
- den normalisierten Export;
- Statistiken und Prüfsummen.

Die Länderquellen unterscheiden sich erheblich: ASCII-Hauskoordinaten,
Shapefiles, WFS/OGC API Features und ALKIS-basierte Ableitungen kommen vor.
Die jeweilige Dokumentation ist deshalb die maßgebliche ETL-Spezifikation.

Für einen exakt identischen Neuaufbau der Konsolidierung werden die
vorhandenen normalisierten Länderarchive verwendet. Wer einen neuen
Quellstand erzeugt, sollte:

1. einen neuen Datumsordner anlegen;
2. Originaldownload, Metadaten und Lizenznachweis unverändert sichern;
3. die Feldzuordnung zum 14-spaltigen Schema dokumentieren;
4. Koordinaten nach WGS84 transformieren und Achsenreihenfolge prüfen;
5. PLZ nur übernehmen, wenn sie tatsächlich in der Quelle enthalten ist;
6. fachliche IDs erhalten;
7. Ausgabe mit `LC_ALL=C sort` und `gzip -n -9` deterministisch bauen;
8. Zeilenzahlen, Schema, IDs, Koordinaten und Prüfsummen prüfen;
9. den verwendeten Konverter zusammen mit dem neuen Snapshot versionieren.

## Basemap mit Länderexporten vergleichen

Nach `assemble.sh` liegen die basemap-Rohdaten je Land unter
`$basemap/work/states/*.tsv`. Der Vergleich wird so erzeugt:

```sh
sh "$tools/run-comparisons.sh" "$basemap" "adressen"
```

Verglichen werden normalisierte Schlüssel aus Ort, Straße und Hausnummer.
Beim Länderbestand werden postalischer Ortsname und Gemeindename alternativ
zugelassen. Zusätzlich wird für passende Schlüssel die räumliche Distanz
gemessen. Die Ergebnisse liegen unter
[`vergleich-laender/`](adressen/bund/basemap/2026-08-28/vergleich-laender/).

## Konsolidierten Bundesbestand reproduzieren

### 1. Arbeitsverzeichnisse vorbereiten

`assemble.sh` muss zuvor gelaufen sein, damit
`$basemap/work/states/<CODE>.tsv` existiert.

```sh
tools="adressen/bund/basemap/tools"
basemap="adressen/bund/basemap/2026-08-28"
target="adressen/bund/konsolidiert/2026-08-28-rebuild"

mkdir -p "$target/work/states"
cp "adressen/bund/konsolidiert/2026-08-28/quellen.csv" "$target/quellen.csv"
```

### 2. Länder einzeln konsolidieren

Für Bayern und Mecklenburg-Vorpommern wird `-` als Länderquelle übergeben,
damit ausschließlich basemap.de verwendet wird:

```sh
for state in BY MV; do
  node "$tools/consolidate-state.mjs" \
    "$state" \
    "$basemap/work/states/$state.tsv" \
    - \
    "$target/work/states/$state"
done
```

Die 14 zugelassenen Länderquellen:

```sh
while IFS="|" read -r state date; do
  node "$tools/consolidate-state.mjs" \
    "$state" \
    "$basemap/work/states/$state.tsv" \
    "adressen/$state/$date/adressen.csv.gz" \
    "$target/work/states/$state"
done <<'EOF'
BB|2026-08-27
BE|2026-08-27
BW|2026-08-28
HB|2026-08-28
HE|2026-08-28
HH|2026-08-28
NI|2026-08-28
NW|2026-08-28
RP|2026-08-28
SH|2026-08-27
SL|2026-08-28
SN|2026-08-28
ST|2026-08-28
TH|2026-08-28
EOF
```

Die Aufrufe sind voneinander unabhängig und können mit angemessener
Parallelität ausgeführt werden. Jeder Prozess verwendet vorübergehend eine
eigene SQLite-Datei.

### 3. Zusammenbauen und vollständig prüfen

```sh
bash "$tools/assemble-consolidated.sh" "$target"
bash "$tools/verify-consolidated.sh" "$target"
```

Der Validator kontrolliert:

- Gzip-Integrität und CSV-Schemas;
- Datensatzzahlen pro Land;
- gültige und eindeutige `BUND-`-IDs;
- exakte Gleichheit der ID-Mengen von Haupt- und Provenienzdatei;
- zulässige Länder-, Quellen- und Lizenzkombinationen;
- PLZ-Herkunft;
- Koordinaten und Abgleichdistanzen;
- Konfliktzählung;
- byteweise Sortierung;
- bytegenaue Reproduktion der Gzip-Dateien.

Bei identischen Eingaben müssen die drei Gzip-Prüfsummen den oben genannten
Sollwerten entsprechen. `generated_at` in
`konsolidierungs-statistik.json` enthält bewusst den Zeitpunkt des neuen
Builds; diese JSON-Datei ist deshalb nicht bytegleich zum historischen
Exemplar.

### 4. Temporäre Dateien

Erst nach erfolgreicher Prüfung können folgende neu erzeugte Verzeichnisse
entfernt werden:

```text
<target>/work/
<basemap>/work/
```

Die prüfsummierten basemap-Chunks, finalen Archive, Statistiken,
Quelldokumentationen und Lizenznachweise müssen erhalten bleiben.

## Konsolidierungslogik

Die Länderquelle ist führend. Jeder Länder-Datensatz bleibt erhalten. Ein
basemap-Objekt wird höchstens einem noch nicht zugeordneten Länderobjekt
zugewiesen:

1. gleicher normalisierter Ort, Straße und Hausnummer, Distanz höchstens 25 m;
2. andernfalls gleiche normalisierte Straße und Hausnummer, Distanz höchstens
   5 m.

Nur genau ein Kandidat wird vereinigt. Mehrere Kandidaten werden in
`konflikte.csv.gz` dokumentiert; der basemap-Punkt bleibt als separates
Objekt erhalten. Fehlende Ortsteile dürfen aus dem eindeutig passenden
basemap-Objekt ergänzt werden. In Niedersachsen ersetzt bei einem Treffer die
basemap-Koordinate die aus kartografischen Positionen oder Fallback-Geometrien
abgeleitete Länderkoordinate.

Die vollständigen Regeln und Länderzahlen stehen in der
[Konsolidierungsdokumentation](adressen/bund/konsolidiert/2026-08-28/QUELLEN-UND-LIZENZEN.md).

## Determinismus und erwartbare Abweichungen

Für bytegleiche Ergebnisse sind entscheidend:

- identische Quellarchive und basemap-Chunks;
- unveränderte Skripte und festgeschriebene npm-Abhängigkeiten;
- `LC_ALL=C` bei allen Sortierungen;
- `gzip -n -9`, damit kein Dateiname oder Zeitstempel in das Gzip-Archiv
  eingeht;
- unveränderte CSV-Quoting- und Zeilenumbruchregeln;
- kein Erraten fehlender Werte.

Nicht bytegleich sein müssen:

- Statistiken mit `generated_at`;
- Serverantworten bei einem späteren Neuabruf;
- ein neuer amtlicher Datenstand;
- Ausgaben nach bewusster Änderung von Normalisierung oder Matching.

## Lizenzen und Weitergabe

Der konsolidierte Bestand ist kein Werk unter einer einzigen neuen
Sammellizenz. Je Datensatz gelten die in `provenienz.csv.gz` genannten
Komponentenlizenzen. Bei Weitergabe des Gesamtbestands müssen die
Quellenvermerke aus `quellen.csv` erhalten bleiben.

Verwendet werden:

- Creative Commons Namensnennung 4.0 International;
- Datenlizenz Deutschland - Namensnennung - Version 2.0;
- Datenlizenz Deutschland - Zero - Version 2.0.

Die Lizenzbegründung, die konkreten Namensnennungen und der Ausschluss des
MV-WFS sind in der
[Quellen- und Lizenzdokumentation](adressen/bund/konsolidiert/2026-08-28/QUELLEN-UND-LIZENZEN.md)
festgehalten.

## Bekannte Grenzen

- Nur acht Länder liefern in den verwendeten offenen Quellen PLZ.
- basemap.de liefert Ort, Straße, Hausnummer und Koordinate, aber keine PLZ,
  Gemeindeschlüssel oder fachliche Objekt-ID.
- Der Bestand ist eine Momentaufnahme amtlicher Quellen und kein
  Zustellverzeichnis der Deutschen Post.
- Nicht zugeordnete Objekte können echte Ergänzungen, unterschiedliche
  Aktualitätsstände, abweichende Schreibweisen oder verschiedene fachliche
  Objektmodellierungen darstellen.
- Für zukünftige Komplettabrufe fehlen noch einheitlich versionierte
  Länderimporter; die vorhandenen Detaildokumentationen bilden deren
  fachliche Spezifikation.
