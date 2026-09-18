# Machbarkeit eines offenen HK-DE-5.2-kompatiblen Exports

Stand: 31. August 2026

## Kurzantwort

Mit den derzeit im Projekt vorhandenen offenen Daten kann eine Datei erzeugt
werden, die die **äußere Struktur** von HK-DE 5.2 erfüllt:

- UTF-8;
- 24 mit Semikolon getrennte Felder;
- exakt 23 Semikolons je Datenzeile;
- vorgegebene Spaltennamen;
- Länderdateien;
- vollständiger Bestand mit `nba=N`;
- Koordinaten in ETRS89/UTM Zone 32 (EPSG:25832).

Ein bundesweiter Bestand, der auch die **fachliche Semantik aller Pflichtfelder**
des ZSHH-Produkts erfüllt, kann aus dem aktuellen Open-Data-Bestand weiterhin
nicht seriös erzeugt werden. Das Projekt verwendet deshalb ein ausdrücklich
unvollständiges Kompatibilitätsprofil:

> **libreHKDE – HK-DE-5.2-kompatible 24-Spalten-Struktur aus offenen Quellen;
> kein HK-DE-Produkt und keine Deutsche-Post-Direkt-Anreicherung.**

Der freestanding-C-Exporter lässt `oid` und `qua` bewusst leer, führt keine neue
libre-ID ein und behält Datensätze auch bei dokumentierten Feldlücken. Der am
31.08.2026 erzeugte Bestand enthält alle **23.252.486** konsolidierten
Adressobjekte.

## Referenz

Geprüft wurde die von der ZSHH veröffentlichte:

> Datenformatbeschreibung Hauskoordinaten Deutschland (HK-DE), Version 5.2,
> Stand 02.05.2025, gültig ab der Datenabgabe aus dem HK-DE-Datenbestand 2025

Lokale Kopie:
`datenformatbeschreibung_hk-de_v_5.2.pdf`

Offizielle URL:
<https://www.ldbv.bayern.de/mam/ldbv/dateien/datenformatbeschreibung_hk-de_v_5.2.pdf>

Öffentliche Testdaten:
<https://www.geodaten.bayern.de/oadownload/bvv_internet/Testdaten/Hauskoordinaten_DE_csv_utm32.zip>

Die Testdateien wurden nur zur Prüfung von Syntax und Belegungsregeln
untersucht. Ihre Datensätze sind keine Quelle für `libreHKDE`.

## Formale Struktur

Die Kopfzeile lautet:

```text
nba;oid;qua;landschl;land;regbezschl;regbez;kreisschl;kreis;gmdschl;gmd;ottschl;ott;strschl;str;hnr;adz;zone;ostwert;nordwert;postplz;postonm;postonmzus;postott
```

Die Formatbeschreibung erlaubt leere Werte ausdrücklich nur für:

- `regbez`;
- `kreis`;
- `ott`;
- `adz`;
- `postonmzus`;
- `postott`.

Nicht geführte Schlüssel werden nicht leer gelassen, sondern mit `0`, `00`,
`000`, `0000` beziehungsweise `00000` belegt. Eine fehlende Hausnummer wird
als `0` ausgegeben. `postplz` und `postonm` besitzen keine dokumentierte
Leerwertregel.

Beide öffentlichen ZSHH-Testdateien bestätigen:

- UTF-8 ohne BOM;
- CRLF-Zeilenenden;
- 24 Felder;
- befüllte Felder 21 `postplz` und 22 `postonm`;
- Leerwerte nur in den ausdrücklich optionalen Feldern.

Thüringens offener Originalbestand ist ein wichtiger Gegenbeleg zur strengen
Leerwertauslegung: Das Land liefert 624.960 Zeilen mit genau 24 Feldern und der
HK-DE-5.2-Beschreibung, lässt aber sämtliche vier postalischen Felder leer.
Damit ist eine **HK-DE-5.2-kompatible Struktur ohne postalische Anreicherung**
in amtlicher Open-Data-Praxis vorhanden. Das beweist nicht, dass die ZSHH eine
solche Datei als ihr vollständiges Vertriebsprodukt ansehen würde.

## Feldweise Bewertung

| Feld | Open-Data-Lage | Bewertung |
|---|---|---|
| `nba` | Komplettbestand | `N` kann regelkonform gesetzt werden. |
| `oid` | In vielen Länderquellen vorhanden; in basemap.de nicht | Eine eigene 16-stellige ID wäre technisch möglich, hätte aber nicht die dokumentierte HK-DE-/ZSHH-Lebenszyklus- und Umschlüsselungssemantik. |
| `qua` | In mehreren originalen Länderdateien vorhanden; im konsolidierten Schema verworfen; in basemap.de nicht vorhanden | Bundesweit nicht belegbar. A/B/C darf nicht geraten werden. |
| `landschl`, `land` | vorhanden | belegbar |
| `regbezschl`, `regbez` | teilweise aus Länderquellen rekonstruierbar; basemap.de liefert sie nicht | Schlüssel darf ggf. `0`, Name leer sein; semantisch rekonstruierbar, aber noch nicht bundesweit in der Konsolidierung erhalten. |
| `kreisschl`, `kreis` | teilweise im Länder-Hausschlüssel; basemap.de liefert sie nicht | Schlüssel darf ggf. `00`, Name leer sein; noch nicht bundesweit erhalten. |
| `gmdschl`, `gmd` | Länderquellen überwiegend vorhanden; für basemap-only fehlen beide | `gmd` ist kein dokumentiertes Leerfeld. Räumliche Zuordnung zu offenen Verwaltungsgrenzen wäre erforderlich. |
| `ottschl`, `ott` | länderspezifisch, vielfach nicht geführt | `0000` und leer sind ausdrücklich zulässig. |
| `strschl` | nur in einem Teil der Länderquellen; basemap.de und OSM liefern keinen amtlichen Straßenschlüssel | `00000` ist als Nichtführung zulässig, verliert aber die fachliche Schlüsselidentität. |
| `str`, `hnr`, `adz` | weitgehend vorhanden | belegbar; Hausnummer muss sauber in Ziffernteil und Zusatz getrennt werden. |
| `zone` | konstant | `32` |
| `ostwert`, `nordwert` | Länderquellen teilweise nativ EPSG:25832; sonst WGS84/Web-Mercator | Reprojektion ist technisch möglich. Millimetergenauigkeit der Darstellung bedeutet bei abgeleiteten Koordinaten keine Millimetergenauigkeit der Quelle. |
| `postplz` | offen nur für einen Teil der Länder; OSM-Polygone liefern eine nahezu vollständige, aber nicht amtlich-postalische Ergänzung | Fünfstellig technisch belegbar, aber nicht in der von der Beschreibung genannten DPD-Semantik. |
| `postonm` | in offenen Länderquellen nur teilweise; der konsolidierte Ersatz ist häufig Gemeinde-/Ortsname | Bundesweit kein nachgewiesener postalischer Ortsname. |
| `postonmzus` | offen praktisch nicht vorhanden | Darf leer sein. |
| `postott` | offen nur teilweise und semantisch nicht einheitlich | Darf leer sein. |

## Quantitative Lücken des konsolidierten Bestands

Geprüfte Datensätze: **23.252.486**

- **4.372.080** reine basemap.de-Datensätze besitzen keine Länder-Quell-ID und
  keinen Länder-Hausschlüssel. Durch Punkt-in-Polygon gegen den offenen
  BKG-Bestand VG250 wurden **4.371.973** davon eindeutig einer Gemeinde
  zugeordnet. Es gab keinen Mehrfachtreffer. **107** Punkte liegen wegen der
  generalisierten Grenzgeometrie außerhalb aller Gemeindegebiete und bleiben
  bewusst unvollständig. Alle liegen höchstens 145,93 m von der nächsten
  Gemeinde entfernt; ein Nearest-Fallback wird nicht als amtliche Zugehörigkeit
  ausgegeben.
- **14.724.132** Datensätze besitzen vor der experimentellen OSM-Anreicherung
  keine offen amtlich bestätigte PLZ.
- **18.880.406** Datensätze besitzen einen Länder-Hausschlüssel;
  **15.739.689** davon folgen dem im Projekt erkannten Schema mit Land-,
  Regierungsbezirks-, Kreis-, Gemeinde-, Ortsteil- und Straßenschlüssel.
- **18.577.998** Länder-Quell-IDs sind exakt 16 alphanumerische Zeichen lang.
  Das allein belegt noch nicht die bundesweite HK-DE-Lebenszyklussemantik.
- Das konsolidierte 14-Spalten-Schema enthält für **keinen** Datensatz die
  Qualitätsklasse `qua`, den Regierungsbezirksnamen, den Kreisnamen oder einen
  Zusatz zum postalischen Ortsnamen.
- **3.008** Datensätze besitzen keine reguläre Hausnummer; die Ausgabe `hnr=0`
  wäre dafür formatgerecht.

Der Wert `ortsname_post` ist zwar in allen konsolidierten Zeilen befüllt, wird
bei fehlender postalischer Quelle aber ausdrücklich durch einen amtlichen
Gemeinde- oder basemap-Ortsnamen ersetzt. Er darf deshalb nicht pauschal als
HK-DE-`postonm` mit DPD-Semantik ausgegeben werden.

## Was konkret fehlt

Für einen bundesweiten, fachlich HK-DE-5.2-konformen Open-Data-Export fehlen:

1. **Qualität A/B/C je Datensatz.**  
   Für basemap-only-Datensätze ist keine HK-DE-Qualitätsklasse veröffentlicht.
   Die Klasse kann nicht aus der bloßen Punktkoordinate geraten werden.

2. **Ein belastbarer Objektidentifikator mit Lebenszyklus.**  
   Eine eigene libre-ID kann stabil und eindeutig gemacht werden, ist aber
   nicht die HK-DE-`oid`. Für Differenzbestände wäre zusätzlich eine dauerhafte
   Historie samt Umschlüsselungen erforderlich.

3. **Gemeinde und Gemeindeschlüssel für basemap-only-Datensätze.**  
   Dies betrifft mindestens 4.372.080 Datensätze. Eine räumliche Zuordnung aus
   offenen Verwaltungsgrenzen ist möglich, aber noch nicht Teil des Bestands.

4. **Vollständige fachliche Schlüssel.**  
   Regierungsbezirks-, Kreis-, Ortsteil- und Straßenschlüssel sind nicht in
   allen Quellen verfügbar. Nullwerte sind formal vorgesehen, ersetzen aber
   keine tatsächlich geführten Schlüssel.

5. **Bundesweit nachgewiesene postalische Namen.**  
   OSM kann PLZ-Gebiete und Adressangaben liefern, aber nicht garantieren, dass
   Schreibweise und Bedeutung `postonm`, `postonmzus` und `postott` entsprechen.

6. **Eine offene Quelle für die DPD-Feldsemantik.**  
   Die Beschreibung bezeichnet Felder 21 bis 24 ausdrücklich als Daten und
   Schreibweisen der Deutsche Post Direkt GmbH. Andere offene Werte können in
   denselben Spalten technisch stehen, sind aber kein identischer Dateninhalt.

7. **Bundesweite Differenzhistorie.**  
   N/L/A-Dateien und Umschlüsselungen setzen einen stabil versionierten
   Vorgängerbestand voraus.

## Umgesetztes pragmatisches Zielprofil

Der Exporter erzeugt folgendes eigenes Profil:

> **libreHKDE – HK-DE-5.2-kompatible 24-Spalten-Struktur aus offenen Quellen;
> kein HK-DE-Produkt und keine Deutsche-Post-Direkt-Anreicherung.**

Die bewusst pragmatischen Regeln sind:

- `oid` bleibt leer; es wird zum jetzigen Zeitpunkt keine neue libre-ID
  eingeführt.
- `qua` bleibt leer; A/B/C wird nicht geraten.
- Unvollständige Datensätze bleiben im Komplettbestand und ihre Lücken werden
  gezählt.
- Vorhandene amtliche PLZ hat Vorrang. Bei leerer PLZ wird nur ein genau
  eindeutiger Treffer aus der selbst erzeugten OSM-Polygonsammlung übernommen.
- Gemeinde und AGS werden bei basemap-only-Datensätzen nur bei strengem
  Punkt-in-Polygon-Treffer gegen VG250 übernommen.
- Nicht vorhandene Schlüssel erhalten die von HK-DE vorgesehenen Nullwerte.
- `postonm` enthält den offenen Ortswert des konsolidierten Bestands, nicht eine
  behauptete Deutsche-Post-Direkt-Schreibweise. `postonmzus` und `postott`
  bleiben leer.
- Das Profil behauptet ausdrücklich keine vollständige fachliche
  HK-DE-5.2-Konformität oder Produktgleichheit.

## Programm und erzeugter Bestand

`experimental/osm/pbf-parser/src/tools/libre_hkde.c` implementiert den Export in
freestanding C ohne Standard-C-Bibliothek und ohne externe Laufzeitabhängigkeit.
Es liest die normalisierte Adressdatei und die zeilengleiche
VG250-Gemeindezuordnung, prüft deren IDs, transformiert WGS84 nach EPSG:25832 und
schreibt CRLF-Zeilen mit exakt 24 Feldern.

Der Bestand liegt unter `experimental/ZSHH/libreHKDE/2026-08-31/`. Ergebnis:

- **23.252.486** Datensätze;
- `oid` und `qua`: in allen Zeilen leer;
- **4.371.973** ergänzte Gemeinden;
- **107** leere Gemeinden;
- **8.528.354** unverändert erhaltene amtliche PLZ;
- **14.724.111** aus aktuellen OSM-Polygonen ergänzte PLZ;
- **21** weiterhin leere PLZ;
- **3.008** formatgerecht als `hnr=0` ausgegebene unregelmäßige Hausnummern;
- keine Semikolons aus Eingabewerten ersetzt;
- vollständige technische Validierung aller Zeilen erfolgreich.

## Rechtliche Abgrenzung

Diese Untersuchung bewertet technische Struktur, Feldsemantik und
Datenprovenienz. Sie ist keine rechtliche Beurteilung der Schutzfähigkeit der
Formatbeschreibung, der Produktbezeichnung oder möglicher Datenbankrechte.
