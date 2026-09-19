# Amtliche Adresslisten

Eine zentrale Anleitung mit Voraussetzungen, Quellenübersicht, konkreten
Build-Befehlen und Hinweisen zur Reproduzierbarkeit steht in der
[Projekt-README](../README.md).

## Verzeichnisstruktur

```text
adressen/
  bund/
    basemap/
      <Abrufdatum>/
        adressen.csv.gz
        vergleich-laender/
    konsolidiert/
      <Abrufdatum>/
        adressen.csv.gz
        provenienz.csv.gz
        konflikte.csv.gz
        quellen.csv
  <Laendercode>/
    <Abrufdatum>/
      adressen.csv.gz
      QUELLE-UND-LIZENZ.md
      export-statistik.txt
      quellmetadaten-*.json
      lizenz-*.html
```

Verwendete Ländercodes:

| Code | Bundesland | Amtlicher Landesschlüssel |
|---|---|---:|
| `BW` | Baden-Württemberg | 08 |
| `BY` | Bayern | 09 |
| `BE` | Berlin | 11 |
| `BB` | Brandenburg | 12 |
| `HB` | Bremen | 04 |
| `HE` | Hessen | 06 |
| `HH` | Hamburg | 02 |
| `MV` | Mecklenburg-Vorpommern | 13 |
| `NI` | Niedersachsen | 03 |
| `NW` | Nordrhein-Westfalen | 05 |
| `RP` | Rheinland-Pfalz | 07 |
| `SH` | Schleswig-Holstein | 01 |
| `SL` | Saarland | 10 |
| `SN` | Sachsen | 14 |
| `ST` | Sachsen-Anhalt | 15 |
| `TH` | Thüringen | 16 |

Das Datum bezeichnet den Tag, an dem die Daten aus der dokumentierten
amtlichen Quelle abgerufen wurden. Jeder Datumsordner ist ein
eigenständiger, reproduzierbar dokumentierter Datenstand.

Alle `adressen.csv.gz`-Dateien besitzen unabhängig von ihrem Quelldatenformat
dasselbe 14-spaltige CSV-Schema. Quellspezifische Transformationen und
Einschränkungen stehen in der Dokumentation des jeweiligen Datenstands.

Der MV-Datenstand vom 27.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Für ihn gelten außerdem die in seiner Dokumentation
beschriebenen Weitergabebeschränkungen.

Der NI-Datenstand vom 28.08.2026 ist ebenfalls ein vorläufiger Export ohne
Postleitzahlen. Seine Koordinaten wurden aus kartografischen
Hausnummernpositionen und dokumentierten Fallbacks abgeleitet.

Der ST-Datenstand vom 28.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Der offen herunterladbare Gebäudereferenzbestand enthält
keine postalischen Angaben; die postalisch ergänzten Hauskoordinaten werden
im kostenlosen Angebot nur als nicht abfragbarer WMS dargestellt. Der in den
Metadaten zusätzlich genannte WFS ist als kostenpflichtig beschrieben und
lieferte beim Abruf keine Features über den anonymen Zugang.

Der HE-Datenstand vom 28.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Das amtliche Produkt trägt ausdrücklich die Bezeichnung
„Hauskoordinaten ohne Postalische Angaben“; auch der hessische
INSPIRE-Adress-WFS enthält keine `PostalDescriptor`-Objekte.

Der TH-Datenstand vom 28.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Das amtliche HK-Format besitzt zwar postalische Spalten,
die Thüringer Metadaten kennzeichnen die Postleitzahl jedoch als „bisher
noch ohne“ und sämtliche postalischen Felder sind im aktuellen Bestand leer.

Der SN-Datenstand vom 28.08.2026 enthält den vollständigen sachsenweiten
Hauskoordinatenbestand von GeoSN. Alle 990.090 Datensätze besitzen eine
gültige fünfstellige Postleitzahl und einen postalischen Ortsnamen.

Der RP-Datenstand vom 28.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Das amtliche Open-Data-Produkt heißt ausdrücklich
„Hauskoordinaten ohne postalische Anreicherung“; die heruntergeladene
Datendatei enthält weder ein PLZ-Feld noch einen postalischen Ortsnamen.

Der SL-Datenstand vom 28.08.2026 enthält den vollständigen landesweiten
Hauskoordinatenbestand des LVGL. 334.991 von 335.022 Datensätzen besitzen
eine gültige fünfstellige Postleitzahl; bei 31 amtlichen Objekten fehlen PLZ
und postalischer Ortsname bereits in der Quelle.

Der BW-Datenstand vom 28.08.2026 ist ein vorläufiger Export ohne
Postleitzahlen. Das LGL kennzeichnet den landesweiten Hauskoordinatenbestand
ausdrücklich als „ohne postalische Angaben“; sämtliche postalischen Felder
sind leer. Von 3.382.566 Datensätzen besitzen 291.809 die Qualitätsklasse C
und damit eine katasterinterne Pseudonummer statt einer regulären amtlichen
Hausnummer.

Der BY-Datenstand vom 28.08.2026 wurde aus dem offenen deutschlandweiten
Produkt basemap.de Web Vektor abgeleitet. Er enthält 3.761.180 amtliche
Adresspunkte mit Straße, Hausnummer, Ort und Koordinate, jedoch keine
Postleitzahlen, Gemeindeschlüssel oder fachlichen Quell-IDs. Die zuvor
geprüften landeseigenen Hauskoordinaten- und INSPIRE-Dienste bleiben
zugangs- beziehungsweise gebührenbeschränkt.

Der gemeinsame basemap.de-Datenstand unter `bund/basemap/2026-08-28`
enthält 22.750.793 Adresspunkte aus allen 16 Ländern. Er dient als
bundesweite Vergleichsquelle. Die dortigen Vergleichsergebnisse verwenden
normalisierte Schlüssel aus Ort, Straße und Hausnummer sowie die räumliche
Distanz passender Punkte. Da basemap.de weder PLZ noch Gemeindeschlüssel
liefert, ersetzt dieser Bestand die postalisch angereicherten Länderquellen
nicht.

Der konsolidierte Bundesdatenstand unter `bund/konsolidiert/2026-08-28`
verbindet die offen nutzbaren Länderbestände mit basemap.de als
Ergänzungsquelle. Er enthält 23.252.486 Adressobjekte, davon 8.528.354 mit
einer aus der jeweiligen Länderquelle bestätigten PLZ. Eine gleich große
Provenienzdatei dokumentiert je Datensatz Quellen, Lizenzen, Original-IDs,
Abgleichregel und Feldherkunft. Bayern und Mecklenburg-Vorpommern stammen im
konsolidierten Bestand ausschließlich aus basemap.de; der MV-Adress-WFS wurde
wegen seines nicht eindeutig offenen Weitergaberechts bewusst ausgeschlossen.
