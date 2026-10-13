package io.github.binaraherath.dbms;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;
import org.springframework.scheduling.annotation.EnableScheduling;

/**
 * Backend of the Distributed BMS.
 * <p>
 * Listens to the IoT gateway over MQTT, keeps the latest snapshot of every pack in
 * memory for the live dashboard, stores sampled history in PostgreSQL, derives
 * alarms from the fault bits and relays remote commands back to the gateway.
 */
@SpringBootApplication
@EnableScheduling
@ConfigurationPropertiesScan
public class DbmsBackendApplication {

    public static void main(String[] args) {
        SpringApplication.run(DbmsBackendApplication.class, args);
    }
}
