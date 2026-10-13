# Database

PostgreSQL 16, schema owned by Flyway (`backend/src/main/resources/db/migration`).
Hibernate only validates the schema at start-up (`ddl-auto: validate`); it never changes it.

## ER diagram

```mermaid
erDiagram
    battery_pack ||--o{ bms_module : "has"
    battery_pack ||--o{ telemetry_sample : "records"
    telemetry_sample ||--|{ cell_reading : "contains"
    telemetry_sample ||--|{ temperature_reading : "contains"
    bms_module ||--o{ cell_reading : "measures"
    bms_module ||--o{ temperature_reading : "measures"
    battery_pack ||--o{ alarm : "raises"
    fault_code ||--o{ alarm : "classifies"
    bms_module |o--o{ alarm : "source of"
    app_user |o--o{ alarm : "acknowledges"
    battery_pack ||--o{ remote_command : "receives"
    app_user ||--o{ remote_command : "issues"

    app_user {
        bigint id PK
        varchar username UK
        varchar display_name
        varchar password_hash "BCrypt"
        varchar role "ADMIN | VIEWER"
        boolean enabled
    }
    battery_pack {
        bigint id PK
        varchar device_id UK "from the MQTT topic"
        varchar name
        smallint series_cells
        numeric rated_capacity_ah
        boolean gateway_online
        timestamptz last_seen_at
    }
    bms_module {
        bigint id PK
        bigint pack_id FK
        smallint node_id "UK with pack_id"
        smallint first_cell_no
        smallint cell_count
    }
    telemetry_sample {
        bigint id PK
        bigint pack_id FK
        timestamptz recorded_at "UK with pack_id"
        int pack_voltage_mv
        int current_ma
        numeric soc_pct
        smallint cell_min_mv
        smallint cell_max_mv
        varchar bms_source "LIVE | SIMULATED"
        varchar ctrl_source "LIVE | SIMULATED | OFFLINE"
    }
    cell_reading {
        bigint sample_id PK, FK
        smallint cell_no PK
        bigint module_id FK
        smallint voltage_mv
        boolean balancing
    }
    temperature_reading {
        bigint sample_id PK, FK
        bigint module_id PK, FK
        smallint sensor_no PK
        smallint temp_dc "0.1 degC"
    }
    fault_code {
        smallint id PK
        varchar source "BMS | CTRL, UK with bit"
        smallint bit
        varchar code
        varchar severity
    }
    alarm {
        bigint id PK
        bigint pack_id FK
        bigint module_id FK "null for controller faults"
        smallint fault_code_id FK
        timestamptz raised_at
        timestamptz cleared_at
        timestamptz acknowledged_at
        bigint acknowledged_by FK
    }
    remote_command {
        bigint id PK
        bigint pack_id FK
        varchar command_type
        smallint argument
        varchar status
        uuid correlation_id UK
        bigint issued_by FK
        timestamptz issued_at
    }
```

## Why it is shaped like this

- **A pack has modules, a module has cells.** Today there is one master node with 16
  cells, but `bms_module` (node id, first cell number, cell count) lets a larger pack
  with several masters fit without a schema change. Cell numbers are pack-wide.
- **Readings are rows, not array columns.** `cell_reading` and `temperature_reading`
  give "history of cell 7" as a simple indexed query and keep every value checkable.
- **`fault_code` is a lookup table** seeded from the firmware's bit definitions
  (`V2__seed_fault_codes.sql`), so an alarm stores only a reference, and names,
  severities and descriptions live in one place.
- **Integers in firmware units** (mV, mA, 0.1 °C): exact, compact, and identical to
  what crossed the CAN bus.
- **Provenance is stored.** `bms_source` / `ctrl_source` record whether a sample was
  measured or filled in by the gateway, so history can never pass simulated data off
  as real.

## Constraints that protect the data

| Constraint | Protects against |
|---|---|
| `UNIQUE (pack_id, recorded_at)` on `telemetry_sample` | The gateway replays buffered messages after a Wi-Fi outage; `INSERT … ON CONFLICT DO NOTHING` turns a repeat into a no-op |
| `CHECK` ranges on voltages, temperatures, SoC, fan duty, node and cell numbers | Corrupt or mis-decoded values reaching the history |
| `CHECK (cell_max_mv >= cell_min_mv)`, `CHECK (cleared_at >= raised_at)` | Impossible combinations |
| `CHECK ((acknowledged_at IS NULL) = (acknowledged_by IS NULL))` | An acknowledgement without a person, or the other way round |
| Partial unique index `ux_alarm_open` (one open alarm per fault per module) | Duplicate alarms if two messages race |
| `ON DELETE CASCADE` from `battery_pack` | Orphan rows when a pack is removed |

## The transaction

Saving a sample is the one operation that must be all or nothing: the
`telemetry_sample` row, its 16 `cell_reading` rows and its 4 `temperature_reading` rows
are written in a single transaction (`TelemetryStore.save`). If any reading is rejected
by a constraint, the sample row is rolled back too, so a chart never shows a sample
with missing cells. An integration test forces this by inserting an out-of-range cell
voltage and checks that no sample remains.

Command handling is the other place where order matters: the command row is committed
first and published to MQTT only **after** the commit
(`@TransactionalEventListener(AFTER_COMMIT)`), so a command can never reach the pack
without being logged.

## Indexes

Only for queries the application runs:

| Index | Query |
|---|---|
| `telemetry_sample (pack_id, recorded_at)` – from the unique constraint | latest sample, history in a time range, CSV export |
| `cell_reading` primary key `(sample_id, cell_no)` | per-cell history |
| `ux_alarm_open` partial, `WHERE cleared_at IS NULL` | active alarms of a pack (also enforces uniqueness) |
| `ix_alarm_pack_raised (pack_id, raised_at DESC)` | paginated alarm list, newest first |
| `ix_command_pack_issued (pack_id, issued_at DESC)` | command log |
| `ix_command_waiting (issued_at) WHERE status IN ('PENDING','PUBLISHED')` | the timeout job, which only looks at commands still waiting |

## Size and retention

One sample every 10 s is 8 640 samples and about 173 000 reading rows (16 cells + 4 sensors each) per pack per
day. A nightly job deletes samples older than 90 days (`bms.storage.retention-days`);
readings go with them through `ON DELETE CASCADE`. History charts never load raw rows
for long ranges: the query averages into 1-minute or 15-minute buckets with
PostgreSQL's `date_bin`, so a 30-day chart is still about 3 000 points.
