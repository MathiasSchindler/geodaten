# Adressliste Nordrhein-Westfalen – Abruf vom 28.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **4.509.543**
- Datensätze mit PLZ: **4.509.538**
- Datensätze ohne PLZ: **5**
- Datenstand der Gebäudereferenzen: **01.07.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `35faeec30ab2fbbf9b6c29de22418519bf86323ab01e67d6d8413d0df126a0d7`

Die Liste bildet die amtlichen Gebäudereferenzen Nordrhein-Westfalens ab und
ergänzt deren Postleitzahl aus dem offiziellen INSPIRE-Adress-WFS von
Geobasis NRW.

## Quellen

### Gebäudereferenzen

- Produkt: **Gebäudereferenzen (ASCII) – gesamt NRW**
- Bereitsteller: **Geobasis NRW, Bezirksregierung Köln**
- Verzeichnis:
  <https://www.opengeodata.nrw.de/produkte/geobasis/lk/akt/gebref_txt/>
- Download:
  <https://www.opengeodata.nrw.de/produkte/geobasis/lk/akt/gebref_txt/gebref_EPSG25832_ASCII.zip>
- Abrufdatum: **28.08.2026**
- Quelldatei: `gebref.txt`
- Auslesedatum in sämtlichen Quellzeilen: **01.07.2026**
- Koordinatenreferenzsystem:
  **ETRS89 / UTM Zone 32N (EPSG:25832)**

Die originale ZIP-Datei liegt als `gebref_EPSG25832_ASCII.zip` bei. Ihre
SHA-256-Summe lautet:

`8f3cc3398799c2e6358f22e1272e435c8dcc3375de1597da1b3339251d4692a8`

### Postleitzahlen

- Datensatz: **INSPIRE NW Adressen ALKIS**
- Bereitsteller: **Geobasis NRW**
- Metadaten:
  <https://open.nrw/dataset/a34677dd-d717-498e-9ae6-c187d0c3efcb>
- WFS:
  <https://www.wfs.nrw.de/geobasis/wfs_nw_inspire-adressen_gebref>
- verwendeter Feature-Typ: `ad:Address`
- PLZ-Komponente: `ad:PostalDescriptor`
- Abrufdatum: **28.08.2026**
- live gemeldete Adressanzahl: **4.509.543**
- Zeitstempel der abschließenden Zählabfrage:
  **2026-08-28T10:02:15.971+02:00**

Die Metadaten beschreiben den Bestand als INSPIRE-Adressen, abgeleitet aus
Gebäudereferenzen plus Postleitzahlen auf Grundlage des
Liegenschaftskatasters. Der dort genannte Stand der verwendeten Daten ist
01.04.2026. Der live abgerufene WFS enthielt jedoch exakt dieselben
4.509.543 Objekt-IDs wie der Gebäudereferenzbestand vom 01.07.2026; auch
stichprobenartig geprüfte Koordinaten stimmten überein. Für die
Gebäudeattribute ist deshalb der in der Quelldatei selbst enthaltene
Datenstand 01.07.2026 maßgeblich. Der PLZ-Zuordnung wird transparent das
WFS-Abrufdatum 28.08.2026 zugeordnet.

Zur Nachvollziehbarkeit liegen bei:

- `quellmetadaten-inspire-adressen.json`
- `wfs-capabilities-inspire-adressen.xml`
- `wfs-address-count.xml`
- `wfs-postal-descriptors.xml`
- `postleitzahl-zuordnung.csv.gz`
- `wfs-adressen-ohne-postaldescriptor.xml`
- `quellverzeichnis-gebaeudereferenzen.json`
- `produktbeschreibung-gebaeudereferenzen.html`

`postleitzahl-zuordnung.csv.gz` enthält die 4.509.538 erfolgreichen
Zuordnungen von Quell-ID zu PLZ. Ihre SHA-256-Summe lautet:

`45d1169d29b2c4f08570e312e57e21e951a53fb8a873620a7b3e37b06b19dd28`

## Bewertung von `gru_xml`

Die ursprünglich vorgeschlagenen `gru_xml`-Dateien sind vollständige
ALKIS-Grundrissdaten im NAS-Format, paketiert nach 53 Kreisen und kreisfreien
Städten. Sie enthalten weit mehr Objekte als für eine Adressliste notwendig
und umfassen komprimiert insgesamt viele Gigabyte. Postleitzahlen gehören
nicht zum ALKIS-Gebäude- beziehungsweise Lagebezeichnungsobjekt.

Für diesen Zweck sind die landesweit in einer einzigen Datei angebotenen
Gebäudereferenzen der bessere Ausgangsbestand. Der separate INSPIRE-WFS
liefert dazu die amtlich veröffentlichte PLZ-Verknüpfung.

## Fünf Adressen ohne PLZ

Für fünf Gebäudereferenzen lieferte keine der 873 PLZ-Abfragen eine
Zuordnung:

- `DENW06HK0000DuOh`
- `DENW18HK00007Gg3`
- `DENW31HKRG00003D`
- `DENW41HKWF00005B`
- `DENW50HK000011Z3`

Diese fünf korrespondierenden `ad:Address`-Objekte wurden anschließend
gezielt per `RESOURCEID` aus dem WFS abgerufen. Sie enthalten tatsächlich
keine `PostalDescriptor`-Komponente. Die vollständigen Antworten sind in
`wfs-adressen-ohne-postaldescriptor.xml` gespeichert. Die PLZ bleibt für
diese Datensätze daher bewusst leer; es wurde keine nichtamtliche
Ersatzquelle verwendet.

## Lizenz

Sowohl die OpenGeodata-Produkte von Geobasis NRW als auch der verwendete
WFS stehen unter der **Datenlizenz Deutschland – Zero – Version 2.0**:

<https://www.govdata.de/dl-de/zero-2-0>

Die WFS-Capabilities erklären ausdrücklich:

> Jede Nutzung ist ohne Einschränkungen oder Bedingungen zulässig.

Eine Namensnennung ist unter dieser Lizenz nicht vorgeschrieben. Zur
transparenten Herkunftsangabe wird dennoch folgender Quellenhinweis
empfohlen:

> Geobasis NRW, Bezirksregierung Köln; Gebäudereferenzen NRW,
> Datenstand 01.07.2026, und INSPIRE NW Adressen ALKIS,
> abgerufen am 28.08.2026; Datenlizenz Deutschland – Zero – Version 2.0;
> bearbeiteter Export.

Der gespeicherte Lizenztext liegt als `lizenz-dl-de-zero-2.0.html` bei.

## Verarbeitung

1. Der vollständige landesweite Gebäudereferenz-Download wurde entpackt.
2. Die WFS-Zählabfrage bestätigte 4.509.543 Adressobjekte, exakt entsprechend
   der Quelldatei.
3. Aus 1.327 `PostalDescriptor`-Objekten wurden 873 unterschiedliche
   fünfstellige Postleitzahlen ermittelt.
4. Für jede PLZ wurden über einen gefilterten WFS-`GetPropertyValue`-Abruf
   ausschließlich die zugehörigen Adress-IDs abgefragt.
5. Die INSPIRE-Adress-IDs wurden anhand ihrer identischen amtlichen lokalen
   ID mit den Gebäudereferenz-IDs verknüpft; nur der Produkttyp unterscheidet
   sich (`AL` gegenüber `HK`).
6. Alle 4.509.538 PLZ-Zuordnungen wurden sortiert, auf Eindeutigkeit geprüft
   und mit den Gebäudereferenzen zusammengeführt.
7. Die Koordinaten wurden von EPSG:25832 nach OGC CRS84 transformiert.
8. Die Daten wurden in das gemeinsame 14-spaltige CSV-Schema überführt,
   byteweise (`LC_ALL=C`) sortiert und deterministisch mit Gzip komprimiert.

Das Feld für den Ortsteilnamen ist in sämtlichen Gebäudereferenzen leer.
`ortsteilname` wurde deshalb nicht durch eine ungesicherte Angabe ersetzt.

Die WFS-Postleitzahlobjekte enthalten auch postalische Namen, ihre IDs sind
jedoch nur über die PLZ adressiert und kommen im Dienst mehrfach vor.
`ortsname_post` wurde daher konsistent mit dem amtlichen Gemeindename belegt;
es ist nicht als separat postalisch validierter Ortsname zu verstehen.

## Feldzuordnung

| Einheitliche Spalte | Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | Straße, Hausnummer, Zusatz, PLZ und Gemeinde |
| `bundesland` | Gebäudereferenzfeld Land |
| `landesschluessel` | Gebäudereferenzfeld Landesschlüssel (`05`) |
| `postleitzahl` | WFS-Verknüpfung zu `PostalDescriptor`; fünf Ausnahmen leer |
| `ortsname_post` | amtlicher Gemeindename als Ersatzwert |
| `gemeindename` | Gebäudereferenzfeld Gemeinde |
| `ortsteilname` | Ortsteilname der Gebäudereferenz; vollständig leer |
| `strassenname` | Gebäudereferenzfeld Straße |
| `hausnummer` | Gebäudereferenzfeld Hausnummer |
| `hausnummernzusatz` | Gebäudereferenzfeld Adresszusatz |
| `longitude_wgs84` | transformiert aus Ostwert |
| `latitude_wgs84` | transformiert aus Nordwert |
| `datensatznummer` | bundesweit eindeutige Gebäudereferenz-ID |
| `hausschluessel` | Land;Regierungsbezirk;Kreis;Gemeinde;Ortsteil;Straße;Hausnummer;Zusatz |

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- exakt 4.509.543 Datenzeilen entsprechend Quelldatei und WFS
- jede Datenzeile besitzt genau 14 Spalten
- gemeinsamer Header aller Länderexporte bestätigt
- 4.509.538 Datensätze mit fünfstelliger PLZ
- 5 durch den WFS bestätigte Datensätze ohne PLZ
- 873 unterschiedliche Postleitzahlen
- keine doppelte `datensatznummer`
- kein doppelter zusammengesetzter `hausschluessel`
- keine verwaiste PLZ-Zuordnung
- keine leere Gemeinde, Straße oder Hausnummer
- 396 Gemeinden und 53 Kreise beziehungsweise kreisfreie Städte
- keine ungültige oder außerhalb Nordrhein-Westfalens liegende Koordinate
- byteweise Sortierreihenfolge bestätigt
- SHA-256-Prüfsummen von Export, Quelle und PLZ-Zuordnung bestätigt

Es gibt 537 mehrfach vorkommende zusammengesetzte Anzeigeadressen mit
insgesamt 563 zusätzlichen Vorkommen, jedoch jeweils mit unterschiedlichen
amtlichen Quell-IDs beziehungsweise Lagebezeichnungsschlüsseln. Sie wurden
bewusst erhalten.

Die Vollständigkeit bezieht sich auf den am 28.08.2026 live verfügbaren WFS
und den dazu objektidentischen Gebäudereferenzbestand mit Auslesedatum
01.07.2026.
