# Adressliste Bayern aus basemap.de - Abruf vom 28.08.2026

## Ergebnis

Der Export enthält **3.761.180** bayerische Adresspunkte aus dem offenen
deutschlandweiten Produkt **basemap.de Web Vektor**.

- Datei: `adressen.csv.gz`
- Datensätze: **3.761.180**
- Datensätze mit Straße und Hausnummer: **3.761.180**
- Datensätze mit Postleitzahl: **0**
- Datenstand der Kacheln laut TileJSON: **20.07.2026**
- Format: UTF-8-CSV, RFC-4180-konform, Gzip-komprimiert
- SHA-256:
  `5bd36a8099ad0e62fac80ff6454d0d9a8f9ff42de862cc83b9c20647fb63d743`

Damit steht erstmals ein offen lizenzierter amtlicher Bayern-Bestand mit
Straßen, Hausnummern und Koordinaten zur Verfügung. Er ist jedoch kein
vollständiger Ersatz für das landeseigene Produkt der amtlichen
Hauskoordinaten: basemap.de enthält keine Postleitzahl, keinen
Gemeindeschlüssel und keine fachliche Quell-ID.

Die zuvor festgestellte Einschränkung der **landeseigenen** Angebote bleibt
bestehen. Hausumringe und ATKIS Basis-DLM enthalten keine vollständigen
Hausnummernadressen; die bayerischen Hauskoordinaten- und INSPIRE-Dienste sind
zugangs- beziehungsweise gebührenbeschränkt. Der vorliegende Export stammt
stattdessen aus dem offenen gemeinsamen Kartenprodukt von Bund und Ländern.

## Amtliche Bundesquelle basemap.de

- Produkt: **basemap.de Web Vektor**
- Bereitsteller: **Bundesamt für Kartographie und Geodäsie (BKG) /
  GeoBasis-DE**
- BKG-Produktseite:
  <https://gdz.bkg.bund.de/index.php/default/gdz-basemapde-vektor-gdz-basemapde-vektor.html>
- TileJSON:
  <https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/bm_web_de_3857.json>
- Kachelschema:
  `https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/{z}/{x}/{y}.pbf`
- Datenmodell:
  <https://basemap.de/data/produkte/web_vektor/meta/bm_web_vektor_datenmodell.html>
- Abrufdatum: **28.08.2026**
- Kachelprojektion: Web Mercator, **EPSG:3857**
- Ausgabeprojektion: OGC CRS84, Länge vor Breite

Der Layer `Adresse` liegt ausschließlich in Zoomstufe 15 vor. Das amtliche
Datenmodell beschreibt ihn als aus amtlichen Hauskoordinaten mit
georeferenzierten Adressangaben hervorgegangen. Seine Attribute sind:

- `land`
- `ort`
- `ortsteil` (optional)
- `strasse`
- `hausnummer` (optional)

Für den Bundesexport wurden sämtliche Zoom-15-Kacheln verwendet, die die
Deutschlandmaske schneiden. Anschließend wurde für Bayern ausschließlich
`land = BY` übernommen.

## Lizenz der basemap.de-Daten

Die Daten werden geldleistungsfrei unter
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)**
bereitgestellt. Die gespeicherten Nutzungsbedingungen nennen als
Quellenvermerk:

> © GeoBasis-DE / BKG (2026) CC BY 4.0

Da der vorliegende Export Kacheln dekodiert, Koordinaten zurücktransformiert,
Attribute normalisiert und in ein anderes Schema überführt, ist zusätzlich
kenntlich zu machen, dass die Daten bearbeitet wurden:

> Adressen: © GeoBasis-DE / BKG (2026), CC BY 4.0; Daten bearbeitet,
> abgerufen am 28.08.2026.

Die Produktseite, das Datenmodell, TileJSON, Nutzungsbedingungen und eine
Originalkachel aus München liegen im gemeinsamen Snapshot unter
`adressen/bund/basemap/2026-08-28/`.

## Verarbeitung

1. Aus einer Deutschland-Grenzgeometrie wurden alle sie schneidenden
   Zoom-15-Kacheln bestimmt.
2. **679.682** Kachelpositionen wurden vollständig geprüft.
3. Der Layer `Adresse` wurde aus den Mapbox Vector Tiles dekodiert.
4. Die Kachelkoordinaten wurden unter Beachtung des jeweiligen
   Layer-`extent` nach WGS84 zurückgerechnet.
5. Nur Datensätze mit `land = BY` wurden für diesen Export übernommen.
6. Byteidentische Wiederholungen aus Kacheln wurden entfernt. Unterschiedliche
   Koordinaten derselben Anzeigeadresse wurden bewusst erhalten.
7. Die Hausnummer wurde, soweit sie mit Ziffern beginnt, in Grundnummer und
   nachfolgenden Zusatz getrennt.
8. Aus dem vollständigen Inhalt wurde eine reproduzierbare abgeleitete
   Datensatznummer gebildet; sie ist keine amtliche Objekt-ID.
9. Das Ergebnis wurde byteweise sortiert und deterministisch mit `gzip -n -9`
   komprimiert.

## Feldzuordnung

| Einheitliche Spalte | basemap.de-Quelle beziehungsweise Ableitung |
|---|---|
| `vollstaendige_adresse` | `strasse`, `hausnummer`, `ort` |
| `bundesland` | `Bayern` aus `land = BY` |
| `landesschluessel` | konstant `09` |
| `postleitzahl` | leer; nicht im Layer vorhanden |
| `ortsname_post` | `ort`; basemap unterscheidet keinen postalischen Ortsnamen |
| `gemeindename` | leer; keine gesonderte Gemeindeangabe vorhanden |
| `ortsteilname` | `ortsteil` |
| `strassenname` | `strasse` |
| `hausnummer` | numerischer Anfang von `hausnummer` |
| `hausnummernzusatz` | Rest von `hausnummer` nach dem numerischen Anfang |
| `longitude_wgs84` | aus Kachel-X und Feature-X abgeleitet |
| `latitude_wgs84` | aus Kachel-Y und Feature-Y abgeleitet |
| `datensatznummer` | abgeleitete stabile Inhalts-ID mit Präfix `BKG-` |
| `hausschluessel` | Land;Ort;Ortsteil;Straße;ursprüngliche Hausnummer |

## Qualitätsprüfung

- exakt **3.761.180** Datenzeilen
- jede Zeile besitzt 14 Ausgabespalten
- keine leere Straße
- keine leere Hausnummer
- kein leerer Ortsname
- **232.028** Datensätze ohne Ortsteil
- **3.400** mehrfach vorkommende Adressschlüssel mit **3.558** zusätzlichen
  Vorkommen; unterschiedliche Koordinaten wurden erhalten
- keine ungültigen Koordinaten
- sämtliche Postleitzahlen leer
- Gzip-Integrität erfolgreich
- deterministische Prüfsumme bestätigt

Die Vollständigkeit bezieht sich auf sämtliche bayerischen Adressobjekte, die
im am 28.08.2026 abgerufenen basemap.de-Kachelstand enthalten sind. Sie ist
nicht gleichbedeutend mit einer Garantie, dass basemap.de jedes Objekt des
zugangsbeschränkten bayerischen Hauskoordinatenprodukts wiedergibt.

## Geprüfte offene Quellen

### Hausumringe

- Produktseite:
  <https://geodaten.bayern.de/opengeodata/OpenDataDetail.html?pn=hausumringe>
- Lizenz: **CC BY 4.0**
- Format: SHAPE
- Raumbezug: EPSG:25832
- Abgabeeinheit: Regierungsbezirk
- Aktualisierung: vierteljährlich

Die Produktseite beschreibt Hausumringe als georeferenzierte
Umringpolygone der Gebäudegrundrisse des Liegenschaftskatasters. Die
amtliche Datenformatbeschreibung weist im dBASE-Teil je Objekt nur das
Attribut `AGS` (Amtlicher Gemeindeschlüssel) aus.

Damit fehlen insbesondere:

- Straßenname
- Hausnummer
- Hausnummernzusatz
- Postleitzahl
- postalischer Ortsname

Ein Polygonschwerpunkt könnte zwar als Gebäudekoordinate berechnet werden,
aus der Geometrie lässt sich aber keine Adresse ableiten. Eine räumliche
Verknüpfung wäre nur mit einer zusätzlichen Adressquelle möglich; genau diese
steht offen nicht vollständig zur Verfügung.

Gespeicherte Nachweise:

- `open-data-hausumringe.html`
- `datenformat-hausumringe.pdf`
- `lizenz-cc-by-4.0.html`

### ATKIS Basis-DLM

- Produktseite:
  <https://geodaten.bayern.de/opengeodata/OpenDataDetail.html?pn=atkis_basis_dlm>
- Lizenz: **CC BY 4.0**
- landesweite Formate: NAS, GeoPackage und SHAPE
- Aktualisierung: je nach Abgabe wöchentlich oder monatlich

Das Basis-DLM modelliert die Erdoberfläche nach topographischen
Gesichtspunkten. Es enthält unter anderem Verkehrswege und deren
Eigenschaften, aber keine vollständige Objektklasse aus Gebäudeadresse,
Hausnummer und Postleitzahl. Straßennamen aus ATKIS können nicht eindeutig
den Hausumringen zugeordnet werden und erzeugen keine Hausnummern.

Gespeicherter Nachweis:

- `open-data-atkis-basis-dlm.html`

### Allgemeines offenes ALKIS

Der von der Bayerischen Vermessungsverwaltung beschriebene vereinfachte
ALKIS-WFS stellt Flurstücke, Gebäude und Bauwerke, Nutzungen,
Verwaltungseinheiten und Katasterbezirke bereit:

<https://geodatenonline.bayern.de/geodatenonline/seiten/wfs_alkis>

Der Dienst steht laut amtlicher Beschreibung jedoch ausschließlich
Ressort- und Rahmenvereinbarungskunden zur Verfügung. Eine Lizenzierung zum
nutzungsabhängigen Tarif sei nicht möglich. Außerdem ist in den aufgeführten
Feature-Typen keine vollständige Adressobjektklasse enthalten.

Gespeicherter Nachweis:

- `wfs-alkis-vereinfacht.html`

## Amtliche Hauskoordinaten

Die amtlichen Hauskoordinaten wären fachlich die richtige Quelle:

- Produktbeschreibung:
  <https://www.ldbv.bayern.de/produkte/liegenschaftsinformationen/hauskoordinaten.html>
- WFS-Beschreibung:
  <https://geodatenonline.bayern.de/geodatenonline/seiten/wfs_hauskoor>
- WFS:
  <https://geoservices.bayern.de/wfs/v1/ogc_hauskoordinaten.cgi?>

Die Produktbeschreibung erläutert, dass jedem Gebäude mit Hausnummer eine
exakte Koordinate zugeordnet wird und die Adressdaten um postalische Angaben
der Deutschen Post AG erweitert sind. Der WFS umfasst nach amtlicher Angabe
rund **3,6 Millionen** georeferenzierte Gebäudeadressen.

Der Dienst ist aber nicht frei zugänglich. Die WFS-Seite verlangt vor der
Nutzung:

- Abschluss einer Nutzungsvereinbarung
- Kennung und Passwort

Ohne diese Zugangsdaten ist kein vollständiger Abruf möglich.

Gespeicherte Nachweise:

- `hauskoordinaten-produkt.html`
- `wfs-hauskoordinaten.html`

## INSPIRE-WFS Adressen

Ein INSPIRE-konformer Downloaddienst für Adressen ist amtlich dokumentiert:

- Metadaten:
  <https://gdk.gdi-de.org/geonetwork/srv/api/records/82844fd3-6abb-490d-be15-3fae3d7790c0/formatters/xml>
- WFS:
  <https://geoservices.bayern.de/inspire-ows/v1/alkis/ad/dls/wfs?>

Dieser Dienst ist ebenfalls keine offene Alternative:

- anonymer `GetCapabilities`-Abruf: **HTTP 401 Unauthorized**
- HTTP-Authentifizierung: `Basic realm="INSPIRE-WFS Hauskoordinaten"`
- öffentlicher Zugriff laut Metadaten aufgrund von Artikel 13 Absatz 1
  Buchstabe e der INSPIRE-Richtlinie beschränkt
- Nutzungsbedingungen und Gebühren-/Preisliste der Bayerischen
  Vermessungsverwaltung gelten
- Gebührenangabe in den Metadaten: **geldleistungspflichtig**

Gespeicherte Nachweise:

- `inspire-wfs-hauskoordinaten.xml`
- `inspire-wfs-anonymer-zugriff-header.txt`
- `inspire-wfs-anonymer-zugriff.html`

## Ortssuchdienst

Der REST-Ortssuchdienst ist eine Suchschnittstelle und kein
Massendownload:

<https://geoservices.bayern.de/bvvapi/tb/adressen/openapi.json>

Die OpenAPI-Beschreibung zeigt:

- API-Schlüssel im Header `x-api-key` erforderlich
- Volltextsuche statt vollständiger Enumeration
- maximal 100 Ergebnisse pro Suchanfrage
- Ergebnis-IDs sind nur temporär gültig

Ein anonymer Testabruf wurde mit **HTTP 403 Forbidden** und dem Hinweis
`ungueltiger Token` abgewiesen. Selbst mit einem Schlüssel wäre eine
systematische Volltextermittlung kein verlässlicher oder dokumentierter
Vollständigkeitsnachweis.

Gespeicherte Nachweise:

- `ortssuchdienst-openapi.json`
- `ortssuchdienst-anonymer-zugriff-header.txt`
- `ortssuchdienst-anonymer-zugriff.html`

## Lizenzbewertung

Für die tatsächlich offenen Hausumringe und das ATKIS Basis-DLM gilt
**Creative Commons Namensnennung 4.0 International (CC BY 4.0)**. Die
allgemeinen Nutzungsbedingungen erlauben Teilen und Bearbeiten, auch
kommerziell.

Diese Lizenz gilt nur für die jeweils als kostenfrei ausgewiesenen
Datensätze und Dienste. Sie darf nicht auf die gesondert zugangs- und
gebührenbeschränkten Hauskoordinaten oder den INSPIRE-Adress-WFS übertragen
werden.

## Einordnung der landeseigenen Quellenprüfung

Ein **postalisch vollständigerer** Bayern-Export wäre möglich, sobald
mindestens eine dieser Voraussetzungen erfüllt ist:

1. Die Bayerische Vermessungsverwaltung veröffentlicht Hauskoordinaten oder
   INSPIRE-Adressen als frei lizenzierbaren vollständigen Download.
2. Es liegt eine Nutzungsvereinbarung vor, die den vollständigen Abruf,
   die Bearbeitung und die Weiterverbreitung des erzeugten Adressarchivs
   ausdrücklich gestattet.
3. Eine weitere frei lizenzierte Quelle liefert belastbare Postleitzahlen, die
   räumlich mit den basemap.de-Adresspunkten verknüpft werden dürfen.

Bis dahin bleibt die Postleitzahl im vorliegenden basemap.de-Export leer.
