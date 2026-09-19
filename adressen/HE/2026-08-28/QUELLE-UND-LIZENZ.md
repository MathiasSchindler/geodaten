# Adressliste Hessen – Abruf vom 28.08.2026

## Vorläufiger Datenstand ohne Postleitzahlen

Dieser Export enthält alle **1.630.801** Datensätze des am 28.08.2026 im
amtlichen Downloadcenter angebotenen Produkts **„Hauskoordinaten ohne
Postalische Angaben-2026-01“**.

Das Feld `postleitzahl` ist in sämtlichen Datensätzen bewusst leer. Die
verwendete Quelldatei enthält weder Postleitzahl noch postalischen Ortsnamen.
Auch der landesweite hessische INSPIRE-Adress-WFS enthält keine
`PostalDescriptor`-Objekte.

## Dateien

### `adressen.csv.gz`

- Datensätze: **1.630.801**
- Datensätze mit PLZ: **0**
- Datensätze ohne PLZ: **1.630.801**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `650976b0a17b116713f64f4b36477509bdfc8f758427859e1eaa8a66169e50d1`

### Originalquelle

- Datei: `Hauskoordinaten-ohne-Postalische-Angaben-2026-01.zip`
- komprimierte Quelldatei: **27,4 MB**
- enthaltene TXT-Datei: **267.702.243 Byte**
- SHA-256:
  `ddbefd1e4826bc26e7ab19dd0c7fb920674cbc93d3e7b4f671bda4832ed1a490`

## Amtliche Quelle

- Produkt: **Hauskoordinaten ohne Postalische Angaben (txt)**
- Bereitsteller:
  **Hessisches Landesamt für Bodenmanagement und Geoinformation (HLBG)**
- Downloadcenter:
  <https://www.gds.hessen.de/INTERSHOP/web/WFS/HLBG-Geodaten-Site/de_DE/-/EUR/ViewDownloadcenter-Start?path=Liegenschaftskataster/Hauskoordinaten%20ohne%20Postalische%20Angaben%20(txt)>
- Produktbeschreibung:
  <https://hvbg.hessen.de/liegenschaftskataster/hauskoordinaten>
- abgerufene Version:
  **Hauskoordinaten ohne Postalische Angaben-2026-01**
- Datum im Downloadcenter: **24.06.2026**
- interner Zeitstempel der TXT-Datei: **23.06.2026, 14:57 Uhr**
- Abrufdatum: **28.08.2026**
- Ursprungskoordinaten:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Die Versionsbezeichnung `2026-01` wird unverändert aus dem Dateinamen
übernommen. Da die TXT-Datei kein separates Datenstandsfeld enthält, wird
daraus kein genauer Stichtag abgeleitet. Das Veröffentlichungsdatum im
Downloadcenter und der interne Dateizeitstempel werden zusätzlich angegeben.

Beigelegt sind:

- `quellmetadaten-hauskoordinaten.json`
- `quellmetadaten-hauskoordinaten.xml`
- `downloadcenter-hauskoordinaten.html`
- `produktbeschreibung-hauskoordinaten.html`

## Warum Hauskoordinaten statt vollständigem ALKIS?

Der vom Nutzer vorgeschlagene hessische ALKIS-Bestand enthält die benötigten
Lagebezeichnungen, Hausnummern, Verwaltungsbezüge und Geometrien. Diese
Informationen liegen dort jedoch verteilt über mehrere verknüpfte
ALKIS-Objektarten vor. Postleitzahlen sind auch im ALKIS nicht Bestandteil
des amtlichen Adressobjekts.

Die separat angebotenen Hauskoordinaten sind bereits eine landesweite,
flache und eindeutig identifizierte Liste georeferenzierter
Gebäudeadressen. Sie liefern daher dieselben benötigten katasterbezogenen
Adressfelder wesentlich kompakter und ohne aufwendige Rekonstruktion aus
dem gesamten ALKIS-NAS-Bestand.

## Abgrenzung zu postalisch ergänzten Hauskoordinaten

Die amtliche hessische Produktbeschreibung führt für den freien
Landesbestand folgende Inhalte auf:

- Kennung und eindeutige Nummer
- Land, Regierungsbezirk, Kreis und Gemeinde
- Straße, Hausnummer und Adressierungszusatz
- ETRS89-/UTM-Koordinaten

Sie weist gesondert darauf hin, dass das bundesweite Produkt
**Amtliche Hauskoordinaten Deutschland (HK-DE)** zusätzlich Postleitzahl,
postalischen Ortsnamen und postalischen Ortszusatz enthält. Dieses Produkt
wird durch die Zentrale Stelle Hauskoordinaten und Hausumringe zu eigenen
Gebühren- und Lizenzmodellen bereitgestellt. Es wurde nicht verwendet.

Kommunale offene Daten, insbesondere der PLZ-haltige
Hauskoordinatenbestand der Stadt Frankfurt am Main, wurden ebenfalls nicht
eingemischt. Eine solche Ergänzung würde nur einen Teil Hessens postalisch
anreichern und unterschiedliche Herausgeber und Datenstände in einem
Landesbestand vermengen.

## Gegenprüfung am INSPIRE-WFS

- Dienst: **INSPIRE-WFS HE Adressen Hauskoordinaten**
- WFS:
  <https://inspire-hessen.de/ows/services/org.2.19698713-4b13-4938-a9db-96bfdc996451_wfs>
- Zeitpunkt der Zählabfragen:
  **28.08.2026, 09:15:41–09:15:42 UTC**
- `ad:Address`: **1.628.122**
- `ad:PostalDescriptor`: **0**

Der WFS ist eine INSPIRE-Umsetzung des Hauskoordinatenbestands. Ein
gezielt abgerufenes Beispielobjekt enthält Komponenten für Straße und
Verwaltungseinheiten, aber keine postalische Komponente. Die Dateien
`wfs-address-count.xml`, `wfs-postaldescriptor-count.xml` und
`wfs-example-address.xml` halten diese Prüfung fest.

Die WFS-Zählung liegt um 2.679 Objekte unter der heruntergeladenen
TXT-Datei. Für diesen Export ist die explizit heruntergeladene Quelldatei
maßgeblich: Sämtliche 1.630.801 Zeilen wurden übernommen, ohne den
Downloadbestand anhand eines anders aufbereiteten Dienstes zu filtern.

Das Beispielobjekt `DEHE06180000xaUk` bestätigt außerdem die
Koordinatentransformation:

- TXT-Quelle: Ostwert `534215.816`, Nordwert `5685098.32`
- Export: `9.49096138`, `51.31615869`
- INSPIRE-WFS: `9.490961`, `51.316159`

## Lizenz

Der Datensatz ist in den amtlichen Metadaten als offen und mit der
**Datenlizenz Deutschland – Zero – Version 2.0** ausgewiesen:

<https://www.govdata.de/dl-de/zero-2-0>

Die Lizenz erlaubt jede Nutzung ohne Einschränkungen oder Bedingungen. Eine
Namensnennung ist nicht vorgeschrieben. Zur transparenten Dokumentation wird
dennoch folgender Quellenhinweis empfohlen:

> Hessisches Landesamt für Bodenmanagement und Geoinformation (HLBG),
> Hauskoordinaten ohne Postalische Angaben, Version 2026-01,
> abgerufen am 28.08.2026; Datenlizenz Deutschland – Zero – Version 2.0;
> bearbeiteter Export.

Der gespeicherte Lizenztext liegt als `lizenz-dl-de-zero-2.0.html` bei.

## Verarbeitung

1. Der vollständige ZIP-Massendownload wurde abgerufen und seine Integrität
   mit `unzip -t` geprüft.
2. Die einzige enthaltene TXT-Datei wurde als UTF-8-CSV mit 20 Quellspalten
   gestreamt eingelesen.
3. Alle 1.630.801 Quellzeilen wurden ohne Filterung übernommen.
4. Die Koordinaten wurden von EPSG:25832 nach OGC CRS84 transformiert.
5. Die Daten wurden in das gemeinsame 14-spaltige CSV-Schema überführt.
6. Quell-ID, Hausschlüssel und Anzeigeadresse wurden unabhängig sortiert und
   auf Mehrfachvorkommen geprüft.
7. Das Ergebnis wurde byteweise (`LC_ALL=C`) sortiert und deterministisch
   mit Gzip komprimiert.

## Feldzuordnung

| Einheitliche Spalte | HE-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz und Gemeinde; ohne PLZ |
| `bundesland` | Quellfeld `land` |
| `landesschluessel` | Quellfeld `landschl` (`06`) |
| `postleitzahl` | leer; ausdrücklich nicht Bestandteil des Produkts |
| `ortsname_post` | ersatzweise amtlicher Gemeindename |
| `gemeindename` | Quellfeld `gmd` |
| `ortsteilname` | Quellfeld `ott` |
| `strassenname` | Quellfeld `str` |
| `hausnummer` | Quellfeld `hnr` |
| `hausnummernzusatz` | Quellfeld `adz` |
| `longitude_wgs84` | transformiert aus `ostwert` |
| `latitude_wgs84` | transformiert aus `nordwert` |
| `datensatznummer` | eindeutige Quell-ID `oid` |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

`ortsname_post` ist lediglich ein Ersatzwert aus dem amtlichen
Gemeindenamen und kein bestätigter postalischer Ortsname.

## Qualitätsprüfung

- Gzip- und ZIP-Integrität erfolgreich
- exakt **1.630.801** Datenzeilen mit jeweils 14 Spalten
- gemeinsame Kopfzeile aller Länderexporte bestätigt
- **1.630.801** leere Postleitzahlen wie vorgesehen
- keine leere Gemeinde, kein leerer Ortsteil, keine leere Straße und keine
  leere Hausnummer
- keine doppelte `datensatznummer`
- 423 Gemeinden
- 27 Kreise beziehungsweise kreisfreie Städte
- 3 Regierungsbezirke
- Qualitätsklasse A: 1.588.801
- Qualitätsklasse B: 42.000
- keine ungültige oder außerhalb Hessens liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- Quell- und Export-Prüfsummen bestätigt

Der Bestand enthält 46 doppelte zusammengesetzte Hausschlüssel und
81 doppelte Anzeigeadressen, jeweils mit genau einem zusätzlichen
Vorkommen. Die amtlichen Quell-IDs sind dabei verschieden. Diese Objekte
wurden deshalb nicht dedupliziert.

Die Vollständigkeit bezieht sich auf die am 28.08.2026 im Downloadcenter
bereitgestellte Datei `Hauskoordinaten ohne Postalische Angaben-2026-01.zip`.
