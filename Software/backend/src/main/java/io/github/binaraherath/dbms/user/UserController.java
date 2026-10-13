package io.github.binaraherath.dbms.user;

import java.net.URI;

import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.data.web.PageableDefault;
import org.springframework.http.ResponseEntity;
import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import io.github.binaraherath.dbms.common.PageResponse;
import io.github.binaraherath.dbms.user.UserService.CreateUserRequest;
import io.github.binaraherath.dbms.user.UserService.UpdateUserRequest;
import io.github.binaraherath.dbms.user.UserService.UserDto;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;

@RestController
@RequestMapping("/api/v1/users")
@PreAuthorize("hasRole('ADMIN')")
@Tag(name = "Users", description = "Account management (admin only)")
public class UserController {

    private final UserService users;

    UserController(UserService users) {
        this.users = users;
    }

    @GetMapping
    @Operation(summary = "List users (paginated)")
    public PageResponse<UserDto> list(@PageableDefault(size = 20, sort = "username", direction = Sort.Direction.ASC)
                                      Pageable pageable) {
        return users.list(pageable);
    }

    @PostMapping
    @Operation(summary = "Create a user (409 if the username exists)")
    public ResponseEntity<UserDto> create(@Valid @RequestBody CreateUserRequest request) {
        UserDto created = users.create(request);
        return ResponseEntity.created(URI.create("/api/v1/users/" + created.id())).body(created);
    }

    @PatchMapping("/{userId}")
    @Operation(summary = "Change name, role, password or disable a user. Users are disabled, never deleted.")
    public UserDto update(@PathVariable long userId, @Valid @RequestBody UpdateUserRequest request) {
        return users.update(userId, request);
    }
}
