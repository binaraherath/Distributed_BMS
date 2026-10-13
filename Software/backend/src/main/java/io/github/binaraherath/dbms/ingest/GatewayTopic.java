package io.github.binaraherath.dbms.ingest;

/**
 * Parsed MQTT topic. The gateway publishes on {@code <root>/<deviceId>/<kind>}:
 * telemetry, status, event, cmd/ack.
 */
public record GatewayTopic(String deviceId, Kind kind) {

    public enum Kind { TELEMETRY, STATUS, COMMAND_ACK, EVENT, OTHER }

    public static GatewayTopic parse(String root, String topic) {
        String prefix = root + "/";
        if (topic == null || !topic.startsWith(prefix)) {
            return new GatewayTopic("", Kind.OTHER);
        }
        String rest = topic.substring(prefix.length());
        int slash = rest.indexOf('/');
        if (slash <= 0) {
            return new GatewayTopic("", Kind.OTHER);
        }
        String device = rest.substring(0, slash);
        Kind kind = switch (rest.substring(slash + 1)) {
            case "telemetry" -> Kind.TELEMETRY;
            case "status" -> Kind.STATUS;
            case "cmd/ack" -> Kind.COMMAND_ACK;
            case "event" -> Kind.EVENT;
            default -> Kind.OTHER;
        };
        return new GatewayTopic(device, kind);
    }
}
