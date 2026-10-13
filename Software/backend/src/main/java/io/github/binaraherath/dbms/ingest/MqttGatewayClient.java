package io.github.binaraherath.dbms.ingest;

import java.nio.charset.StandardCharsets;

import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.SmartLifecycle;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.config.BmsProperties;

/**
 * The backend's single MQTT connection. Subscribes to every gateway under the topic
 * root and publishes commands. Connection loss is normal (broker restart, Wi-Fi):
 * the first connect is retried here, later ones by Paho's automatic reconnect.
 */
@Component
public class MqttGatewayClient implements GatewayCommandPublisher, SmartLifecycle, MqttCallbackExtended {

    private static final Logger log = LoggerFactory.getLogger(MqttGatewayClient.class);
    private static final long RETRY_MS = 5_000;

    private final BmsProperties.Mqtt cfg;
    private final GatewayMessageRouter router;

    private volatile MqttClient client;
    private volatile boolean running;
    private Thread connector;

    public MqttGatewayClient(BmsProperties props, GatewayMessageRouter router) {
        this.cfg = props.mqtt();
        this.router = router;
    }

    // ------------------------------------------------------------ lifecycle

    @Override
    public void start() {
        running = true;
        if (!cfg.enabled()) {
            log.info("MQTT disabled (bms.mqtt.enabled=false)");
            return;
        }
        connector = Thread.ofVirtual().name("mqtt-connect").start(this::connectUntilUp);
    }

    private void connectUntilUp() {
        while (running) {
            MqttClient c = null;
            try {
                c = new MqttClient(cfg.url(), cfg.clientId(), new MemoryPersistence());
                c.setCallback(this);
                client = c; // set before connect: connectComplete() may run before connect() returns
                c.connect(options());
                return;
            } catch (MqttException ex) {
                log.warn("MQTT broker {} not reachable ({}), retrying in {} s",
                        cfg.url(), ex.getMessage(), RETRY_MS / 1000);
                closeQuietly(c);
                sleep();
            }
        }
    }

    private MqttConnectOptions options() {
        MqttConnectOptions o = new MqttConnectOptions();
        o.setAutomaticReconnect(true);
        o.setCleanSession(true);
        o.setKeepAliveInterval(30);
        o.setConnectionTimeout(10);
        if (cfg.username() != null && !cfg.username().isBlank()) {
            o.setUserName(cfg.username());
            o.setPassword(cfg.password().toCharArray());
        }
        return o;
    }

    @Override
    public void stop() {
        running = false;
        if (connector != null) {
            connector.interrupt();
        }
        MqttClient c = client;
        if (c != null) {
            try {
                if (c.isConnected()) {
                    c.disconnect(1000);
                }
                c.close();
            } catch (MqttException ex) {
                log.debug("MQTT close: {}", ex.getMessage());
            }
        }
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    public boolean isConnected() {
        MqttClient c = client;
        return c != null && c.isConnected();
    }

    // ------------------------------------------------------------ callbacks

    @Override
    public void connectComplete(boolean reconnect, String serverUri) {
        String root = cfg.topicRoot();
        try {
            client.subscribe(new String[] {root + "/+/telemetry", root + "/+/status", root + "/+/cmd/ack"},
                    new int[] {0, 1, 1});
            log.info("MQTT {} to {} - subscribed to {}/+/...", reconnect ? "reconnected" : "connected",
                    serverUri, root);
        } catch (MqttException ex) {
            log.error("MQTT subscribe failed", ex);
        }
    }

    @Override
    public void connectionLost(Throwable cause) {
        log.warn("MQTT connection lost: {}", cause == null ? "unknown" : cause.getMessage());
    }

    @Override
    public void messageArrived(String topic, MqttMessage message) {
        // Never let an exception escape: Paho would drop the connection.
        try {
            router.route(topic, message.getPayload());
        } catch (Exception ex) {
            log.warn("Dropped message on {}: {}", topic, ex.getMessage());
        }
    }

    @Override
    public void deliveryComplete(IMqttDeliveryToken token) {
        // QoS 1 publish confirmed by the broker; command state is tracked via cmd/ack.
    }

    // ------------------------------------------------------------ publishing

    @Override
    public void publishCommand(String deviceId, String json) {
        MqttClient c = client;
        if (c == null || !c.isConnected()) {
            throw new GatewayUnavailableException("MQTT broker is not connected", null);
        }
        try {
            MqttMessage msg = new MqttMessage(json.getBytes(StandardCharsets.UTF_8));
            msg.setQos(1);
            c.publish(cfg.topicRoot() + "/" + deviceId + "/cmd", msg);
        } catch (MqttException ex) {
            throw new GatewayUnavailableException("MQTT publish failed: " + ex.getMessage(), ex);
        }
    }

    private static void closeQuietly(MqttClient c) {
        if (c != null) {
            try {
                c.close();
            } catch (MqttException ignored) {
                // nothing useful to do
            }
        }
    }

    private void sleep() {
        try {
            Thread.sleep(RETRY_MS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            running = false;
        }
    }
}
