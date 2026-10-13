package io.github.binaraherath.dbms.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.Instant;

import jakarta.servlet.http.Cookie;

import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.context.annotation.Import;
import org.springframework.http.MediaType;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.ActiveProfiles;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;

import io.github.binaraherath.dbms.TestcontainersConfiguration;
import io.github.binaraherath.dbms.ingest.GatewayCommandPublisher;
import io.github.binaraherath.dbms.ingest.GatewayMessageRouter;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

/**
 * The API end to end on a real PostgreSQL: gateway messages go in through the same
 * router the MQTT client uses; only the outgoing MQTT publish is replaced by a mock.
 */
@SpringBootTest
@AutoConfigureMockMvc
@ActiveProfiles("test")
@Import(TestcontainersConfiguration.class)
class ApiIntegrationTest {

    @Autowired MockMvc mvc;
    @Autowired GatewayMessageRouter router;
    @Autowired JdbcTemplate jdbc;
    @Autowired ObjectMapper mapper;
    @MockitoBean GatewayCommandPublisher publisher;

    // ------------------------------------------------------------ helpers

    private Cookie login(String user, String password) throws Exception {
        return mvc.perform(post("/api/v1/auth/login").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"username\":\"%s\",\"password\":\"%s\"}".formatted(user, password)))
                .andExpect(status().isOk())
                .andReturn().getResponse().getCookie("DBMS_SESSION");
    }

    private Cookie admin() throws Exception {
        return login("admin", "admin12345");
    }

    private Cookie viewer() throws Exception {
        return login("viewer", "viewer12345");
    }

    /** Publishes the fixture as if it came from gateway {@code device}, with fresh time and the given fault bits. */
    private long telemetry(String device, int bmsFaultBits) throws IOException {
        try (InputStream in = getClass().getResourceAsStream("/fixtures/telemetry-v1.json")) {
            String json = new String(in.readAllBytes(), StandardCharsets.UTF_8)
                    .replace("\"ts\":1791316800000", "\"ts\":" + Instant.now().toEpochMilli())
                    .replace("\"fault_bits\":0,\"faults\":[],\"warning_bits\":0,\"warnings\":[],\n  \"bq76952\"",
                            "\"fault_bits\":" + bmsFaultBits + ",\"faults\":[],\"warning_bits\":0,\"warnings\":[],\n  \"bq76952\"");
            router.route("dbms/" + device + "/telemetry", json.getBytes(StandardCharsets.UTF_8));
        }
        return jdbc.queryForObject("select id from battery_pack where device_id = ?", Long.class, device);
    }

    // ------------------------------------------------------------ auth

    @Test
    void everyApiCallNeedsASession() throws Exception {
        mvc.perform(get("/api/v1/packs"))
                .andExpect(status().isUnauthorized())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON));
        mvc.perform(post("/api/v1/auth/login").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"username\":\"admin\",\"password\":\"wrong\"}"))
                .andExpect(status().isUnauthorized());
    }

    @Test
    void theSessionCookieIsHttpOnlyAndStrict() throws Exception {
        Cookie c = admin();
        assertThat(c.isHttpOnly()).isTrue();
        assertThat(c.getAttribute("SameSite")).isEqualTo("Strict");
        mvc.perform(get("/api/v1/auth/me").cookie(c))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.role").value("ADMIN"));
    }

    @Test
    void viewersCanReadButNotCommand() throws Exception {
        long packId = telemetry("it-roles", 0);
        Cookie v = viewer();

        mvc.perform(get("/api/v1/packs/{id}/telemetry/latest", packId).cookie(v)).andExpect(status().isOk());
        mvc.perform(post("/api/v1/packs/{id}/commands", packId).cookie(v)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"STOP\"}"))
                .andExpect(status().isForbidden());
        mvc.perform(get("/api/v1/users").cookie(v)).andExpect(status().isForbidden());
    }

    // ------------------------------------------------------------ telemetry

    @Test
    void oneGatewayMessageBecomesOneSampleWithItsCellsAndTemperatures() throws Exception {
        long packId = telemetry("it-ingest", 0);

        assertThat(jdbc.queryForObject("select count(*) from telemetry_sample where pack_id = ?", Long.class, packId))
                .isEqualTo(1);
        assertThat(jdbc.queryForObject("""
                select count(*) from cell_reading c join telemetry_sample s on s.id = c.sample_id
                where s.pack_id = ?""", Long.class, packId)).isEqualTo(16);

        mvc.perform(get("/api/v1/packs/{id}/telemetry/latest", packId).cookie(admin()))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.snapshot.pack.voltageMv").value(59380))
                .andExpect(jsonPath("$.snapshot.controller.source").value("SIMULATED"));
        mvc.perform(get("/api/v1/packs/{id}/telemetry", packId).cookie(admin()))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.resolution").value("RAW"))
                .andExpect(jsonPath("$.points.length()").value(1));
    }

    @Test
    void unknownPackIs404AndBadRangeIs400() throws Exception {
        Cookie a = admin();
        mvc.perform(get("/api/v1/packs/999999").cookie(a))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.status").value(404));
        long packId = telemetry("it-range", 0);
        mvc.perform(get("/api/v1/packs/{id}/telemetry", packId).cookie(a)
                        .param("from", "2026-01-01T00:00:00Z").param("to", "2026-03-01T00:00:00Z"))
                .andExpect(status().isBadRequest());
    }

    // ------------------------------------------------------------ alarms

    @Test
    void faultBitsBecomeAlarmsThatArePagedAndAcknowledgedOnce() throws Exception {
        String device = "it-alarms";
        telemetry(device, 1 << 4);                    // OVER_TEMP raised
        telemetry(device, (1 << 4) | (1 << 2));       // CELL_OV raised
        telemetry(device, 0);                         // both cleared
        long packId = telemetry(device, 1 << 3);      // CELL_UV raised, still active
        Cookie a = admin();

        mvc.perform(get("/api/v1/packs/{id}/alarms", packId).cookie(a).param("size", "2"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.totalElements").value(3))
                .andExpect(jsonPath("$.totalPages").value(2))
                .andExpect(jsonPath("$.content.length()").value(2))
                .andExpect(jsonPath("$.content[0].code").value("CELL_UV"));

        String body = mvc.perform(get("/api/v1/packs/{id}/alarms", packId).cookie(a).param("status", "ACTIVE"))
                .andExpect(jsonPath("$.totalElements").value(1))
                .andReturn().getResponse().getContentAsString();
        long alarmId = mapper.readTree(body).path("content").get(0).path("id").asLong();

        mvc.perform(patch("/api/v1/alarms/{id}", alarmId).cookie(viewer())
                        .contentType(MediaType.APPLICATION_JSON).content("{\"acknowledged\":true}"))
                .andExpect(status().isForbidden());
        mvc.perform(patch("/api/v1/alarms/{id}", alarmId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"acknowledged\":true}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.acknowledgedBy").value("admin"));
        mvc.perform(patch("/api/v1/alarms/{id}", alarmId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"acknowledged\":true}"))
                .andExpect(status().isConflict());
    }

    // ------------------------------------------------------------ commands

    @Test
    void aCommandIsAcceptedThenConfirmedByTheGatewayAck() throws Exception {
        String device = "it-cmd";
        long packId = telemetry(device, 0);
        Cookie a = admin();

        String created = mvc.perform(post("/api/v1/packs/{id}/commands", packId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"FAN_OVERRIDE\",\"argument\":40}"))
                .andExpect(status().isAccepted())
                .andExpect(header().string("Location", org.hamcrest.Matchers.startsWith("/api/v1/commands/")))
                .andExpect(jsonPath("$.status").value("PENDING"))
                .andReturn().getResponse().getContentAsString();
        long commandId = mapper.readTree(created).path("id").asLong();

        ArgumentCaptor<String> json = ArgumentCaptor.forClass(String.class);
        verify(publisher).publishCommand(eq(device), json.capture());
        JsonNode sent = mapper.readTree(json.getValue());
        assertThat(sent.path("cmd").asString()).isEqualTo("fan");
        assertThat(sent.path("duty").asInt()).isEqualTo(40);
        assertThat(sent.path("token").asString()).isNotBlank();

        router.route("dbms/" + device + "/cmd/ack", """
                {"cmd":"fan","result":"sent","id":"%s","ts":0}""".formatted(sent.path("id").asString())
                .getBytes(StandardCharsets.UTF_8));

        mvc.perform(get("/api/v1/commands/{id}", commandId).cookie(a))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("ACCEPTED"));
    }

    @Test
    void commandsAreRefusedWhenTheGatewayIsOfflineOrTheArgumentIsWrong() throws Exception {
        String device = "it-offline";
        long packId = telemetry(device, 0);
        Cookie a = admin();

        mvc.perform(post("/api/v1/packs/{id}/commands", packId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"FAN_OVERRIDE\",\"argument\":250}"))
                .andExpect(status().isBadRequest());
        mvc.perform(post("/api/v1/packs/{id}/commands", packId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{}"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.errors.type").exists());

        router.route("dbms/" + device + "/status", "offline".getBytes(StandardCharsets.UTF_8));
        mvc.perform(post("/api/v1/packs/{id}/commands", packId).cookie(a)
                        .contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"STOP\"}"))
                .andExpect(status().isConflict());
        verify(publisher, org.mockito.Mockito.never()).publishCommand(eq(device), anyString());
    }
}
