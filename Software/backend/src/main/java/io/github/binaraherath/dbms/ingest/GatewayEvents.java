package io.github.binaraherath.dbms.ingest;

import java.time.Instant;

/**
 * Events published when something arrives from a gateway. Listeners (observers)
 * react independently: the ingest code does not know who is interested.
 */
public final class GatewayEvents {

    private GatewayEvents() {
    }

    /** A telemetry snapshot was received and resolved to a pack. */
    public record TelemetryReceived(long packId, PackSnapshot snapshot) {
    }

    /** The gateway's retained status topic changed ("online" / last-will "offline"). */
    public record GatewayStatusChanged(long packId, boolean online, Instant at) {
    }

    /**
     * The gateway answered a remote command on {@code <root>/<device>/cmd/ack}.
     *
     * @param correlationId the command id echoed back, or null for firmware that does not echo it
     */
    public record CommandAcknowledged(long packId, String command, boolean accepted,
                                      String reason, String correlationId, Instant at) {
    }
}
