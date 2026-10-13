package io.github.binaraherath.dbms.common;

/** Exceptions that map one-to-one to HTTP status codes in {@link GlobalExceptionHandler}. */
public final class ApiExceptions {

    private ApiExceptions() {
    }

    /** 404 - the requested resource does not exist. */
    public static class NotFound extends RuntimeException {
        public NotFound(String message) {
            super(message);
        }
    }

    /** 409 - the request conflicts with the current state (duplicate, already done, offline). */
    public static class Conflict extends RuntimeException {
        public Conflict(String message) {
            super(message);
        }
    }

    /** 400 - the request is well-formed JSON but breaks a business rule. */
    public static class BadRequest extends RuntimeException {
        public BadRequest(String message) {
            super(message);
        }
    }
}
