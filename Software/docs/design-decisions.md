# Design decisions

Short records of the choices that shaped the software, with the alternative that was
considered and why it lost.

### 1. A modular monolith, not microservices
One Spring Boot service, split into feature packages with clear boundaries (they talk
through services and Spring events, not each other's repositories). One gateway at
1 message per second does not need independent scaling, and one deployable is far
easier to run on a laptop at an exhibition. The event boundaries mean a feature such
as alarms could be moved out later if it ever needed to be.

### 2. Spring Boot (Java) rather than Node.js
Strong typing end to end, mature transaction handling for the sample-plus-readings
write, Spring Security for roles, and first-class Testcontainers support. Node would
have been quicker to start, but the transactional and security parts would have needed
more hand-written code.

### 3. PostgreSQL rather than a time-series database (InfluxDB, TimescaleDB)
The data is relational as much as it is time series: users, packs, modules, alarms
and commands reference each other. At one sample every 10 s per pack, plain
PostgreSQL with a `(pack_id, recorded_at)` index and `date_bin` averaging is more than
fast enough, and one database is simpler to back up and explain. TimescaleDB would be
the next step if many packs were connected.

### 4. 1 Hz live, 10 s stored
The dashboard shows every message from memory (no database round trip), while the
database keeps one sample per 10 s bucket. Storing 1 Hz would multiply the reading
rows by ten with no extra value for history charts. Buckets use the gateway's own
timestamp, so data replayed after a Wi-Fi outage still lands in the right place.

### 5. Server-Sent Events rather than WebSocket
Live data flows one way, server to browser. SSE works over plain HTTP, reconnects by
itself, passes through nginx with buffering turned off, and is authenticated by the
same session cookie. Commands go the other way through ordinary REST calls, where they
get validation, status codes and an audit record.

### 6. Session in an httpOnly cookie, not a token in localStorage
The JWT is signed by the backend and sent as an `httpOnly`, `SameSite=Strict` cookie,
so page scripts cannot read it and other sites cannot send it. That is also what lets
the browser's `EventSource` authenticate. CSRF tokens are not added: `SameSite=Strict`
plus JSON-only endpoints already block cross-site requests. The user is re-read from
the database on every request, so disabling an account takes effect immediately.

### 7. Alarms from telemetry fault bits, not the gateway's event topic
The gateway also publishes fault-change events, but those are not buffered while
Wi-Fi is down. The fault bits inside every telemetry message are, so comparing them
with the open alarms cannot miss a fault. Alarms are raised only from live data, never
from simulated or replayed messages.

### 8. Commands are asynchronous: `202 Accepted`
The server cannot know the result when the HTTP request ends: the command still has
to travel over MQTT and CAN. The API answers `202` with a `Location` to poll. The
command's id travels to the gateway and back in its acknowledgement, and a scheduled
job marks commands with no answer after 10 s as `TIMED_OUT`.

### 9. No safety settings from the web
Cell voltage and temperature limits, current limits and contactor sequencing stay in
the node firmware. The web can only ask for actions the controller already guards
(stop, fan, balancing permission, clear faults), and starting the pack still needs the
physical key switch. A compromised dashboard therefore cannot make the pack unsafe.

### 10. Simulated data is labelled, never hidden
When the gateway fills in for a silent node, every message says so (`"source":
"simulated"`), the database stores it, and the UI marks it with a hatched "Simulated"
tag on the node, the panel and the history. The alternative of presenting modelled
values as measurements was rejected.

### 11. A custom design system rather than a component library
The dashboard uses CSS variables, CSS Modules and unstyled Radix primitives
(accessible dialogs, menus, switches) instead of Material UI. It is lighter, it does
not look like every other admin template, and both themes are designed separately
rather than inverted.
