# Adressliste Thüringen – Abruf vom 28.08.2026

## Vorläufiger Datenstand ohne Postleitzahlen

Dieser Export enthält alle **624.960** Hauskoordinaten aus dem am
28.08.2026 abgerufenen amtlichen Landesarchiv `HK-TH.zip`.

Die Quelldatei besitzt zwar die vier postalischen Felder `postplz`,
`postonm`, `postonmzus` und `postott`, sie sind jedoch in sämtlichen
624.960 Datensätzen leer. Die amtlichen Metadaten beschreiben dies
ausdrücklich als:

> Postleitzahl (bisher noch ohne)

Das Feld `postleitzahl` bleibt deshalb in allen exportierten Datensätzen
bewusst leer.

## Ergebnisdatei

- Datei: `adressen.csv.gz`
- Datensätze: **624.960**
- Datensätze mit PLZ: **0**
- Datensätze ohne PLZ: **624.960**
- Datenstand laut `info-th.txt`: **26.03.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `3c4b1faa400e9c797578141b43f1c56ef55629c101cafe6a2cee1676d663d02e`

## Primärquelle: Hauskoordinaten

- Produkt: **Hauskoordinaten Thüringen**
- Bereitsteller:
  **Thüringer Landesamt für Bodenmanagement und Geoinformation (TLBG)**
- Infrastruktur und Quellenvermerk: **GDI-Th**
- Download:
  <https://geoportal.geoportal-th.de/hausko_umr/HK-TH.zip>
- Metadaten:
  <https://geomis.geoportal-th.de/geonetwork/inspire/api/records/666deaca-27ec-4fdf-91b0-253bff94b738>
- Abrufdatum: **28.08.2026**
- Auslesedatum laut `info-th.txt`: **26.03.2026**
- Zeitstempel der enthaltenen Adressdatei:
  **06.07.2026, 11:18 Uhr**
- Koordinatenreferenzsystem:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Das Originalarchiv `HK-TH.zip` liegt bei. Es enthält:

- `adressen-th.txt`
- `info-th.txt`
- `2025-05-02_Datenformatbeschreibung_HK-DE_V_5.2-extern.pdf`

SHA-256 von `HK-TH.zip`:

`9c1b227f782d15579601e4a309a6779de4aca5f07dafc35c8d873fef3e15fa3f`

Zusätzlich wurden `quellinfo-hauskoordinaten.txt`, die amtlichen Metadaten
als HTML und XML sowie die Datenformatbeschreibung als eigenständige Datei
gespeichert.

## Ergänzend geprüfte Hausumringe

Das vom Nutzer ebenfalls genannte Archiv wurde vollständig abgerufen:

- Produkt: **Hausumringe Thüringen**
- Download:
  <https://geoportal.geoportal-th.de/hausko_umr/HU-TH.zip>
- Auslesedatum laut `info-th.txt`: **17.03.2026**
- Objekte: **2.318.005**
- SHA-256:
  `9e93e2e15c8c5ea6a8307161a4c7bdbf45bb5dc2d47c9571cea5db1625a2c09d`

`HU-TH.zip` enthält Gebäudepolygone als Shapefile. Die Attributtabelle
besitzt ausschließlich:

- `AGS`: Amtlicher Gemeindeschlüssel
- `OI`: Objektidentifikator
- `GFK`: Gebäudefunktionskennung

Straße, Hausnummer und Postleitzahl sind nicht enthalten. Die Hausumringe
sind daher für eine Adressliste nicht geeignet und wurden nicht mit dem
Adressbestand verschnitten. Originalarchiv, `quellinfo-hausumringe.txt` und
die mitgelieferte HU-DE-Datenformatbeschreibung bleiben als Nachweis
erhalten.

## Lizenz

Die Hauskoordinaten und Hausumringe sind offene Geobasisdaten unter der
**Datenlizenz Deutschland – Namensnennung – Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Die amtlichen Metadaten verlangen die Nennung der datenhaltenden Stelle und
des Bezugsjahres. Das Thüringer Geoportal gibt als Quelle `© GDI-Th` vor.
Da dieser Export die Quelldaten transformiert und in ein einheitliches
Schema überführt, wird folgender Quellenvermerk verwendet:

> © GDI-Th (2026), Datenlizenz Deutschland – Namensnennung – Version 2.0
> (dl-de/by-2-0); Daten verändert, abgerufen am 28.08.2026.

Die Daten dürfen bei Einhaltung dieses Quellenvermerks insbesondere
kommerziell und nicht kommerziell vervielfältigt, bearbeitet, weitergegeben
und mit anderen Daten verbunden werden.

Beigelegt sind:

- `nutzungsbedingungen-offene-geodaten.html`
- `lizenz-dl-de-by-2.0.html`
- `quellmetadaten-hauskoordinaten.html`
- `quellmetadaten-hauskoordinaten.xml`

## Verarbeitung

1. `HK-TH.zip` und `HU-TH.zip` wurden vollständig heruntergeladen und mit
   `unzip -t` geprüft.
2. Anzahl und Schema der Adressdatei wurden gegen `info-th.txt` und die
   HK-DE-Datenformatbeschreibung geprüft.
3. Sämtliche 624.960 Adresszeilen besitzen exakt 24 Quellfelder.
4. Alle vier postalischen Quellfelder wurden über den gesamten Bestand auf
   Befüllung geprüft; sie sind vollständig leer.
5. Alle Adresszeilen wurden ohne Filterung in das gemeinsame 14-spaltige
   Schema überführt.
6. Die Koordinaten wurden von EPSG:25832 nach OGC CRS84 transformiert.
7. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
8. Die Ergebnisdatei wurde byteweise (`LC_ALL=C`) sortiert und
   deterministisch mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | TH-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz und Gemeinde; ohne PLZ |
| `bundesland` | Quellfeld `land` |
| `landesschluessel` | Quellfeld `landschl` (`16`) |
| `postleitzahl` | Quellfeld `postplz`; vollständig leer |
| `ortsname_post` | `postonm`, ersatzweise amtlicher Gemeindename |
| `gemeindename` | Quellfeld `gmd` |
| `ortsteilname` | Quellfeld `ott`; vollständig leer |
| `strassenname` | Quellfeld `str` |
| `hausnummer` | Quellfeld `hnr` |
| `hausnummernzusatz` | Quellfeld `adz` |
| `longitude_wgs84` | transformiert aus `ostwert` |
| `latitude_wgs84` | transformiert aus `nordwert` |
| `datensatznummer` | eindeutige Quell-ID `oid` |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

`ortsname_post` ist bis zur Ergänzung einer postalischen Quelle lediglich
ein Ersatzwert aus dem amtlichen Gemeindenamen und kein bestätigter
postalischer Ortsname.

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **624.960** Datenzeilen entsprechend `info-th.txt`
- jede Datenzeile besitzt genau 14 Ausgabespalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- **624.960** leere Postleitzahlen wie in der Quelle
- **624.960** leere Ortsteilnamen wie in der Quelle
- keine leere Gemeinde, Straße oder Hausnummer
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- 601 Gemeinden
- 22 Kreise beziehungsweise kreisfreie Städte
- Qualitätsklasse A: 609.040
- Qualitätsklasse B: 15.920
- keine ungültige oder außerhalb Thüringens liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

Es gibt 1.000 mehrfach vorkommende Anzeigeadressen mit insgesamt
1.245 zusätzlichen Vorkommen. Die betroffenen Objekte besitzen
unterschiedliche amtliche Quell-IDs und Hausschlüssel und wurden deshalb
bewusst erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 abgerufenen
Hauskoordinatenbestand mit Auslesedatum 26.03.2026.
