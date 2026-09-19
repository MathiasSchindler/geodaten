# Adressliste Mecklenburg-Vorpommern – Abruf vom 27.08.2026

## Vorläufiger Datenstand

Dieser Export enthält die amtlichen georeferenzierten Adressen des
INSPIRE-Adress-WFS von Mecklenburg-Vorpommern. Das Feld `postleitzahl` ist in
allen Datensätzen absichtlich leer, weil der Dienst keine
`PostalDescriptor`-Objekte bereitstellt.

Die Datei ist als Arbeitsstand für eine spätere Ergänzung der Postleitzahlen
gedacht. Sie darf nicht mit den vollständigen PLZ-bestückten Exporten für
Berlin, Brandenburg und Schleswig-Holstein gleichgesetzt werden.

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **518.416**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `05499271c5a6a108253da8ff93d1488ca21ed1734f8239914ecf5316c7276cb1`
- Postleitzahlen: **0 befüllt, 518.416 leer**

Die Liste bildet amtliche Hauskoordinaten beziehungsweise Adressen ab. Sie
ist keine Liste aller physischen Gebäude: Gebäude ohne Adresse sind nicht
enthalten.

## Quelle

- Datensatz: **INSPIRE MV Adressen Hauskoordinaten**
- Bereitsteller: **Landesamt für innere Verwaltung Mecklenburg-Vorpommern,
  Amt für Geoinformation, Vermessung und Katasterwesen**
- Datengrundlage: Liegenschaftskataster (ALKIS)
- WFS:
  <https://www.geodaten-mv.de/dienste/inspire_ad_alkis_download>
- Metadatensatz:
  <https://www.geodaten-mv.de/geomis/id/6ffa1ae6-eff4-4ac3-98af-73109464e9c5>
- Abrufdatum: **27.08.2026**
- Zeitstempel des WFS während des Exports:
  **2026-08-27T18:04:41Z**
- Koordinatenreferenzsystem der Abfrage: **EPSG:4326**
- Reihenfolge in der erzeugten CSV: **Längengrad, Breitengrad**

Zur Nachvollziehbarkeit liegen bei:

- `quellmetadaten-inspire-adressen.xml`: vollständiger ISO-Metadatensatz
- `wfs-capabilities.xml`: WFS-GetCapabilities-Antwort
- `AfGVK_AGNB.pdf`: vom Metadatensatz referenzierte Allgemeine
  Nutzungsbedingungen

## Fehlende Postleitzahlen

Der WFS meldete zum Abrufzeitpunkt:

- `Address`: 518.416 Objekte
- `AdminUnitName`: 732 Objekte
- `ThoroughfareName`: 26.254 Objekte
- `PostalDescriptor`: **0 Objekte**

Eine Postleitzahl lässt sich deshalb nicht aus diesem Dienst übernehmen. Es
wurde bewusst weder eine Postleitzahl geschätzt noch ein nicht dokumentierter
Fremdbestand räumlich zugespielt.

Auch `ortsname_post` ist kein postalisch bestätigter Ortsname. Bis ein
postalischer Referenzbestand ergänzt wird, enthält dieses Feld ersatzweise
den amtlichen Gemeindename.

## Nutzungsbedingungen und Weitergabevorbehalt

Für diesen Export sind die Nutzungsbedingungen des konkret verwendeten
INSPIRE-Adressdienstes maßgeblich. Der zugehörige Metadatensatz nennt keine
offene Standardlizenz wie CC BY 4.0. Er enthält insbesondere folgende
Hinweise:

- Für die Nutzung können Kosten anfallen.
- Die Zusammenführung mit eigenen oder fremden Daten und Diensten für interne
  Geschäftsprozesse, Produkte und Anwendungen ist genehmigungs- und
  geldleistungsfrei, soweit die Rechte der weiteren Quellen dies erlauben.
- Darüber hinausgehende externe Nutzungen können genehmigungs- und
  geldleistungspflichtig sein.
- Es gelten die Allgemeinen Nutzungsbedingungen des AfGVK.

Der für öffentliche Wiedergaben geforderte Quellenvermerk lautet sinngemäß:

> © GeoBasis-DE/M-V 2026

Die für den separaten ALKIS-WMS angegebenen Bedingungen unter CC BY 4.0
werden nicht auf diesen Adress-WFS übertragen. Vor einer externen
Veröffentlichung oder Weitergabe von `adressen.csv.gz` ist daher eine
entsprechende Freigabe des Bereitstellers einzuholen. Ansprechpartner laut
Metadaten ist `geodatenservice@laiv-mv.de`.

## Verarbeitung

Der WFS begrenzt Antworten auf 10.000 Objekte. Eine zunächst geprüfte
seitenbasierte Abfrage lieferte ohne stabile Sortierung überlappende Seiten
und wurde verworfen. Der endgültige Export wurde deshalb folgendermaßen
erzeugt:

- rekursive Aufteilung der Landesfläche in 111 räumliche Abfragegebiete mit
  jeweils höchstens 9.000 Treffern
- Abruf aller Adressobjekte in EPSG:4326
- Deduplizierung räumlicher Überschneidungen anhand der eindeutigen
  `gml:id`
- gebündeltes Auflösen aller referenzierten Gemeinden und Straßennamen über
  WFS-`resourceID`-Abfragen
- Auswahl der Verwaltungseinheit 5. Ordnung als Gemeinde
- für 43.674 Adressen in den kreisfreien Städten Auswahl der
  Verwaltungseinheit 4. Ordnung:
  - `Hanse- und Universitätsstadt Rostock`
  - `Landeshauptstadt Schwerin`
- Übernahme von Hausnummer und Hausnummernzusatz aus den
  `LocatorDesignator`-Elementen
- Ergänzung der konstanten Landeswerte
- alphabetische Sortierung der CSV-Zeilen
- Gzip-Komprimierung

Die räumlichen Abfragen lieferten 61.073 bereits anhand ihrer `gml:id`
bekannte Treffer aus überlappenden Abfragebereichen beziehungsweise
mehrfachen räumlichen Positionen. Nach der ID-basierten Deduplizierung
entspricht die Anzahl von 518.416 eindeutigen Adressobjekten exakt der
`resultType=hits`-Gesamtzahl des WFS.

## Feldzuordnung

| Einheitliche Spalte | MV-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer und Gemeinde; ohne PLZ |
| `bundesland` | Konstante `Mecklenburg-Vorpommern` |
| `landesschluessel` | Konstante `13` |
| `postleitzahl` | leer; Quelle enthält keine `PostalDescriptor` |
| `ortsname_post` | ersatzweise amtlicher Gemeindename |
| `gemeindename` | Name der referenzierten Verwaltungseinheit |
| `ortsteilname` | leer; nicht separat geliefert |
| `strassenname` | referenziertes `ThoroughfareName`-Objekt |
| `hausnummer` | `LocatorDesignator` vom Typ `addressNumber` |
| `hausnummernzusatz` | `LocatorDesignator` vom Typ `addressNumberExtension` |
| `longitude_wgs84` | erster Wert von `gml:pos` bei Abfrage in EPSG:4326 |
| `latitude_wgs84` | zweiter Wert von `gml:pos` bei Abfrage in EPSG:4326 |
| `datensatznummer` | `alternativeIdentifier` |
| `hausschluessel` | lokale `gml:id` des `Address`-Objekts |

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- 518.416 Datenzeilen mit jeweils 14 Spalten
- identische Kopfzeile wie die Exporte für BE, BB und SH
- 518.416 leere Postleitzahlen wie vorgesehen
- keine doppelte `datensatznummer`
- keine doppelten `hausschluessel`
- keine leere Gemeinde, Straße oder Hausnummer
- keine ungültige oder außerhalb Mecklenburg-Vorpommerns liegende Koordinate
- alphabetische Sortierreihenfolge bestätigt
- SHA-256-Prüfsumme nach der Konvertierung bestätigt

Es gibt 3.033 mehrfach vorkommende zusammengesetzte Anzeigeadressen. Die
zugrunde liegenden amtlichen Datensatznummern und Hausschlüssel sind jeweils
eindeutig; die Datensätze wurden deshalb erhalten.

Die Vollständigkeit bezieht sich auf den am 27.08.2026 vom WFS gemeldeten
Bestand.
