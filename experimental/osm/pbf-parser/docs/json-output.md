# JSON-OUTPUT

## NAME

json-output - streaming JSON Lines contract for newos tools

## DESCRIPTION

Tools that accept `--json` write newline-delimited JSON events. Each event is a complete JSON object followed by a newline, and tools should flush each event as it is produced. This lets front ends and other supervisors consume long-running commands incrementally without waiting for the process to exit.

JSON mode is opt-in. Normal text, CSV, binary, quiet, and interactive output remain unchanged unless `--json` is present.

## ENVELOPE

Every JSON event uses a common envelope:

```json
{"schema":"newos.tool.v1","tool":"name","stream":"stdout","event":"record","seq":1,"data":{}}
```

The common fields are:

- `schema` - the envelope schema version, currently `newos.tool.v1`
- `tool` - the command name that emitted the event
- `stream` - `stdout` or `stderr`, matching the file descriptor used
- `event` - the event kind; each tool documents its own event names
- `seq` - a process-local event sequence number, starting at 1
- `data` - a tool-specific object for normal output events

Additional common fields may appear for specific event families. Consumers should ignore unknown fields so tools can add compatible metadata later.

## STREAMS

Normal command output is emitted on standard output with `"stream":"stdout"`.
Diagnostics, usage messages, warnings, and errors that would normally be written to standard error are emitted on standard error with `"stream":"stderr"`.

A consumer that needs the full event stream should read both pipes. The sequence number is process-local and shared by both streams, but operating systems do not guarantee perfect ordering between separate stdout and stderr pipes.

## DIAGNOSTICS

Shared diagnostics use this event shape:

```json
{"schema":"newos.tool.v1","tool":"tool","stream":"stderr","event":"diagnostic","seq":1,"level":"error","message":"cannot open ","detail":"file.txt"}
```

`level` is usually `error` or `warning`. `message` is a short stable diagnostic string, and `detail` contains the path, argument, or other context when one is available. When no detail is available, `detail` is `null`.

Shared usage messages use this event shape:

```json
{"schema":"newos.tool.v1","tool":"tool","stream":"stderr","event":"usage","seq":1,"data":{"program":"tool","usage_suffix":"[file ...]"}}
```

## STRINGS AND BINARY DATA

JSON strings are escaped according to JSON rules. The shared writer escapes quotation marks, backslashes, newline, carriage return, tab, and control bytes below `0x20`.

Tools that stream arbitrary bytes should not place unchecked binary data directly in JSON strings. They should emit chunk events with either UTF-8 text when the bytes are known to be text, or base64 when the data may be binary:

```json
{"schema":"newos.tool.v1","tool":"cat","stream":"stdout","event":"data","seq":1,"data":{"encoding":"base64","bytes":"AAECAw=="}}
```

## COLOR AND OTHER FORMATS

`--json` disables terminal color and styling, including automatic color. `--json` and `--csv` are mutually exclusive. A tool that supports CSV should reject the combination rather than guessing which machine-readable format the caller intended.

For options that only affect human formatting, JSON mode should either ignore the option or report a documented diagnostic. Tool-specific manual pages describe those choices.

## INTERACTIVE TOOLS

JSON mode is intended for non-interactive command execution. Fully interactive terminal applications may reject `--json` for their interactive mode. Tools that have both interactive and non-interactive modes, such as mail-style commands or batch process viewers, should support JSON for the non-interactive modes and document unsupported interactive modes.

## TOOL-SPECIFIC DATA

Each command that supports `--json` documents its event names and `data` fields in a `JSON Output` section of its manual page.

### rte-route

`rte-route --json` emits route progress as JSON Lines. Successful walking routes include `metadata`, `address`, `tile_context`, `graph_loaded`, `route_status`, `route`, `route_point`, and `route_step` events. When the route pack contains embedded GTFS data, the stream also includes one `transit_plan` event. With `--transit --json`, the stream skips walking graph events and emits only endpoint metadata/address events plus `transit_plan`. The human-output `--verbose` flag does not change JSON output. If the query does not include `--depart` or `--arrive`, the tool treats it as a departure at the current time.

A found transit plan includes the first vehicle leg in the legacy top-level fields and the complete itinerary in `legs`:

```json
{"schema":"newos.tool.v1","tool":"rte-route","stream":"stdout","event":"transit_plan","seq":273,"data":{"status":"found","mode":"subway","route_short_name":"U5","route_long_name":"","board_stop":"S+U Brandenburger Tor (Berlin)","alight_stop":"S+U Berlin Hauptbahnhof","board_departure_sec":32580,"alight_arrival_sec":32760,"walk_to_stop_m":273,"walk_from_stop_m":214,"total_sec":3900,"transit_leg_count":3,"active_trips":70778,"events_scanned":35730192,"board_lat":52.5169410,"board_lon":13.3803900,"alight_lat":52.5255890,"alight_lon":13.3695480,"legs":[{"kind":"ride","mode":"subway","route_short_name":"U5","route_long_name":"","board_stop":"S+U Brandenburger Tor (Berlin)","alight_stop":"S+U Berlin Hauptbahnhof","departure_sec":32580,"arrival_sec":32760},{"kind":"transfer_walk","from_stop":"S+U Berlin Hauptbahnhof","to_stop":"S+U Berlin Hauptbahnhof","walk_m":48,"departure_sec":32760,"arrival_sec":32796},{"kind":"ride","mode":"train","route_short_name":"S7","route_long_name":"","board_stop":"S+U Berlin Hauptbahnhof","alight_stop":"S Potsdam Hauptbahnhof","departure_sec":32820,"arrival_sec":34920}]}}
```

If no embedded GTFS plan is available, `status` is `unavailable` and the event still reports `active_trips` and `events_scanned` for diagnostics.

## SEE ALSO

output-style, project-layout, testing
