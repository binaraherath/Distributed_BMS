package io.github.binaraherath.dbms.command;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatNoException;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.junit.jupiter.api.Test;

import io.github.binaraherath.dbms.command.CommandDefinitions.Catalog;
import io.github.binaraherath.dbms.common.ApiExceptions;

class CommandDefinitionsTest {

    private final Catalog catalog = new Catalog(List.of(
            new CommandDefinitions.ClearFaults(), new CommandDefinitions.SetBalancing(),
            new CommandDefinitions.FanOverride(), new CommandDefinitions.Stop()));

    private Map<String, Object> payload(CommandType type, Integer arg) {
        Map<String, Object> json = new LinkedHashMap<>();
        catalog.get(type).writePayload(json, arg);
        return json;
    }

    @Test
    void writesTheJsonTheGatewayFirmwareExpects() {
        assertThat(payload(CommandType.CLEAR_FAULTS, null)).containsExactly(Map.entry("cmd", "clear_faults"));
        assertThat(payload(CommandType.STOP, null)).containsExactly(Map.entry("cmd", "stop"));
        assertThat(payload(CommandType.SET_BALANCING, 0)).containsEntry("cmd", "balancing").containsEntry("enable", false);
        assertThat(payload(CommandType.FAN_OVERRIDE, 60)).containsEntry("cmd", "fan").containsEntry("duty", 60);
        assertThat(payload(CommandType.FAN_OVERRIDE, null)).containsEntry("duty", "auto");
    }

    @Test
    void acceptsOnlyTheArgumentsTheFirmwareAccepts() {
        assertThatNoException().isThrownBy(() -> catalog.get(CommandType.FAN_OVERRIDE).validate(100));
        assertThatNoException().isThrownBy(() -> catalog.get(CommandType.FAN_OVERRIDE).validate(null));
        assertThatThrownBy(() -> catalog.get(CommandType.FAN_OVERRIDE).validate(101))
                .isInstanceOf(ApiExceptions.BadRequest.class);
        assertThatThrownBy(() -> catalog.get(CommandType.SET_BALANCING).validate(null))
                .isInstanceOf(ApiExceptions.BadRequest.class);
        assertThatThrownBy(() -> catalog.get(CommandType.SET_BALANCING).validate(2))
                .isInstanceOf(ApiExceptions.BadRequest.class);
        assertThatThrownBy(() -> catalog.get(CommandType.STOP).validate(1))
                .isInstanceOf(ApiExceptions.BadRequest.class);
    }

    @Test
    void mapsAckNamesBackToCommandTypes() {
        for (CommandType t : CommandType.values()) {
            assertThat(CommandType.fromGatewayName(t.gatewayName())).isEqualTo(t);
        }
        assertThat(CommandType.fromGatewayName("reboot")).isNull();
    }
}
