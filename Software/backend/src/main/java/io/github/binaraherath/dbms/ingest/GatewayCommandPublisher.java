package io.github.binaraherath.dbms.ingest;

/** Sends a command JSON to a gateway. Implemented over MQTT; mocked in tests. */
public interface GatewayCommandPublisher {

    /** @throws GatewayUnavailableException if the broker connection is down */
    void publishCommand(String deviceId, String json);

    class GatewayUnavailableException extends RuntimeException {
        public GatewayUnavailableException(String message, Throwable cause) {
            super(message, cause);
        }
    }
}
