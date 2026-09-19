# Adressliste Saarland - Abruf vom 28.08.2026

## Ergebnis

Dieser Export enthält alle **335.022** Datensätze des am 28.08.2026
abgerufenen landesweiten Hauskoordinatenbestands des LVGL.

- Datei: `adressen.csv.gz`
- Datensätze: **335.022**
- Datensätze mit gültiger fünfstelliger PLZ: **334.991**
- Datensätze ohne PLZ: **31**
- unterschiedliche Postleitzahlen: **72**
- Datenstand im Quellfeld `AUD`: **05.10.2025**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `617fb651c33fda0baf18e4c6b14d146c29ea79d2dd07663cd959644b430f951f`

Der Export ist bezüglich des heruntergeladenen amtlichen Landesbestands
vollständig. Nur 31 der 335.022 amtlichen Datensätze besitzen in der Quelle
weder eine Postleitzahl noch einen postalischen Ortsnamen.

## Amtliche Quelle

- Produkt: **Hauskoordinaten Saarland**
- Bereitsteller:
  **Landesamt für Vermessung, Geoinformation und Landentwicklung Saarland
  (LVGL)**
- Open-Data-Seite:
  <https://www.shop.lvgl.saarland.de/index.php?option=com_content&view=article&id=18>
- öffentliche Cloud-Freigabe:
  <https://www.shop.lvgl.saarland.de/cloud/index.php/s/NK8ndP55qAqGEZD>
- Ordner Hauskoordinaten:
  <https://www.shop.lvgl.saarland.de/cloud/index.php/s/NK8ndP55qAqGEZD?dir=/OD_Hauskoordinaten_csv_SL>
- Direktdownload:
  <https://www.shop.lvgl.saarland.de/cloud/public.php/dav/files/NK8ndP55qAqGEZD/OD_Hauskoordinaten_csv_SL/HK_SL_EPSG-25832.zip>
- Abrufdatum: **28.08.2026**
- Ursprungskoordinaten:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Das Originalarchiv `HK_SL_EPSG-25832.zip` liegt bei:

- Größe: **7.648.458 Byte**
- SHA-256:
  `74a3c6f2a7ff42320eb33c1e70c917a443e59e35651f3aae2ab82285d799e162`
- enthaltene Datendatei: `HK_SL.csv`
- unkomprimierte Größe der Datendatei: **44.937.287 Byte**
- SHA-256 der Datendatei:
  `0ea5b0bcdbc2f3f7b6f527ef88e245445d6ac32258fc7a8b376ab5115fe38e49`
- enthaltenes Schlüsselverzeichnis: `schluessel-sl.txt`
- interner Dateizeitstempel: **07.10.2025, 08:32:04 Uhr**
- HTTP-`Last-Modified`: **07.10.2025, 06:40:02 UTC**

Der fachliche Datenstand **05.10.2025** ist in allen Quellzeilen im Feld
`AUD` enthalten. Er ist deshalb maßgeblich gegenüber dem zwei Tage späteren
Dateizeitstempel.

Zusätzlich zur unveränderten Originaldatei wurden folgende Abrufnachweise
gespeichert:

- `open-data-lvgl.html`
- `cloud-hauskoordinaten.html`
- `cloud-hausumringe.html`
- `download-header-hauskoordinaten.txt`
- `lizenz-dl-de-by-2.0.html`

## Inhalt und Postleitzahlen

Die Quelldatei enthält 20 Spalten. Für die Adressliste besonders relevant
sind:

- `OI`: eindeutige Objektkennung
- `QUA`: Qualitätsklasse
- `LAN`, `RBZ`, `KRS`, `GMD`, `OTT`: Verwaltungsschlüssel
- `SSS`: Straßenschlüssel
- `HNR`, `ADZ`: Hausnummer und Zusatz
- `XCOORD`, `YCOORD`: Koordinaten
- `STN`: amtlicher Straßenname
- `PLZ`: Postleitzahl
- `ONM`: postalischer Ortsname
- `PSN`: postalischer Straßenname
- `AUD`: Datenstand

Das mitgelieferte `schluessel-sl.txt` ordnet den Verwaltungskennungen die
amtlichen Namen von Land, Kreis, Gemeinde und Ortsteil zu.

Sämtliche 335.022 Quellzeilen wurden geprüft:

- jede Zeile besitzt genau 20 Felder
- 334.991 Zeilen besitzen eine gültige fünfstellige PLZ
- 31 Zeilen besitzen weder `PLZ` noch `ONM`
- `PSN` ist in allen Zeilen befüllt
- bei drei Zeilen unterscheidet sich `PSN` korrigierend von `STN`
- Gemeinde, Straße und Hausnummer sind in allen Zeilen vorhanden

Für die 31 Datensätze ohne `ONM` wird `ortsname_post` mit dem amtlichen
Gemeindenamen belegt. Deren Feld `postleitzahl` bleibt leer; es wird keine
PLZ geschätzt oder räumlich ergänzt.

Für `strassenname` und die Anzeigeadresse wird der postalische Straßenname
`PSN` verwendet. Dies betrifft gegenüber `STN` nur drei Schreibkorrekturen:
`St.-Wolfgang-Straße`, `Dr.-Wolfgang-Krämer-Straße` und
`Dr.-Rudolf-Drumm-Straße`.

Sechs Quellobjekte besitzen Ortsteilschlüssel, die nicht im mitgelieferten
Schlüsselverzeichnis vorkommen. Ihre übrigen Adressbestandteile einschließlich
PLZ sind vollständig; nur `ortsteilname` bleibt in diesen sechs Zeilen leer.

## Warum Hauskoordinaten statt Hausumrisse?

Hausumringe beschreiben Gebäudegrundrisse als Polygone. Die Hauskoordinaten
enthalten dagegen bereits die strukturierte amtliche Adresse einschließlich
postalischer Anreicherung, Objektkennung und Lagekoordinate. Ein räumlicher
Verschnitt mit Hausumringen wäre unnötig und könnte bei Haupt-, Neben- oder
Mehrfachgebäuden zusätzliche Uneindeutigkeiten erzeugen. Die verlinkte
Hausumring-Freigabe wird daher nur als Quellenkontext dokumentiert, nicht für
den Export verwendet.

## Lizenz und Weiterverwendung

Die Open-Data-Seite erklärt die offenen Geobasisdaten ausdrücklich für frei
nutzbar, weiterverbreitbar und weiterverwendbar. Es gilt die
**Datenlizenz Deutschland - Namensnennung - Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Vorgegebener Quellenvermerk:

> © GeoBasis DE/LVGL-SL (Jahr der Bereitstellung)

Das Archiv und der darin ausgewiesene Datenstand stammen aus 2025. Für
diesen transformierten Export wird der Quellenvermerk deshalb konkret so
geführt:

> © GeoBasis DE/LVGL-SL (2025), dl-de/by-2-0; Daten bearbeitet,
> abgerufen am 28.08.2026.

Der gespeicherte Lizenztext liegt als `lizenz-dl-de-by-2.0.html` bei.

## Verarbeitung

1. Das einzige ZIP-Archiv im amtlichen Cloud-Ordner
   `OD_Hauskoordinaten_csv_SL` wurde über dessen öffentlichen WebDAV-Link
   heruntergeladen und mit `unzip -t` geprüft.
2. `HK_SL.csv` wurde als UTF-8-Datei mit Semikolontrennung und 20 exakt
   geprüften Spalten eingelesen.
3. Sämtliche **335.022** Quellzeilen wurden ohne Filterung übernommen.
4. Verwaltungsnamen und Ortsteile wurden über das mitgelieferte
   `schluessel-sl.txt` aufgelöst.
5. Die in `XCOORD` vorangestellte Zonennummer `32` wurde entfernt; danach
   wurden die Koordinaten von EPSG:25832 nach OGC CRS84 transformiert.
6. PLZ wurden auf genau fünf Ziffern geprüft; fehlende PLZ blieben leer.
7. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
8. Das Ergebnis wurde byteweise (`LC_ALL=C`) sortiert und deterministisch
   mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | SL-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | `PSN`, `HNR`, `ADZ`, `PLZ` und `ONM`; bei leerem `ONM` Gemeinde |
| `bundesland` | Konstante `Saarland` |
| `landesschluessel` | Quellfeld `LAN` (`10`) |
| `postleitzahl` | Quellfeld `PLZ` |
| `ortsname_post` | Quellfeld `ONM`, sonst amtlicher Gemeindename |
| `gemeindename` | Auflösung von `LAN;RBZ;KRS;GMD` über `schluessel-sl.txt` |
| `ortsteilname` | Auflösung von `LAN;RBZ;KRS;GMD;OTT` über `schluessel-sl.txt` |
| `strassenname` | postalischer Straßenname `PSN` |
| `hausnummer` | Quellfeld `HNR` |
| `hausnummernzusatz` | Quellfeld `ADZ` |
| `longitude_wgs84` | transformiert aus `XCOORD` |
| `latitude_wgs84` | transformiert aus `YCOORD` |
| `datensatznummer` | eindeutige Quell-ID `OI` |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **335.022** Quell- und Ausgabedatensätze
- jede Quelldatenzeile besitzt genau 20 Spalten
- jede Ausgabedatenzeile besitzt genau 14 Spalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- **334.991** gültige fünfstellige Postleitzahlen
- **31** Datensätze ohne PLZ
- **72** unterschiedliche Postleitzahlen
- keine leere Gemeinde, Straße oder Hausnummer
- keine doppelte `datensatznummer`
- 52 Gemeinden
- 6 Kreise beziehungsweise Regionalverband
- 378 aufgelöste Ortsteile
- Qualitätsklasse A: 327.516
- Qualitätsklasse B: 6.787
- Qualitätsklasse C: 719
- keine ungültige oder außerhalb des Saarlands liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

Vier Hausschlüssel kommen jeweils zweimal vor. Die acht betroffenen
Quellobjekte besitzen unterschiedliche eindeutige Objektkennungen und
unterschiedliche Koordinaten. Sie wurden deshalb nicht dedupliziert.

Insgesamt sechs Anzeigeadressen kommen jeweils zweimal vor. Vier Paare sind
die genannten doppelten Hausschlüssel; zwei weitere Paare haben
unterschiedliche amtliche Straßen- oder Ortsteilschlüssel. Auch sie wurden
als unterschiedliche amtliche Objekte erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 abgerufenen
landesweiten LVGL-Hauskoordinatenbestand mit dem in allen Quellzeilen
ausgewiesenen Stand 05.10.2025.
