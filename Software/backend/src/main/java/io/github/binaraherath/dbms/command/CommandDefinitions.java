package io.github.binaraherath.dbms.command;

import java.util.EnumMap;
import java.util.List;
import java.util.Map;

import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.common.ApiExceptions;

/**
 * One class per remote command. Each knows its own argument rules and how it looks
 * in the gateway JSON, so adding a command means adding a class, not editing a switch.
 */
public interface CommandDefinitions {

    interface CommandDefinition {

        CommandType type();

        /** @throws ApiExceptions.BadRequest if the argument is not allowed */
        void validate(Integer argument);

        /** Adds the command-specific fields to the JSON sent to the gateway. */
        void writePayload(Map<String, Object> json, Integer argument);
    }

    @Component
    class ClearFaults implements CommandDefinition {
        public CommandType type() { return CommandType.CLEAR_FAULTS; }

        public void validate(Integer argument) {
            noArgument(argument);
        }

        public void writePayload(Map<String, Object> json, Integer argument) {
            json.put("cmd", type().gatewayName());
        }
    }

    /** argument 1 = allow balancing, 0 = block it. */
    @Component
    class SetBalancing implements CommandDefinition {
        public CommandType type() { return CommandType.SET_BALANCING; }

        public void validate(Integer argument) {
            if (argument == null || (argument != 0 && argument != 1)) {
                throw new ApiExceptions.BadRequest("SET_BALANCING needs argument 0 (off) or 1 (on)");
            }
        }

        public void writePayload(Map<String, Object> json, Integer argument) {
            json.put("cmd", type().gatewayName());
            json.put("enable", argument == 1);
        }
    }

    /** argument 0..100 = fixed duty in %, null = back to automatic control. */
    @Component
    class FanOverride implements CommandDefinition {
        public CommandType type() { return CommandType.FAN_OVERRIDE; }

        public void validate(Integer argument) {
            if (argument != null && (argument < 0 || argument > 100)) {
                throw new ApiExceptions.BadRequest("FAN_OVERRIDE argument must be 0..100, or empty for automatic");
            }
        }

        public void writePayload(Map<String, Object> json, Integer argument) {
            json.put("cmd", type().gatewayName());
            json.put("duty", argument == null ? "auto" : argument);
        }
    }

    /** Opens the contactors. The firmware only re-arms after the key is turned off. */
    @Component
    class Stop implements CommandDefinition {
        public CommandType type() { return CommandType.STOP; }

        public void validate(Integer argument) {
            noArgument(argument);
        }

        public void writePayload(Map<String, Object> json, Integer argument) {
            json.put("cmd", type().gatewayName());
        }
    }

    /** Looks up the definition for a command type. */
    @Component
    class Catalog {
        private final Map<CommandType, CommandDefinition> byType = new EnumMap<>(CommandType.class);

        Catalog(List<CommandDefinition> definitions) {
            definitions.forEach(d -> byType.put(d.type(), d));
        }

        public CommandDefinition get(CommandType type) {
            CommandDefinition d = byType.get(type);
            if (d == null) {
                throw new ApiExceptions.BadRequest("Unsupported command " + type);
            }
            return d;
        }
    }

    private static void noArgument(Integer argument) {
        if (argument != null) {
            throw new ApiExceptions.BadRequest("This command takes no argument");
        }
    }
}
