# Adressliste Baden-Württemberg - Abruf vom 28.08.2026

## Ergebnis

Dieser Export enthält alle **3.382.566** Datensätze des am 28.08.2026
abgerufenen landesweiten Hauskoordinatenbestands des LGL.

- Datei: `adressen.csv.gz`
- Datensätze: **3.382.566**
- reguläre amtliche Hausnummern (Qualität A oder B): **3.090.757**
- katasterinterne Pseudonummern (Qualität C): **291.809**
- Datensätze mit PLZ: **0**
- Datensätze ohne PLZ: **3.382.566**
- Datenstand: **15.07.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `7373497308cde05b36d33453bce897bfdf8caa8e3a4fab6ec3af2db7b3fc1c28`

Der Export ist bezüglich des heruntergeladenen amtlichen Landesbestands
vollständig, aber wegen der ausdrücklich fehlenden postalischen Angaben ein
**vorläufiger Bestand ohne Postleitzahlen**.

## Amtliche Quelle

- Produkt: **Hauskoordinaten Baden-Württemberg**
- Bereitsteller:
  **Landesamt für Geoinformation und Landentwicklung Baden-Württemberg
  (LGL)**
- Open GeoData Portal:
  <https://opengeodata.lgl-bw.de/#/>
- Produktansicht:
  <https://opengeodata.lgl-bw.de/#/(sidenav:product/hk)>
- Direktdownload:
  <https://opengeodata.lgl-bw.de/data/hk/hk_bw.zip>
- Metadatensatz:
  <https://metadaten.geoportal-bw.de/geonetwork/srv/api/records/209e0375-f7b5-1bd8-e7ba-e5ff8b5efb5f/formatters/xml>
- Datenportal BW:
  <https://www.daten-bw.de/suche/daten/alkis-hauskoordinaten>
- Abrufdatum: **28.08.2026**
- Ursprungskoordinaten:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Die offizielle Portal-Konfiguration definiert `hk_bw.zip` als exakten,
landesweiten Direktdownload. Sie beschreibt das Produkt ausdrücklich als
„landesweit ohne postalische Angaben“, im TXT-Format und mit halbjährlichem
Aktualisierungszyklus.

Das Originalarchiv `hk_bw.zip` liegt bei:

- Größe: **73.432.517 Byte**
- SHA-256:
  `5c5bce32698eb568f6b3a87a832eb16d379251f3b5bd00c82b7f2904e695abb7`
- enthaltene Datendatei: `adressen-bw.txt`
- unkomprimierte Größe der Datendatei: **545.646.546 Byte**
- SHA-256 der Datendatei:
  `eb2a00d2bda933d465fb975b56cdbc9cd88c7f236228734b93f7c6c42538d6dc`
- enthaltenes Schlüsselverzeichnis: `schluessel-bw.txt`
- interner Dateizeitstempel: **15.07.2026, 09:27:04 Uhr**
- HTTP-`Last-Modified`: **15.07.2026, 07:27:05 UTC**

Das Archiv enthält außerdem:

- `GOVDATA-Datenlizenz_Deutschland.pdf`
- `Meta-ALKIS-HK.txt` mit dem amtlichen Metadatenlink
- `Aktualitaet-HK.txt` mit dem Datenstand `2026-07-15`

Zusätzlich zur unveränderten Originaldatei wurden folgende Abrufnachweise
gespeichert:

- `open-geodata-portal.html`
- `portal-produkte.json`
- `quellmetadaten-hauskoordinaten.xml`
- `daten-bw-hauskoordinaten.html`
- `open-data-mlw.html`
- `download-header-hauskoordinaten.txt`
- `lizenz-dl-de-by-2.0.html`

## Inhalt und fehlende Postleitzahlen

Die Datendatei besitzt das erweiterte Hauskoordinatenformat mit 26 Spalten.
Sie enthält unter anderem:

- amtliche Objekt-ID und Qualitätsklasse
- Land, Regierungsbezirk, Kreis und Gemeinde
- Straßenname, Hausnummer und Hausnummernzusatz
- Lagekoordinate in ETRS89/UTM
- fünf postalische Felder: `postplz`, `postonm`, `postonmzus`, `postott`
  und `poststr`
- Datenstand `aud`

Sämtliche 3.382.566 Quellzeilen wurden geprüft:

- jede Zeile besitzt exakt 26 Felder
- alle fünf postalischen Felder sind in jeder Zeile leer
- `ott` und der Ortsteilschlüssel `ottschl` enthalten keine
  Ortsteilinformation
- keine Gemeinde, Straße oder Hausnummer ist leer
- sämtliche Koordinaten liegen in UTM-Zone 32
- `aud` ist überall `2026-07-15`

Deshalb bleibt `postleitzahl` im Export leer. `ortsname_post` enthält als
offengelegten Ersatz den amtlichen Gemeindenamen. Das ist keine Behauptung,
dass der Gemeindename dem postalischen Ortsnamen entspricht.

## Qualitätsklassen und Pseudonummern

Die amtlichen Metadaten definieren:

- **A:** amtliche Hausnummer; Koordinate sicher innerhalb der erfassten
  Gebäudegeometrie
- **B:** amtliche Hausnummer; Koordinate sicher innerhalb der
  Flurstücksfläche, Gebäude in der Örtlichkeit nicht sicher vorhanden
- **C:** katasterinterne Hausnummer beziehungsweise Pseudonummer; Koordinate
  sicher innerhalb der erfassten Gebäudegeometrie

Der Landesbestand enthält:

- Qualität A: **3.036.455**
- Qualität B: **54.302**
- Qualität C: **291.809**

Von den Pseudonummern sind 291.793 achtstellig. Sie sind keine regulären
postalischen Hausnummern. Da Ziel dieses Datenstands die vollständige,
verlustfreie Abbildung des amtlichen Hauskoordinatenbestands ist, werden sie
nicht herausgefiltert. Anwendungen, die ausschließlich reguläre amtliche
Hausnummern benötigen, müssen Datensätze der Qualitätsklasse C ausschließen;
die Qualitätsklasse ist jedoch im gemeinsamen 14-Spalten-Schema nicht als
eigene Spalte enthalten. Für einen solchen Filter ist daher die archivierte
Originalquelle maßgeblich.

## Warum Hauskoordinaten statt ALKIS oder Hausumrisse?

Die Hauskoordinaten werden bereits aus ALKIS abgeleitet und liefern die für
diesen Export erforderlichen Gebäudeadressen, stabilen Objektkennungen und
Koordinaten in einer kompakten landesweiten Datei. Ein Export aus vollständigem
ALKIS wäre erheblich größer und würde dieselben Adressinformationen erst aus
mehreren Objektarten und Relationen rekonstruieren.

Hausumringe beschreiben dagegen Gebäudegrundrisse als Polygone und lösen die
fehlende postalische Anreicherung nicht. Ein räumlicher Verschnitt wäre
unnötig und könnte bei Haupt-, Neben- oder Mehrfachgebäuden zusätzliche
Uneindeutigkeiten erzeugen.

## Lizenz und Weiterverwendung

Das Open GeoData Portal erlaubt ausdrücklich jede kommerzielle und
nicht-kommerzielle Nutzung. Soweit nicht anders gekennzeichnet, gilt für
jeden dort verfügbaren Datensatz die
**Datenlizenz Deutschland - Namensnennung - Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Vorgegebener Quellenvermerk:

> Datenquelle: LGL, www.lgl-bw.de, dl-de/by-2-0

Für diesen transformierten Export wird der Quellenvermerk ergänzt:

> Datenquelle: LGL, www.lgl-bw.de, dl-de/by-2-0; Daten bearbeitet,
> abgerufen am 28.08.2026.

Das Originalarchiv enthält den amtlich beigelegten Lizenztext als
`GOVDATA-Datenlizenz_Deutschland.pdf`. Zusätzlich liegt der Lizenztext als
`lizenz-dl-de-by-2.0.html` bei.

## Verarbeitung

1. Das in der amtlichen Portal-Konfiguration als landesweiter Download
   hinterlegte ZIP-Archiv wurde direkt heruntergeladen und mit `unzip -t`
   geprüft.
2. `adressen-bw.txt` wurde als UTF-8-Datei mit Semikolontrennung und 26
   exakt geprüften Spalten eingelesen.
3. Sämtliche **3.382.566** Quellzeilen wurden ohne Filterung übernommen.
4. Landeskennung, UTM-Zone, Datenstand, Pflichtfelder und die vollständige
   Abwesenheit postalischer Angaben wurden für jede Zeile geprüft.
5. Die Koordinaten wurden von EPSG:25832 nach OGC CRS84 transformiert.
6. Da die Quelle keine postalischen Werte besitzt, bleibt `postleitzahl`
   leer und `ortsname_post` wird mit `gmd` belegt.
7. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
8. Das Ergebnis wurde byteweise (`LC_ALL=C`) sortiert und deterministisch
   mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | BW-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz und Gemeinde |
| `bundesland` | Konstante `Baden-Württemberg` |
| `landesschluessel` | Quellfeld `landschl` (`08`) |
| `postleitzahl` | leer; alle postalischen Quellfelder sind leer |
| `ortsname_post` | Gemeinde `gmd` als dokumentierter Fallback |
| `gemeindename` | Quellfeld `gmd` |
| `ortsteilname` | Quellfeld `ott`, im aktuellen Bestand vollständig leer |
| `strassenname` | Quellfeld `str` |
| `hausnummer` | Quellfeld `hnr`, einschließlich Pseudonummern der Klasse C |
| `hausnummernzusatz` | Quellfeld `adz` |
| `longitude_wgs84` | transformiert aus `ostwert` |
| `latitude_wgs84` | transformiert aus `nordwert` |
| `datensatznummer` | eindeutige Quell-ID `oid` |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **3.382.566** Quell- und Ausgabedatensätze
- jede Quelldatenzeile besitzt genau 26 Spalten
- jede Ausgabedatenzeile besitzt genau 14 Spalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- keine postalischen Werte in der Quelle und keine scheinbare PLZ ergänzt
- keine leere Gemeinde, Straße oder Hausnummer
- keine doppelte `datensatznummer`
- 1.103 unterschiedliche amtliche Gemeindeschlüssel
- 44 Kreise beziehungsweise Stadtkreise
- 4 Regierungsbezirke
- keine ungültige oder außerhalb Baden-Württembergs liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

Zwölf Hausschlüssel kommen jeweils zweimal vor. Die 24 betroffenen
Quellobjekte besitzen unterschiedliche eindeutige Objektkennungen und wurden
deshalb nicht dedupliziert.

Es gibt 18.140 mehrfach vorkommende Anzeigeadressen mit insgesamt 18.164
zusätzlichen Vorkommen. Die betroffenen Objekte besitzen unterschiedliche
amtliche Quell-IDs und überwiegend unterschiedliche Hausschlüssel; sie wurden
bewusst erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 abgerufenen
landesweiten LGL-Hauskoordinatenbestand mit Stand 15.07.2026.
