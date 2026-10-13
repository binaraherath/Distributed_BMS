#!/bin/sh
# Builds the password file from environment variables at every start, so no
# password is ever committed to git. Usernames are fixed because the ACL uses them.
set -eu

data=/mosquitto/data
mkdir -p "$data"
rm -f "$data/passwd"
touch "$data/passwd"
mosquitto_passwd -b "$data/passwd" backend "${MQTT_BACKEND_PASSWORD:?MQTT_BACKEND_PASSWORD is not set}"
mosquitto_passwd -b "$data/passwd" gateway "${MQTT_GATEWAY_PASSWORD:?MQTT_GATEWAY_PASSWORD is not set}"
cp /mosquitto/config/acl "$data/acl"
chown mosquitto:mosquitto "$data/passwd" "$data/acl"
chmod 0700 "$data/passwd" "$data/acl"

exec /docker-entrypoint.sh /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf
