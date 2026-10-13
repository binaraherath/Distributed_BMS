package io.github.binaraherath.dbms.auth;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;

import org.springframework.http.HttpHeaders;
import org.springframework.http.ResponseCookie;
import org.springframework.http.ResponseEntity;
import org.springframework.security.authentication.AuthenticationManager;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.Authentication;
import org.springframework.security.oauth2.jose.jws.MacAlgorithm;
import org.springframework.security.oauth2.jwt.JwsHeader;
import org.springframework.security.oauth2.jwt.JwtClaimsSet;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.JwtEncoderParameters;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.user.AppUser;
import io.github.binaraherath.dbms.user.UserService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import jakarta.validation.constraints.NotBlank;

@RestController
@RequestMapping("/api/v1/auth")
@Tag(name = "Authentication")
public class AuthController {

    public record LoginRequest(@NotBlank String username, @NotBlank String password) {
    }

    public record MeDto(String username, String displayName, AppUser.Role role) {

        static MeDto of(AppUser u) {
            return new MeDto(u.getUsername(), u.getDisplayName(), u.getRole());
        }
    }

    private final AuthenticationManager authManager;
    private final UserService users;
    private final JwtEncoder jwtEncoder;
    private final Clock clock;
    private final BmsProperties.Security cfg;

    AuthController(AuthenticationManager authManager, UserService users, JwtEncoder jwtEncoder,
                   Clock clock, BmsProperties props) {
        this.authManager = authManager;
        this.users = users;
        this.jwtEncoder = jwtEncoder;
        this.clock = clock;
        this.cfg = props.security();
    }

    @PostMapping("/login")
    @Operation(summary = "Log in; sets the httpOnly session cookie (401 on wrong credentials)")
    public ResponseEntity<MeDto> login(@Valid @RequestBody LoginRequest req) {
        authManager.authenticate(UsernamePasswordAuthenticationToken.unauthenticated(req.username(), req.password()));
        AppUser user = users.findByUsername(req.username());
        return ResponseEntity.ok()
                .header(HttpHeaders.SET_COOKIE, sessionCookie(issueToken(user), cfg.tokenTtl()).toString())
                .body(MeDto.of(user));
    }

    @PostMapping("/logout")
    @Operation(summary = "Log out; clears the session cookie")
    public ResponseEntity<Void> logout() {
        return ResponseEntity.noContent()
                .header(HttpHeaders.SET_COOKIE, sessionCookie("", Duration.ZERO).toString())
                .build();
    }

    @GetMapping("/me")
    @Operation(summary = "The logged-in user")
    public MeDto me(Authentication auth) {
        return MeDto.of(users.findByUsername(auth.getName()));
    }

    private String issueToken(AppUser user) {
        Instant now = clock.instant();
        JwtClaimsSet claims = JwtClaimsSet.builder()
                .issuer("dbms-backend")
                .subject(user.getUsername())
                .issuedAt(now)
                .expiresAt(now.plus(cfg.tokenTtl()))
                .claim("role", user.getRole().name())
                .build();
        JwsHeader header = JwsHeader.with(MacAlgorithm.HS256).build();
        return jwtEncoder.encode(JwtEncoderParameters.from(header, claims)).getTokenValue();
    }

    private ResponseCookie sessionCookie(String value, Duration maxAge) {
        return ResponseCookie.from(cfg.cookieName(), value)
                .httpOnly(true)
                .secure(cfg.cookieSecure())
                .sameSite("Strict")
                .path("/")
                .maxAge(maxAge)
                .build();
    }
}
