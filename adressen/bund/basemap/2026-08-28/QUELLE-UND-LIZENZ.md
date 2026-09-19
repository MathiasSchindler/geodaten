# Deutschlandweite Adressliste aus basemap.de - Abruf vom 28.08.2026

## Ergebnis

Der Export enthält **22.750.793** exakt unterschiedliche Adresspunkte aus
allen 16 Ländern aus dem offenen amtlichen Produkt
**basemap.de Web Vektor**.

- Datei: `adressen.csv.gz`
- Kachelobjekte vor Entfernung byteidentischer Wiederholungen: **22.758.976**
- unterschiedliche exportierte Datensätze: **22.750.793**
- Datensätze mit Straße: **22.750.793**
- Datensätze mit Hausnummer: **22.750.658**
- Datensätze mit Postleitzahl: **0**
- Datenstand laut TileJSON: **20.07.2026**
- Abrufdatum: **28.08.2026**
- SHA-256:
  `d80d5517339936da51bbf47585e05d3f0899cd2e2a695ef23f4b19acaf1308d4`

Der Bestand ist eine offene amtliche Quelle für Ort, Straße, Hausnummer und
Koordinate. Er enthält keine Postleitzahlen, Gemeindeschlüssel oder
fachlichen Objektidentifikatoren und ersetzt deshalb postalisch angereicherte
Hauskoordinatenbestände der Länder nicht.

## Amtliche Quelle

- Produkt: **basemap.de Web Vektor**
- Bereitsteller: **Bundesamt für Kartographie und Geodäsie (BKG) /
  GeoBasis-DE**
- BKG-Produktseite:
  <https://gdz.bkg.bund.de/index.php/default/gdz-basemapde-vektor-gdz-basemapde-vektor.html>
- TileJSON:
  <https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/bm_web_de_3857.json>
- Datenmodell:
  <https://basemap.de/data/produkte/web_vektor/meta/bm_web_vektor_datenmodell.html>
- Kachelschema:
  `https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/{z}/{x}/{y}.pbf`
- Kachelprojektion: Web Mercator, **EPSG:3857**
- Ausgabeprojektion: OGC CRS84

Das offizielle Datenmodell beschreibt `Adresse` als Punktlayer mit den
Attributen `land`, `ort`, `ortsteil`, `strasse` und `hausnummer`. Er geht aus
amtlichen Hauskoordinaten mit georeferenzierten Adressangaben hervor und ist
ausschließlich in Zoomstufe 15 vorhanden.

Gespeicherte Nachweise:

- `tilejson.json`
- `datenmodell.html`
- `produktseite.html`
- `open-data-katalog-seite-2.html`
- `nutzungsbedingungen-basemapde.pdf`
- `beispielkachel-muenchen-15-17437-11371.pbf`
- `beispielkachel-muenchen-http-header.txt`
- `quellen-sha256.txt`

## Lizenz

Die basemap.de-Daten werden geldleistungsfrei unter
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)**
bereitgestellt.

Vorgegebener Quellenvermerk:

> © GeoBasis-DE / BKG (2026) CC BY 4.0

Für diesen bearbeiteten Export wird verwendet:

> Adressen: © GeoBasis-DE / BKG (2026), CC BY 4.0; Daten bearbeitet,
> abgerufen am 28.08.2026.

## Vollständiger Kachelabzug

Aus einer Deutschland-Grenzgeometrie wurden mit `@mapbox/tile-cover` alle
Zoom-15-Kacheln bestimmt, welche die Geometrie schneiden.

- geplante Kachelpositionen: **679.682**
- vollständig verarbeitete Kachelpositionen: **679.682**
- Kacheln mit HTTP 404: **28.376**
- vorhandene Kacheln ohne Adressobjekt: **325.188**
- übertragene Kacheldaten: **5.446.985.408 Byte**
- gespeicherte, einzeln prüfsummierte Abrufblöcke: **1.360**
- dekodierte Adressobjekte vor exakter Deduplizierung: **22.758.976**

HTTP 404 bedeutet bei diesem gekachelten Dienst, dass für die betreffende
Position kein Kachelobjekt bereitsteht. Alle anderen HTTP-Fehler wurden mit
exponentieller Verzögerung wiederholt und hätten den Lauf nach sieben
Fehlversuchen abgebrochen.

Die Deutschlandmaske wurde aus dem gemeinfreien Referenzprojekt
<https://github.com/yetzt/adressen> übernommen. Sie dient ausschließlich zur
Bestimmung der abzurufenden Kacheln; die exportierten Adressdaten stammen
direkt vom amtlichen basemap.de-Dienst. Anders als die ältere
Referenzimplementierung verwendet dieser Export den aktuellen `v2`-Endpunkt.

## Verarbeitung

1. Alle Deutschland schneidenden Zoom-15-Kacheln wurden geplant.
2. Die Kacheln wurden in 1.360 atomaren, wiederaufnehmbaren Blöcken
   heruntergeladen.
3. Jeder Block wurde deterministisch komprimiert und mit SHA-256 gesichert.
4. Der Layer `Adresse` wurde aus den Mapbox Vector Tiles dekodiert.
5. Feature-Koordinaten wurden unter Verwendung des Layer-`extent` aus dem
   globalen Kachelraster nach WGS84 zurückgerechnet.
6. Byteidentische Datensätze wurden entfernt. Gleiche Adressschlüssel mit
   verschiedenen Koordinaten blieben erhalten.
7. Die Hausnummer wurde, soweit sie mit Ziffern beginnt, in Grundnummer und
   Zusatz getrennt.
8. Weil basemap.de keine fachliche Objekt-ID ausliefert, wurde eine stabile
   Inhalts-ID aus Bundesland, Ortsangaben, Straße, ursprünglicher Hausnummer
   und Koordinate abgeleitet.
9. Das Ergebnis wurde byteweise sortiert und deterministisch mit `gzip -n -9`
   komprimiert.

Die vollständige Extraktions- und Prüfpipeline liegt im benachbarten
Verzeichnis `../tools/`.

## Feldzuordnung

| Einheitliche Spalte | basemap.de-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | `strasse`, `hausnummer`, `ort` |
| `bundesland` | Langname aus `land` |
| `landesschluessel` | amtlicher Schlüssel aus `land` |
| `postleitzahl` | leer; nicht vorhanden |
| `ortsname_post` | `ort`; keine gesonderte postalische Kennzeichnung |
| `gemeindename` | leer; nicht vorhanden |
| `ortsteilname` | `ortsteil` |
| `strassenname` | `strasse` |
| `hausnummer` | numerischer Anfang von `hausnummer` beziehungsweise Original |
| `hausnummernzusatz` | Rest von `hausnummer` nach dem numerischen Anfang |
| `longitude_wgs84` | aus Kachel- und Feature-X abgeleitet |
| `latitude_wgs84` | aus Kachel- und Feature-Y abgeleitet |
| `datensatznummer` | abgeleitete Inhalts-ID mit Präfix `BKG-` |
| `hausschluessel` | Land;Ort;Ortsteil;Straße;ursprüngliche Hausnummer |

## Datensätze nach Land

| Code | Datensätze |
|---|---:|
| BB | 865.218 |
| BE | 394.452 |
| BW | 3.081.505 |
| BY | 3.761.180 |
| HB | 176.265 |
| HE | 1.628.646 |
| HH | 284.445 |
| MV | 517.422 |
| NI | 2.630.761 |
| NW | 4.447.809 |
| RP | 1.402.151 |
| SH | 949.935 |
| SL | 334.367 |
| SN | 987.910 |
| ST | 664.049 |
| TH | 624.678 |

## Qualitätsprüfung

- alle **679.682** geplanten Kachelpositionen verarbeitet
- SHA-256 aller **1.360** Abrufblöcke bestätigt
- **22.750.793** CSV-Datenzeilen
- jede Zeile besitzt exakt 14 Spalten
- gemeinsame Kopfzeile aller Exporte bestätigt
- kein leerer Ortsname
- keine leere Straße
- **135** Objekte ohne Hausnummer
- **2.783.740** Objekte ohne Ortsteil
- keine ungültigen Koordinaten
- Koordinatenbereich plausibel für Deutschland
- **14.798** mehrfach vorkommende Adressschlüssel mit **15.134**
  zusätzlichen Koordinatenobjekten bewusst erhalten
- byteweise Sortierung bestätigt
- deterministische Gzip-Reproduktion bestätigt

## Vergleich mit den Länderexporten

Die Ergebnisse liegen unter `vergleich-laender/`. Verglichen wurden
normalisierte Schlüssel aus Ort, Straße und Hausnummer. Für den Länderexport
wurde ein Treffer sowohl über `ortsname_post` als auch über `gemeindename`
zugelassen. Das ist ein konservativer Vergleich:

- unterschiedliche Ortshierarchien und vollständig abweichende Schreibweisen
  werden nicht erraten;
- ein einzelner Länder-Datensatz kann wegen unterschiedlicher postalischer
  und kommunaler Namen mehr als einen Vergleichsschlüssel liefern;
- die Quote des Länder-Schlüsselbestands ist daher keine unmittelbare
  Datensatzquote.

Bei allen tatsächlich passenden Adressschlüsseln stimmen die Koordinaten sehr
stark überein: je nach Land liegen **97,19 bis 100 Prozent** innerhalb von
5 Metern und mindestens **99,39 Prozent** innerhalb von 25 Metern. Dies
bestätigt, dass basemap.de weitgehend aus denselben amtlichen
Hauskoordinatenobjekten abgeleitet ist.

Die Schlüsselquoten unterscheiden sich stärker. Besonders niedrige Werte in
Sachsen-Anhalt, Niedersachsen und Mecklenburg-Vorpommern weisen auf
abweichende Orts-/Straßenmodellierung oder Benennung hin. Sie dürfen ohne
weitergehende semantische Zuordnung nicht als räumliche Fehlquote interpretiert
werden.

Die vollständige Tabelle steht in
`vergleich-laender/vergleich-zusammenfassung.md`; die Einzelwerte mit
Feld- und Koordinatenstatistik liegen als JSON vor.
