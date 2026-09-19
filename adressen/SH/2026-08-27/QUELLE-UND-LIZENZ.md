# Adressliste Schleswig-Holstein – Abruf vom 27.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **953.360**
- Quelldatenstand: **01.06.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `e2b4ac6bc46eda4fef158e55125b25a11bd4e91f42fb225a855893ce90be2155`
- Alle Datensätze besitzen eine Postleitzahl.

Die Liste bildet amtliche Hauskoordinaten beziehungsweise Adressen ab. Sie
ist keine Liste aller physischen Gebäude: Gebäude ohne Adresse sind nicht
enthalten; eine Adresse kann außerdem auf einem Flurstück liegen, wenn noch
kein Gebäude vorhanden ist.

## Quelle

- Produkt: **Hauskoordinaten aus ALKIS ohne PLZ-Abgleich**
- Bereitsteller: **GeoBasis-DE / LVermGeo SH**
- Downloadportal:
  <https://geodaten.schleswig-holstein.de/gaialight-sh/_apps/dladownload/dl-hk_alkis.html>
- Quelldatei:
  <https://opendata.schleswig-holstein.de/data/OpenGBD/202606_HK_aus_ALKIS_ohne_PLZ_Abgleich_UTMo32.zip>
- Abrufzeitpunkt: **2026-08-27T17:12:23Z**
- Quelldatenstand laut `info-sh.txt`: **01.06.2026**
- Koordinatenreferenzsystem der Quelle:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**
- Koordinatenreferenzsystem der erzeugten CSV:
  **OGC CRS84** (Längengrad, Breitengrad; WGS-84-kompatibel)

Zur Nachvollziehbarkeit liegen bei:

- `quellinformationen.txt`: Originaldatei `info-sh.txt`
- `datenformat-hauskoordinaten.pdf`: Formatbeschreibung des Anbieters
- `download-metadaten.txt`: URL, Abrufzeit, HTTP-Metadaten und Prüfsumme
- `quellmetadaten-inspire-adressen.json`: Metadaten des korrespondierenden
  INSPIRE-Datensatzes „INSPIRE SH Adressen Hauskoordinaten“

Die nicht beigelegte Original-ZIP-Datei hatte beim Abruf die SHA-256-Summe
`a41c532dcb2454aec619ab403d2dd43a3c6778d7d7aec825487ae61bd38673e0`.

## Aussagekraft der Postleitzahl

Die Postleitzahl ist für alle 953.360 Datensätze befüllt. Die
Produktbezeichnung weist jedoch ausdrücklich darauf hin, dass die aus ALKIS
abgeleiteten Daten **keinen PLZ-Abgleich** durchlaufen haben. Die PLZ sollte
daher nicht als gegen einen externen postalischen Referenzbestand validiert
verstanden werden.

Die drei zusätzlichen postalischen Quellfelder für postalischen Ortsnamen,
Ortsnamenszusatz und postalischen Ortsteil sind in diesem Datenstand leer.
Für `ortsname_post` wurde deshalb der amtliche Gemeindename verwendet.

## Lizenz

Das OpenGBD-Downloadportal stellt die Daten unter
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)** bereit:

<https://creativecommons.org/licenses/by/4.0/>

Die offiziellen Nutzungsbedingungen und der beim Abruf gespeicherte
Lizenztext sind unter
<https://geodaten.schleswig-holstein.de/gaialight-sh/_apps/dladownload/lizenz.html>
beziehungsweise in `lizenz-cc-by-4.0.html` verfügbar.

Der vom LVermGeo SH vorgegebene Quellenvermerk für bearbeitete Daten lautet:

> ©GeoBasis-DE/LVermGeo SH/CC BY 4.0 (Quelle verändert)

Für die Weitergabe dieses Exports wird der ausführlichere Quellenvermerk
empfohlen:

> © GeoBasis-DE/LVermGeo SH/CC BY 4.0 (Quelle verändert);
> Hauskoordinaten aus ALKIS ohne PLZ-Abgleich,
> <https://geodaten.schleswig-holstein.de/gaialight-sh/_apps/dladownload/dl-hk_alkis.html>;
> Quelldatenstand 01.06.2026, abgerufen am 27.08.2026;
> Lizenz: <https://creativecommons.org/licenses/by/4.0/>.

## Verarbeitung

Der vollständige ZIP-Massendownload wurde verarbeitet. Folgende Änderungen
wurden vorgenommen:

- Ergänzung der zur Quelldatei gehörenden 24 Feldnamen anhand der
  mitgelieferten Formatbeschreibung
- Umwandlung der semikolongetrennten Textdatei in dasselbe 14-spaltige
  UTF-8-CSV-Schema wie die Exporte für Berlin und Brandenburg
- Filterung auf den amtlichen Landesschlüssel `01`
- Transformation der Koordinaten von EPSG:25832 nach CRS84
- Ergänzung von `bundesland` mit `Schleswig-Holstein`
- Ergänzung von `vollstaendige_adresse`
- Verwendung des Gemeindennamens für `ortsname_post`, weil die postalischen
  Ortsfelder der Quelle leer sind
- Zusammensetzung von `hausschluessel` aus den amtlichen Schlüssel- und
  Adresskomponenten der Quelle; dieses Feld ist somit ein Bearbeitungsergebnis
  und kein unverändert geliefertes Quellfeld
- Gzip-Komprimierung

## Feldzuordnung

| Einheitliche Spalte | SH-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | aus Straße, Hausnummer, Zusatz, PLZ und Gemeinde |
| `bundesland` | Konstante `Schleswig-Holstein` |
| `landesschluessel` | `LANDSCHL` |
| `postleitzahl` | `POSTPLZ` |
| `ortsname_post` | `POSTONM`, ersatzweise `GMD` |
| `gemeindename` | `GMD` |
| `ortsteilname` | `OTT` |
| `strassenname` | `STR` |
| `hausnummer` | `HNR` |
| `hausnummernzusatz` | `ADZ` |
| `longitude_wgs84` | transformiert aus `OSTWERT` |
| `latitude_wgs84` | transformiert aus `NORDWERT` |
| `datensatznummer` | `OID` |
| `hausschluessel` | zusammengesetzt aus amtlichen Schlüsselkomponenten |

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- 953.360 Datenzeilen mit jeweils 14 Spalten
- keine leere Postleitzahl
- 509 unterschiedliche Postleitzahlen
- keine doppelte `datensatznummer`
- keine ungültige oder außerhalb Schleswig-Holsteins liegende Koordinate
- keine fehlgeschlagene Koordinatentransformation
- SHA-256-Prüfsumme nach der Konvertierung bestätigt

Es gibt 447 mehrfach vorkommende zusammengesetzte Anzeigeadressen, jedoch
jeweils mit unterschiedlichen Koordinaten und eindeutigen amtlichen
Datensatz-IDs. Sie wurden bewusst erhalten; unter anderem können gleich
benannte Straßen und Hausnummern in verschiedenen Ortsteilen derselben
Gemeinde vorkommen.

Die Vollständigkeit bezieht sich auf den in der Quelldatei bereitgestellten
Bestand mit Auslesedatum 01.06.2026.
