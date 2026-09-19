# VG250-Gemeindegrenzen – Stand 01.01.2026

## Quelle

- Produkt: Verwaltungsgebiete 1:250 000, Stand 01.01. (VG250 01.01.)
- Bereitsteller: Bundesamt für Kartographie und Geodäsie (BKG)
- Produktseite:
  <https://gdz.bkg.bund.de/index.php/default/verwaltungsgebiete-1-250-000-stand-01-01-vg250-01-01.html>
- Direktverzeichnis:
  <https://daten.gdz.bkg.bund.de/produkte/vg/vg250_ebenen_0101/2026/>
- Datei: `vg250_01-01.utm32s.shape.ebenen.zip`
- Koordinatenreferenzsystem: ETRS89 / UTM Zone 32N, EPSG:25832
- Abrufdatum: 31.08.2026
- SHA-256:
  `a21bd17e3616fded6a1cae3311f6b0e8f156ba3939d113b8b760fec5664dd529`

Die vom BKG gelieferte MD5-Prüfsumme
`19abb1eda644eea121ac037ca3c12e46` wurde beim Abruf bestätigt.

## Lizenz

Die beigefügten Nutzungsbedingungen stellen VG250 unter die Datenlizenz
Deutschland – Namensnennung – Version 2.0. Der dort vorgegebene Quellenvermerk
ist bei der Weitergabe zu beachten.

## Verwendung im Projekt

Verwendet wird der Layer `VG250_GEM` mit Gemeindename, AGS und den einzelnen
Schlüsselbestandteilen. Sowohl Landflächen (`GF=4`) als auch ergänzende
Wasserflächen (`GF=2`) gehen in die Zuordnung ein.

Von 4.372.080 basemap-only-Adresspunkten liegen 4.371.973 eindeutig in genau
einer Gemeinde. Kein Punkt besitzt mehrere Gemeindezuordnungen. 107 Punkte
liegen außerhalb aller generalisierten VG250-Flächen und bleiben unzugeordnet.
Die vollständige Statistik steht in
`adressen/bund/konsolidiert/2026-08-28/basemap-gemeinden-statistik.json`.
