-- Distributed BMS - initial schema.
-- Electrical values are stored as integers in the firmware's own units
-- (mV, mA, 0.1 degC) so nothing is lost to floating-point rounding.
-- All timestamps are UTC (timestamptz); the UI converts to Asia/Colombo.

-- ---------------------------------------------------------------- users
CREATE TABLE app_user (
    id             BIGSERIAL    PRIMARY KEY,
    username       VARCHAR(50)  NOT NULL UNIQUE,
    display_name   VARCHAR(100) NOT NULL,
    password_hash  VARCHAR(100) NOT NULL,
    role           VARCHAR(10)  NOT NULL CHECK (role IN ('ADMIN', 'VIEWER')),
    enabled        BOOLEAN      NOT NULL DEFAULT TRUE,
    created_at     TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------- packs
-- One row per gateway (device_id comes from the MQTT topic).
CREATE TABLE battery_pack (
    id                 BIGSERIAL    PRIMARY KEY,
    device_id          VARCHAR(64)  NOT NULL UNIQUE,
    name               VARCHAR(100) NOT NULL,
    location           VARCHAR(120),
    chemistry          VARCHAR(40)  NOT NULL DEFAULT 'Li-ion (LMO/NMC)',
    series_cells       SMALLINT     NOT NULL CHECK (series_cells BETWEEN 1 AND 64),
    rated_capacity_ah  NUMERIC(6,1) CHECK (rated_capacity_ah > 0),
    gateway_online     BOOLEAN      NOT NULL DEFAULT FALSE,
    gateway_status_at  TIMESTAMPTZ,
    last_seen_at       TIMESTAMPTZ,
    created_at         TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- A master monitoring node (BQ76952 + STM32) and the 16 cells it watches.
CREATE TABLE bms_module (
    id             BIGSERIAL PRIMARY KEY,
    pack_id        BIGINT    NOT NULL REFERENCES battery_pack (id) ON DELETE CASCADE,
    node_id        SMALLINT  NOT NULL CHECK (node_id BETWEEN 1 AND 4),
    first_cell_no  SMALLINT  NOT NULL CHECK (first_cell_no >= 1),
    cell_count     SMALLINT  NOT NULL CHECK (cell_count BETWEEN 1 AND 16),
    UNIQUE (pack_id, node_id)
);

-- ---------------------------------------------------------------- telemetry
-- Pack-level snapshot, stored every few seconds (live view is 1 Hz from memory).
-- cell_min/max are the BMS's own reported values, kept here so history charts
-- do not have to scan cell_reading.
CREATE TABLE telemetry_sample (
    id               BIGSERIAL    PRIMARY KEY,
    pack_id          BIGINT       NOT NULL REFERENCES battery_pack (id) ON DELETE CASCADE,
    recorded_at      TIMESTAMPTZ  NOT NULL,
    received_at      TIMESTAMPTZ  NOT NULL,
    pack_voltage_mv  INTEGER      NOT NULL CHECK (pack_voltage_mv >= 0),
    current_ma       INTEGER      NOT NULL,
    soc_pct          NUMERIC(4,1) NOT NULL CHECK (soc_pct BETWEEN 0 AND 100),
    soh_pct          NUMERIC(4,1) CHECK (soh_pct BETWEEN 0 AND 100),
    cell_min_mv      SMALLINT     NOT NULL,
    cell_max_mv      SMALLINT     NOT NULL,
    temp_max_dc      SMALLINT     NOT NULL,
    bms_state        VARCHAR(12)  NOT NULL,
    bms_fault_bits   INTEGER      NOT NULL DEFAULT 0,
    bms_source       VARCHAR(10)  NOT NULL CHECK (bms_source IN ('LIVE', 'SIMULATED')),
    ctrl_state       VARCHAR(12),
    ctrl_fault_bits  SMALLINT     NOT NULL DEFAULT 0,
    contactor_bits   SMALLINT     NOT NULL DEFAULT 0,
    fan_duty_pct     SMALLINT     CHECK (fan_duty_pct BETWEEN 0 AND 100),
    ctrl_source      VARCHAR(10)  NOT NULL CHECK (ctrl_source IN ('LIVE', 'SIMULATED', 'OFFLINE')),
    CHECK (cell_max_mv >= cell_min_mv),
    -- the gateway replays buffered data after a Wi-Fi outage: a repeated
    -- (pack, time) pair is a duplicate and is ignored on insert
    UNIQUE (pack_id, recorded_at)
);

CREATE TABLE cell_reading (
    sample_id   BIGINT   NOT NULL REFERENCES telemetry_sample (id) ON DELETE CASCADE,
    module_id   BIGINT   NOT NULL REFERENCES bms_module (id),
    cell_no     SMALLINT NOT NULL CHECK (cell_no BETWEEN 1 AND 64),     -- pack-wide number
    voltage_mv  SMALLINT NOT NULL CHECK (voltage_mv BETWEEN 0 AND 5000),
    balancing   BOOLEAN  NOT NULL DEFAULT FALSE,
    PRIMARY KEY (sample_id, cell_no)
);

CREATE TABLE temperature_reading (
    sample_id  BIGINT   NOT NULL REFERENCES telemetry_sample (id) ON DELETE CASCADE,
    module_id  BIGINT   NOT NULL REFERENCES bms_module (id),
    sensor_no  SMALLINT NOT NULL CHECK (sensor_no BETWEEN 1 AND 4),
    temp_dc    SMALLINT NOT NULL CHECK (temp_dc BETWEEN -400 AND 1500),
    PRIMARY KEY (sample_id, module_id, sensor_no)
);

-- ---------------------------------------------------------------- alarms
-- Lookup of every fault bit the firmware can report (seeded in V2).
CREATE TABLE fault_code (
    id           SMALLSERIAL  PRIMARY KEY,
    source       VARCHAR(4)   NOT NULL CHECK (source IN ('BMS', 'CTRL')),
    bit          SMALLINT     NOT NULL CHECK (bit BETWEEN 0 AND 23),
    code         VARCHAR(40)  NOT NULL,
    severity     VARCHAR(8)   NOT NULL CHECK (severity IN ('CRITICAL', 'WARNING')),
    description  VARCHAR(200) NOT NULL,
    UNIQUE (source, bit)
);

-- One row per occurrence of a fault: raised -> cleared -> acknowledged.
CREATE TABLE alarm (
    id               BIGSERIAL   PRIMARY KEY,
    pack_id          BIGINT      NOT NULL REFERENCES battery_pack (id) ON DELETE CASCADE,
    module_id        BIGINT      REFERENCES bms_module (id),
    fault_code_id    SMALLINT    NOT NULL REFERENCES fault_code (id),
    raised_at        TIMESTAMPTZ NOT NULL,
    cleared_at       TIMESTAMPTZ,
    acknowledged_at  TIMESTAMPTZ,
    acknowledged_by  BIGINT      REFERENCES app_user (id),
    CHECK (cleared_at IS NULL OR cleared_at >= raised_at),
    CHECK ((acknowledged_at IS NULL) = (acknowledged_by IS NULL))
);

-- ---------------------------------------------------------------- commands
CREATE TABLE remote_command (
    id              BIGSERIAL    PRIMARY KEY,
    pack_id         BIGINT       NOT NULL REFERENCES battery_pack (id) ON DELETE CASCADE,
    command_type    VARCHAR(20)  NOT NULL
                    CHECK (command_type IN ('CLEAR_FAULTS', 'SET_BALANCING', 'FAN_OVERRIDE', 'STOP')),
    argument        SMALLINT,
    status          VARCHAR(12)  NOT NULL
                    CHECK (status IN ('PENDING', 'PUBLISHED', 'ACCEPTED', 'REJECTED', 'TIMED_OUT', 'FAILED')),
    correlation_id  UUID         NOT NULL UNIQUE,
    issued_by       BIGINT       NOT NULL REFERENCES app_user (id),
    issued_at       TIMESTAMPTZ  NOT NULL,
    completed_at    TIMESTAMPTZ,
    result_message  VARCHAR(200)
);

-- ---------------------------------------------------------------- indexes
-- Only for queries the application actually runs (see docs/database.md).
-- telemetry_sample (pack_id, recorded_at) is already covered by its UNIQUE
-- constraint: it serves "latest sample", "history in a time range" and CSV export.

-- active alarms of a pack, and only one open alarm per fault per module
CREATE UNIQUE INDEX ux_alarm_open
    ON alarm (pack_id, fault_code_id, COALESCE(module_id, 0))
    WHERE cleared_at IS NULL;
-- paginated alarm history, newest first
CREATE INDEX ix_alarm_pack_raised ON alarm (pack_id, raised_at DESC);

-- command log, newest first
CREATE INDEX ix_command_pack_issued ON remote_command (pack_id, issued_at DESC);
-- timeout job only scans commands still waiting for an answer
CREATE INDEX ix_command_waiting ON remote_command (issued_at)
    WHERE status IN ('PENDING', 'PUBLISHED');

-- per-cell history joins cell_reading to samples by (sample_id, cell_no): the PK covers it.
