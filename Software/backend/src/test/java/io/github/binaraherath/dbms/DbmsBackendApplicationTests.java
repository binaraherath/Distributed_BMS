package io.github.binaraherath.dbms;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.test.context.ActiveProfiles;

/** Flyway migrates a fresh database and Hibernate validates every entity against it. */
@Import(TestcontainersConfiguration.class)
@SpringBootTest
@ActiveProfiles("test")
class DbmsBackendApplicationTests {

	@Test
	void contextLoads() {
	}

}
