-- Removing a pack must remove its whole history. V1 cascaded pack -> module and
-- pack -> sample, but readings and alarms also reference the module without a
-- cascade, so PostgreSQL refused to delete any pack that had data.

ALTER TABLE cell_reading
    DROP CONSTRAINT cell_reading_module_id_fkey,
    ADD CONSTRAINT cell_reading_module_id_fkey
        FOREIGN KEY (module_id) REFERENCES bms_module (id) ON DELETE CASCADE;

ALTER TABLE temperature_reading
    DROP CONSTRAINT temperature_reading_module_id_fkey,
    ADD CONSTRAINT temperature_reading_module_id_fkey
        FOREIGN KEY (module_id) REFERENCES bms_module (id) ON DELETE CASCADE;

ALTER TABLE alarm
    DROP CONSTRAINT alarm_module_id_fkey,
    ADD CONSTRAINT alarm_module_id_fkey
        FOREIGN KEY (module_id) REFERENCES bms_module (id) ON DELETE CASCADE;
