package io.github.binaraherath.dbms.command;

/**
 * Life cycle: PENDING (saved) -> PUBLISHED (sent to the broker) -> ACCEPTED / REJECTED
 * (gateway's cmd/ack). TIMED_OUT if no answer arrives, FAILED if the broker was down.
 */
public enum CommandStatus {
    PENDING, PUBLISHED, ACCEPTED, REJECTED, TIMED_OUT, FAILED;

    public boolean isFinal() {
        return this != PENDING && this != PUBLISHED;
    }
}
