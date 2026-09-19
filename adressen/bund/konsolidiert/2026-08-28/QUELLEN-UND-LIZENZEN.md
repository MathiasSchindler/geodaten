# Konsolidiertes Adressverzeichnis Deutschland - Stand 28.08.2026

## Ergebnis

Dieser Datenstand verbindet die offen nutzbaren amtlichen Länderbestände mit
den deutschlandweiten Adresspunkten aus **basemap.de Web Vektor**. Die
Länderquelle ist grundsätzlich führend; basemap.de ergänzt fehlende Objekte
und einzelne fehlende Ortsteile. Postleitzahlen werden ausschließlich aus
postalisch angereicherten amtlichen Länderquellen übernommen und niemals
geschätzt.

- konsolidierte Adressen: **23.252.486**
- Adressen mit Postleitzahl: **8.528.354** (**36,68 %**)
- Adressen ohne Postleitzahl: **14.724.132**
- verknüpfte Länder-/basemap-Objekte: **18.378.713**
- reine Länderobjekte: **501.693**
- reine basemap-Objekte: **4.372.080**
- nicht automatisch aufgelöste mehrdeutige basemap-Objekte: **212**
- abgedeckte Länder: **16**

Die Anzahl beschreibt Adressobjekte, nicht Gebäude oder Haushalte. Mehrere
Adresspunkte können dieselbe darstellbare Anschrift besitzen, etwa bei
getrennten Gebäudeteilen oder unterschiedlichen amtlichen Objektidentitäten.

## Dateien

| Datei | Inhalt | Datensätze | SHA-256 |
|---|---|---:|---|
| `adressen.csv.gz` | konsolidierter 14-spaltiger Hauptbestand | 23.252.486 | `4d3761bccc2db2e3f08a0d86845a90629437400f1d85b641096f1d7b4be84dd4` |
| `provenienz.csv.gz` | Herkunft und Abgleich je Hauptdatensatz | 23.252.486 | `1be96b3d9fab7d0035e16e8b910c2bc698699f9261c713aab2f2c9b4dba695a5` |
| `konflikte.csv.gz` | Kandidaten bei nicht eindeutigen Zuordnungen | 212 | `ca64109816647ea8479cb233a2c17fb5747946edbe3f6d8c43e8c9f2253ad823` |
| `quellen.csv` | Quellen, Lizenzen und erforderliche Quellenvermerke | 15 | `92d6b6de0f298c74109507090270ba5c4583c65174097ecf377c2d3a375c4689` |
| `konsolidierungs-statistik.json` | vollständige Gesamt- und Länderstatistik | - | `d968ac816be6ccb78d36a60fc2498f71772f2a80017e5865c1b8a8bb6d8dc5ae` |
| `validierungs-statistik.json` | Ergebnis der technischen Vollprüfung | - | `d9ff74c89708bc34541b56d2c80a165e2bc08521df36a7c36208600b6629239d` |

Alle CSV-Dateien sind UTF-8-kodiert, RFC-4180-konform, byteweise sortiert und
deterministisch mit `gzip -n -9` komprimiert.

## Datenmodell

`adressen.csv.gz` verwendet das gemeinsame 14-spaltige Schema:

1. `vollstaendige_adresse`
2. `bundesland`
3. `landesschluessel`
4. `postleitzahl`
5. `ortsname_post`
6. `gemeindename`
7. `ortsteilname`
8. `strassenname`
9. `hausnummer`
10. `hausnummernzusatz`
11. `longitude_wgs84`
12. `latitude_wgs84`
13. `datensatznummer`
14. `hausschluessel`

Die neue `datensatznummer` beginnt mit `BUND-` und enthält 24 hexadezimale
Zeichen aus einem SHA-256-basierten, stabilen Identitätswert. Sie ist innerhalb
dieses Exports eindeutig. Fachliche Original-IDs bleiben in
`provenienz.csv.gz` erhalten.

## Quellenpriorität und Feldherkunft

Für Brandenburg, Berlin, Baden-Württemberg, Bremen, Hessen, Hamburg,
Niedersachsen, Nordrhein-Westfalen, Rheinland-Pfalz, Schleswig-Holstein,
Saarland, Sachsen, Sachsen-Anhalt und Thüringen gilt:

1. Jeder Datensatz der Länderquelle bleibt erhalten.
2. Ein eindeutig passender basemap-Datensatz wird mit ihm verknüpft.
3. Nicht passende oder mehrdeutige basemap-Datensätze bleiben als eigene
   Adressobjekte erhalten.
4. PLZ, postalischer Ort, Gemeinde, Straße, Hausnummer, Originalschlüssel und
   grundsätzlich auch die Koordinate stammen aus der Länderquelle.
5. Ein in der Länderquelle leerer Ortsteil wird aus basemap.de ergänzt, sofern
   das eindeutig verknüpfte basemap-Objekt einen Ortsteil besitzt.

Abweichungen:

- **Niedersachsen:** Bei 2.564.571 eindeutig verknüpften Datensätzen wird die
  basemap-Koordinate verwendet. Der vorherige NI-Export enthält neben
  kartografischen Hausnummernpositionen dokumentierte geometrische Fallbacks;
  basemap.de liefert hier den einheitlicheren amtlichen Adresspunkt.
- **Bayern:** ausschließlich basemap.de, weil kein gleichwertiger offener
  landeseigener Hauskoordinatenbestand verfügbar war.
- **Mecklenburg-Vorpommern:** ausschließlich basemap.de. Der untersuchte
  INSPIRE-Adress-WFS besitzt keine klar ausgewiesene offene Standardlizenz und
  nennt für externe Nutzungen mögliche Genehmigungs- und Entgeltpflichten.
  Seine 518.416 Datensätze sind daher nicht Bestandteil dieses öffentlich
  weitergabefähigen konsolidierten Exports.

## Abgleichverfahren

Die Zeichenketten werden für den Abgleich kleingeschrieben, Umlaute und `ß`
werden vereinheitlicht, Unicode-Zeichenmarken sowie Interpunktion und
Leerzeichen entfernt und `str.` beziehungsweise endständiges `str` zu
`strasse` normalisiert.

Ein basemap-Objekt wird höchstens einem noch unverknüpften Länderobjekt
zugeordnet:

1. gleiche normalisierte Kombination aus Ort, Straße und Hausnummer sowie
   höchstens **25 m** Abstand;
2. falls Stufe 1 keinen Treffer liefert: gleiche normalisierte Kombination aus
   Straße und Hausnummer sowie höchstens **5 m** Abstand.

Nur genau ein räumlicher Kandidat wird verknüpft. Bei mehreren Kandidaten wird
nichts geraten: Das basemap-Objekt bleibt separat erhalten und der Fall wird
in `konflikte.csv.gz` dokumentiert.

Ergebnis:

- Stufe 1, Ort + Straße + Hausnummer bis 25 m: **14.840.548**
- Stufe 2, Straße + Hausnummer bis 5 m: **3.538.165**
- mehrdeutig in Stufe 1: **212**
- mehrdeutig in Stufe 2: **0**
- aus basemap.de ergänzte Ortsteile: **11.899.068**

## Postleitzahlen

Postleitzahlen werden nicht aus Polygonen, Nachbaradressen oder anderen
Hilfsbeständen abgeleitet. Eine befüllte PLZ stammt immer aus der in
`plz_herkunft` genannten Länderquelle:

- Brandenburg: 867.290
- Berlin: 394.303
- Bremen: 176.374
- Hamburg: 302.408
- Nordrhein-Westfalen: 4.509.538
- Schleswig-Holstein: 953.360
- Saarland: 334.991
- Sachsen: 990.090

Summe: **8.528.354**. Fünf NRW-Objekte und 31 Saarland-Objekte besitzen bereits
in ihrer jeweiligen Quelle keine PLZ. Basemap-only-Datensätze enthalten nie
eine PLZ.

## Datensätze nach Land

| Land | Länderinput | basemap-Input | Treffer 25 m | Treffer 5 m | nur basemap | nur Land | Ergebnis | mit PLZ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| BB | 867.290 | 865.218 | 863.967 | 106 | 1.145 | 3.217 | 868.435 | 867.290 |
| BE | 394.303 | 394.452 | 394.277 | 22 | 153 | 4 | 394.456 | 394.303 |
| BW | 3.382.566 | 3.081.505 | 2.670.772 | 406.043 | 4.690 | 305.751 | 3.387.256 | 0 |
| BY | 0 | 3.761.180 | 0 | 0 | 3.761.180 | 0 | 3.761.180 | 0 |
| HB | 176.374 | 176.265 | 176.040 | 72 | 153 | 262 | 176.527 | 176.374 |
| HE | 1.630.801 | 1.628.646 | 1.279.751 | 347.147 | 1.748 | 3.903 | 1.632.549 | 0 |
| HH | 302.408 | 284.445 | 283.407 | 32 | 1.006 | 18.969 | 303.414 | 302.408 |
| MV | 0 | 517.422 | 0 | 0 | 517.422 | 0 | 517.422 | 0 |
| NI | 2.643.144 | 2.630.761 | 1.003.106 | 1.561.465 | 66.190 | 78.573 | 2.709.334 | 0 |
| NW | 4.509.543 | 4.447.809 | 4.235.831 | 204.563 | 7.415 | 69.149 | 4.516.958 | 4.509.538 |
| RP | 1.406.086 | 1.402.151 | 1.223.972 | 176.892 | 1.287 | 5.222 | 1.407.373 | 0 |
| SH | 953.360 | 949.935 | 763.308 | 184.776 | 1.851 | 5.276 | 955.211 | 953.360 |
| SL | 335.022 | 334.367 | 316.544 | 17.664 | 159 | 814 | 335.181 | 334.991 |
| SN | 990.090 | 987.910 | 964.330 | 19.731 | 3.849 | 6.029 | 993.939 | 990.090 |
| ST | 664.459 | 664.049 | 103.500 | 557.892 | 2.657 | 3.067 | 667.116 | 0 |
| TH | 624.960 | 624.678 | 561.743 | 61.760 | 1.175 | 1.457 | 626.135 | 0 |
| **Bund** | **18.880.406** | **22.750.793** | **14.840.548** | **3.538.165** | **4.372.080** | **501.693** | **23.252.486** | **8.528.354** |

## Provenienzdatei

`provenienz.csv.gz` besitzt genau eine Zeile je Hauptdatensatz. Wesentliche
Felder sind:

- `quellen`: `LAND-XX`, `BASEMAP` oder beide Quellen;
- `land_datensatznummer` und `land_hausschluessel`: fachliche Originalwerte;
- `basemap_datensatznummer`: abgeleitete `BKG-`-ID;
- `lizenzen`: auf den Datensatz anwendbare Komponentenlizenzen;
- `abgleich_status`, `abgleich_regel`, `abgleich_distanz_meter`;
- `plz_herkunft`, `koordinaten_herkunft`, `ortsteil_herkunft`;
- `qualitaetshinweise`.

Die Quellcodes sind über `quellen.csv` auf Produkt, Bereitsteller, Datenstand,
Lizenz, Lizenz-URL, Quellenvermerk und lokale Detaildokumentation abbildbar.

## Lizenzen und Namensnennung

Die Komponenten stehen je nach Quelle unter:

- Creative Commons Namensnennung 4.0 International (**CC BY 4.0**),
- Datenlizenz Deutschland - Namensnennung - Version 2.0
  (**DL-DE-BY-2.0**),
- Datenlizenz Deutschland - Zero - Version 2.0
  (**DL-DE-ZERO-2.0**).

Die Komponentenlizenzen werden nicht durch eine neue Sammellizenz ersetzt.
Welche Quellen und Lizenzen für einen einzelnen Datensatz relevant sind, steht
in `provenienz.csv.gz`. Bei Weitergabe des Gesamtbestands sind alle in
`quellen.csv` aufgeführten Quellenvermerke beizubehalten. Insbesondere gilt für
die bundesweite Ergänzungsquelle:

> © GeoBasis-DE / BKG (2026) CC BY 4.0; Daten bearbeitet, abgerufen am
> 28.08.2026.

`quellen.csv` enthält zusätzlich die von den 14 verwendeten Länderquellen
vorgegebenen Namensnennungen. Die Detaildokumentationen der Originalexporte
sind dort relativ verlinkt.

## Qualitäts- und Integritätsprüfung

Die vollständige Prüfung war erfolgreich:

- Gzip-Integrität aller drei Archive;
- Schemas und Spaltenzahlen aller 46.505.184 Haupt- und Provenienzzeilen;
- exakt 23.252.486 Haupt- und 23.252.486 Provenienzdatensätze;
- eindeutige `BUND-`-IDs und exakte Gleichheit beider ID-Mengen;
- alle 16 Länder und korrekte amtliche Landesschlüssel;
- gültige fünfstellige PLZ oder leeres Feld;
- plausible WGS84-Koordinaten innerhalb Deutschlands;
- PLZ nur mit passender `LAND-XX`-Herkunft;
- Bayern und Mecklenburg-Vorpommern ausschließlich aus `BASEMAP`;
- zulässige Quellen-/Lizenzkombinationen und Abgleichregeln;
- exakt 212 Konfliktzeilen entsprechend der Konsolidierungsstatistik;
- byteweise Sortierung;
- bytegenaue Reproduktion mit `gzip -n -9`;
- unveränderte SHA-256-Prüfsummen nach der Validierung.

Die Pipeline liegt unter `../../basemap/tools/`. Maßgebliche Programme sind
`consolidate-state.mjs`, `summarize-consolidation.mjs`,
`assemble-consolidated.sh`, `validate-consolidated.mjs` und
`verify-consolidated.sh`.

## Grenzen

- PLZ-Abdeckung besteht nur für acht Länder und wird nicht räumlich ergänzt.
- Die offene basemap.de-Quelle liefert keine PLZ, Gemeindeschlüssel oder
  fachlichen Objekt-IDs.
- Ein fehlender Abgleich bedeutet nicht zwingend ein zusätzliches reales
  Gebäude; Schreibweisen, Ortshierarchien, Aktualitätsunterschiede und
  verschiedene Objektmodellierung können getrennte Datensätze verursachen.
- Die konservative Konfliktbehandlung bevorzugt nachvollziehbare
  Doppelrepräsentation gegenüber einer möglicherweise falschen Vereinigung.
- Der Datenstand ist eine Momentaufnahme der jeweils dokumentierten
  Quellstände und Abrufdaten, kein postalisches Zustellverzeichnis.
