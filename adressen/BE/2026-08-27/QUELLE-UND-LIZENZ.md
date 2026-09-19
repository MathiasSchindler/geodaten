# Adressliste Berlin – Datenstand vom 27.08.2026

## Datei

- Datei: `adressen.csv.gz`
- Datensätze: **394.303**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `88f00f0865f8faa3446a79f5fcf182f36c6e1a37e72f4d5701f43d84eb0cf661`
- Alle Datensätze besitzen eine Postleitzahl.

Die Liste bildet amtliche Hauskoordinaten beziehungsweise Adressen ab. Sie
ist keine Liste aller physischen Gebäude: Gebäude ohne Adresse sind nicht
enthalten; eine Adresse kann außerdem auf einem Flurstück liegen, wenn noch
kein Gebäude vorhanden ist.

## Quelle

- Dienst: **OAF BB-BE Gazetteer**
- Collection: **Hauskoordinaten**
- Bereitsteller des Dienstes: **GeoBasis-DE / LGB**
- Herkunft laut Collection-Metadaten für Berlin:
  **Geoportal Berlin / Amtliche Hauskoordinaten**
- Collection:
  <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten>
- API-Endpunkt:
  <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten/items>
- Abrufzeitpunkt: **2026-08-27T16:56:19Z**
- Koordinatenreferenzsystem:
  **OGC CRS84** (Längengrad, Breitengrad; WGS-84-kompatibel)

Die beim Export abgerufenen Dienst- und Collection-Metadaten liegen als
`quellmetadaten-gazetteer.json` und
`quellmetadaten-hauskoordinaten.json` bei.

## Lizenz

Der Gazetteer weist die **Datenlizenz Deutschland – Namensnennung –
Version 2.0** (`dl-de/by-2-0`) aus:

<https://www.govdata.de/dl-de/by-2-0>

Der beim Export abgerufene Lizenztext liegt zusätzlich als
`lizenz-dl-de-by-2.0.html` bei. Bei einer Nutzung müssen Bereitsteller,
Lizenz mit Link und Datensatz-URI genannt werden. Bearbeitungen müssen als
solche gekennzeichnet werden.

Empfohlener Quellenvermerk:

> Quelle: GeoBasis-DE / LGB, BB-BE Gazetteer – Hauskoordinaten; Berliner
> Daten laut Collection-Metadaten aus Geoportal Berlin / Amtliche
> Hauskoordinaten,
> <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten>,
> Datenlizenz Deutschland – Namensnennung – Version 2.0
> (<https://www.govdata.de/dl-de/by-2-0>); abgerufen am 27.08.2026.
> Daten verändert: in CSV konvertiert, nach Bundesland gefiltert und um ein
> zusammengesetztes Adressfeld ergänzt.

## Verarbeitung und Spalten

Der gemeinsame Berlin-Brandenburg-Bestand wurde vollständig abgerufen und
anhand des amtlichen Landesschlüssels `11` nach Berlin gefiltert.
`vollstaendige_adresse` wurde aus den Quellfeldern zusammengesetzt. Da
`ortsnamePost` bei Berliner Datensätzen leer sein kann, wurde für das
Ausgabefeld `ortsname_post` in diesem Fall `Berlin` eingesetzt.

Die 14 Spalten sind:

`vollstaendige_adresse`, `bundesland`, `landesschluessel`,
`postleitzahl`, `ortsname_post`, `gemeindename`, `ortsteilname`,
`strassenname`, `hausnummer`, `hausnummernzusatz`, `longitude_wgs84`,
`latitude_wgs84`, `datensatznummer`, `hausschluessel`.

Die Originalidentifikatoren `datensatznummer` und `hausschluessel` wurden
unverändert übernommen.

## Qualitätsprüfung

- Gzip-Integrität erfolgreich
- 394.303 Datenzeilen mit jeweils 14 Spalten
- keine leere Postleitzahl
- keine doppelte `datensatznummer`
- SHA-256-Prüfsumme nach der Umstrukturierung erneut bestätigt

Die Vollständigkeit bezieht sich auf den zum Abrufzeitpunkt vom Dienst
bereitgestellten Bestand.
