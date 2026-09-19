# Adressliste Niedersachsen – Abruf vom 28.08.2026

## Vorläufiger Datenstand

Dieser Export enthält alle zum Abrufzeitpunkt vom offenen ALKIS-WFS des LGLN
gemeldeten Objekte der Art `AX_LagebezeichnungMitHausnummer`.

Das Feld `postleitzahl` ist in allen Datensätzen absichtlich leer. Die
Postleitzahl ist nicht Bestandteil des ALKIS-Adressobjekts. Das separate
LGLN-Produkt „Hauskoordinaten“ enthält laut Produktinformation postalische
Attribute der Deutschen Post Direkt GmbH, ist aber nicht als Download im
geprüften OpenGeoData-/STAC-Angebot aufgeführt und wurde nicht verwendet.

Die Koordinaten dieser Datei sind ebenfalls nicht mit den Koordinaten des
separaten Produkts „Hauskoordinaten“ gleichzusetzen. Sie wurden aus
kartografischen ALKIS-Hausnummernpositionen und, wo diese fehlten, aus
dokumentierten Geometrie- beziehungsweise Zentroid-Fallbacks abgeleitet.

## Dateien

### `adressen.csv.gz`

- Datensätze: **2.643.144**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `efed9f677e902d628f377d471ffcb54699e11b5f938815dd2d27dd1379e6aec6`
- Postleitzahlen: **0 befüllt, 2.643.144 leer**

### `koordinatenherkunft.csv.gz`

Diese Zusatzdatei ordnet jeder `datensatznummer` die konkrete Herkunft ihrer
Koordinate zu. Sie besitzt 2.643.144 Datenzeilen und die Spalten
`datensatznummer` und `koordinatenherkunft`.

- SHA-256:
  `a1c9ac7957048fc07dfbed705274d63e516e4e17e421bfd627897b57d16eb912`

## Quellen

### Adressobjekte und Geometrien

- Dienst: **ALKIS WFS Simple Features**
- Bereitsteller: **Landesamt für Geoinformation und Landesvermessung
  Niedersachsen (LGLN)**
- WFS:
  <https://opendata.lgln.niedersachsen.de/doorman/noauth/alkis_wfs_sf>
- Verwendete Objektarten:
  - `AX_LagebezeichnungMitHausnummer`
  - `AP_PTO`
  - `AX_Gebaeude`
  - `AX_Flurstueck`
  - `AX_LagebezeichnungKatalogeintrag`
  - `AX_Gemeinde`
- WFS-Zeitstempel während des Adressabrufs:
  **2026-08-28T01:20:40.279+02:00**
- WFS-Zeitstempel während des Koordinatenabrufs:
  **2026-08-28T02:07:07.979+02:00**
- Ursprüngliches Koordinatenreferenzsystem:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**
- Ausgabekoordinaten:
  **WGS 84, Längengrad/Breitengrad**; vom WFS in EPSG:4326 transformiert

`wfs-capabilities-alkis.xml` enthält die beim Abruf gespeicherte
GetCapabilities-Antwort.

### Straßen- und Gemeindenamen

- STAC-Collection: **ALKIS Straßennamen**
- Collection:
  <https://alkis.stac.lgln.niedersachsen.de/collections/alkis-strassennamen>
- Quelldatei:
  <https://strassennamen.s3.eu-de.cloud-object-storage.appdomain.cloud/nds_strassennamen.zip>
- Datenstand laut STAC: **21.08.2026**
- SHA-256 der beigelegten Quelldatei:
  `57308c8ed8e6e30018e2ad72da56b998e7e269edaa77107639c61ccee5bf7a19`

Die landesweite Liste enthält 178.683 Zeilen mit Landkreis, Gemeinde,
Straßenname und den jeweiligen Schlüsseln. 125 noch nicht in dieser Liste
enthaltene Straßenschlüssel wurden direkt über
`AX_LagebezeichnungKatalogeintrag` aufgelöst. Zwei zugehörige
Gemeindebezeichnungen wurden über `AX_Gemeinde` ergänzt.

## Lizenz

Die GetCapabilities-Antwort des verwendeten offenen ALKIS-WFS nennt
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)**:

<https://creativecommons.org/licenses/by/4.0/>

Auch die STAC-Collection `alkis-strassennamen` weist `CC-BY-4.0` aus.
Abschnitt 7 der Allgemeinen Geschäfts- und Nutzungsbedingungen des LGLN
bestätigt, dass die im OpenGeoData-Portal aufgeführten offenen Geodaten unter
CC BY 4.0 kostenfrei intern und extern genutzt werden dürfen.

Der vom WFS vorgegebene Quellenvermerk lautet mit dem Jahr des Datenbezugs:

> LGLN (2026) Creative Commons Namensnennung – 4.0 International
> (CC BY 4.0)

Für diesen bearbeiteten Export wird der folgende ausführlichere
Quellenvermerk empfohlen:

> LGLN (2026), ALKIS Open Data und ALKIS Straßennamen,
> Creative Commons Namensnennung – 4.0 International (CC BY 4.0);
> Quelle verändert, abgerufen am 28.08.2026.

Beigelegt sind:

- `AGNB-LGLN.html`: gespeicherte Allgemeine Geschäfts- und
  Nutzungsbedingungen
- `lizenz-cc-by-4.0.html`: gespeicherter CC-BY-4.0-Lizenztext
- `stac-alkis-strassennamen.json`: STAC-Collection
- `stac-alkis-strassennamen-item.json`: STAC-Item der Quelldatei
- `produktinformation-hauskoordinaten.html`: Produktinformation zur
  Abgrenzung vom nicht verwendeten separaten Hauskoordinatenprodukt

## Verarbeitung der Adressen

Der WFS begrenzt eine Antwort auf 10.000 Objekte und unterstützt laut
Capabilities kein Result-Paging. Deshalb wurde der Bestand verlustfrei
partitioniert:

- Ermittlung von 1.127 amtlichen Gemeindeschlüsseln aus
  `AX_Gemeinde` und der Straßenschlüsselliste
- 945 Gemeindeschlüssel enthielten Adressen
- weitere Teilung großer Gemeinden anhand disjunkter Präfixe des
  ALKIS-Lageschlüssels
- 1.500 Teilabfragen mit jeweils höchstens 9.000 Objekten
- Prüfung der Summe jeder Partition gegen `resultType=hits`

Die Summe der Teilabfragen beträgt exakt 2.643.144 und stimmt mit der
landesweiten Trefferzahl des WFS überein. Alle `gml:id` sind eindeutig.

Straßen- und Gemeindenamen wurden über den zusammengesetzten
ALKIS-Lageschlüssel mit der Straßenschlüsselliste beziehungsweise den
ergänzend abgerufenen Katalogeinträgen verbunden.

## Herkunft und Aussagekraft der Koordinaten

| Herkunft | Datensätze | Bedeutung |
|---|---:|---|
| `AP_PTO-HNR` | 2.620.591 | räumlich abgerufener kartografischer Hausnummernpunkt |
| `AP_PTO-direct` | 444 | direkt über das Adressobjekt referenzierter Darstellungspunkt |
| `AX_Gebaeude-centroid` | 1.467 | Zentroid des referenzierten Gebäudepolygons |
| `AX_Flurstueck-centroid` | 19.822 | Zentroid des referenzierten Flurstückspolygons |
| `street-centroid` | 776 | Mittelwert vorhandener Adresskoordinaten derselben Straße |
| `municipality-centroid` | 40 | Mittelwert vorhandener Adresskoordinaten derselben Gemeinde |
| `county-centroid` | 4 | Mittelwert vorhandener Adresskoordinaten desselben Landkreises |

Die 2.622.708 vom WFS gemeldeten `AP_PTO`-Objekte der Art `HNR` wurden über
488 räumliche Teilabfragen abgerufen. Eine einzige Überlappung an einer
Teilgebietsgrenze wurde anhand der Adress-ID dedupliziert. 2.118
Darstellungspunkte bezogen sich nicht auf einen aktuellen Datensatz des
exportierten Adressbestands und wurden nicht übernommen.

Kartografische Darstellungspunkte dienen der Platzierung von
Hausnummerntexten. Sie können aus Darstellungsgründen neben dem Gebäude
liegen und sind keine amtlichen Hauskoordinaten des gleichnamigen separaten
Produkts. Gebäude-, Flurstücks-, Straßen-, Gemeinde- und Landkreiszentroide
sind abgeleitete Näherungswerte mit entsprechend geringerer räumlicher
Aussagekraft.

Die konkrete Herkunft jeder Koordinate steht in
`koordinatenherkunft.csv.gz`.

## Feldzuordnung

| Einheitliche Spalte | NI-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer und Gemeinde; ohne PLZ |
| `bundesland` | Konstante `Niedersachsen` |
| `landesschluessel` | Konstante `03` |
| `postleitzahl` | leer; nicht Bestandteil der verwendeten ALKIS-Quelle |
| `ortsname_post` | ersatzweise amtlicher Gemeindename |
| `gemeindename` | Straßenschlüsselliste beziehungsweise `AX_Gemeinde` |
| `ortsteilname` | leer; nicht separat geliefert |
| `strassenname` | Straßenschlüsselliste beziehungsweise ALKIS-Katalogeintrag |
| `hausnummer` | numerischer Anfang von `hausnummer` |
| `hausnummernzusatz` | verbleibender Zusatz von `hausnummer` |
| `longitude_wgs84` | transformierte beziehungsweise abgeleitete Koordinate |
| `latitude_wgs84` | transformierte beziehungsweise abgeleitete Koordinate |
| `datensatznummer` | `gml:id` von `AX_LagebezeichnungMitHausnummer` |
| `hausschluessel` | zusammengesetzter ALKIS-Lageschlüssel und Hausnummer |

`ortsname_post` ist bis zur Ergänzung einer postalischen Quelle lediglich
ein Ersatzwert und kein bestätigter postalischer Ortsname.

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- 2.643.144 Datenzeilen mit jeweils 14 Spalten
- identische Kopfzeile wie die Exporte für BE, BB, SH und MV
- 2.643.144 leere Postleitzahlen wie vorgesehen
- keine doppelte `datensatznummer`
- keine doppelten `hausschluessel`
- keine leere Gemeinde, Straße oder Hausnummer
- keine ungültige oder außerhalb Niedersachsens liegende Koordinate
- byteweise alphabetische Sortierreihenfolge bestätigt
- SHA-256-Prüfsumme nach der Konvertierung bestätigt

Es gibt 2.333 mehrfach vorkommende zusammengesetzte Anzeigeadressen.
Die amtlichen Objekt- und Hausschlüssel sind jeweils eindeutig; die
Datensätze wurden deshalb erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 vom offenen
ALKIS-WFS gemeldeten Bestand `AX_LagebezeichnungMitHausnummer`.
