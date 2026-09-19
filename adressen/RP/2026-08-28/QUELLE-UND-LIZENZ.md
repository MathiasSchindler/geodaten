# Adressliste Rheinland-Pfalz - Abruf vom 28.08.2026

## Ergebnis

Dieser Export enthält alle **1.406.086** Datensätze des am 28.08.2026
abgerufenen landesweiten Bestands **Amtliche Gebäudereferenz
(Hauskoordinaten ohne postalische Anreicherung)**.

- Datei: `adressen.csv.gz`
- Datensätze: **1.406.086**
- Datensätze mit PLZ: **0**
- Datensätze ohne PLZ: **1.406.086**
- Datenstand laut eingebetteten Auftragsmetadaten: **30.03.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `ed9fdc8b44abe393792615c65a190b979db9d4304746b9d0c4d0ed40f08cba61`

Der Export ist bezüglich des heruntergeladenen amtlichen Landesbestands
vollständig, aber wegen der ausdrücklich fehlenden postalischen Anreicherung
ein **vorläufiger Adressbestand ohne Postleitzahlen**.

## Amtliche Quelle

- Produkt: **Amtliche Gebäudereferenz**
  beziehungsweise **Hauskoordinaten ohne postalische Anreicherung**
- Bereitsteller:
  **Landesamt für Vermessung und Geobasisinformation Rheinland-Pfalz
  (LVermGeoRP)**
- Open-Data-Seite:
  <https://lvermgeo.rlp.de/geodaten-geoshop/open-data>
- GeoShop-Produktseite:
  <https://geoshop.rlp.de/opendata-hk.html>
- GeoShop-Konfiguration:
  <https://geoshop.rlp.de/files/anpassungen/hvd/products/hk.json>
- Direktdownload:
  <https://geobasis-rlp.de/data/hk/current/zip/HAUSKOORDINATEN_RP.zip>
- Produktbeschreibung:
  <https://lvermgeo.rlp.de/produktinformationen/liegenschaftskataster/hauskoordinaten-/hausumringe>
- Metadatensatz:
  <https://vocabulary.geoportal.rlp.de/geonetwork/srv/api/records/a688600a-7996-4c71-98ab-368feb7977bc>
- Abrufdatum: **28.08.2026**
- Ursprungskoordinaten:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Die im GeoShop verwendete Konfigurationsdatei ordnet den Direktdownload der
Auswahlebene `vermkv:landesgrenze_rlp` zu und erläutert, dass der
Download-Link auf dieser Ebene stets das gesamte Bundesland umfasst.

Das Originalarchiv `HAUSKOORDINATEN_RP.zip` liegt bei:

- Größe: **26.578.807 Byte**
- SHA-256:
  `7ec6e57c0b1fbbab282ecf24f99aa963235294ef80bb13be7596c87cc4b58d3a`
- enthaltene Datendatei: `HAUSKOORDINATEN_RP_hk.csv`
- unkomprimierte Größe der Datendatei: **205.622.113 Byte**
- SHA-256 der Datendatei:
  `6795a5ba9319d288d8f95dec7e9c7d90e5afb55f89958a549bfcc3966a2010fd`
- interner Dateizeitstempel: **26.08.2026, 10:34:42 Uhr**

Das Archiv enthält außerdem die Original-Dateibeschreibung
`HAUSKOORDINATEN_RP_dat.pdf`, das Schlüsselverzeichnis
`HAUSKOORDINATEN_RP_schluessel.txt` sowie weitere PDF-, XML- und
CSV-Metadaten. Der Datenstand **30.03.2026** stammt aus der eingebetteten
Datei `HK_RP_0_.xml`.

Zusätzlich zur Originaldatei wurden folgende Abrufnachweise gespeichert:

- `geoshop-gebaeudereferenz.html`
- `geoshop-konfiguration-hauskoordinaten.json`
- `quellmetadaten-hauskoordinaten.html`
- `quellmetadaten-hauskoordinaten.xml`
- `produktbeschreibung-hauskoordinaten.html`
- `open-data-nutzungsbedingungen.html`
- `open-data-lizenzseite.html`
- `lizenz-dl-de-by-2.0.html`

## Inhalt und fehlende Postleitzahlen

Die GeoShop-Produktseite bezeichnet den offenen Datensatz ausdrücklich als
**Hauskoordinaten ohne postalische Anreicherung**. Die tatsächliche
Datendatei bestätigt dies:

- sie besitzt genau 20 Quellspalten
- enthalten sind Land, ehemaliger Regierungsbezirk, Kreis/Stadt, Gemeinde,
  Straße, Hausnummer, amtliche Objekt-ID und Koordinaten
- es gibt weder ein PLZ-Feld noch ein Feld für den postalischen Ortsnamen
- das Kataster-Ortsteilfeld `ott` ist in allen Datensätzen leer

Eine andere allgemeine Produktbeschreibung des LVermGeoRP beschreibt auch
postalisch angereicherte Hauskoordinaten. Diese Beschreibung darf nicht auf
den konkreten Open-Data-Download übertragen werden: Dessen Bezeichnung,
GeoShop-Information und reales Schema weisen die postalische Anreicherung
gerade nicht auf.

Deshalb bleibt `postleitzahl` im Export leer. `ortsname_post` enthält als
offengelegten Ersatz den amtlichen Gemeindenamen. Das ist keine Behauptung,
dass der Gemeindename dem postalischen Ortsnamen entspricht.

## Warum Hauskoordinaten statt Hausumrisse?

Hausumringe beschreiben Gebäudegrundrisse als Polygone. Sie enthalten nicht
die bereits strukturierte Liste aus Straße und Hausnummer und lösen die
fehlende postalische Anreicherung nicht. Die Gebäudereferenzen liefern
dagegen unmittelbar die amtliche Adresse, eine stabile Objekt-ID und die
zugehörige Koordinate. Ein räumlicher Verschnitt mit Hausumringen wäre daher
unnötig und könnte wegen Haupt-, Neben- oder Mehrfachgebäuden zusätzliche
Uneindeutigkeiten erzeugen.

## Lizenz und Weiterverwendung

Die auf der Open-Data-Seite angebotenen Geodaten und -dienste der
Vermessungs- und Katasterverwaltung Rheinland-Pfalz stehen unter der
**Datenlizenz Deutschland - Namensnennung - Version 2.0**:

<https://www.govdata.de/dl-de/by-2-0>

Der vom LVermGeoRP vorgegebene Quellenvermerk lautet:

> ©GeoBasis-DE / LVermGeoRP&lt;Jahr des Datenbezugs&gt;, dl-de/by-2-0,
> www.lvermgeo.rlp.de [Daten bearbeitet]

Für diesen transformierten und neu strukturierten Export wird er konkret so
geführt:

> ©GeoBasis-DE / LVermGeoRP2026, dl-de/by-2-0,
> www.lvermgeo.rlp.de, Daten bearbeitet; abgerufen am 28.08.2026.

Der gespeicherte Lizenztext liegt als `lizenz-dl-de-by-2.0.html` bei.

## Verarbeitung

1. Das vollständige Landesarchiv wurde über den in der amtlichen
   GeoShop-Konfiguration hinterlegten Direktlink heruntergeladen und mit
   `unzip -t` geprüft.
2. `HAUSKOORDINATEN_RP_hk.csv` wurde als UTF-8-Datei mit
   Semikolontrennung und 20 exakt geprüften Spalten eingelesen.
3. Sämtliche **1.406.086** Quellzeilen wurden ohne Filterung übernommen.
4. Landeskennung, UTM-Zone, Pflichtfelder und Koordinaten wurden für jede
   Zeile geprüft.
5. Die Koordinaten wurden von EPSG:25832 nach OGC CRS84 transformiert.
6. Da die Quelle keine postalischen Felder besitzt, bleibt `postleitzahl`
   leer und `ortsname_post` wird mit `gmd` belegt.
7. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
8. Das Ergebnis wurde byteweise (`LC_ALL=C`) sortiert und deterministisch
   mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | RP-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz und Gemeinde |
| `bundesland` | Konstante `Rheinland-Pfalz` |
| `landesschluessel` | Quellfeld `landschl` (`07`) |
| `postleitzahl` | leer; in der Quelle nicht vorhanden |
| `ortsname_post` | Gemeinde `gmd` als dokumentierter Fallback |
| `gemeindename` | Quellfeld `gmd` |
| `ortsteilname` | Quellfeld `ott`, im aktuellen Bestand vollständig leer |
| `strassenname` | Quellfeld `str` |
| `hausnummer` | Quellfeld `hnr` |
| `hausnummernzusatz` | Quellfeld `adz` |
| `longitude_wgs84` | transformiert aus `ostwert` |
| `latitude_wgs84` | transformiert aus `nordwert` |
| `datensatznummer` | eindeutige Quell-ID `oid` |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **1.406.086** Quell- und Ausgabedatensätze
- jede Quelldatenzeile besitzt genau 20 Spalten
- jede Ausgabedatenzeile besitzt genau 14 Spalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- keine PLZ-Spalte in der Quelle und keine scheinbare PLZ ergänzt
- keine leere Gemeinde, Straße oder Hausnummer
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- 2.300 Gemeinden
- 36 Kreise beziehungsweise kreisfreie Städte
- 3 ehemalige Regierungsbezirke
- Qualitätsklasse A: 1.336.485
- Qualitätsklasse B: 67.522
- Qualitätsklasse C: 2.079
- keine ungültige oder außerhalb von Rheinland-Pfalz liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

Alle 1.406.086 Datensätze besitzen keinen Kataster-Ortsteil; das Ausgabefeld
`ortsteilname` bleibt daher leer.

Es gibt 1.813 mehrfach vorkommende Anzeigeadressen mit insgesamt 1.903
zusätzlichen Vorkommen. Die betroffenen Objekte besitzen unterschiedliche
amtliche Quell-IDs und Hausschlüssel und wurden deshalb bewusst erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 abgerufenen
landesweiten Open-Data-Bestand mit dem in den Auftragsmetadaten ausgewiesenen
Stand 30.03.2026.
