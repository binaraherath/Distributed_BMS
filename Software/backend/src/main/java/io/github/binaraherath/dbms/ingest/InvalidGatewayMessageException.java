package io.github.binaraherath.dbms.ingest;

/** A gateway message that cannot be understood; it is logged and dropped. */
public class InvalidGatewayMessageException extends RuntimeException {

    public InvalidGatewayMessageException(String message) {
        super(message);
    }

    public InvalidGatewayMessageException(String message, Throwable cause) {
        super(message, cause);
    }
}
