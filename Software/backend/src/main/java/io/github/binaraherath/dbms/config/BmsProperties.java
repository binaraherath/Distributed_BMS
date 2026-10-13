package io.github.binaraherath.dbms.config;

import java.time.Duration;

import org.springframework.boot.context.properties.ConfigurationProperties;

/** Every application setting under {@code bms.*} in application.yml, in one typed place. */
@ConfigurationProperties("bms")
public record BmsProperties(Mqtt mqtt, Storage storage, Live live, Commands commands, Security security) {

    public record Mqtt(boolean enabled, String url, String username, String password,
                       String clientId, String topicRoot, String commandToken) {
    }

    /** Live data arrives at 1 Hz; only one sample per interval is written to the database. */
    public record Storage(Duration sampleInterval, int retentionDays) {
    }

    public record Live(Duration nodeStaleAfter, Duration gatewayStaleAfter) {
    }

    public record Commands(Duration ackTimeout) {
    }

    public record Security(String jwtSecret, Duration tokenTtl, String cookieName,
                           boolean cookieSecure, Seed seed) {

        public record Seed(String adminUsername, String adminPassword,
                           String viewerUsername, String viewerPassword) {
        }
    }
}
