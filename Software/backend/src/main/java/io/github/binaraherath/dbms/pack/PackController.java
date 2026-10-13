package io.github.binaraherath.dbms.pack;

import java.util.List;

import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import io.github.binaraherath.dbms.pack.PackService.PackDto;
import io.github.binaraherath.dbms.pack.PackService.PackUpdateRequest;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;

@RestController
@RequestMapping("/api/v1/packs")
@Tag(name = "Packs", description = "Battery packs (one per IoT gateway)")
public class PackController {

    private final PackService packs;

    PackController(PackService packs) {
        this.packs = packs;
    }

    @GetMapping
    @Operation(summary = "List all packs with their master nodes")
    public List<PackDto> list() {
        return packs.list();
    }

    @GetMapping("/{packId}")
    @Operation(summary = "Get one pack")
    public PackDto get(@PathVariable long packId) {
        return packs.get(packId);
    }

    @PatchMapping("/{packId}")
    @PreAuthorize("hasRole('ADMIN')")
    @Operation(summary = "Update pack details (admin)")
    public PackDto update(@PathVariable long packId, @Valid @RequestBody PackUpdateRequest request) {
        return packs.update(packId, request);
    }
}
