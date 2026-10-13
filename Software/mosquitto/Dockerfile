# Config is baked in rather than bind-mounted: no dependency on host folder
# permissions (macOS protects ~/Downloads and ~/Documents from Docker).
FROM eclipse-mosquitto:2
COPY mosquitto.conf acl entrypoint.sh /mosquitto/config/
ENTRYPOINT ["/bin/sh", "/mosquitto/config/entrypoint.sh"]
