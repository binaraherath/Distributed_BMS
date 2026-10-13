package io.github.binaraherath.dbms.auth;

import java.util.List;

import org.springframework.core.convert.converter.Converter;
import org.springframework.security.authentication.AbstractAuthenticationToken;
import org.springframework.security.authentication.DisabledException;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.server.resource.authentication.JwtAuthenticationToken;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.user.AppUser;
import io.github.binaraherath.dbms.user.AppUserRepository;

/**
 * Turns a valid JWT into an authenticated user. The role is read from the database,
 * not from the token, so disabling a user or changing a role takes effect at once.
 */
@Component
class UserJwtAuthenticationConverter implements Converter<Jwt, AbstractAuthenticationToken> {

    private final AppUserRepository users;

    UserJwtAuthenticationConverter(AppUserRepository users) {
        this.users = users;
    }

    @Override
    public AbstractAuthenticationToken convert(Jwt jwt) {
        AppUser user = users.findByUsername(jwt.getSubject())
                .filter(AppUser::isEnabled)
                .orElseThrow(() -> new DisabledException("Account is disabled or no longer exists"));
        return new JwtAuthenticationToken(jwt,
                List.of(new SimpleGrantedAuthority("ROLE_" + user.getRole().name())),
                user.getUsername());
    }
}
