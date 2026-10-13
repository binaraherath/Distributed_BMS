package io.github.binaraherath.dbms.config;

import java.time.Clock;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.event.ApplicationEventMulticaster;
import org.springframework.context.event.SimpleApplicationEventMulticaster;
import org.springframework.scheduling.support.TaskUtils;

import io.swagger.v3.oas.models.Components;
import io.swagger.v3.oas.models.OpenAPI;
import io.swagger.v3.oas.models.info.Info;
import io.swagger.v3.oas.models.security.SecurityRequirement;
import io.swagger.v3.oas.models.security.SecurityScheme;

@Configuration(proxyBeanMethods = false)
public class AppConfig {

    /** Injected everywhere "now" is needed, so tests can control time. */
    @Bean
    Clock clock() {
        return Clock.systemUTC();
    }

    /**
     * Telemetry listeners (live stream, storage, alarms) are independent observers.
     * A failure in one of them is logged and must not stop the others.
     */
    @Bean(name = "applicationEventMulticaster")
    ApplicationEventMulticaster applicationEventMulticaster() {
        SimpleApplicationEventMulticaster multicaster = new SimpleApplicationEventMulticaster();
        multicaster.setErrorHandler(TaskUtils.LOG_AND_SUPPRESS_ERROR_HANDLER);
        return multicaster;
    }

    @Bean
    OpenAPI openApi(BmsProperties props) {
        String cookie = props.security().cookieName();
        return new OpenAPI()
                .info(new Info()
                        .title("Distributed BMS API")
                        .version("v1")
                        .description("""
                                REST API of the Distributed Battery Management System.
                                Log in with POST /api/v1/auth/login; the session is an httpOnly cookie.
                                Live data is streamed with Server-Sent Events from /api/v1/packs/{packId}/stream."""))
                .components(new Components().addSecuritySchemes("session", new SecurityScheme()
                        .type(SecurityScheme.Type.APIKEY)
                        .in(SecurityScheme.In.COOKIE)
                        .name(cookie)))
                .addSecurityItem(new SecurityRequirement().addList("session"));
    }
}
