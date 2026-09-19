# Adressliste Sachsen – Abruf vom 28.08.2026

## Ergebnis

Dieser Export enthält alle **990.090** Datensätze des am 28.08.2026
abgerufenen sachsenweiten Hauskoordinatenarchivs von GeoSN.

- Datei: `adressen.csv.gz`
- Datensätze: **990.090**
- Datensätze mit gültiger fünfstelliger PLZ: **990.090**
- Datensätze ohne PLZ: **0**
- unterschiedliche Postleitzahlen: **385**
- Datenstand laut Quelldateiname: **02.07.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `0f7043c8f471dc7bc304fb35f1411caa53641fefc5cbf037997371290bbfb97f`

## Amtliche Quelle

- Produkt: **Hauskoordinaten / georeferenzierte Gebäudeadressen Sachsen**
- Bereitsteller:
  **Landesamt für Geobasisinformation Sachsen (GeoSN)**
- Downloadseite:
  <https://www.geodaten.sachsen.de/downloadbereich-hauskoordinaten-4172.html>
- Direktdownload:
  <https://geocloud.landesvermessung.sachsen.de/public.php/dav/files/B3HnXbDDgAkw69a/hk_sn_ascii.zip>
- Produktbeschreibung:
  <https://www.landesvermessung.sachsen.de/hauskordinaten-4014.html>
- GeoMIS:
  <https://geomis.sachsen.de/geomis-client/?lang=de#/datasets/1cf02f99-3e1c-471e-b2a3-573e2d345c18>
- Abrufdatum: **28.08.2026**
- Ursprungskoordinaten:
  **ETRS89 / UTM Zone 33N (EPSG:25833)**

Die Downloadseite bezeichnet den Bestand als sachsenweite Abgabe im
CSV-Textformat und nennt eine vierteljährliche Aktualisierung.

Das Originalarchiv `hk_sn_ascii.zip` liegt bei:

- Größe: **51.236.896 Byte**
- SHA-256:
  `44a1721f3af10158837c6ce5ede1164b0cb6296c9a6d87db3066fc5536bdaab4`
- enthaltene Datei: `hk_sn_adressen_20260702.txt`
- unkomprimierte Größe: **206.543.408 Byte**
- interner Dateizeitstempel: **09.07.2026, 11:46 Uhr**

Das Datum `02.07.2026` wird aus dem amtlichen Quelldateinamen übernommen.
Ein separates Datenstandsfeld ist in den einzelnen Quellzeilen nicht
enthalten.

Beigelegt sind:

- `downloadbereich-hauskoordinaten.html`
- `produktbeschreibung-hauskoordinaten.html`
- `produktuebersicht-liegenschaftskataster.html`
- `rechtsgrundlagen-und-nutzungsbedingungen.html`

## Inhalt und Postleitzahlen

Die amtliche Produktbeschreibung definiert die georeferenzierten
Gebäudeadressen als Postanschriften aus:

- Postleitzahl
- Ort
- Straße
- Hausnummer
- zugehöriger Lagekoordinate des Gebäudes beziehungsweise Flurstücks

Als Datenquellen nennt GeoSN das Liegenschaftskataster sowie die
Postleitzahlen der Deutschen Post AG.

Die heruntergeladene Datei besitzt das bundesweit einheitliche
24-spaltige Hauskoordinatenformat. Sämtliche 990.090 Quellzeilen wurden
geprüft:

- jede Zeile besitzt exakt 24 Felder
- `postplz` ist in allen Zeilen fünfstellig
- `postonm` ist in allen Zeilen befüllt
- keine Gemeinde, Straße oder Hausnummer ist leer
- sämtliche Koordinaten liegen in UTM-Zone 33

Die zusätzlichen postalischen Quellfelder `postonmzus` und `postott`
werden nicht in eigene neue Spalten überführt, weil das gemeinsame
Länderschema dafür keine separaten Felder besitzt. `ortsname_post` enthält
wie bei den bisherigen HK-Exporten den postalischen Ortsnamen `postonm`;
`ortsteilname` enthält den amtlichen Kataster-Ortsteil `ott`.

## Warum Hauskoordinaten statt Hausumrisse?

Die Hausumringe beschreiben Gebäudegrundrisse als Polygone. Sie sind für
räumliche Gebäudeanalysen geeignet, enthalten aber nicht die bereits
postalisch ergänzte, eindeutig referenzierte Adressliste.

Die Hauskoordinaten liefern dagegen genau die für diesen Export benötigten
Attribute in einer einzelnen sachsenweiten Datei. Ein räumlicher Verschnitt
mit Hausumringen wäre unnötig und könnte durch Mehrfachgebäude oder
Nebengebäude zusätzliche Uneindeutigkeiten erzeugen. Deshalb wurde
ausschließlich der Hauskoordinatenbestand verwendet.

## Lizenz und Weiterverwendung

Für die als Replikationen heruntergeladenen digitalen Geodaten des
amtlichen Vermessungswesens gilt die
**Datenlizenz Deutschland – Namensnennung – Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Die sächsischen Nutzungsbedingungen erklären, dass diese Erlaubnis eine
uneingeschränkte Weiterverwendung durch jedermann ermöglicht. Der
Selbstabruf und die Nutzung der online bereitgestellten Daten sind
kostenfrei.

Vorgegebener Quellenvermerk:

> Quelle: GeoSN, dl-de/by-2-0

Da dieser Export die Daten transformiert, normalisiert und in ein neues
Schema überführt, wird der Quellenvermerk ergänzt:

> Quelle: GeoSN, dl-de/by-2-0; Daten verändert, abgerufen am 28.08.2026.

Die Einschränkungen für Präsentationsausgaben und Eigentümerdaten sind für
diesen Export nicht einschlägig: Verwendet wurde die frei bereitgestellte
digitale Hauskoordinaten-Replikation; sie enthält keine Eigentümerdaten.

Der gespeicherte Lizenztext liegt als `lizenz-dl-de-by-2.0.html` bei.

## Verarbeitung

1. Das vollständige ZIP-Archiv wurde direkt von der GeoSN-Geocloud
   heruntergeladen und mit `unzip -t` geprüft.
2. Die einzige enthaltene TXT-Datei wurde als UTF-8-Datei mit
   Semikolontrennung eingelesen.
3. Sämtliche 990.090 Quellzeilen wurden ohne Filterung übernommen.
4. Alle PLZ wurden auf genau fünf Ziffern geprüft.
5. Die Koordinaten wurden von EPSG:25833 nach OGC CRS84 transformiert.
6. Die source-seitige Landesbezeichnung `Freistaat Sachsen` wurde im
   gemeinsamen Feld `bundesland` auf `Sachsen` normalisiert.
7. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
8. Das Ergebnis wurde byteweise (`LC_ALL=C`) sortiert und deterministisch
   mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | SN-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz, `postplz` und `postonm` |
| `bundesland` | normalisiert auf `Sachsen` |
| `landesschluessel` | Quellfeld `landschl` (`14`) |
| `postleitzahl` | Quellfeld `postplz` |
| `ortsname_post` | Quellfeld `postonm` |
| `gemeindename` | Quellfeld `gmd` |
| `ortsteilname` | Quellfeld `ott` |
| `strassenname` | Quellfeld `str` |
| `hausnummer` | Quellfeld `hnr` |
| `hausnummernzusatz` | Quellfeld `adz` |
| `longitude_wgs84` | transformiert aus `ostwert` |
| `latitude_wgs84` | transformiert aus `nordwert` |
| `datensatznummer` | eindeutige Quell-ID `oid` |
| `hausschluessel` | Land;NUTS-2-Region;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **990.090** Datenzeilen
- jede Datenzeile besitzt genau 14 Ausgabespalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- **990.090** gültige fünfstellige Postleitzahlen
- **385** unterschiedliche Postleitzahlen
- kein leerer postalischer Ortsname
- keine leere Gemeinde, Straße oder Hausnummer
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- 418 Gemeinden
- 13 Kreise beziehungsweise kreisfreie Städte
- 3 NUTS-2-Regionen
- Qualitätsklasse A: 957.914
- Qualitätsklasse B: 32.176
- keine ungültige oder außerhalb Sachsens liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

50.625 Datensätze besitzen keinen amtlichen Kataster-Ortsteil; das
Ausgabefeld `ortsteilname` bleibt dort leer.

Es gibt 334 mehrfach vorkommende Anzeigeadressen mit insgesamt
368 zusätzlichen Vorkommen. Die betroffenen Objekte besitzen
unterschiedliche amtliche Quell-IDs und Hausschlüssel und wurden deshalb
bewusst erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 abgerufenen
sachsenweiten GeoSN-Hauskoordinatenbestand mit dem im Dateinamen
ausgewiesenen Stand 02.07.2026.
