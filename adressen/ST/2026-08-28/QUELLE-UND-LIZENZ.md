# Adressliste Sachsen-Anhalt – Abruf vom 28.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **664.459**
- Quelldatenstand: **04.06.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `b7b6adb07e59af69647ea57a44ea6c31c99a8b3e299b419bfd35c84e34e12376`
- Status: **vorläufiger Export ohne Postleitzahlen**

Die Liste bildet die offen bereitgestellten Gebäudereferenzen ab. Diese
definieren die Position eines Gebäudes mit der im Liegenschaftskataster
geführten Straße und Hausnummer. Sie sind keine Liste aller physischen
Gebäude: Gebäude ohne Hausnummer sind nicht enthalten.

## Quelle

- Produkt: **Gebäudereferenzen Sachsen-Anhalt**
- Bereitsteller: **Landesamt für Vermessung und Geoinformation
  Sachsen-Anhalt (LVermGeo)**
- Open-Data-Portal:
  <https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html>
- Download:
  <https://www.lvermgeo.sachsen-anhalt.de/datei/anzeigen/id/258997,501/gebaeudereferenzen.zip>
- Abrufdatum: **28.08.2026**
- Quelldatei innerhalb des ZIP-Archivs: `adressen-st.txt`
- Auslesedatum in sämtlichen Quellzeilen: **04.06.2026**
- Quellformat: semikolongetrennte UTF-8-Textdatei
- Koordinatenreferenzsystem der Quelle:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**
- Koordinatenreferenzsystem der erzeugten CSV:
  **OGC CRS84** (Längengrad, Breitengrad; WGS-84-kompatibel)

Die originale Downloaddatei liegt als `gebaeudereferenzen.zip` bei. Ihre
SHA-256-Summe lautet:

`9e5a874097ee5a50e955c99aa3cd6f335c5bcd33de569e22fc152cbd7a6def95`

Zur Nachvollziehbarkeit liegen außerdem bei:

- `open-data.html`: gespeicherte Open-Data-Angebotsseite
- `datenformatbeschreibung-gebaeudereferenzen.pdf`
- `produktbeschreibung-amtliche-hauskoordinaten.html`
- `download-http-header.txt`
- `wms-capabilities-inspire-adressen.xml`
- `quellmetadaten-inspire-adressen.xml`
- `quellmetadaten-inspire-wms-adressen.xml`
- `wfs-capabilities-inspire-adressen.xml`
- `quellmetadaten-inspire-wfs-adressen.xml`

## Fehlende Postleitzahlen

Das LVermGeo unterscheidet auf dem Portal zwischen:

1. **Amtlichen Hauskoordinaten**, die um postalische Angaben einschließlich
   PLZ ergänzt sind, und
2. **Gebäudereferenzen**, die ausdrücklich die Geobasisdaten des
   Liegenschaftskatasters ohne postalische Informationen bereitstellen.

Für die postalisch ergänzten Hauskoordinaten ist auf der Open-Data-Seite
derzeit der kostenfrei nutzbare INSPIRE-Darstellungsdienst
`INSPIRE-WMS ST Adressen Hauskoordinaten` mit Datenstand September 2025
aufgeführt. Er fällt unter die für die kostenfrei bereitgestellten
Geobasisdaten genannte Datenlizenz Deutschland – Namensnennung – Version
2.0. Ein WMS liefert jedoch nur Kartenbilder und keinen vollständigen,
verlustfreien Vektordownload aller Attribute. Die Schicht `AD.Address` ist
laut WMS-Capabilities nicht abfragbar (`queryable="0"`); ein am 28.08.2026
ausgeführter `GetFeatureInfo`-Test wurde vom Dienst mit
`LayerNotQueryable` abgewiesen.

Die Datensatzmetadaten verweisen zusätzlich auf den nicht auf der
Open-Data-Seite verlinkten Dienst
`INSPIRE-WFS ST Adressen Hauskoordinaten`. Dessen eigene MetaVer-Beschreibung
bezeichnet ihn ausdrücklich als **kostenpflichtigen Dienst**. Der anonyme
Endpunkt liefert zwar Capabilities und Schemata, wies am 28.08.2026 aber
sämtliche getesteten `GetFeature`-Anfragen für die beworbenen Feature-Typen
mit „FeatureType ... is not available for the WFS“ zurück. Er stellt damit
derzeit keinen frei nutzbaren Massendownload dar. Die Produktseite beschreibt
die vollständige Textdatei der Amtlichen Hauskoordinaten als antragsbezogene
Abgabe.

Der landesweite, direkt herunterladbare Open-Data-Bestand
`Gebäudereferenzen` enthält 664.459 Adressen mit Straße, Hausnummer,
Gemeinde und Koordinate. Seine fünf reservierten postalischen Felder sind in
allen Datensätzen leer. Es wurde keine Postleitzahl aus einer externen oder
nicht eindeutig offen lizenzierten Quelle ergänzt. Daher ist
`postleitzahl` transparent leer.

`ortsname_post` wurde – wie bei den anderen vorläufigen Länderexporten ohne
PLZ – ersatzweise mit dem amtlichen Gemeindename belegt. Dies ist keine
postalisch validierte Ortsangabe.

### Prüfung anderer Open-Data-Produkte

Die vollständige Open-Data-Seite wurde zusätzlich nach einer anderen
Postleitzahlenquelle durchsucht. Weder `PLZ` noch `Postleitzahl` kommen in
der Produktübersicht vor. Die naheliegendsten Alternativen wurden auch
inhaltlich geprüft:

- Das tagaktuelle `Gemeinde- und Gemarkungsverzeichnis` enthält nur
  Kreis-, Gemeinde- und Gemarkungsschlüssel sowie deren Namen.
- Die tagaktuellen `Katalogdaten ALKIS` enthalten keine PLZ- oder
  Postleitzahlfelder.
- Verwaltungsgrenzen und INSPIRE-Verwaltungseinheiten bilden administrative
  Grenzen ab, keine postalischen Zustellgebiete.
- Geographische Bezeichnungen enthalten Orts- und Landschaftsnamen, aber
  keine vollständige Zuordnung von Gebäudeadressen zu Postleitzahlen.
- Gebäude, 3D-Gebäude und Hausumringe enthalten Gebäudegeometrien, jedoch
  keine postalische Adresse.
- Der offene ALKIS-WFS stellt Flurstücke, Gebäude und tatsächliche Nutzung
  bereit; Postleitzahlen gehören nicht zum ALKIS-Gebäudeobjekt.

Damit ist auf der dokumentierten Open-Data-Seite keine zweite frei
herunterladbare Quelle erkennbar, mit der sich die 664.459
Gebäudereferenzen vollständig und amtlich um PLZ ergänzen ließen.

## Lizenz

Für die kostenfrei bereitgestellten Geobasisdaten gelten die
**Datenlizenz Deutschland – Namensnennung – Version 2.0** und die
Nutzungsbedingungen des LVermGeo:

- <https://www.govdata.de/dl-de/by-2-0>
- <https://www.lvermgeo.sachsen-anhalt.de/de/nutzungsbedingungen.html>

Die beim Abruf gespeicherten Texte liegen als
`lizenz-dl-de-by-2.0.html` und `nutzungsbedingungen-lvermgeo.pdf` bei.

Der Quellenvermerk lautet:

> © GeoBasis-DE / LVermGeo ST

Für die Weitergabe dieses bearbeiteten Exports wird der ausführlichere
Quellenvermerk empfohlen:

> © GeoBasis-DE / LVermGeo ST; Gebäudereferenzen Sachsen-Anhalt,
> Datenstand 04.06.2026, abgerufen am 28.08.2026 über
> <https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html>;
> Datenlizenz Deutschland – Namensnennung – Version 2.0,
> <https://www.govdata.de/dl-de/by-2-0>; Quelle verändert.

## Verarbeitung

Der vollständige ZIP-Massendownload wurde verarbeitet. Folgende Änderungen
wurden vorgenommen:

- Auslesen aller 664.459 Quelldatensätze
- Umwandlung in das einheitliche 14-spaltige UTF-8-CSV-Schema
- Übernahme der amtlichen Verwaltungs-, Straßen- und Hausnummernfelder
- Verwendung des Gemeindennamens für `ortsname_post`, da sämtliche
  postalischen Quellfelder leer sind
- Transformation der Koordinaten von EPSG:25832 nach OGC CRS84
- Zusammensetzung von `vollstaendige_adresse`
- Zusammensetzung eines stabilen `hausschluessel` aus den amtlichen
  Schlüssel- und Adresskomponenten
- byteweise Sortierung aller Datenzeilen (`LC_ALL=C`)
- deterministische Gzip-Komprimierung

Das Ortsteil-Namensfeld der Quelle ist ebenfalls in sämtlichen Datensätzen
leer. Der vorhandene Ortsteilschlüssel `0000` wurde nicht als Name
interpretiert; `ortsteilname` bleibt daher leer.

## Feldzuordnung

| Einheitliche Spalte | Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz und Gemeinde |
| `bundesland` | Quellfeld Land (`Sachsen-Anhalt`) |
| `landesschluessel` | Quellfeld Landesschlüssel (`15`) |
| `postleitzahl` | leer; postalische Quellfelder sind leer |
| `ortsname_post` | ersatzweise amtlicher Gemeindename |
| `gemeindename` | amtlicher Gemeindename |
| `ortsteilname` | Ortsteilname der Quelle, vollständig leer |
| `strassenname` | Straßenname |
| `hausnummer` | Hausnummer |
| `hausnummernzusatz` | Adresszusatz |
| `longitude_wgs84` | transformiert aus Ostwert |
| `latitude_wgs84` | transformiert aus Nordwert |
| `datensatznummer` | bundesweit eindeutige Quell-ID |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- exakt 664.459 Datenzeilen entsprechend der Quelldatei
- jede Datenzeile besitzt genau 14 Spalten
- gemeinsamer Header aller Länderexporte bestätigt
- 664.459 leere PLZ entsprechend dem dokumentierten Quellinhalt
- sämtliche reservierten postalischen Quellfelder leer
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- keine doppelte zusammengesetzte Anzeigeadresse
- keine leere Gemeinde, Straße oder Hausnummer
- 218 Gemeinden und 14 Landkreise beziehungsweise kreisfreie Städte
- alle Quelldatensätze mit Qualitätsstufe `A`
- keine ungültige oder außerhalb Sachsen-Anhalts liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- SHA-256-Prüfsumme nach der Komprimierung bestätigt

Die Vollständigkeit bezieht sich auf den am 28.08.2026 über die dokumentierte
Downloadadresse bereitgestellten Datenstand vom 04.06.2026.
