# Adressliste Bremen – Abruf vom 28.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **176.374**
- davon Stadtgemeinde Bremen: **151.714**
- davon Stadtgemeinde Bremerhaven: **24.660**
- Quelldatenstand: **August 2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `fd47811e4481d01ed4a8611d66af8a4a8a8744fbd477d813b4fac4c82e392fae`
- Alle Datensätze besitzen eine fünfstellige Postleitzahl.

Die Liste bildet die vom Landesamt GeoInformation Bremen bereitgestellten
georeferenzierten Gebäudeadressen ab. Sie ist keine Liste aller physischen
Gebäude: Gebäude ohne Adresse sind nicht enthalten.

## Quelle

- Produkt: **ALKIS – Gebäudeadressen Land Bremen (Shapefiles)**
- Bereitsteller: **Landesamt GeoInformation Bremen**
- Download:
  <https://gdi2.geo.bremen.de/inspire/download/ALKIS_Gebaeudeadressen/data/GEBAEUDEADRESSEN_SHP_FHB.zip>
- Abrufdatum: **28.08.2026**
- HTTP-Änderungsdatum der Quelldatei:
  **24.08.2026, 05:26:46 UTC**
- innere Quelldatei: `GEBAEUDEADRESSEN_SHP_FHB_2026_08.zip`
- Quellformat: ESRI Shapefile mit UTF-8-kodierter DBF-Tabelle
- Koordinatenreferenzsystem der Quelle:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**
- Koordinatenreferenzsystem der erzeugten CSV:
  **OGC CRS84** (Längengrad, Breitengrad; WGS-84-kompatibel)

Die originale Download-ZIP-Datei liegt als
`GEBAEUDEADRESSEN_SHP_FHB.zip` bei. Ihre SHA-256-Summe lautet:

`ea6c6772b7bf4d96696e2e081d3a9e482ddc593ed14742b6b4a0902c219fd9dc`

Die darin enthaltene ZIP-Datei hat die SHA-256-Summe:

`5cbab86f303cc3a84539d7c2cc0126e8415eefc7c543e822e0b43b52bc8032cc`

Die beim Abruf gelieferten HTTP-Metadaten sind in
`download-http-header.txt` gespeichert.

## Lizenz

Die Daten stehen gemäß den Nutzungsbedingungen des Angebots unter
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)**:

<https://creativecommons.org/licenses/by/4.0/deed.de>

Der beim Abruf gespeicherte Lizenztext liegt als
`lizenz-cc-by-4.0.html` bei.

Der vorgegebene Quellenvermerk lautet:

> Landesamt GeoInformation Bremen

Für die Weitergabe dieses bearbeiteten Exports wird der ausführlichere
Quellenvermerk empfohlen:

> Landesamt GeoInformation Bremen; ALKIS – Gebäudeadressen Land Bremen
> (Shapefiles), Datenstand August 2026, abgerufen am 28.08.2026;
> CC BY 4.0, <https://creativecommons.org/licenses/by/4.0/deed.de>;
> Quelle verändert.

## Verarbeitung

Der vollständige ZIP-Massendownload wurde verarbeitet. Folgende Änderungen
wurden vorgenommen:

- Entpacken der verschachtelten ZIP- und Shapefile-Struktur
- Auslesen aller 176.374 DBF-Datensätze und der zugehörigen Punktgeometrien
- Umwandlung in das einheitliche 14-spaltige UTF-8-CSV-Schema
- Zuordnung der Stadtgemeinde anhand des amtlichen Kreisschlüssels:
  `11` zu Bremen und `12` zu Bremerhaven
- Transformation der Koordinaten von EPSG:25832 nach OGC CRS84
- Zusammensetzung von `vollstaendige_adresse`
- Zusammensetzung eines stabilen `hausschluessel` aus den amtlichen
  Schlüssel- und Adresskomponenten
- byteweise Sortierung aller Datenzeilen (`LC_ALL=C`)
- deterministische Gzip-Komprimierung

Das Quellfeld `pot` für den postalischen Ortsteil ist in sämtlichen 176.374
Datensätzen leer. `ortsteilname` wurde deshalb bewusst nicht durch eine
abgeleitete oder ungesicherte Angabe ersetzt. Die Quelle enthält zwar den
numerischen Ortsteilschlüssel `ott`, aber keinen zugehörigen Ortsteilnamen.

Das Quellfeld `onm` wurde unverändert als `ortsname_post` übernommen. Es
enthält 150.804-mal `Bremen`, 24.652-mal `Bremerhaven`, 841-mal
`Stadtgemeinde Bremen` und 77-mal `Stadtgemeinde Bremerhaven`.

Die Qualitätsangabe der Quelle lautet bei 172.928 Datensätzen
`AmtlichMit (A)` und bei 3.446 Datensätzen `Amtlich (B)`. Beide Gruppen wurden
vollständig übernommen.

## Feldzuordnung

| Einheitliche Spalte | Bremer Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | aus `stn`, `hnr`, `adz`, `plz`, `onm` und `zon` |
| `bundesland` | Konstante `Bremen` |
| `landesschluessel` | `lan` (`04`) |
| `postleitzahl` | `plz` |
| `ortsname_post` | `onm` |
| `gemeindename` | aus `krs`: `11` = Bremen, `12` = Bremerhaven |
| `ortsteilname` | `pot`, in diesem Datenstand vollständig leer |
| `strassenname` | `stn` |
| `hausnummer` | `hnr` |
| `hausnummernzusatz` | `adz` |
| `longitude_wgs84` | aus der Shapefile-X-Koordinate transformiert |
| `latitude_wgs84` | aus der Shapefile-Y-Koordinate transformiert |
| `datensatznummer` | `uuid` |
| `hausschluessel` | `lan;rbz;krs;gmd;ott;sss;hnr;adz` |

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- exakt 176.374 Datenzeilen entsprechend der DBF-Datensatzanzahl
- jede Datenzeile besitzt genau 14 Spalten
- gemeinsamer Header aller Länderexporte bestätigt
- keine leere oder nicht fünfstellige Postleitzahl
- 48 unterschiedliche Postleitzahlen
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- keine doppelte zusammengesetzte Anzeigeadresse
- keine leere Straße, Hausnummer oder postalische Ortsangabe
- keine ungültige oder außerhalb des Landes Bremen liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- SHA-256-Prüfsumme nach der Komprimierung bestätigt

Die Vollständigkeit bezieht sich auf den am 28.08.2026 über die dokumentierte
Downloadadresse bereitgestellten Datenstand August 2026.
