package io.github.binaraherath.dbms.auth;

import org.springframework.security.oauth2.server.resource.web.BearerTokenResolver;
import org.springframework.security.oauth2.server.resource.web.DefaultBearerTokenResolver;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.config.BmsProperties;
import jakarta.servlet.http.Cookie;
import jakarta.servlet.http.HttpServletRequest;

/**
 * Finds the JWT in the session cookie (browser) or the Authorization header
 * (curl / Swagger). The browser's EventSource cannot send headers, so the cookie
 * is what makes the live stream work.
 */
@Component
class CookieBearerTokenResolver implements BearerTokenResolver {

    private final String cookieName;
    private final DefaultBearerTokenResolver headerResolver = new DefaultBearerTokenResolver();

    CookieBearerTokenResolver(BmsProperties props) {
        this.cookieName = props.security().cookieName();
    }

    @Override
    public String resolve(HttpServletRequest request) {
        String path = request.getRequestURI();
        // An expired cookie must not block logging in again.
        if (path.startsWith("/api/v1/auth/login") || path.startsWith("/api/v1/auth/logout")
                || path.startsWith("/actuator/health") || path.startsWith("/api/docs")
                || path.startsWith("/api/swagger-ui")) {
            return null;
        }
        String header = headerResolver.resolve(request);
        if (header != null) {
            return header;
        }
        Cookie[] cookies = request.getCookies();
        if (cookies == null) {
            return null;
        }
        for (Cookie c : cookies) {
            if (cookieName.equals(c.getName()) && !c.getValue().isBlank()) {
                return c.getValue();
            }
        }
        return null;
    }
}
