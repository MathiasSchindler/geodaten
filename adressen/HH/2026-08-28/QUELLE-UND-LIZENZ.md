# Adressliste Hamburg – Abruf vom 28.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **302.408**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `bc3694be11c9f3493f5ef5498a9141ba1827da5ed809649514ca1f8b90aa0ad8`
- Alle Datensätze besitzen eine fünfstellige Postleitzahl.

Die Liste bildet die im Zentralen AdressService Hamburg bereitgestellten
Hauskoordinaten ab. Sie ist keine Liste aller physischen Gebäude: Gebäude ohne
Adresse sind nicht enthalten; mehrere Gebäude können außerdem dieselbe Adresse
verwenden.

## Quelle

- Datensatz: **Zentraler AdressService Hamburg**
- Collection: **Hauskoordinaten**
- Bereitsteller: **Freie und Hansestadt Hamburg, Landesbetrieb
  Geoinformation und Vermessung**
- Transparenzportal:
  <https://suche.transparenz.hamburg.de/dataset/zentraler-adressservice-hamburg3>
- OGC API – Features:
  <https://api.hamburg.de/datasets/v1/gages_vereinfacht>
- verwendete Collection:
  <https://api.hamburg.de/datasets/v1/gages_vereinfacht/collections/hauskoordinaten>
- Abrufdatum: **28.08.2026**
- Zeitstempel der ersten Antwort: **2026-08-28T06:22:53Z**
- Zeitstempel der letzten Antwort: **2026-08-28T06:23:51Z**
- Koordinatenreferenzsystem: **OGC CRS84**
  (Längengrad, Breitengrad; WGS-84-kompatibel)

Der Dienst bezeichnet den Bestand als tagesaktuell. Der live über die
Items-Schnittstelle gelieferte Wert `numberMatched` betrug beim Abruf 302.408.
Der in den Collection-Metadaten zwischengespeicherte `itemCount` betrug noch
302.401. Für Export und Vollständigkeitsprüfung wurde deshalb der aktuelle
`numberMatched`-Wert verwendet.

Zur Nachvollziehbarkeit liegen die beim Abruf gespeicherten Metadaten bei:

- `oaf-landing.json`
- `oaf-collections.json`
- `oaf-hauskoordinaten.json`
- `oaf-queryables.json`
- `oaf-sortables.json`
- `oaf-schema.json`
- `quellmetadaten-zentraler-adressservice.xml`
- `transparenzportal-metadaten.json`

## Lizenz

Der Zentrale AdressService Hamburg und die verwendete OAF-Collection stehen
unter der **Datenlizenz Deutschland – Namensnennung – Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Der beim Abruf gespeicherte Lizenztext liegt als
`lizenz-dl-de-by-2.0.html` bei.

Der in den amtlichen Metadaten vorgegebene Namensnennungstext lautet:

> Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation und Vermessung

Für die Weitergabe dieses bearbeiteten Exports wird der ausführlichere
Quellenvermerk empfohlen:

> Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation und Vermessung;
> Zentraler AdressService Hamburg, Collection Hauskoordinaten;
> abgerufen am 28.08.2026 über
> <https://api.hamburg.de/datasets/v1/gages_vereinfacht>;
> Datenlizenz Deutschland – Namensnennung – Version 2.0,
> <https://www.govdata.de/dl-de/by-2-0>; Quelle verändert.

## Warum die separate PLZ-Polygonquelle nicht verwendet wurde

Die separat angebotenen Hamburger Postleitzahlen-Polygone wurden weder zur
Erzeugung noch zur Ergänzung dieser Adressliste verwendet. Die
WFS-Nutzungsbeschränkung erklärt, dass die PLZ-Gebiete nur „nachrichtlich“ auf
Basis von ALKIS erfasst seien, die offiziellen Daten der Deutschen Post AG
kostenpflichtig seien und der beim LGV vorhandene Bestand nur für interne
Bearbeitung bereitgestellt werden könne.

Die entsprechende GetCapabilities-Antwort wurde ausschließlich als Nachweis
dieser Quellenentscheidung in `wfs-capabilities-postleitzahlen.xml`
gespeichert. Die Postleitzahl der Adressliste stammt stattdessen unmittelbar
aus dem Feld `postleitzahl` der ausdrücklich offen lizenzierten
Hauskoordinaten-Collection.

## Verarbeitung

Die vollständige OAF-Collection wurde in 31 Seiten mit höchstens 10.000
Objekten abgerufen. Die Abfragen waren aufsteigend nach `hausschluessel`
sortiert und verwendeten fortlaufende Offsets. Anschließend wurden:

- die Quellfelder in das einheitliche 14-spaltige Adressschema überführt,
- feste Bundesland-, Landes- und Gemeindefelder ergänzt,
- `vollstaendige_adresse` aus Straße, Hausnummer, Zusatz, PLZ und Hamburg
  gebildet,
- die bereits in CRS84 gelieferten Koordinaten unverändert übernommen,
- sämtliche Datenzeilen byteweise (`LC_ALL=C`) sortiert und
- die CSV deterministisch mit Gzip komprimiert.

Die Quellverteilung des Feldes `amtliche_adresse` betrug 300.711-mal `ja` und
1.697-mal `nein`. Es wurde nicht nach diesem Feld gefiltert, damit der Export
den vollständigen, vom Zentralen AdressService angebotenen Adressbestand
abbildet. Die Kennzeichnung selbst gehört nicht zum einheitlichen
14-Spalten-Schema; ihre Verteilung ist in `export-statistik.txt` festgehalten.

## Feldzuordnung

| Einheitliche Spalte | Hamburger Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz, PLZ und `Hamburg` |
| `bundesland` | Konstante `Hamburg` |
| `landesschluessel` | Konstante `02` |
| `postleitzahl` | `postleitzahl` |
| `ortsname_post` | Konstante `Hamburg` |
| `gemeindename` | Konstante `Hamburg` |
| `ortsteilname` | `postOrtsteil` |
| `strassenname` | `strassenname` |
| `hausnummer` | `hausnummer` |
| `hausnummernzusatz` | `hausnummernzusatz` |
| `longitude_wgs84` | erster Wert der CRS84-Punktkoordinate |
| `latitude_wgs84` | zweiter Wert der CRS84-Punktkoordinate |
| `datensatznummer` | Feature-ID beziehungsweise `hausschluessel` |
| `hausschluessel` | Feature-ID beziehungsweise `hausschluessel` |

Der vereinfachte OAF-Datensatz enthält kein separates Feld für den
postalischen Ortsnamen. Da sämtliche Datensätze Hamburg betreffen und der
Dienst selbst Hamburg als Ortsangabe verwendet, wurde `ortsname_post` mit
`Hamburg` belegt.

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- exakt 302.408 Datenzeilen entsprechend dem live gemeldeten `numberMatched`
- jede Datenzeile besitzt genau 14 Spalten
- gemeinsamer Header aller Länderexporte bestätigt
- keine leere oder nicht fünfstellige Postleitzahl
- 102 unterschiedliche Postleitzahlen
- keine doppelte `datensatznummer` oder doppelter `hausschluessel`
- keine doppelte zusammengesetzte Anzeigeadresse
- keine leere Straße, Hausnummer oder Ortsteilangabe
- keine ungültige oder außerhalb des Quellgebiets liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- SHA-256-Prüfsumme nach der Komprimierung bestätigt

Die Vollständigkeit bezieht sich auf den am 28.08.2026 live über die
dokumentierte Collection bereitgestellten Bestand.
