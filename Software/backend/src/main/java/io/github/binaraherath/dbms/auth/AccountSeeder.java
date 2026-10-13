package io.github.binaraherath.dbms.auth;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.user.AppUser.Role;
import io.github.binaraherath.dbms.user.UserService;

/**
 * Creates the first admin and a read-only viewer from environment variables on the
 * very first start. No password is hard-coded in the source.
 */
@Component
class AccountSeeder implements ApplicationRunner {

    private final UserService users;
    private final BmsProperties.Security.Seed seed;

    AccountSeeder(UserService users, BmsProperties props) {
        this.users = users;
        this.seed = props.security().seed();
    }

    @Override
    public void run(ApplicationArguments args) {
        users.ensureUser(seed.adminUsername(), "Administrator", seed.adminPassword(), Role.ADMIN);
        users.ensureUser(seed.viewerUsername(), "Viewer", seed.viewerPassword(), Role.VIEWER);
    }
}
