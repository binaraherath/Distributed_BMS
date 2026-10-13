package io.github.binaraherath.dbms.user;

import java.time.Clock;
import java.time.Instant;

import org.springframework.data.domain.Pageable;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.common.PageResponse;
import io.github.binaraherath.dbms.user.AppUser.Role;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;
import jakarta.validation.constraints.Pattern;
import jakarta.validation.constraints.Size;

@Service
public class UserService {

    public record UserDto(long id, String username, String displayName, Role role, boolean enabled,
                          Instant createdAt) {

        static UserDto of(AppUser u) {
            return new UserDto(u.getId(), u.getUsername(), u.getDisplayName(), u.getRole(), u.isEnabled(),
                    u.getCreatedAt());
        }
    }

    public record CreateUserRequest(
            @NotBlank @Size(min = 3, max = 50)
            @Pattern(regexp = "[a-z0-9._-]+", message = "use lower-case letters, digits, '.', '_' or '-'")
            String username,
            @NotBlank @Size(max = 100) String displayName,
            @NotBlank @Size(min = 8, max = 72) String password,
            @NotNull Role role) {
    }

    /** PATCH body: a null field means "leave unchanged". */
    public record UpdateUserRequest(
            @Size(min = 1, max = 100) String displayName,
            Role role,
            Boolean enabled,
            @Size(min = 8, max = 72) String password) {
    }

    private final AppUserRepository users;
    private final PasswordEncoder encoder;
    private final Clock clock;

    UserService(AppUserRepository users, PasswordEncoder encoder, Clock clock) {
        this.users = users;
        this.encoder = encoder;
        this.clock = clock;
    }

    @Transactional(readOnly = true)
    public PageResponse<UserDto> list(Pageable pageable) {
        return PageResponse.of(users.findAll(pageable), UserDto::of);
    }

    @Transactional(readOnly = true)
    public AppUser findByUsername(String username) {
        return users.findByUsername(username)
                .orElseThrow(() -> new ApiExceptions.NotFound("User " + username + " not found"));
    }

    @Transactional
    public UserDto create(CreateUserRequest req) {
        if (users.existsByUsername(req.username())) {
            throw new ApiExceptions.Conflict("Username '" + req.username() + "' is already taken");
        }
        AppUser saved = users.save(new AppUser(req.username(), req.displayName().trim(),
                encoder.encode(req.password()), req.role(), clock.instant()));
        return UserDto.of(saved);
    }

    @Transactional
    public UserDto update(long id, UpdateUserRequest req) {
        AppUser user = users.findById(id).orElseThrow(() -> new ApiExceptions.NotFound("User " + id + " not found"));
        boolean losesAdmin = user.getRole() == Role.ADMIN && user.isEnabled()
                && ((req.role() != null && req.role() != Role.ADMIN) || Boolean.FALSE.equals(req.enabled()));
        if (losesAdmin && users.countByRoleAndEnabledTrue(Role.ADMIN) <= 1) {
            throw new ApiExceptions.Conflict("At least one enabled admin must remain");
        }
        if (req.displayName() != null) {
            user.setDisplayName(req.displayName().trim());
        }
        if (req.role() != null) {
            user.setRole(req.role());
        }
        if (req.enabled() != null) {
            user.setEnabled(req.enabled());
        }
        if (req.password() != null) {
            user.setPasswordHash(encoder.encode(req.password()));
        }
        return UserDto.of(user);
    }

    /** Creates an account at start-up if it does not exist yet (used by the seeder). */
    @Transactional
    public void ensureUser(String username, String displayName, String rawPassword, Role role) {
        if (username == null || username.isBlank() || users.existsByUsername(username)) {
            return;
        }
        users.save(new AppUser(username, displayName, encoder.encode(rawPassword), role, clock.instant()));
    }
}
