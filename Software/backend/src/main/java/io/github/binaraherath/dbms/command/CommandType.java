package io.github.binaraherath.dbms.command;

/**
 * Commands the gateway firmware understands (Node3 net_link.cpp). Starting the pack
 * (closing contactors) is deliberately missing: that needs the physical key switch.
 */
public enum CommandType {
    CLEAR_FAULTS("clear_faults"),
    SET_BALANCING("balancing"),
    FAN_OVERRIDE("fan"),
    STOP("stop");

    private final String gatewayName;

    CommandType(String gatewayName) {
        this.gatewayName = gatewayName;
    }

    /** The "cmd" value in the gateway's JSON. */
    public String gatewayName() {
        return gatewayName;
    }

    public static CommandType fromGatewayName(String name) {
        for (CommandType t : values()) {
            if (t.gatewayName.equals(name)) {
                return t;
            }
        }
        return null;
    }
}
