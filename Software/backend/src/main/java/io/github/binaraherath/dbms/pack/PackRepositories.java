package io.github.binaraherath.dbms.pack;

import java.util.List;
import java.util.Optional;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;

interface BatteryPackRepository extends JpaRepository<BatteryPack, Long> {

    Optional<BatteryPack> findByDeviceId(String deviceId);

    @Query("select distinct p from BatteryPack p left join fetch p.modules order by p.id")
    List<BatteryPack> findAllWithModules();
}

interface BmsModuleRepository extends JpaRepository<BmsModule, Long> {

    List<BmsModule> findByPackId(Long packId);
}
