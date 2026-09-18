# libreHKDE – Datenstand 31.08.2026

Dieses Dokument ist die eigenständige Quellen-, Lizenz- und
Verarbeitungsdokumentation für den Inhalt dieses Verzeichnisses. Zum Verständnis
oder zur Weitergabe von `libreHKDE.csv.gz` werden keine Dokumente aus einem
übergeordneten Projektverzeichnis benötigt.

## Inhalt des Verzeichnisses

| Datei | Inhalt |
|---|---|
| `libreHKDE.csv.gz` | bundesweiter Adressbestand im unten beschriebenen 24-Spalten-Profil |
| `export-statistik.txt` | Lücken- und Ableitungsstatistik des Exports |
| `validierung.json` | Ergebnis der technischen Vollprüfung |
| `SHA256SUMS` | SHA-256-Prüfsummen der Daten-, Statistik-, Validierungs- und Quelleninventardatei |
| `QUELLEN.csv` | maschinenlesbares Inventar aller Datenquellen und Quellenvermerke |
| `QUELLE-UND-LIZENZ.md` | dieses Dokument |

Für `libreHKDE.csv.gz` gilt:

- Datensätze: **23.252.486**
- Format: UTF-8 ohne BOM, Semikolon, 24 Felder, CRLF, Gzip
- SHA-256:
  `ca00024123cf15b874dce717c115153c09b894bc93dd47365ae736938653dedc`
- leere `oid`: **23.252.486**
- leere `qua`: **23.252.486**
- leere Gemeinden: **107**
- leere PLZ: **21**
- als `hnr=0` ausgegebene unregelmäßige oder fehlende Grundhausnummern:
  **3.008**

Alle 23.252.486 Zeilen wurden vollständig technisch validiert.

## Einordnung des Datenprofils

Die Datei verwendet diese Kopfzeile aus der HK-DE-Formatbeschreibung 5.2:

```text
nba;oid;qua;landschl;land;regbezschl;regbez;kreisschl;kreis;gmdschl;gmd;ottschl;ott;strschl;str;hnr;adz;zone;ostwert;nordwert;postplz;postonm;postonmzus;postott
```

Sie ist ausdrücklich:

> **libreHKDE – eine HK-DE-5.2-kompatible 24-Spalten-Struktur aus offenen
> Quellen; kein HK-DE-Produkt der ZSHH und keine
> Deutsche-Post-Direkt-Anreicherung.**

Die Struktur ist technisch kompatibel, die fachliche Belegung weicht aber
bewusst vom kommerziellen HK-DE-Produkt ab:

- `oid` bleibt leer; es wurde keine eigene libre-ID eingeführt.
- `qua` bleibt leer; die Qualitätsklassen A/B/C wurden nicht geraten.
- unvollständige Datensätze bleiben enthalten und werden gezählt;
- offene postalische Werte werden nicht als Schreibweisen oder Daten der
  Deutsche Post Direkt GmbH bezeichnet;
- ein vollständiger N/L/A-Änderungsdienst und eine Objektlebenszyklus-Historie
  sind nicht Bestandteil dieses Datenstands.

Die Bezeichnung `libreHKDE` dient nur zur Beschreibung dieses offenen
Kompatibilitätsprofils. Sie behauptet keine Herausgeberschaft, Freigabe oder
Produktgleichheit durch beziehungsweise mit der ZSHH.

## Aufbau des offenen Adressgrundbestands

### Gemeinsames Zwischenschema

Alle Länderquellen und basemap.de wurden zunächst auf ein gemeinsames
14-spaltiges Arbeitsschema normalisiert:

1. vollständige Anzeigeadresse
2. Bundesland
3. Landesschlüssel
4. Postleitzahl
5. offener Ortsname
6. Gemeindename
7. Ortsteilname
8. Straßenname
9. Hausnummer
10. Hausnummernzusatz
11. WGS84-Längengrad
12. WGS84-Breitengrad
13. technische Datensatznummer
14. Quell- beziehungsweise Hausschlüssel

Die technischen Datensatznummern dieses Zwischenschritts dienen nur dem
reproduzierbaren Abgleich. Sie werden **nicht** als `oid` in libreHKDE
veröffentlicht.

### Konsolidierungsregel

Für Brandenburg, Berlin, Baden-Württemberg, Bremen, Hessen, Hamburg,
Niedersachsen, Nordrhein-Westfalen, Rheinland-Pfalz, Schleswig-Holstein,
Saarland, Sachsen, Sachsen-Anhalt und Thüringen war jeweils ein offen
weitergabefähiger Länderbestand vorhanden.

Für diese 14 Länder galt:

1. Jeder Datensatz der Länderquelle blieb erhalten.
2. basemap.de-Adressen wurden anhand normalisierter Orts-, Straßen- und
   Hausnummernwerte sowie der räumlichen Entfernung abgeglichen.
3. Erste Abgleichstufe: gleicher normalisierter Ort, Straße und Hausnummer bei
   höchstens 25 m Entfernung.
4. Zweite Abgleichstufe, wenn Stufe 1 keinen Treffer hatte: gleiche
   normalisierte Straße und Hausnummer bei höchstens 5 m Entfernung.
5. Nur genau ein räumlicher Kandidat wurde verbunden.
6. Bei mehreren Kandidaten wurde nichts geraten; das basemap.de-Objekt blieb als
   eigener Datensatz erhalten.
7. Fachliche Länderfelder hatten grundsätzlich Vorrang.
8. Ein leerer Ortsteil der Länderquelle wurde nur bei eindeutigem Treffer aus
   basemap.de ergänzt.
9. In Niedersachsen wurde für 2.564.571 verknüpfte Datensätze die
   basemap.de-Koordinate verwendet, weil die Länderableitung neben
   kartografischen Hausnummernpositionen auch geometrische Fallbacks enthielt.

Für Bayern und Mecklenburg-Vorpommern wurde ausschließlich basemap.de
verwendet:

- Für Bayern stand kein gleichwertiger offener landeseigener
  Hauskoordinatenbestand zur Verfügung.
- Der untersuchte INSPIRE-Adress-WFS Mecklenburg-Vorpommerns wies keine klare
  offene Standardlizenz aus und nannte für externe Nutzungen mögliche
  Genehmigungs- und Entgeltpflichten. Seine Daten wurden deshalb nicht in den
  weitergabefähigen Bestand aufgenommen.

### Ergebnis der Konsolidierung

| Kennzahl | Datensätze |
|---|---:|
| Länderinput aus 14 Ländern | 18.880.406 |
| deutschlandweiter basemap.de-Input | 22.750.793 |
| Treffer Ort + Straße + Hausnummer bis 25 m | 14.840.548 |
| Treffer Straße + Hausnummer bis 5 m | 3.538.165 |
| insgesamt verknüpfte Länder-/Basemap-Objekte | 18.378.713 |
| nur in Länderquellen | 501.693 |
| nur in basemap.de | 4.372.080 |
| konservativ nicht vereinigte Mehrdeutigkeiten | 212 |
| konsolidierter Bestand | **23.252.486** |

Mehrere Adressobjekte können dieselbe darstellbare Anschrift besitzen, etwa bei
getrennten Gebäudeteilen, verschiedenen amtlichen Objektidentitäten oder
unterschiedlichen Datenständen. Die Anzahl ist daher keine Anzahl von
Gebäuden, Haushalten oder Zustellpunkten.

## Amtliche Länderquellen

Die nachfolgenden Quellen sind die tatsächlichen Länderkomponenten des
Adressgrundbestands. Die angegebenen Quellenvermerke sind bei Weitergabe dieses
Gesamtbestands beizubehalten.

### Brandenburg

- Produkt: BB-BE Gazetteer, Collection Hauskoordinaten
- Bereitsteller: GeoBasis-DE / Landesvermessung und
  Geobasisinformation Brandenburg (LGB)
- Herkunft laut Metadaten: Georeferenzierte Adresse Land Brandenburg
- Quelle:
  <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten>
- Abruf: 27.08.2026
- verarbeitete Länderobjekte: **867.290**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> Quelle: GeoBasis-DE / LGB, BB-BE Gazetteer – Hauskoordinaten,
> <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten>,
> dl-de/by-2-0; Daten verändert, abgerufen am 27.08.2026.

### Berlin

- Produkt: BB-BE Gazetteer, Collection Hauskoordinaten
- Bereitsteller des Dienstes: GeoBasis-DE / LGB
- Herkunft laut Metadaten: Geoportal Berlin / Amtliche Hauskoordinaten
- Quelle:
  <https://ogc-api.geobasis-bb.de/datasets/gazetteer/collections/Hauskoordinaten>
- Abruf: 27.08.2026
- verarbeitete Länderobjekte: **394.303**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> Quelle: GeoBasis-DE / LGB, BB-BE Gazetteer – Hauskoordinaten; Berliner
> Daten laut Collection-Metadaten aus Geoportal Berlin / Amtliche
> Hauskoordinaten; dl-de/by-2-0; Daten verändert, abgerufen am 27.08.2026.

### Baden-Württemberg

- Produkt: Hauskoordinaten Baden-Württemberg ohne postalische Angaben
- Bereitsteller: Landesamt für Geoinformation und Landentwicklung
  Baden-Württemberg (LGL)
- Portal: <https://opengeodata.lgl-bw.de/#/(sidenav:product/hk)>
- Download: <https://opengeodata.lgl-bw.de/data/hk/hk_bw.zip>
- Datenstand: 15.07.2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **3.382.566**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> Datenquelle: LGL, www.lgl-bw.de, dl-de/by-2-0; Daten bearbeitet,
> abgerufen am 28.08.2026.

### Bremen

- Produkt: ALKIS – Gebäudeadressen Land Bremen
- Bereitsteller: Landesamt GeoInformation Bremen
- Download:
  <https://gdi2.geo.bremen.de/inspire/download/ALKIS_Gebaeudeadressen/data/GEBAEUDEADRESSEN_SHP_FHB.zip>
- Datenstand: August 2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **176.374**
- Lizenz: Creative Commons Namensnennung 4.0 International,
  <https://creativecommons.org/licenses/by/4.0/>

> Landesamt GeoInformation Bremen; ALKIS – Gebäudeadressen Land Bremen;
> CC BY 4.0; Quelle verändert, abgerufen am 28.08.2026.

### Hessen

- Produkt: Hauskoordinaten ohne Postalische Angaben, Version 2026-01
- Bereitsteller: Hessisches Landesamt für Bodenmanagement und Geoinformation
  (HLBG)
- Downloadcenter:
  <https://www.gds.hessen.de/INTERSHOP/web/WFS/HLBG-Geodaten-Site/de_DE/-/EUR/ViewDownloadcenter-Start?path=Liegenschaftskataster/Hauskoordinaten%20ohne%20Postalische%20Angaben%20(txt)>
- Produktbeschreibung:
  <https://hvbg.hessen.de/liegenschaftskataster/hauskoordinaten>
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **1.630.801**
- Lizenz: Datenlizenz Deutschland – Zero – Version 2.0,
  <https://www.govdata.de/dl-de/zero-2-0>

> Hessisches Landesamt für Bodenmanagement und Geoinformation (HLBG),
> Hauskoordinaten ohne Postalische Angaben, Version 2026-01;
> dl-de/zero-2-0; bearbeiteter Export.

### Hamburg

- Produkt: Zentraler AdressService Hamburg, Collection Hauskoordinaten
- Bereitsteller: Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation
  und Vermessung
- Portal:
  <https://suche.transparenz.hamburg.de/dataset/zentraler-adressservice-hamburg3>
- Dienst:
  <https://api.hamburg.de/datasets/v1/gages_vereinfacht/collections/hauskoordinaten>
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **302.408**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> Freie und Hansestadt Hamburg, Landesbetrieb Geoinformation und Vermessung;
> Zentraler AdressService Hamburg, Collection Hauskoordinaten;
> dl-de/by-2-0; Quelle verändert, abgerufen am 28.08.2026.

### Niedersachsen

- Produkt: ALKIS Open Data und ALKIS Straßennamen
- Bereitsteller: Landesamt für Geoinformation und Landesvermessung
  Niedersachsen (LGLN)
- ALKIS-WFS:
  <https://opendata.lgln.niedersachsen.de/doorman/noauth/alkis_wfs_sf>
- Straßennamen:
  <https://alkis.stac.lgln.niedersachsen.de/collections/alkis-strassennamen>
- Datenstände: ALKIS-Abruf 28.08.2026; Straßennamen 21.08.2026
- verarbeitete Länderobjekte: **2.643.144**
- Lizenz: Creative Commons Namensnennung 4.0 International,
  <https://creativecommons.org/licenses/by/4.0/>

> LGLN (2026), ALKIS Open Data und ALKIS Straßennamen, CC BY 4.0;
> Quelle verändert, abgerufen am 28.08.2026.

### Nordrhein-Westfalen

- Produkte: Gebäudereferenzen NRW und INSPIRE NW Adressen ALKIS
- Bereitsteller: Geobasis NRW, Bezirksregierung Köln
- Gebäudereferenzen:
  <https://www.opengeodata.nrw.de/produkte/geobasis/lk/akt/gebref_txt/>
- INSPIRE-Adress-WFS:
  <https://www.wfs.nrw.de/geobasis/wfs_nw_inspire-adressen_gebref>
- Datenstand Gebäudereferenzen: 01.07.2026
- PLZ-Abruf aus dem WFS: 28.08.2026
- verarbeitete Länderobjekte: **4.509.543**
- Lizenz: Datenlizenz Deutschland – Zero – Version 2.0,
  <https://www.govdata.de/dl-de/zero-2-0>

> Geobasis NRW, Bezirksregierung Köln; Gebäudereferenzen NRW und INSPIRE
> NW Adressen ALKIS; dl-de/zero-2-0; bearbeiteter Export.

### Rheinland-Pfalz

- Produkt: Amtliche Gebäudereferenz / Hauskoordinaten ohne postalische
  Anreicherung
- Bereitsteller: Landesamt für Vermessung und Geobasisinformation
  Rheinland-Pfalz (LVermGeoRP)
- Produktseite: <https://geoshop.rlp.de/opendata-hk.html>
- Download:
  <https://geobasis-rlp.de/data/hk/current/zip/HAUSKOORDINATEN_RP.zip>
- Datenstand: 30.03.2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **1.406.086**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> © GeoBasis-DE / LVermGeoRP 2026, dl-de/by-2-0,
> www.lvermgeo.rlp.de, Daten bearbeitet; abgerufen am 28.08.2026.

### Schleswig-Holstein

- Produkt: Hauskoordinaten aus ALKIS ohne PLZ-Abgleich
- Bereitsteller: GeoBasis-DE / LVermGeo SH
- Portal:
  <https://geodaten.schleswig-holstein.de/gaialight-sh/_apps/dladownload/dl-hk_alkis.html>
- Datenstand: 01.06.2026
- Abruf: 27.08.2026
- verarbeitete Länderobjekte: **953.360**
- Lizenz: Creative Commons Namensnennung 4.0 International,
  <https://creativecommons.org/licenses/by/4.0/>

> © GeoBasis-DE/LVermGeo SH/CC BY 4.0 (Quelle verändert);
> Hauskoordinaten aus ALKIS ohne PLZ-Abgleich; Datenstand 01.06.2026,
> abgerufen am 27.08.2026.

Die PLZ ist in der Quelle befüllt, wurde laut Produktbezeichnung aber nicht
gegen einen externen postalischen Referenzbestand abgeglichen.

### Saarland

- Produkt: Hauskoordinaten Saarland
- Bereitsteller: Landesamt für Vermessung, Geoinformation und
  Landentwicklung Saarland (LVGL)
- Portal:
  <https://www.shop.lvgl.saarland.de/index.php?option=com_content&view=article&id=18>
- Download:
  <https://www.shop.lvgl.saarland.de/cloud/public.php/dav/files/NK8ndP55qAqGEZD/OD_Hauskoordinaten_csv_SL/HK_SL_EPSG-25832.zip>
- Datenstand: 05.10.2025
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **335.022**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> © GeoBasis DE/LVGL-SL (2025), dl-de/by-2-0; Daten bearbeitet,
> abgerufen am 28.08.2026.

### Sachsen

- Produkt: Hauskoordinaten / georeferenzierte Gebäudeadressen Sachsen
- Bereitsteller: Landesamt für Geobasisinformation Sachsen (GeoSN)
- Downloadseite:
  <https://www.geodaten.sachsen.de/downloadbereich-hauskoordinaten-4172.html>
- Datenstand: 02.07.2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **990.090**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> Quelle: GeoSN, dl-de/by-2-0; Daten verändert, abgerufen am 28.08.2026.

Die amtliche Produktbeschreibung nennt das Liegenschaftskataster und
Postleitzahlen der Deutschen Post AG als Quellen des sächsischen
Originalprodukts. libreHKDE übernimmt nur die unter der genannten offenen
Lizenz veröffentlichte Länderdatei und behauptet für deren postalische Felder
keine eigenen weitergehenden Rechte.

### Sachsen-Anhalt

- Produkt: Gebäudereferenzen Sachsen-Anhalt
- Bereitsteller: Landesamt für Vermessung und Geoinformation
  Sachsen-Anhalt (LVermGeo)
- Portal:
  <https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html>
- Datenstand: 04.06.2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **664.459**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> © GeoBasis-DE / LVermGeo ST; Gebäudereferenzen Sachsen-Anhalt;
> dl-de/by-2-0; Quelle verändert, Datenstand 04.06.2026,
> abgerufen am 28.08.2026.

### Thüringen

- Produkt: Hauskoordinaten Thüringen
- Bereitsteller: Thüringer Landesamt für Bodenmanagement und
  Geoinformation (TLBG)
- Infrastruktur und Quellenvermerk: GDI-Th
- Download:
  <https://geoportal.geoportal-th.de/hausko_umr/HK-TH.zip>
- Metadaten:
  <https://geomis.geoportal-th.de/geonetwork/inspire/api/records/666deaca-27ec-4fdf-91b0-253bff94b738>
- Datenstand: 26.03.2026
- Abruf: 28.08.2026
- verarbeitete Länderobjekte: **624.960**
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

> © GDI-Th (2026), Datenlizenz Deutschland – Namensnennung –
> Version 2.0; Daten verändert, abgerufen am 28.08.2026.

## Bundesweite Ergänzungsquelle basemap.de

- Produkt: basemap.de Web Vektor
- Bereitsteller: Bundesamt für Kartographie und Geodäsie (BKG) /
  GeoBasis-DE
- Produktseite:
  <https://gdz.bkg.bund.de/index.php/default/gdz-basemapde-vektor-gdz-basemapde-vektor.html>
- TileJSON:
  <https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/bm_web_de_3857.json>
- Datenmodell:
  <https://basemap.de/data/produkte/web_vektor/meta/bm_web_vektor_datenmodell.html>
- Datenstand laut TileJSON: 20.07.2026
- Abruf: 28.08.2026
- Lizenz: Creative Commons Namensnennung 4.0 International,
  <https://creativecommons.org/licenses/by/4.0/>

Beizubehaltender Quellenvermerk:

> Adressen: © GeoBasis-DE / BKG (2026), CC BY 4.0; Daten bearbeitet,
> abgerufen am 28.08.2026.

Aus allen Deutschland schneidenden Zoom-15-Kacheln wurde der Punktlayer
`Adresse` mit den Attributen `land`, `ort`, `ortsteil`, `strasse` und
`hausnummer` extrahiert:

- geplante und vollständig verarbeitete Kachelpositionen: **679.682**
- dekodierte Adressobjekte vor exakter Deduplizierung: **22.758.976**
- exakt unterschiedliche Adresspunkte: **22.750.793**
- Postleitzahlen, Gemeindeschlüssel und fachliche Objekt-IDs: nicht enthalten

Byteidentische Wiederholungen wurden entfernt. Gleiche Adressschlüssel mit
unterschiedlichen Koordinaten blieben erhalten. Die Web-Mercator-Koordinaten
der Kacheln wurden nach WGS84 zurückgerechnet.

## Gemeindezuordnung mit VG250

- Produkt: Verwaltungsgebiete 1:250 000, Stand 01.01.2026
- Bereitsteller: Bundesamt für Kartographie und Geodäsie (BKG)
- Produktseite:
  <https://gdz.bkg.bund.de/index.php/default/verwaltungsgebiete-1-250-000-stand-01-01-vg250-01-01.html>
- Downloadverzeichnis:
  <https://daten.gdz.bkg.bund.de/produkte/vg/vg250_ebenen_0101/2026/>
- verwendete Datei: `vg250_01-01.utm32s.shape.ebenen.zip`
- Koordinatenreferenzsystem: ETRS89 / UTM Zone 32N, EPSG:25832
- SHA-256 der Quelldatei:
  `a21bd17e3616fded6a1cae3311f6b0e8f156ba3939d113b8b760fec5664dd529`
- Lizenz: Datenlizenz Deutschland – Namensnennung – Version 2.0,
  <https://www.govdata.de/dl-de/by-2-0>

Beizubehaltender Quellenvermerk:

> Gemeindegrenzen und Verwaltungsschlüssel: © BKG (2026),
> dl-de/by-2-0; Daten bearbeitet, abgerufen am 31.08.2026.

Für die 4.372.080 reinen basemap.de-Adresspunkte fehlten Gemeinde und AGS.
Sie wurden per Punkt-in-Polygon gegen den Layer `VG250_GEM` geprüft. Verwendet
wurden Landflächen (`GF=4`) und ergänzende Wasserflächen (`GF=2`):

- eindeutige Zuordnung zu genau einer Gemeinde: **4.371.973**
- Mehrfachtreffer: **0**
- unzugeordnet: **107**

Die 107 unzugeordneten Punkte liegen außerhalb der auf den Maßstab 1:250.000
generalisierten Flächen. Obwohl jeder höchstens 145,93 m von der nächsten
Gemeinde entfernt liegt, wurde kein Nearest-Fallback als amtliche
Gemeindezugehörigkeit ausgegeben.

## Postleitzahlen aus Länderquellen und OpenStreetMap

### Vorrang amtlicher offener Länderwerte

Vorhandene PLZ aus den offenen Länderdateien blieben unverändert. Dies betrifft:

| Land | unverändert erhaltene PLZ |
|---|---:|
| Brandenburg | 867.290 |
| Berlin | 394.303 |
| Bremen | 176.374 |
| Hamburg | 302.408 |
| Nordrhein-Westfalen | 4.509.538 |
| Schleswig-Holstein | 953.360 |
| Saarland | 334.991 |
| Sachsen | 990.090 |
| **Summe** | **8.528.354** |

### OSM-Ergänzung

Nur bei leerem Länderwert wurde eine PLZ aus offenen
OpenStreetMap-Polygonen ergänzt. Dabei galt:

1. Es wurde ausschließlich ein genau eindeutiger fünfstelliger Polygonwert
   übernommen.
2. Bei keinem Polygon oder mehreren unterschiedlichen PLZ blieb das Feld leer.
3. Vorhandene amtliche offene Länderwerte wurden nie überschrieben.

Quelle:

- Datenbank: OpenStreetMap
- Urheberhinweis: © OpenStreetMap-Mitwirkende
- Lizenz: Open Database License 1.0 (ODbL),
  <https://opendatacommons.org/licenses/odbl/1-0/>
- Urheber- und Lizenzseite: <https://www.openstreetmap.org/copyright>
- Deutschland-PBF-Abzug: BBBike,
  <https://download.bbbike.org/osm/bbbike/Germany/>
- PBF-Abruf: 30.08.2026

Beizubehaltender Quellen- und Lizenzhinweis:

> Postleitzahl-Ergänzung enthält aus OpenStreetMap abgeleitete Daten:
> © OpenStreetMap-Mitwirkende, ODbL 1.0,
> <https://www.openstreetmap.org/copyright>. Deutschland-Abzug bereitgestellt
> durch BBBike; Daten bearbeitet, abgerufen am 30.08.2026.

Die Polygone wurden unabhängig direkt aus dem PBF-Abzug erzeugt. Verwendet
wurden OSM-Relationen `boundary=postal_code`; fragmentierte Ways wurden zu
geschlossenen Außen- und Innenringen zusammengesetzt. Unvollständige oder
geometrisch nicht rekonstruierbare Relationen wurden nicht teilweise
ausgegeben. Es wurden keine Daten der ZSHH, der Deutschen Post oder des
Projekts `yetzt/postleitzahlen` verwendet.

Ergebnis der Polygonerzeugung:

- **8.175** PLZ-Features
- **8.712** Außenringe und **144** Innenringe
- keine Relation mit fehlerhaft rekonstruierter vollständiger Geometrie

Ergebnis der Adresszuordnung:

- genau eine OSM-PLZ für **23.252.435** Adresspunkte
- kein Polygon für 43 Punkte
- mehrere unterschiedliche PLZ für 8 Punkte
- unverändert erhaltene amtliche PLZ: **8.528.354**
- OSM-Ergänzungen bei zuvor leerer PLZ: **14.724.111**
- endgültig leere PLZ: **21**

Die OSM-Polygone bilden räumliche PLZ-Gebiete ab. Sie sind keine amtliche oder
postalische Bestätigung und decken insbesondere nicht notwendig Postfächer,
Großempfänger oder andere nicht flächenhafte Sonderfälle ab. Die ODbL-Pflichten
für die aus OSM abgeleiteten Bestandteile werden durch die übrigen
Komponentenlizenzen nicht aufgehoben.

## Ableitung der 24 Ausgabefelder

| Feld | Belegung in libreHKDE |
|---|---|
| `nba` | konstant `N` für Komplettbestand |
| `oid` | leer; keine neue libre-ID |
| `qua` | leer; keine geratene A/B/C-Klasse |
| `landschl`, `land` | Länderquelle oder basemap.de-Land |
| `regbezschl` | strukturierter Länder-Hausschlüssel, sonst VG250, sonst `0` |
| `regbez` | leer |
| `kreisschl` | strukturierter Länder-Hausschlüssel, sonst VG250, sonst `00` |
| `kreis` | leer |
| `gmdschl` | strukturierter Länder-Hausschlüssel, sonst VG250, sonst `000` |
| `gmd` | Länderquelle, sonst eindeutige VG250-Zuordnung, in 107 Fällen leer |
| `ottschl` | strukturierter Länder-Hausschlüssel, sonst `0000` |
| `ott` | offener Ortsteilname, soweit vorhanden |
| `strschl` | strukturierter Länder-Hausschlüssel, sonst `00000` |
| `str` | Länderquelle oder basemap.de |
| `hnr` | führender Ziffernteil; bei fehlender Grundnummer `0` |
| `adz` | nachfolgender Hausnummernrest und vorhandener Zusatz |
| `zone` | konstant `32` |
| `ostwert`, `nordwert` | WGS84 nach EPSG:25832 transformiert, drei Dezimalstellen |
| `postplz` | offener Länderwert, sonst genau eindeutige OSM-Zuordnung |
| `postonm` | offener Ortswert; keine behauptete DPD-Schreibweise |
| `postonmzus`, `postott` | leer |

## Technische Prüfung

Die vollständige Ausgabeprüfung bestätigte:

- genau **23.252.486** Datenzeilen;
- genau 24 Felder je Zeile;
- exakt die dokumentierte Kopfzeile;
- `nba=N`, `zone=32`;
- `oid` und `qua` in jeder Zeile leer;
- gültige zwei-, ein-, zwei-, drei-, vier- und fünfstellige Schlüsselformate;
- sechsstellige Ostwerte und siebenstellige Nordwerte mit je drei
  Dezimalstellen;
- gültige fünfstellige oder leere PLZ;
- UTF-8 ohne Ersatzzeichen;
- ausschließlich CRLF-Zeilenenden;
- kein BOM;
- gültiges Gzip-Archiv;
- mit `SHA256SUMS` übereinstimmende Prüfsumme.

Koordinatenbereich der Ausgabe:

- Ostwert: 280.406,830 bis 920.637,606
- Nordwert: 5.241.017,328 bis 6.100.794,594

## Lizenzzusammenfassung für die Weitergabe

Der kombinierte Bestand erhält **keine neue einheitliche Sammellizenz**, welche
die Bedingungen der Komponenten ersetzt. Je nach Datensatz beziehungsweise
Feld gelten:

- Creative Commons Namensnennung 4.0 International (CC BY 4.0);
- Datenlizenz Deutschland – Namensnennung – Version 2.0
  (dl-de/by-2-0);
- Datenlizenz Deutschland – Zero – Version 2.0
  (dl-de/zero-2-0);
- für OSM-abgeleitete PLZ die Open Database License 1.0 (ODbL).

Bei Weitergabe des Gesamtbestands sind daher mindestens alle in diesem Dokument
als beizubehalten gekennzeichneten Quellenvermerke zusammen mit dem
OpenStreetMap-/ODbL-Hinweis weiterzugeben. Bearbeitungen müssen entsprechend
den jeweiligen Lizenzbedingungen kenntlich gemacht werden.

Diese Dokumentation beschreibt die verwendeten Quellen und die technische
Verarbeitung. Sie ist keine Rechtsberatung und trifft keine rechtliche Aussage
über die Schutzfähigkeit der HK-DE-Formatbeschreibung, Produktbezeichnungen
oder mögliche Datenbankrechte.
