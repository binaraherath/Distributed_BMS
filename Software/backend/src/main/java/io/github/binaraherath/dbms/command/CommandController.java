package io.github.binaraherath.dbms.command;

import java.net.URI;

import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.data.web.PageableDefault;
import org.springframework.http.ResponseEntity;
import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;

import io.github.binaraherath.dbms.command.CommandService.CommandDto;
import io.github.binaraherath.dbms.command.CommandService.IssueCommandRequest;
import io.github.binaraherath.dbms.common.PageResponse;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;

@RestController
@Tag(name = "Commands", description = "Remote commands relayed to the pack through the gateway")
public class CommandController {

    private final CommandService commands;

    CommandController(CommandService commands) {
        this.commands = commands;
    }

    /**
     * 202 Accepted: the command is delivered asynchronously over MQTT; poll the
     * Location URL (or the command list) for ACCEPTED / REJECTED / TIMED_OUT.
     */
    @PostMapping("/api/v1/packs/{packId}/commands")
    @PreAuthorize("hasRole('ADMIN')")
    @Operation(summary = "Send a command (admin). 409 if the gateway is offline")
    public ResponseEntity<CommandDto> issue(@PathVariable long packId,
                                            @Valid @RequestBody IssueCommandRequest request,
                                            Authentication auth) {
        CommandDto created = commands.issue(packId, request, auth.getName());
        return ResponseEntity.accepted().location(URI.create("/api/v1/commands/" + created.id())).body(created);
    }

    @GetMapping("/api/v1/packs/{packId}/commands")
    @Operation(summary = "Command history of a pack, newest first")
    public PageResponse<CommandDto> list(@PathVariable long packId,
                                         @PageableDefault(size = 20, sort = "issuedAt", direction = Sort.Direction.DESC)
                                         Pageable pageable) {
        return commands.list(packId, pageable);
    }

    @GetMapping("/api/v1/commands/{commandId}")
    @Operation(summary = "One command and its current status")
    public CommandDto get(@PathVariable long commandId) {
        return commands.get(commandId);
    }
}
